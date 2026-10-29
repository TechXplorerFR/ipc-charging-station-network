#include "allocator.h"

// Une borne couvrant la demande l'emporte ; entre deux qui la couvrent, la moins puissante
// (les bornes puissantes restent libres pour les gros besoins) ; sinon, la plus puissante
static bool isBetterFit(int candidatePower, int currentPower, int desiredPower) {
    bool candidateCovers = candidatePower >= desiredPower;
    bool currentCovers = currentPower >= desiredPower;
    if (candidateCovers != currentCovers) {
        return candidateCovers;
    }
    return candidateCovers ? candidatePower < currentPower : candidatePower > currentPower;
}

// Verrou acquis : indice de la borne libre la mieux adaptée, ou -1 si aucune n'est libre
static int findBestCharger(const struct Station* station, int desiredPower) {
    int best = -1;
    for (int i = 0; i < station->numChargers; i++) {
        if (station->chargers[i].status != AVAILABLE) {
            continue;
        }
        if (best == -1 || isBetterFit(station->chargers[i].maxPower, station->chargers[best].maxPower, desiredPower)) {
            best = i;
        }
    }
    return best;
}

// Verrou acquis : la borne passe à OCCUPIED et une session de recharge démarre pour le véhicule
static void occupyCharger(struct Station* station, int chargerIndex, const struct WaitingVehicle* vehicle) {
    setStationChargerStatus(station, chargerIndex, OCCUPIED);
    startChargingSession(&station->chargers[chargerIndex], vehicle->vehicleId, vehicle->desiredPower,
                         vehicle->priority == PRIORITY_HIGH);
}

// Verrou acquis : copie la session de la borne dans *ended, figée à l'instant présent
static void copyEndedSession(const struct Charger* charger, struct ChargingSession* ended) {
    *ended = charger->session;
    endChargingSession(ended);
}

// Verrou acquis, sur une borne qui redevient utilisable : la donne au prochain véhicule en attente
// (après promotion des véhicules qui attendent depuis trop longtemps), sinon la libère
static HandoverResult handOverCharger(struct Station* station, int chargerIndex, struct WaitingVehicle* next) {
    promoteLongWaitingVehicles(&station->waitQueue, PRIORITY_AGING_SECONDS);
    if (dequeueNextVehicle(&station->waitQueue, next)) {
        occupyCharger(station, chargerIndex, next);
        return HANDOVER_REASSIGNED;
    }
    setStationChargerStatus(station, chargerIndex, AVAILABLE);
    clearChargingSession(&station->chargers[chargerIndex]);
    return HANDOVER_FREED;
}

// Réserve une borne ou, à défaut, place le véhicule en file d'attente (devant ceux de même priorité
// si aheadOfSamePriority), dans une seule section critique
static struct Assignment assignOrEnqueue(struct Station* station, const struct WaitingVehicle* vehicle,
                                         bool aheadOfSamePriority) {
    struct Assignment assignment = { .result = ASSIGN_REFUSED, .chargerIndex = -1, .queuePosition = -1 };

    lockStation(station);
    int chargerIndex = findBestCharger(station, vehicle->desiredPower);
    bool queued = false;
    if (chargerIndex != -1) {
        occupyCharger(station, chargerIndex, vehicle);
        assignment.result = ASSIGN_OK;
        assignment.chargerIndex = chargerIndex;
    } else if (aheadOfSamePriority) {
        queued = enqueueVehicleFirst(&station->waitQueue, vehicle);
    } else {
        queued = enqueueVehicle(&station->waitQueue, vehicle);
    }
    if (queued) {
        assignment.result = ASSIGN_QUEUED;
        assignment.queuePosition = waitQueuePosition(&station->waitQueue, vehicle->vehicleId);
    }
    unlockStation(station);

    return assignment;
}

struct Assignment requestCharger(struct Station* station, const struct WaitingVehicle* vehicle) {
    return assignOrEnqueue(station, vehicle, false);
}

HandoverResult releaseChargerAndServeQueue(struct Station* station, int chargerIndex,
                                           struct ChargingSession* finished, struct WaitingVehicle* next) {
    if (!isValidCharger(station, chargerIndex)) {
        return HANDOVER_INVALID;
    }

    lockStation(station);
    HandoverResult result = HANDOVER_INVALID;
    if (station->chargers[chargerIndex].status == OCCUPIED) {
        copyEndedSession(&station->chargers[chargerIndex], finished);
        result = handOverCharger(station, chargerIndex, next);
    }
    unlockStation(station);
    return result;
}

HandoverResult repairChargerAndServeQueue(struct Station* station, int chargerIndex, struct WaitingVehicle* next) {
    if (!isValidCharger(station, chargerIndex)) {
        return HANDOVER_INVALID;
    }

    lockStation(station);
    HandoverResult result = HANDOVER_INVALID;
    if (isChargerOutOfOrder(station->chargers[chargerIndex].status)) {
        result = handOverCharger(station, chargerIndex, next);
    }
    unlockStation(station);
    return result;
}

bool takeChargerOutOfService(struct Station* station, int chargerIndex, ChargerStatus status,
                             struct ChargingSession* interrupted) {
    if (!isValidCharger(station, chargerIndex) || !isChargerOutOfOrder(status)) {
        return false;
    }

    lockStation(station);
    struct Charger* charger = &station->chargers[chargerIndex];
    bool wasCharging = charger->status == OCCUPIED;
    if (wasCharging) {
        copyEndedSession(charger, interrupted);
    }
    setStationChargerStatus(station, chargerIndex, status);
    clearChargingSession(charger);
    unlockStation(station);
    return wasCharging;
}

struct Assignment redirectInterruptedVehicle(struct Station* station, const struct ChargingSession* interrupted) {
    struct WaitingVehicle vehicle = {
        .vehicleId = interrupted->vehicleId,
        .desiredPower = interrupted->desiredPower,
        .priority = interrupted->isPrioritary ? PRIORITY_HIGH : PRIORITY_NORMAL,
    };
    return assignOrEnqueue(station, &vehicle, true);
}

bool cancelWaitingVehicle(struct Station* station, int vehicleId) {
    lockStation(station);
    bool removed = removeWaitingVehicle(&station->waitQueue, vehicleId);
    unlockStation(station);
    return removed;
}

int expireWaitingVehicles(struct Station* station, double maxWaitSeconds, struct WaitingVehicle* expired,
                          int capacity) {
    lockStation(station);
    int removed = removeExpiredVehicles(&station->waitQueue, maxWaitSeconds, expired, capacity);
    unlockStation(station);
    return removed;
}
