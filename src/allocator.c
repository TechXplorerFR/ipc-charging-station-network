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
    unlockStation(station);

    if (assignedCharger != -1) {
        // TODO Enregistrer le début de session
        printf("Véhicule %d assigné à la borne %d de la station %d\n", vehicleId, assignedCharger, station->id);
    }
    return assignedCharger;
}

bool releaseCharger(struct Station* station, int chargerIndex) {
    if (!isValidCharger(station, chargerIndex)) {
        return false;
    }

    lockStation(station);
    bool released = station->chargers[chargerIndex].status == OCCUPIED;
    if (released) {
        setStationChargerStatus(station, chargerIndex, AVAILABLE);
    }
    unlockStation(station);
    return released;
}
