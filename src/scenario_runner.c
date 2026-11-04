#include "scenario.h"
#include "server.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// Exécution d'un scénario en cours : tout ce que les threads partagent et que le nettoyage doit libérer
struct ScenarioRun {
    const struct Scenario* scenario;
    struct Server server;
    bool serverStarted;
    pthread_t serverTid;

    struct Vehicle* vehicles[MAX_SCENARIO_VEHICLES];
    struct VehicleTrip trips[MAX_SCENARIO_VEHICLES];
    pthread_t vehicleTids[MAX_SCENARIO_VEHICLES];
    int startedVehicles;

    struct VehicleGenerator generator;
    struct GeneratorRun generatorRun;
    bool generatorStarted;
    pthread_t generatorTid;
    atomic_bool stopArrivals;

    struct FaultSimulator faultSimulator;
    struct FaultSimulatorRun faultRun;
    bool faultsStarted;
    pthread_t faultTid;
    atomic_bool stopFaults;
};

static double nowSeconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

static bool isValidEvent(const struct Scenario* scenario, const struct ScenarioEvent* event) {
    if (event->stationId < 0 || event->stationId >= scenario->numStations) {
        return false;
    }
    switch (event->type) {
        case SCENARIO_CANCEL_WAITING:
            return true;
        case SCENARIO_FAULT_AND_REPAIR:
            return event->vehicleId >= 0 && event->vehicleId < scenario->numVehicles;
    }
    return false;
}

// Configuration aléatoire du scénario, ramenée à son réseau ; NULL est accepté (pas de circulation aléatoire)
static struct GeneratorConfig generatorConfigFor(const struct Scenario* scenario, uint64_t seed) {
    struct GeneratorConfig config = *scenario->generator;
    config.numStations = scenario->numStations;
    if (seed != 0) {
        config.seed = seed;
    }
    return config;
}

static struct FaultSimulatorConfig faultConfigFor(const struct Scenario* scenario, uint64_t seed) {
    struct FaultSimulatorConfig config = *scenario->faults;
    config.numStations = scenario->numStations;
    config.chargersPerStation = scenario->chargersPerStation;
    if (seed != 0) {
        config.seed = seed + 1; // Suite de tirages différente de celle des arrivées
    }
    return config;
}

bool isValidScenario(const struct Scenario* scenario) {
    if (scenario == NULL || scenario->name == NULL || scenario->description == NULL
        || scenario->numStations <= 0 || scenario->numStations > MAX_STATIONS
        || scenario->chargersPerStation <= 0 || scenario->chargersPerStation > MAX_CHARGERS_PER_STATION
        || scenario->numVehicles < 0 || scenario->numVehicles > MAX_SCENARIO_VEHICLES
        || (scenario->numVehicles > 0 && scenario->vehicles == NULL)
        || scenario->numEvents < 0 || (scenario->numEvents > 0 && scenario->events == NULL)
        || (scenario->numVehicles == 0 && scenario->generator == NULL)) {
        return false;
    }

    for (int i = 0; i < scenario->numVehicles; i++) {
        int stationId = scenario->vehicles[i].stationId;
        if (stationId < 0 || stationId >= scenario->numStations) {
            return false;
        }
    }
    for (int i = 0; i < scenario->numEvents; i++) {
        if (!isValidEvent(scenario, &scenario->events[i])) {
            return false;
        }
    }

    if (scenario->generator != NULL) {
        struct GeneratorConfig config = generatorConfigFor(scenario, 0);
        if (!isValidGeneratorConfig(&config)) {
            return false;
        }
    }
    if (scenario->faults != NULL) {
        struct FaultSimulatorConfig config = faultConfigFor(scenario, 0);
        if (!isValidFaultSimulatorConfig(&config)) {
            return false;
        }
    }
    return true;
}

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
            // Identifiants des véhicules temporaires : juste après ceux des véhicules scriptés
            return demonstrateQueueCancellation(server, event->stationId, scenario->numVehicles + eventIndex);
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

static bool startThread(pthread_t* tid, void* (*routine)(void*), void* arg, const char* what) {
    int error = pthread_create(tid, NULL, routine, arg);
    if (error != 0) {
        fprintf(stderr, "[SIMULATION] Impossible de démarrer %s : %s.\n", what, strerror(error));
        return false;
    }
    return true;
}

static void joinThread(pthread_t tid, const char* what, bool* succeeded) {
    int error = pthread_join(tid, NULL);
    if (error != 0) {
        fprintf(stderr, "[SIMULATION] Impossible de rejoindre %s : %s.\n", what, strerror(error));
        *succeeded = false;
    }
}

