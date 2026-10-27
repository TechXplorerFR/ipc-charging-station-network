#ifndef STATION_H
#define STATION_H

#include <pthread.h>
#include <stdbool.h>
#include "message_queue.h" // IPC pour les requêtes
#include "charger.h"

struct Station {
    int id;
    struct Charger* chargers;
    int numChargers;
    msgq_t waitQueue; 
    pthread_mutex_t lock; // Protège l'accès concurrent aux bornes de cette station
};

void initStation(struct Station* station, int id, int numChargers);
bool assignChargerAtomically(struct Station* station, struct Vehicle* vehicle);

#endif // STATION_H