#include "scenario.h"
#include "server.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int waitForVehicleCharger(struct Server* server, int stationId, int vehicleId) {
    const struct timespec retry = { .tv_sec = 0, .tv_nsec = 10000000L };
    struct Station* station = &server->stations[stationId];

    for (int attempt = 0; attempt < 500; attempt++) {
        int chargerId = -1;
        lockStation(station);
        for (int i = 0; i < station->numChargers; i++) {
            const struct Charger* charger = &station->chargers[i];
            if (charger->status == OCCUPIED && charger->session.vehicleId == vehicleId) {
                chargerId = i;
                break;
            }
        }
        unlockStation(station);
        if (chargerId != -1) {
            return chargerId;
        }
        nanosleep(&retry, NULL);
    }

    return -1;
}

static bool demonstrateQueueCancellation(struct Server* server, int stationId, int vehicleId) {
    Message demand = {
        .request = REQ_CHARGE_DEMAND,
        .vehicle_id = vehicleId,
        .station_id = stationId,
        .charger_id = -1,
        .desired_power = 50,
        .estimated_duration = 1,
    };
    if (!sendToServer(server->queue, &demand)) {
        fprintf(stderr, "[SIMULATION] Impossible d'envoyer la demande d'annulation.\n");
        return false;
    }

    Message response;
    if (receiveMessage(server->queue, &response, vehicleMsgType(vehicleId)) != MSG_RECEIVED
        || response.request != RESP_QUEUED) {
        fprintf(stderr, "[SIMULATION] Le véhicule temporaire n'a pas été mis en attente.\n");
        return false;
    }

    Message cancel = {
        .request = REQ_CANCEL,
        .vehicle_id = vehicleId,
        .station_id = stationId,
    };
    if (!sendToServer(server->queue, &cancel)) {
        fprintf(stderr, "[SIMULATION] Impossible d'annuler la demande en attente.\n");
        return false;
    }
    printf("[SIMULATION] Le véhicule temporaire %d a annulé sa demande en attente.\n", vehicleId);
    return true;
}

static bool demonstrateFaultAndRepair(struct Server* server, int stationId, int vehicleId) {
    int chargerId = waitForVehicleCharger(server, stationId, vehicleId);
    if (chargerId == -1) {
        fprintf(stderr, "[SIMULATION] Le véhicule %d n'a pas obtenu de borne à temps pour la panne.\n", vehicleId);
        return false;
    }

    Message fault = {
        .request = REQ_ALERT,
        .vehicle_id = -1,
        .station_id = stationId,
        .charger_id = chargerId,
    };
    if (!sendToServer(server->queue, &fault)) {
        fprintf(stderr, "[SIMULATION] Impossible de signaler la panne de la borne %d.\n", chargerId);
        return false;
    }

    Message repair = {
        .request = REQ_REPAIR,
        .vehicle_id = -1,
        .station_id = stationId,
        .charger_id = chargerId,
    };
    if (!sendToServer(server->queue, &repair)) {
        fprintf(stderr, "[SIMULATION] Impossible de demander la réparation de la borne %d.\n", chargerId);
        return false;
    }
    return true;
}

static bool runScenarioEvent(struct Server* server, const struct Scenario* scenario,
                             const struct ScenarioEvent* event, int eventIndex) {
    switch (event->type) {
        case SCENARIO_CANCEL_WAITING:
            return demonstrateQueueCancellation(server, event->stationId,
                                                scenario->numVehicles + eventIndex);
        case SCENARIO_FAULT_AND_REPAIR:
            return demonstrateFaultAndRepair(server, event->stationId, event->vehicleId);
    }
    fprintf(stderr, "[SIMULATION] Type d'événement inconnu dans le scénario.\n");
    return false;
}

static void* runServerThread(void* arg) {
    runServer(arg);
    return NULL;
}

static bool requestServerShutdown(struct Server* server) {
    Message shutdown = { .request = REQ_SHUTDOWN };
    if (sendToServer(server->queue, &shutdown)) {
        return true;
    }

    fprintf(stderr, "[SIMULATION] Impossible d'envoyer la demande d'arrêt du serveur.\n");
    destroyMessageQueue(server->queue);
    server->queue = -1;
    return false;
}