static bool createScriptedVehicles(struct ScenarioRun* run) {
    const struct Scenario* scenario = run->scenario;
    for (int i = 0; i < scenario->numVehicles; i++) {
        const struct ScenarioVehicle* demo = &scenario->vehicles[i];
        run->vehicles[i] = createVehicle(i, demo->type, demo->capacity, demo->initialCharge, demo->maxPower,
                                         demo->destination, demo->isPrioritary);
        if (run->vehicles[i] == NULL) {
            return false;
        }
        run->trips[i] = (struct VehicleTrip){
            .vehicle = run->vehicles[i],
            .serverQueue = run->server.queue,
            .stationId = demo->stationId,
        };
    }
    return true;
}

// Démarre le serveur, les pannes, les arrivées aléatoires et les véhicules scriptés
static bool startSimulation(struct ScenarioRun* run, uint64_t seed) {
    const struct Scenario* scenario = run->scenario;
    run->serverStarted = startThread(&run->serverTid, runServerThread, &run->server, "le serveur");
    if (!run->serverStarted) {
        return false;
    }

    if (scenario->faults != NULL) {
        struct FaultSimulatorConfig config = faultConfigFor(scenario, seed);
        if (!initFaultSimulator(&run->faultSimulator, &config, faultSimulatorNow())) {
            return false;
        }
        run->faultRun = (struct FaultSimulatorRun){
            .simulator = &run->faultSimulator,
            .serverQueue = run->server.queue,
            .stop = &run->stopFaults,
        };
        run->faultsStarted = startThread(&run->faultTid, faultSimulatorThread, &run->faultRun,
                                         "le simulateur de pannes");
        if (!run->faultsStarted) {
            return false;
        }
    }

    if (scenario->generator != NULL) {
        // Après les véhicules scriptés et temporaires des événements
        struct GeneratorConfig config = generatorConfigFor(scenario, seed);
        if (!initVehicleGenerator(&run->generator, &config, scenario->numVehicles + scenario->numEvents)) {
            return false;
        }
        run->generatorRun = (struct GeneratorRun){
            .generator = &run->generator,
            .serverQueue = run->server.queue,
            .stop = &run->stopArrivals,
        };
        run->generatorStarted = startThread(&run->generatorTid, vehicleGeneratorThread, &run->generatorRun,
                                            "le générateur de véhicules");
        if (!run->generatorStarted) {
            return false;
        }
    }

    for (int i = 0; i < scenario->numVehicles; i++) {
        if (!startThread(&run->vehicleTids[i], vehicleThread, &run->trips[i], "un véhicule scripté")) {
            return false;
        }
        run->startedVehicles++;
    }
    return true;
}

// Arrêt d'urgence : plus d'arrivées ni de pannes, arrêt du serveur, puis suppression de la file pour que
// les véhicules qui attendent encore une réponse (MSG_CLOSED) se terminent
static void abortSimulation(struct ScenarioRun* run, bool* succeeded) {
    atomic_store(&run->stopArrivals, true);
    atomic_store(&run->stopFaults, true);
    if (!run->serverStarted) {
        return;
    }
    requestServerShutdown(&run->server);
    joinThread(run->serverTid, "le thread serveur", succeeded);
    run->serverStarted = false;
    destroyMessageQueue(run->server.queue);
    run->server.queue = -1;
}

// Attend la fin des véhicules (scriptés puis aléatoires), des pannes et du serveur
static void finishSimulation(struct ScenarioRun* run, bool* succeeded, struct ScenarioResult* result) {
    for (int i = 0; i < run->startedVehicles; i++) {
        bool joined = true;
        joinThread(run->vehicleTids[i], "un véhicule scripté", &joined);
        if (!joined) {
            *succeeded = false;
            continue;
        }
        VehicleOutcome outcome = run->trips[i].outcome;
        result->charged += outcome == VEHICLE_CHARGED ? 1 : 0;
        result->refused += outcome == VEHICLE_REFUSED ? 1 : 0;
        if (outcome != VEHICLE_CHARGED) {
            fprintf(stderr, "[SIMULATION] Le véhicule %d n'a pas terminé sa recharge (issue %d).\n", i, outcome);
            *succeeded = false;
        }
    }
    result->vehicles = run->startedVehicles;

    if (run->generatorStarted) {
        joinThread(run->generatorTid, "le générateur de véhicules", succeeded);
        const struct GeneratorReport* report = &run->generatorRun.report;
        result->vehicles += report->generated;
        result->charged += report->charged;
        result->refused += report->refused;
        if (!run->generatorRun.succeeded) {
            *succeeded = false;
        }
    }

    // Plus aucun véhicule : les pannes s'arrêtent et les bornes encore en panne sont réparées
    atomic_store(&run->stopFaults, true);
    if (run->faultsStarted) {
        joinThread(run->faultTid, "le simulateur de pannes", succeeded);
        if (!run->faultRun.succeeded) {
            *succeeded = false;
        }
    }

    if (run->serverStarted) {
        if (!requestServerShutdown(&run->server)) {
            *succeeded = false;
        }
        joinThread(run->serverTid, "le thread serveur", succeeded);
        run->serverStarted = false;
    }
}

