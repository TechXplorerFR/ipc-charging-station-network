#include <stdbool.h>
#include "station.h"
#include "server.h"

#ifndef VEHICLE_H
#define VEHICLE_H

typedef enum {
    CAR,
    CAMIONETTE,
    BUS
} VehicleType;

struct Vehicle {
    int id;
    VehicleType type;
    unsigned int capacity;
    unsigned int currentCharge; // Niveau de charge initial
    int maxPower;
    char* destination;
    bool isPrioritary; // Permet de gérer la file d'attente prioritaire
};

struct Vehicle* createVehicle(int id, VehicleType type, unsigned int capacity, int maxPower, const char* destination, bool prioritary);
void charge(struct Vehicle* vehicle, int power);
void sendChargeRequest(msgq_t serverQueue, struct Vehicle* vehicle, int stationId, int desiredPower);
void respondToPing(struct Server* server, struct Vehicle* vehicle);

#endif // VEHICLE_H