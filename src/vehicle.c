#include "vehicle.h"
#include "message_queue.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

struct Vehicle* createVehicle(int id, VehicleType type, unsigned int capacity, int maxPower, const char* destination, bool prioritary) {
    struct Vehicle* v = malloc(sizeof(struct Vehicle));
    v->id = id;
    v->type = type;
    v->capacity = capacity;
    v->maxPower = maxPower;
    v->destination = strdup(destination);
    v->isPrioritary = prioritary; // Détermine si le véhicule est prioritaire[cite: 5]
    return v;
}

void sendChargeRequest(msgq_t serverQueue, struct Vehicle* vehicle, int stationId, int desiredPower) {
    Message msg;
    msg.mtype = SERVER_MSG_TYPE;
    msg.vehicle_id = vehicle->id;
    msg.station_id = stationId;
    msg.desired_power = desiredPower;
    msg.is_prioritary = vehicle->isPrioritary ? 1 : 0;

    // Envoyer la demande au serveur par IPC[cite: 5]
    sendMessage(serverQueue, &msg);
    printf("Véhicule %d a envoyé une demande pour la station %d\n", vehicle->id, stationId);
}

void charge(struct Vehicle* vehicle, int power) {
    printf("Véhicule %d en charge à %d kW...\n", vehicle->id, power);
    // Simuler la progression de la recharge[cite: 7]
    sleep(2); 
    printf("Recharge terminée pour le véhicule %d.\n", vehicle->id);
}