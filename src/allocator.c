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

    struct ChargingSession* session = &station->chargers[chargerIndex].session;
    session->vehicleId = vehicle->vehicleId;
    session->desiredPower = vehicle->desiredPower;
    session->isPrioritary = vehicle->priority == PRIORITY_HIGH;
    clock_gettime(CLOCK_MONOTONIC, &session->start);
}

// Verrou acquis
static void clearSession(struct Charger* charger) {
    charger->session = (struct ChargingSession){ .vehicleId = NO_VEHICLE };
}

// Verrou acquis, sur une borne qui redevient utilisable : la donne au prochain véhicule en attente,
// sinon la libère
static HandoverResult handOverCharger(struct Station* station, int chargerIndex, struct WaitingVehicle* next) {
    if (dequeueNextVehicle(&station->waitQueue, next)) {
        occupyCharger(station, chargerIndex, next);
        return HANDOVER_REASSIGNED;
    }
    setStationChargerStatus(station, chargerIndex, AVAILABLE);
    clearSession(&station->chargers[chargerIndex]);
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

static bool isOutOfOrder(ChargerStatus status) {
    return status == OUT_OF_SERVICE || status == MAINTENANCE;
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
        *finished = station->chargers[chargerIndex].session;
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
    if (isOutOfOrder(station->chargers[chargerIndex].status)) {
        result = handOverCharger(station, chargerIndex, next);
    }
    unlockStation(station);
    return result;
}

bool takeChargerOutOfService(struct Station* station, int chargerIndex, ChargerStatus status,
                             struct ChargingSession* interrupted) {
    if (!isValidCharger(station, chargerIndex) || !isOutOfOrder(status)) {
        return false;
    }

    lockStation(station);
    struct Charger* charger = &station->chargers[chargerIndex];
    bool wasCharging = charger->status == OCCUPIED;
    if (wasCharging) {
        *interrupted = charger->session;
    }
    setStationChargerStatus(station, chargerIndex, status);
    clearSession(charger);
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
