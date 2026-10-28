#ifndef VEHICLE_H
#define VEHICLE_H

#include <stdbool.h>
#include "message_queue.h"

typedef enum {
    CAR,
    VAN,
    BUS
} VehicleType;

struct Vehicle {
    int id;
    VehicleType type;
    unsigned int capacity;
    unsigned int currentCharge;
    int maxPower;
    char* destination;
    bool isPrioritary; // Permet de gérer la file d'attente prioritaire
};

// Renvoie NULL en cas d'échec d'allocation ; à libérer avec destroyVehicle()
struct Vehicle* createVehicle(int id, VehicleType type, unsigned int capacity, unsigned int initialCharge,
                              int maxPower, const char* destination, bool prioritary);
void destroyVehicle(struct Vehicle* vehicle);
void charge(struct Vehicle* vehicle, int power);
bool sendChargeRequest(msgq_t serverQueue, const struct Vehicle* vehicle, int stationId, int desiredPower);
void respondToPing(msgq_t serverQueue, const struct Vehicle* vehicle);

#endif // VEHICLE_H
