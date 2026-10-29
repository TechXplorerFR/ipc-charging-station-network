#include "vehicle.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

struct Vehicle* createVehicle(int id, VehicleType type, unsigned int capacity, unsigned int initialCharge,
                              int maxPower, const char* destination, bool prioritary) {
    struct Vehicle* v = malloc(sizeof(struct Vehicle));
    if (v == NULL) {
        perror("Erreur malloc du véhicule");
        return NULL;
    }

    v->destination = strdup(destination);
    if (v->destination == NULL) {
        perror("Erreur strdup de la destination");
        free(v);
        return NULL;
    }
    v->id = id;
    v->type = type;
    v->capacity = capacity;
    v->currentCharge = initialCharge;
    v->maxPower = maxPower;
    v->isPrioritary = prioritary;
    return v;
}

void destroyVehicle(struct Vehicle* vehicle) {
    if (vehicle == NULL) {
        return;
    }
    free(vehicle->destination);
    free(vehicle);
}

bool sendChargeRequest(msgq_t serverQueue, const struct Vehicle* vehicle, int stationId, int desiredPower) {
    Message msg = {
        .mtype = SERVER_MSG_TYPE,
        .request = REQ_CHARGE_DEMAND,
        .vehicle_id = vehicle->id,
        .station_id = stationId,
        .charger_id = -1,
        .desired_power = desiredPower,
        .is_prioritary = vehicle->isPrioritary,
    };

    if (!sendMessage(serverQueue, &msg)) {
        return false;
    }
    printf("Véhicule %d a envoyé une demande pour la station %d\n", vehicle->id, stationId);
    return true;
}

void charge(struct Vehicle* vehicle, int power) {
    // La puissance effective est bornée par ce que le véhicule accepte
    int effectivePower = power < vehicle->maxPower ? power : vehicle->maxPower;
    printf("Véhicule %d en charge à %d kW...\n", vehicle->id, effectivePower);

    // TODO Simuler une durée proportionnelle à l'énergie manquante
    sleep(2);
    vehicle->currentCharge = vehicle->capacity;
    printf("Recharge terminée pour le véhicule %d.\n", vehicle->id);
}
