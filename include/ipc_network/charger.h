#ifndef CHARGER_H
#define CHARGER_H

typedef enum {
    AVAILABLE,
    OCCUPIED,
    OUT_OF_SERVICE,
    MAINTENANCE
} ChargerStatus;

// Valeur de Charger.vehicleId lorsqu'aucun véhicule n'utilise la borne
#define NO_VEHICLE (-1)

struct Charger {
    int id;
    int maxPower; // kW
    ChargerStatus status;
    int vehicleId; // Véhicule en charge si OCCUPIED, NO_VEHICLE sinon
};

// Modifie l'état brut de la borne, sans toucher aux sémaphores de la station :
// depuis l'extérieur de station.c, passer par setStationChargerStatus()
void setChargerStatus(struct Charger* charger, ChargerStatus newStatus);

#endif // CHARGER_H
