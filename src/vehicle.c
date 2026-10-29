#include "vehicle.h"
#include "charger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SECONDS_PER_HOUR 3600.0

// Intervalle entre deux mises à jour de la batterie (et deux lectures des messages) pendant la recharge
#define CHARGE_TICK_NANOSECONDS 50000000L // 50 ms

typedef enum {
    CHARGE_COMPLETED,
    CHARGE_INTERRUPTED, // Borne tombée en panne (NOTIF_FAULT)
    CHARGE_STOPPED,     // Fin de la simulation
    CHARGE_FAILED       // Erreur de réception
} ChargeResult;

struct Vehicle* createVehicle(int id, VehicleType type, double capacity, double initialCharge, int maxPower,
                              const char* destination, bool prioritary) {
    if (id < 0 || capacity <= 0.0 || initialCharge < 0.0 || initialCharge > capacity || maxPower <= 0
        || destination == NULL) {
        fprintf(stderr, "Erreur : paramètres invalides pour le véhicule %d\n", id);
        return NULL;
    }

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

const char* vehicleTypeName(VehicleType type) {
    switch (type) {
        case CAR:
            return "voiture";
        case VAN:
            return "camionnette";
        case BUS:
            return "bus";
    }
    return "inconnu";
}

int batteryLevelPercent(const struct Vehicle* vehicle) {
    return (int)(vehicle->currentCharge * 100.0 / vehicle->capacity);
}

bool isFullyCharged(const struct Vehicle* vehicle) {
    return vehicle->currentCharge >= vehicle->capacity;
}

int estimatedChargeDurationSeconds(const struct Vehicle* vehicle, int power) {
    int effectivePower = effectiveChargingPower(vehicle->maxPower, power);
    if (effectivePower <= 0) {
        return 0;
    }
    double missingEnergy = vehicle->capacity - vehicle->currentCharge;
    double seconds = missingEnergy / effectivePower * SECONDS_PER_HOUR / SIMULATED_SECONDS_PER_SECOND;

    int rounded = (int)seconds;
    return rounded < seconds ? rounded + 1 : rounded;
}

double addCharge(struct Vehicle* vehicle, int power, double realSeconds) {
    int effectivePower = effectiveChargingPower(vehicle->maxPower, power);
    double energy = effectivePower * realSeconds * SIMULATED_SECONDS_PER_SECOND / SECONDS_PER_HOUR;
    double missingEnergy = vehicle->capacity - vehicle->currentCharge;
    if (energy > missingEnergy) {
        energy = missingEnergy;
    }
    vehicle->currentCharge += energy;
    return energy;
}

bool sendChargeRequest(msgq_t serverQueue, const struct Vehicle* vehicle, int stationId) {
    Message msg = {
        .request = REQ_CHARGE_DEMAND,
        .vehicle_id = vehicle->id,
        .station_id = stationId,
        .charger_id = -1,
        .desired_power = vehicle->maxPower,
        .estimated_duration = estimatedChargeDurationSeconds(vehicle, vehicle->maxPower),
        .battery_level = batteryLevelPercent(vehicle),
        .is_prioritary = vehicle->isPrioritary,
    };

    if (!sendToServer(serverQueue, &msg)) {
        return false;
    }
    printf("[VÉHICULE %d] Demande de recharge sur la station %d (batterie %d %%).\n", vehicle->id, stationId,
           msg.battery_level);
    return true;
}

bool respondToPing(msgq_t serverQueue, const struct Vehicle* vehicle) {
    Message msg = {
        .request = REQ_PONG,
        .vehicle_id = vehicle->id,
        .charger_id = -1,
        .battery_level = batteryLevelPercent(vehicle),
    };
    return sendToServer(serverQueue, &msg);
}

static bool sendChargeEnd(msgq_t serverQueue, const struct Vehicle* vehicle, int stationId, int chargerId) {
    Message msg = {
        .request = REQ_CHARGE_END,
        .vehicle_id = vehicle->id,
        .station_id = stationId,
        .charger_id = chargerId,
        .battery_level = batteryLevelPercent(vehicle),
    };
    return sendToServer(serverQueue, &msg);
}

static double secondsBetween(const struct timespec* start, const struct timespec* end) {
    return (double)(end->tv_sec - start->tv_sec) + (double)(end->tv_nsec - start->tv_nsec) / 1e9;
}

// Recharge jusqu'à ce que la batterie soit pleine, en traitant entre deux pas les messages du serveur
static ChargeResult chargeUntilFull(struct Vehicle* vehicle, msgq_t serverQueue, int power) {
    const struct timespec tick = { .tv_sec = 0, .tv_nsec = CHARGE_TICK_NANOSECONDS };
    struct timespec previous;
    clock_gettime(CLOCK_MONOTONIC, &previous);

    while (!isFullyCharged(vehicle)) {
        nanosleep(&tick, NULL);
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        addCharge(vehicle, power, secondsBetween(&previous, &now));
        previous = now;

        Message msg;
        ReceiveStatus status;
        while ((status = tryReceiveMessage(serverQueue, &msg, vehicleMsgType(vehicle->id))) == MSG_RECEIVED) {
            switch (msg.request) {
                case NOTIF_PING:
                    respondToPing(serverQueue, vehicle);
                    break;
                case NOTIF_FAULT:
                    return CHARGE_INTERRUPTED;
                case NOTIF_SHUTDOWN:
                    return CHARGE_STOPPED;
                default: // Sans objet pendant la recharge
                    break;
            }
        }
        if (status == MSG_CLOSED) {
            return CHARGE_STOPPED;
        }
        if (status == MSG_ERROR) {
            return CHARGE_FAILED;
        }
    }
    return CHARGE_COMPLETED;
}

// Recharge sur la borne attribuée ; renvoie true si le véhicule doit attendre la suite (redirection après panne)
static bool chargeOnAssignedCharger(struct Vehicle* vehicle, msgq_t serverQueue, const Message* assignment,
                                    VehicleOutcome* outcome) {
    // Puissance délivrée par la borne, sinon celle du véhicule si le serveur ne l'a pas précisée
    int power = assignment->desired_power > 0 ? assignment->desired_power : vehicle->maxPower;
    printf("[VÉHICULE %d] Borne %d de la station %d attribuée : recharge à %d kW.\n", vehicle->id,
           assignment->charger_id, assignment->station_id, effectiveChargingPower(vehicle->maxPower, power));

    switch (chargeUntilFull(vehicle, serverQueue, power)) {
        case CHARGE_COMPLETED:
            printf("[VÉHICULE %d] Recharge terminée, libération de la borne %d.\n", vehicle->id,
                   assignment->charger_id);
            *outcome = sendChargeEnd(serverQueue, vehicle, assignment->station_id, assignment->charger_id)
                     ? VEHICLE_CHARGED
                     : VEHICLE_ERROR;
            return false;

        case CHARGE_INTERRUPTED:
            printf("[VÉHICULE %d] Borne %d en panne (batterie %d %%) : en attente de redirection.\n", vehicle->id,
                   assignment->charger_id, batteryLevelPercent(vehicle));
            return true;

        case CHARGE_STOPPED:
            *outcome = VEHICLE_STOPPED;
            return false;

        case CHARGE_FAILED:
            *outcome = VEHICLE_ERROR;
            return false;
    }
    *outcome = VEHICLE_ERROR;
    return false;
}

VehicleOutcome runVehicle(struct Vehicle* vehicle, msgq_t serverQueue, int stationId) {
    if (!sendChargeRequest(serverQueue, vehicle, stationId)) {
        return VEHICLE_ERROR;
    }

    while (true) {
        Message msg;
        ReceiveStatus status = receiveMessage(serverQueue, &msg, vehicleMsgType(vehicle->id));
        if (status == MSG_CLOSED) {
            return VEHICLE_STOPPED;
        }
        if (status != MSG_RECEIVED) {
            return VEHICLE_ERROR;
        }

        VehicleOutcome outcome;
        switch (msg.request) {
            case RESP_ASSIGNED:
                if (!chargeOnAssignedCharger(vehicle, serverQueue, &msg, &outcome)) {
                    return outcome;
                }
                break;

            case RESP_QUEUED:
                printf("[VÉHICULE %d] En file d'attente (position %d).\n", vehicle->id, msg.queue_position);
                break;

            case RESP_REFUSED:
                printf("[VÉHICULE %d] Demande refusée.\n", vehicle->id);
                return VEHICLE_REFUSED;

            case NOTIF_PING:
                respondToPing(serverQueue, vehicle);
                break;

            case NOTIF_SHUTDOWN:
                return VEHICLE_STOPPED;

            default: // NOTIF_FAULT hors recharge, requêtes destinées au serveur : sans objet
                break;
        }
    }
}

void* vehicleThread(void* arg) {
    struct VehicleTrip* trip = arg;
    trip->outcome = runVehicle(trip->vehicle, trip->serverQueue, trip->stationId);
    return NULL;
}
