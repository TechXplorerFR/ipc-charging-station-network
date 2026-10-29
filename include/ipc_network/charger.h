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

// Accélération de la simulation : une seconde réelle représente une minute de recharge
#define SIMULATED_SECONDS_PER_SECOND 60.0

// Recharge sur une borne : de quoi facturer la session et rediriger le véhicule en cas de panne
struct ChargingSession {
    int vehicleId;         // NO_VEHICLE si la borne n'est pas occupée
    int desiredPower;      // kW demandés par le véhicule
    int power;             // kW réellement délivrés : le minimum entre la borne et la demande
    bool isPrioritary;
    struct timespec start; // Horloge CLOCK_MONOTONIC
    struct timespec end;   // Significative seulement si ended
    bool ended;            // Session terminée ou interrompue
};

struct Charger {
    int id;
    int maxPower; // kW
    ChargerStatus status;
    struct ChargingSession session; // Significative seulement si status == OCCUPIED

    // Tenus à jour par setStationChargerStatus() pour le taux d'occupation
    double occupiedSeconds;        // Temps passé à l'état OCCUPIED, recharge en cours non comprise
    struct timespec occupiedSince; // Passage à l'état OCCUPIED (horloge CLOCK_MONOTONIC), si OCCUPIED
};

// Modifie l'état brut de la borne, sans toucher aux sémaphores de la station :
// depuis l'extérieur de station.c, passer par setStationChargerStatus()
void setChargerStatus(struct Charger* charger, ChargerStatus newStatus);

// Libellé de l'état, pour l'affichage
const char* chargerStatusName(ChargerStatus status);

// Borne en panne ou en maintenance : ni attribuable, ni libérable comme une borne occupée
bool isChargerOutOfOrder(ChargerStatus status);

// Puissance délivrée à un véhicule : celle demandée, dans la limite de la borne
int effectiveChargingPower(int chargerPower, int desiredPower);

// Ouvre une session sur la borne (sans changer son état : voir setStationChargerStatus)
void startChargingSession(struct Charger* charger, int vehicleId, int desiredPower, bool isPrioritary);
// Remet la session de la borne à vide (NO_VEHICLE)
void clearChargingSession(struct Charger* charger);
// Fige la fin de la session à l'instant présent ; sans effet si elle est déjà terminée
void endChargingSession(struct ChargingSession* session);

// Durée réelle de la session : jusqu'à sa fin si elle est terminée, jusqu'à maintenant sinon
double sessionDurationSeconds(const struct ChargingSession* session);
// Énergie délivrée (kWh) sur la durée simulée de la session
double sessionEnergyKWh(const struct ChargingSession* session);

#endif // CHARGER_H
