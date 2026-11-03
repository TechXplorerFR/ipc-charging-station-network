#include "server.h"
#include "vehicle.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define DEMO_STATIONS 2
#define DEMO_CHARGERS_PER_STATION 2
#define DEMO_VEHICLES 8

struct DemoVehicle {
    VehicleType type;
    double capacity;
    double initialCharge;
    int maxPower;
    const char* destination;
    bool isPrioritary;
    int stationId;
};

static const struct DemoVehicle demoVehicles[DEMO_VEHICLES] = {
    { CAR, 50.0, 49.0, 50, "Belfort", false, 0 },
    { VAN, 80.0, 79.0, 50, "Montbeliard", false, 0 },
    { BUS, 200.0, 199.0, 150, "Belfort", false, 0 },
    { CAR, 50.0, 49.0, 50, "Montbeliard", true, 0 },
    { CAR, 50.0, 49.0, 50, "Belfort", false, 1 },
    { VAN, 80.0, 79.0, 50, "Montbeliard", false, 1 },
    { BUS, 200.0, 199.0, 150, "Belfort", false, 1 },
    { CAR, 50.0, 49.0, 50, "Montbeliard", true, 1 },
};

static void destroyVehicles(struct Vehicle* vehicles[DEMO_VEHICLES]) {
    for (int i = 0; i < DEMO_VEHICLES; i++) {
        destroyVehicle(vehicles[i]);
    }
}

static bool requestServerShutdown(struct Server* server) {
    Message shutdown = { .request = REQ_SHUTDOWN };
    if (sendToServer(server->queue, &shutdown)) {
        return true;
    }

    fprintf(stderr, "[SIMULATION] Impossible d'envoyer la demande d'arret du serveur.\n");
    destroyMessageQueue(server->queue);
    server->queue = -1;
    return false;
}

static void* runServerThread(void* arg) {
    runServer(arg);
    return NULL;
}

int main(void) {
    struct Server server;
    struct Vehicle* vehicles[DEMO_VEHICLES] = { 0 };
    struct VehicleTrip trips[DEMO_VEHICLES] = { 0 };
    pthread_t serverTid;
    pthread_t vehicleThreads[DEMO_VEHICLES];
    int startedVehicles = 0;
    bool serverStarted = false;
    bool simulationSucceeded = true;

    printf("=== Simulation du reseau de bornes de recharge ===\n");
    if (!initServer(&server, DEMO_STATIONS, DEMO_CHARGERS_PER_STATION)) {
        fprintf(stderr, "[SIMULATION] Impossible d'initialiser le serveur.\n");
        return 1;
    }

    for (int i = 0; i < DEMO_VEHICLES; i++) {
        const struct DemoVehicle* demo = &demoVehicles[i];
        vehicles[i] = createVehicle(i, demo->type, demo->capacity, demo->initialCharge, demo->maxPower,
                                    demo->destination, demo->isPrioritary);
        if (vehicles[i] == NULL) {
            simulationSucceeded = false;
            goto cleanup;
        }
        trips[i] = (struct VehicleTrip){
            .vehicle = vehicles[i],
            .serverQueue = server.queue,
            .stationId = demo->stationId,
        };
    }

    int error = pthread_create(&serverTid, NULL, runServerThread, &server);
    if (error != 0) {
        fprintf(stderr, "[SIMULATION] Impossible de demarrer le serveur : %s.\n", strerror(error));
        simulationSucceeded = false;
        goto cleanup;
    }
    serverStarted = true;

    for (int i = 0; i < DEMO_VEHICLES; i++) {
        error = pthread_create(&vehicleThreads[i], NULL, vehicleThread, &trips[i]);
        if (error != 0) {
            fprintf(stderr, "[SIMULATION] Impossible de demarrer le vehicule %d : %s.\n", i, strerror(error));
            simulationSucceeded = false;
            break;
        }
        startedVehicles++;
    }

    if (startedVehicles < DEMO_VEHICLES) {
        requestServerShutdown(&server);
    }

    for (int i = 0; i < startedVehicles; i++) {
        error = pthread_join(vehicleThreads[i], NULL);
        if (error != 0) {
            fprintf(stderr, "[SIMULATION] Impossible de rejoindre le thread du vehicule %d : %s.\n",
                    i, strerror(error));
            simulationSucceeded = false;
        } else if (trips[i].outcome != VEHICLE_CHARGED) {
            fprintf(stderr, "[SIMULATION] Le vehicule %d n'a pas termine sa recharge (issue %d).\n",
                    i, trips[i].outcome);
            simulationSucceeded = false;
        }
    }

    if (startedVehicles == DEMO_VEHICLES && !requestServerShutdown(&server)) {
        simulationSucceeded = false;
    }

    error = pthread_join(serverTid, NULL);
    if (error != 0) {
        fprintf(stderr, "[SIMULATION] Impossible de rejoindre le thread serveur : %s.\n", strerror(error));
        simulationSucceeded = false;
    }
    serverStarted = false;

    printServerReport(&server);

cleanup:
    if (serverStarted) {
        requestServerShutdown(&server);
        pthread_join(serverTid, NULL);
    }
    shutdownServer(&server);
    destroyVehicles(vehicles);
    return simulationSucceeded ? 0 : 1;
}
