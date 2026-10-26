#include <stdbool.h>

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
    int maxPower;
    char* destination;
    bool isPrioritary;
};

struct Vehicle* createVehicle(int id, VehicleType type, unsigned int capacity, int maxPower, const char* destination);
void charge(struct Vehicle* vehicle, int power);
void sendChargeRequest(struct Station* station, struct Vehicle* vehicle, int desiredPower);
void respondToPing(struct Server* server, struct Vehicle* vehicle);

#endif // VEHICLE_H