#include "station.h"
#include <stdlib.h>
#include <stdio.h>

void initStation(struct Station* station, int id, int numChargers, int mq_key) {
    station->id = id;
    station->numChargers = numChargers;
    station->chargers = malloc(numChargers * sizeof(struct Charger));
    
    // Initialisation des files de messages IPC et des mutex[cite: 4]
    pthread_mutex_init(&station->lock, NULL);
    station->waitQueue = initMessageQueue(mq_key);

    for (int i = 0; i < numChargers; i++) {
        station->chargers[i].id = i;
        station->chargers[i].maxPower = 50; // Exemple: 50 kW
        station->chargers[i].status = AVAILABLE; // État initial[cite: 4]
    }
}

bool hasAvailableCharger(struct Station* station) {
    pthread_mutex_lock(&station->lock);
    for (int i = 0; i < station->numChargers; i++) {
        if (station->chargers[i].status == AVAILABLE) {
            pthread_mutex_unlock(&station->lock);
            return true;
        }
    }
    pthread_mutex_unlock(&station->lock);
    return false;
}

bool isChargerAvailable(struct Station* station, int chargerId) {
    // La vérification de disponibilité doit être protégée[cite: 6]
    pthread_mutex_lock(&station->lock);
    bool available = (station->chargers[chargerId].status == AVAILABLE);
    pthread_mutex_unlock(&station->lock);
    return available;
}