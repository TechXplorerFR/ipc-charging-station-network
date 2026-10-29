#ifndef CHARGER_H
#define CHARGER_H

#include <stdbool.h>
#include <time.h>

typedef enum {
    AVAILABLE,
    OCCUPIED,
    OUT_OF_SERVICE,
    MAINTENANCE
} ChargerStatus;

// Valeur de ChargingSession.vehicleId lorsqu'aucun véhicule n'utilise la borne
#define NO_VEHICLE (-1)

// Recharge en cours sur une borne : de quoi facturer la session et rediriger le véhicule en cas de panne
struct ChargingSession {
    int vehicleId;         // NO_VEHICLE si la borne n'est pas occupée
    int desiredPower;      // kW demandés par le véhicule
    bool isPrioritary;
    struct timespec start; // Horloge CLOCK_MONOTONIC
};

struct Charger {
    int id;
    int maxPower; // kW
    ChargerStatus status;
    struct ChargingSession session; // Significative seulement si status == OCCUPIED
};

// Modifie l'état brut de la borne, sans toucher aux sémaphores de la station :
// depuis l'extérieur de station.c, passer par setStationChargerStatus()
void setChargerStatus(struct Charger* charger, ChargerStatus newStatus);

#endif // CHARGER_H
