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

// Verrou acquis
static void occupyCharger(struct Station* station, int chargerIndex, int vehicleId) {
    setStationChargerStatus(station, chargerIndex, OCCUPIED);
    station->chargers[chargerIndex].vehicleId = vehicleId;
    // TODO Enregistrer le début de session
}

// Verrou acquis, sur une borne qui redevient utilisable : la donne au prochain véhicule en attente,
// sinon la libère
static HandoverResult handOverCharger(struct Station* station, int chargerIndex, struct WaitingVehicle* next) {
    if (dequeueNextVehicle(&station->waitQueue, next)) {
        occupyCharger(station, chargerIndex, next->vehicleId);
        return HANDOVER_REASSIGNED;
    }
    setStationChargerStatus(station, chargerIndex, AVAILABLE);
    station->chargers[chargerIndex].vehicleId = NO_VEHICLE;
    return HANDOVER_FREED;
}

static bool isOutOfOrder(ChargerStatus status) {
    return status == OUT_OF_SERVICE || status == MAINTENANCE;
}

struct Assignment requestCharger(struct Station* station, const struct WaitingVehicle* vehicle) {
    struct Assignment assignment = { .result = ASSIGN_REFUSED, .chargerIndex = -1, .queuePosition = -1 };

    lockStation(station);
    int chargerIndex = findBestCharger(station, vehicle->desiredPower);
    if (chargerIndex != -1) {
        occupyCharger(station, chargerIndex, vehicle->vehicleId);
        assignment.result = ASSIGN_OK;
        assignment.chargerIndex = chargerIndex;
    } else if (enqueueVehicle(&station->waitQueue, vehicle)) {
        assignment.result = ASSIGN_QUEUED;
        assignment.queuePosition = waitQueuePosition(&station->waitQueue, vehicle->vehicleId);
    }
    unlockStation(station);

    return assignment;
}

HandoverResult releaseChargerAndServeQueue(struct Station* station, int chargerIndex, struct WaitingVehicle* next) {
    if (!isValidCharger(station, chargerIndex)) {
        return HANDOVER_INVALID;
    }

    lockStation(station);
    HandoverResult result = HANDOVER_INVALID;
    if (station->chargers[chargerIndex].status == OCCUPIED) {
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

int takeChargerOutOfService(struct Station* station, int chargerIndex, ChargerStatus status) {
    if (!isValidCharger(station, chargerIndex) || !isOutOfOrder(status)) {
        return NO_VEHICLE;
    }

    lockStation(station);
    int interruptedVehicle = station->chargers[chargerIndex].vehicleId;
    setStationChargerStatus(station, chargerIndex, status);
    station->chargers[chargerIndex].vehicleId = NO_VEHICLE;
    unlockStation(station);
    return interruptedVehicle;
}

bool cancelWaitingVehicle(struct Station* station, int vehicleId) {
    lockStation(station);
    bool removed = removeWaitingVehicle(&station->waitQueue, vehicleId);
    unlockStation(station);
    return removed;
}
