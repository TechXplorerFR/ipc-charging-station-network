#include "station.h"
#include <stdio.h>

bool assignChargerAtomically(struct Station* station, struct Vehicle* vehicle) {
    // 1. Acquérir le verrou[cite: 6]
    pthread_mutex_lock(&station->lock);

    int assignedChargerId = -1;

    // 2. Rechercher une borne compatible et libre[cite: 6]
    for (int i = 0; i < station->numChargers; i++) {
        if (station->chargers[i].status == AVAILABLE) {
            assignedChargerId = i;
            // 3. Réserver la borne et passer l'état à OCCUPIED dans la même section critique[cite: 6, 14]
            station->chargers[i].status = OCCUPIED;
            break;
        }
    }

    if (assignedChargerId != -1) {
        // Enregistrer le début de session ici si nécessaire[cite: 6]
        printf("Véhicule %d assigné à la borne %d de la station %d\n", vehicle->id, assignedChargerId, station->id);
        pthread_mutex_unlock(&station->lock);
        return true;
    }

    // 4. Si aucune borne n'est disponible, le véhicule devra être mis en file d'attente
    pthread_mutex_unlock(&station->lock);
    return false; 
}