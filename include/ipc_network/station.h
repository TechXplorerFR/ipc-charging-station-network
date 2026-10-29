#ifndef STATION_H
#define STATION_H

#include <stdbool.h>
#include "charger.h"
#include "queue_manager.h"

struct Station {
    int id;
    struct Charger* chargers;
    int numChargers;
    struct WaitQueue waitQueue; // File d'attente des véhicules de la station, protégée par le verrou
    int semId;                  // Sémaphores System V : verrou des bornes + compteur de bornes libres
};

bool initStation(struct Station* station, int id, int numChargers);
void destroyStation(struct Station* station);

// Section critique sur l'état des bornes de la station
void lockStation(struct Station* station);
void unlockStation(struct Station* station);

// À appeler verrou acquis : change l'état d'une borne et tient à jour le compteur de bornes libres
void setStationChargerStatus(struct Station* station, int chargerIndex, ChargerStatus status);

bool isValidCharger(const struct Station* station, int chargerIndex);
bool hasAvailableCharger(struct Station* station);
bool isChargerAvailable(struct Station* station, int chargerIndex);

#endif // STATION_H