static int runScenario(const struct Scenario* scenario) {
    struct Server server;
    struct Vehicle* vehicles[MAX_SCENARIO_VEHICLES] = { 0 };
    struct VehicleTrip trips[MAX_SCENARIO_VEHICLES] = { 0 };
    pthread_t serverTid;
    pthread_t vehicleThreads[MAX_SCENARIO_VEHICLES];
    int startedVehicles = 0;
    bool serverStarted = false;
    bool simulationSucceeded = true;

    if (scenario->numStations <= 0 || scenario->numStations > MAX_STATIONS
        || scenario->chargersPerStation <= 0 || scenario->chargersPerStation > MAX_CHARGERS_PER_STATION
        || scenario->numVehicles <= 0 || scenario->numVehicles > MAX_SCENARIO_VEHICLES
        || scenario->vehicles == NULL || scenario->numEvents < 0
        || (scenario->numEvents > 0 && scenario->events == NULL)) {
        fprintf(stderr, "[SIMULATION] Configuration invalide pour le scénario « %s ».\n", scenario->name);
        return 1;
    }
    for (int i = 0; i < scenario->numEvents; i++) {
        const struct ScenarioEvent* event = &scenario->events[i];
        if (event->stationId < 0 || event->stationId >= scenario->numStations
            || (event->type == SCENARIO_FAULT_AND_REPAIR
                && (event->vehicleId < 0 || event->vehicleId >= scenario->numVehicles))) {
            fprintf(stderr, "[SIMULATION] Événement invalide dans le scénario « %s ».\n", scenario->name);
            return 1;
        }
    }

    printf("=== Scénario : %s — %s ===\n", scenario->name, scenario->description);
    if (!initServer(&server, scenario->numStations, scenario->chargersPerStation)) {
        fprintf(stderr, "[SIMULATION] Impossible d'initialiser le serveur.\n");
        return 1;
    }

    for (int i = 0; i < scenario->numVehicles; i++) {
        const struct ScenarioVehicle* demo = &scenario->vehicles[i];
        if (demo->stationId < 0 || demo->stationId >= scenario->numStations) {
            fprintf(stderr, "[SIMULATION] Station invalide pour le véhicule %d.\n", i);
            simulationSucceeded = false;
            goto cleanup;
        }
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
        fprintf(stderr, "[SIMULATION] Impossible de démarrer le serveur : %s.\n", strerror(error));
        simulationSucceeded = false;
        goto cleanup;
    }
    serverStarted = true;

    for (int i = 0; i < scenario->numVehicles; i++) {
        error = pthread_create(&vehicleThreads[i], NULL, vehicleThread, &trips[i]);
        if (error != 0) {
            fprintf(stderr, "[SIMULATION] Impossible de démarrer le véhicule %d : %s.\n", i, strerror(error));
            simulationSucceeded = false;
            break;
        }
        startedVehicles++;
    }

    for (int i = 0; i < scenario->numEvents && simulationSucceeded; i++) {
        if (!runScenarioEvent(&server, scenario, &scenario->events[i], i)) {
            simulationSucceeded = false;
        }
    }

    if (!simulationSucceeded || startedVehicles < scenario->numVehicles) {
        requestServerShutdown(&server);
    }

    for (int i = 0; i < startedVehicles; i++) {
        error = pthread_join(vehicleThreads[i], NULL);
        if (error != 0) {
            fprintf(stderr, "[SIMULATION] Impossible de rejoindre le thread du véhicule %d : %s.\n",
                    i, strerror(error));
            simulationSucceeded = false;
        } else if (trips[i].outcome != VEHICLE_CHARGED) {
            fprintf(stderr, "[SIMULATION] Le véhicule %d n'a pas terminé sa recharge (issue %d).\n",
                    i, trips[i].outcome);
            simulationSucceeded = false;
        }
    }

    if (simulationSucceeded && !requestServerShutdown(&server)) {
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
    for (int i = 0; i < scenario->numVehicles; i++) {
        destroyVehicle(vehicles[i]);
    }
    return simulationSucceeded ? 0 : 1;
}

int main(int argc, char* argv[]) {
    if (argc > 2 || (argc == 2 && (strcmp(argv[1], "--list") == 0 || strcmp(argv[1], "-l") == 0))) {
        printScenarios();
        return argc > 2 ? 1 : 0;
    }

    const char* scenarioName = argc == 2 ? argv[1] : "standard";
    const struct Scenario* scenario = findScenario(scenarioName);
    if (scenario == NULL) {
        fprintf(stderr, "[SIMULATION] Scénario inconnu : %s\n", scenarioName);
        printScenarios();
        return 1;
    }
    return runScenario(scenario);
}
