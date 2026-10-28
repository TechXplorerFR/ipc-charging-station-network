#ifndef CHARGER_H
#define CHARGER_H

typedef enum {
    AVAILABLE,
    OCCUPIED,
    OUT_OF_SERVICE,
    MAINTENANCE
} ChargerStatus;

struct Charger {
    int id;
    int maxPower; // kW
    ChargerStatus status;
};

// Modifie l'état brut de la borne, sans toucher aux sémaphores de la station :
// depuis l'extérieur de station.c, passer par setStationChargerStatus()
void setChargerStatus(struct Charger* charger, ChargerStatus newStatus);

#endif // CHARGER_H