static void collectServerResult(struct Server* server, struct ScenarioResult* result) {
    struct ServerSummary summary = summarizeServer(server);
    result->interruptedCharges = server->stats.interruptedCharges;
    result->faults = server->stats.faults;
    result->redirections = server->stats.redirections;
    result->averageWaitMinutes = simulatedMinutes(summary.averageWaitSeconds);
    result->occupancyRate = summary.occupancyRate;
    result->energyKWh = server->stats.deliveredEnergyKWh;
    result->revenue = summary.history.revenue;
}

bool runScenario(const struct Scenario* scenario, uint64_t seed, bool printReport, struct ScenarioResult* result) {
    *result = (struct ScenarioResult){
        .name = scenario != NULL ? scenario->name : "?",
        .randomTraffic = scenario != NULL && scenario->generator != NULL,
    };
    if (!isValidScenario(scenario)) {
        fprintf(stderr, "[SIMULATION] Configuration invalide pour le scénario « %s ».\n", result->name);
        return false;
    }

    // Statique : l'exécution regroupe des centaines de Ko (stations, véhicules) ; un scénario à la fois
    static struct ScenarioRun run;
    memset(&run, 0, sizeof(run));
    run.scenario = scenario;
    atomic_init(&run.stopArrivals, false);
    atomic_init(&run.stopFaults, false);

    printf("=== Scénario : %s — %s ===\n", scenario->name, scenario->description);
    if (!initServer(&run.server, scenario->numStations, scenario->chargersPerStation)) {
        fprintf(stderr, "[SIMULATION] Impossible d'initialiser le serveur.\n");
        return false;
    }

    double start = nowSeconds();
    bool succeeded = createScriptedVehicles(&run) && startSimulation(&run, seed);
    for (int i = 0; i < scenario->numEvents && succeeded; i++) {
        succeeded = runScenarioEvent(&run.server, scenario, &scenario->events[i], i);
    }
    if (!succeeded) {
        abortSimulation(&run, &succeeded);
    }
    finishSimulation(&run, &succeeded, result);
    result->durationSeconds = nowSeconds() - start;
    collectServerResult(&run.server, result);
    result->succeeded = succeeded;

    if (printReport) {
        printServerReport(&run.server);
        if (scenario->generator != NULL) {
            printGeneratorReport(&run.generatorRun.report);
        }
        if (scenario->faults != NULL) {
            printFaultSimulatorReport(&run.faultSimulator);
        }
    }

    shutdownServer(&run.server);
    for (int i = 0; i < scenario->numVehicles; i++) {
        destroyVehicle(run.vehicles[i]);
    }
    return succeeded;
}

void printScenarioComparison(const char* title, const struct ScenarioResult* results, int count) {
    printf("\n========== %s ==========\n", title);
    // En-tête et type alignés à la main : les lettres accentuées occupent deux octets, ce qui fausse les largeurs
    // de printf
    printf("Scénario        Type      Véhicules Rechargés Refusés  Pannes Redir.   Interr. Attente moy  Occupation"
           "        CA   Durée\n");
    for (int i = 0; i < count; i++) {
        const struct ScenarioResult* r = &results[i];
        printf("%-15s %s %9d %9d %7d %7d %6d %9d %7.1f min %9.1f %% %7.2f € %5.1f s%s\n", r->name,
               r->randomTraffic ? "aléatoire" : "scripté  ", r->vehicles, r->charged, r->refused, r->faults,
               r->redirections, r->interruptedCharges, r->averageWaitMinutes, r->occupancyRate * 100.0, r->revenue,
               r->durationSeconds, r->succeeded ? "" : "  (échec)");
    }
}
