#include "allocator.h"
#include <stdio.h>

int assignChargerAtomically(struct Station* station, int vehicleId) {
    // Aucune borne libre : inutile de prendre le verrou, le véhicule ira en file d'attente
    if (!hasAvailableCharger(station)) {
        return -1;
    }

    // Vérification, réservation et mise à jour de l'état dans la même section critique
    lockStation(station);
    int assignedCharger = -1;
    for (int i = 0; i < station->numChargers; i++) {
        if (station->chargers[i].status == AVAILABLE) {
            setStationChargerStatus(station, i, OCCUPIED);
            assignedCharger = i;
            break;
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

    if (assignedCharger != -1) {
        // TODO Enregistrer le début de session
        printf("Véhicule %d assigné à la borne %d de la station %d\n", vehicleId, assignedCharger, station->id);
    }
    return assignedCharger;
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
    return released;
}
