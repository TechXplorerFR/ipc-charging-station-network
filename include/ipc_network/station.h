#ifndef STATION_H
#define STATION_H

#include <stdbool.h>
#include <time.h>
#include "charger.h"
#include "queue_manager.h"

// Nombre maximal de bornes par station
#define MAX_CHARGERS_PER_STATION 32

struct Station {
    int id;
    struct Charger* chargers;
    int numChargers;
    struct WaitQueue waitQueue; // File d'attente des véhicules de la station, protégée par le verrou
    int semId;                  // Sémaphores System V : verrou des bornes + compteur de bornes libres
    struct timespec openedAt;   // Ouverture de la station (horloge CLOCK_MONOTONIC), pour le taux d'occupation
};

// Bornes de puissances variées (22, 50 et 150 kW, en boucle)
bool initStation(struct Station* station, int id, int numChargers);
// Bornes dont la puissance (kW, > 0) est donnée par chargerPowers[i]
bool initStationWithPowers(struct Station* station, int id, const int* chargerPowers, int numChargers);
// Sans effet sur une station déjà détruite
void destroyStation(struct Station* station);

// Section critique sur l'état des bornes et la file d'attente de la station
void lockStation(struct Station* station);
void unlockStation(struct Station* station);

// À appeler verrou acquis : change l'état d'une borne, tient à jour le compteur de bornes libres
// et le temps d'occupation de la borne
void setStationChargerStatus(struct Station* station, int chargerIndex, ChargerStatus status);

bool isValidCharger(const struct Station* station, int chargerIndex);
bool hasAvailableCharger(struct Station* station);
bool isChargerAvailable(struct Station* station, int chargerIndex);

// Les fonctions suivantes prennent elles-mêmes le verrou

// Nombre de bornes dans l'état donné
int countChargers(struct Station* station, ChargerStatus status);

// Part du temps, depuis l'ouverture de la station, pendant laquelle ses bornes ont été occupées (0 à 1)
double stationOccupancyRate(struct Station* station);

// Affiche l'état des bornes et de la file d'attente (pour la CLI)
void printStation(struct Station* station);

#endif // STATION_H
