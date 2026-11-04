#include "fault_simulator.h"
#include "server.h"
#include "test_utils.h"
#include <math.h>
#include <pthread.h>
#include <time.h>

// Utilise la vraie clé de la file du serveur : ne pas lancer pendant une simulation.
// Hors test de bout en bout, le test lit lui-même les messages destinés au serveur.
static msgq_t queue;

static struct FaultSimulatorConfig testConfig(void) {
    struct FaultSimulatorConfig config = DEFAULT_FAULT_SIMULATOR_CONFIG;
    config.numStations = 2;
    config.chargersPerStation = 2;
    config.minRepairSeconds = 10.0;
    config.maxRepairSeconds = 20.0;
    config.maxConcurrentFaults = 4;
    config.seed = 7;
    return config;
}

// Prochain message reçu par le serveur (request vaut REQ_SHUTDOWN s'il n'y en a pas)
static Message nextServerMessage(void) {
    Message msg = { .request = REQ_SHUTDOWN };
    CHECK(tryReceiveMessage(queue, &msg, SERVER_MSG_TYPE) == MSG_RECEIVED);
    return msg;
}

static bool noServerMessage(void) {
    Message msg;
    return tryReceiveMessage(queue, &msg, SERVER_MSG_TYPE) == MSG_NONE;
}

// Vide la file entre deux tests
static void drainServerMessages(void) {
    while (!noServerMessage()) {
    }
}

static void testConfigValidation(void) {
    struct FaultSimulatorConfig config = testConfig();
    struct FaultSimulator simulator;
    CHECK(isValidFaultSimulatorConfig(&config));
    CHECK(isValidFaultSimulatorConfig(&DEFAULT_FAULT_SIMULATOR_CONFIG));
    CHECK(initFaultSimulator(&simulator, &config, 0.0));
    CHECK(simulator.nextFaultAt >= 0.0);
    CHECK(!isValidFaultSimulatorConfig(NULL));

    config = testConfig();
    config.faultRate = 0.0;
    CHECK(!isValidFaultSimulatorConfig(&config));

    config = testConfig();
    config.chargersPerStation = 0;
    CHECK(!isValidFaultSimulatorConfig(&config));

    config = testConfig();
    config.minRepairSeconds = 30.0;
    CHECK(!isValidFaultSimulatorConfig(&config));

    config = testConfig();
    config.maxConcurrentFaults = 5; // Plus que les 4 bornes du réseau
    CHECK(!isValidFaultSimulatorConfig(&config));
    CHECK(!initFaultSimulator(&simulator, &config, 0.0));
}

static void testFaultsHitDistinctChargersUpToLimit(void) {
    struct FaultSimulatorConfig config = testConfig();
    struct FaultSimulator simulator;
    if (!initFaultSimulator(&simulator, &config, 0.0)) {
        CHECK(false);
        return;
    }

    bool hit[2][2] = { { false, false }, { false, false } };
    for (int i = 0; i < 4; i++) {
        CHECK(triggerRandomFault(&simulator, queue, 100.0));
        Message alert = nextServerMessage();
        CHECK(alert.request == REQ_ALERT);
        CHECK(isWellFormedMessage(&alert));
        CHECK(alert.station_id >= 0 && alert.station_id < 2 && alert.charger_id >= 0 && alert.charger_id < 2);
        if (alert.station_id >= 0 && alert.station_id < 2 && alert.charger_id >= 0 && alert.charger_id < 2) {
            CHECK(!hit[alert.station_id][alert.charger_id]);
            hit[alert.station_id][alert.charger_id] = true;
            CHECK(isSimulatedFault(&simulator, alert.station_id, alert.charger_id));
        }
    }

    // Toutes les bornes sont en panne
    CHECK(!triggerRandomFault(&simulator, queue, 100.0));
    CHECK(noServerMessage());
    CHECK(simulator.stats.faults == 4);

    for (int i = 0; i < simulator.numFaults; i++) {
        CHECK(simulator.faults[i].repairAt >= 110.0 && simulator.faults[i].repairAt <= 120.0);
    }
    CHECK(repairDueChargers(&simulator, queue, INFINITY) == 4);
    for (int i = 0; i < 4; i++) {
        CHECK(nextServerMessage().request == REQ_REPAIR);
    }
    CHECK(simulator.numFaults == 0);
}

static void testOnlyDueChargersAreRepaired(void) {
    struct FaultSimulatorConfig config = testConfig();
    config.maxConcurrentFaults = 2;
    struct FaultSimulator simulator;
    if (!initFaultSimulator(&simulator, &config, 0.0)) {
        CHECK(false);
        return;
    }

    CHECK(triggerRandomFault(&simulator, queue, 0.0));
    CHECK(triggerRandomFault(&simulator, queue, 50.0));
    nextServerMessage();
    nextServerMessage();

    // Aucune réparation n'est due avant 10 s ; la première l'est à 30 s, la seconde non (60 s au plus tôt)
    CHECK(repairDueChargers(&simulator, queue, 5.0) == 0);
    CHECK(noServerMessage());
    struct SimulatedFault second = simulator.faults[1];
    CHECK(repairDueChargers(&simulator, queue, 30.0) == 1);
    Message repair = nextServerMessage();
    CHECK(repair.request == REQ_REPAIR);
    CHECK(isWellFormedMessage(&repair));
    CHECK(simulator.numFaults == 1);
    CHECK(isSimulatedFault(&simulator, second.stationId, second.chargerId));

    // La borne réparée peut retomber en panne
    CHECK(triggerRandomFault(&simulator, queue, 30.0));
    nextServerMessage();

    CHECK(repairDueChargers(&simulator, queue, INFINITY) == 2);
    CHECK(nextServerMessage().request == REQ_REPAIR);
    CHECK(nextServerMessage().request == REQ_REPAIR);
    CHECK(simulator.numFaults == 0);
    CHECK(simulator.stats.repairs == 3);
}

static void testStepFollowsSchedule(void) {
    struct FaultSimulatorConfig config = testConfig();
    config.maxConcurrentFaults = 1;
    struct FaultSimulator simulator;
    if (!initFaultSimulator(&simulator, &config, 0.0)) {
        CHECK(false);
        return;
    }

    double firstFault = simulator.nextFaultAt;
    stepFaultSimulator(&simulator, queue, firstFault - 0.001);
    CHECK(noServerMessage());

    stepFaultSimulator(&simulator, queue, firstFault);
    CHECK(nextServerMessage().request == REQ_ALERT);
    CHECK(simulator.nextFaultAt > firstFault);

    // Limite atteinte : une panne prévue avant la réparation n'a pas lieu, la suivante est tirée
    double repairAt = simulator.faults[0].repairAt;
    simulator.nextFaultAt = repairAt - 1.0;
    stepFaultSimulator(&simulator, queue, repairAt - 1.0);
    CHECK(noServerMessage());
    CHECK(simulator.stats.faults == 1);
    CHECK(simulator.nextFaultAt > repairAt - 1.0);

    // À la réparation, la borne est remise en service avant toute nouvelle panne
    simulator.nextFaultAt = repairAt;
    stepFaultSimulator(&simulator, queue, repairAt);
    CHECK(nextServerMessage().request == REQ_REPAIR);
    CHECK(nextServerMessage().request == REQ_ALERT);
    CHECK(simulator.stats.faults == 2);

    repairDueChargers(&simulator, queue, INFINITY);
    drainServerMessages();
}

static void* serverThread(void* server) {
    runServer(server);
    return NULL;
}

static void testFaultsAndRepairsReachServer(void) {
    destroyMessageQueue(queue);
    struct Server server;
    if (!initServer(&server, 2, 3)) {
        CHECK(false);
        queue = createServerQueue();
        return;
    }
    pthread_t serverTid;
    CHECK(pthread_create(&serverTid, NULL, serverThread, &server) == 0);

    // Pannes fréquentes et réparations rapides pendant une seconde
    struct FaultSimulatorConfig config = DEFAULT_FAULT_SIMULATOR_CONFIG;
    config.faultRate = 20.0;
    config.numStations = 2;
    config.chargersPerStation = 3;
    config.minRepairSeconds = 0.05;
    config.maxRepairSeconds = 0.2;
    config.maxConcurrentFaults = 3;
    config.seed = 11;
    struct FaultSimulator simulator;
    CHECK(initFaultSimulator(&simulator, &config, faultSimulatorNow()));

    atomic_bool stop = false;
    struct FaultSimulatorRun run = { .simulator = &simulator, .serverQueue = server.queue, .stop = &stop };
    pthread_t simulatorTid;
    CHECK(pthread_create(&simulatorTid, NULL, faultSimulatorThread, &run) == 0);
    struct timespec duration = { .tv_sec = 1, .tv_nsec = 0 };
    nanosleep(&duration, NULL);
    atomic_store(&stop, true);
    pthread_join(simulatorTid, NULL);

    CHECK(run.succeeded);
    CHECK(simulator.stats.faults > 0);
    CHECK(simulator.stats.repairs == simulator.stats.faults);
    CHECK(simulator.numFaults == 0);
    printFaultSimulatorReport(&simulator);

    // La file est FIFO : le serveur a traité toutes les alertes et réparations avant l'arrêt
    Message shutdown = { .request = REQ_SHUTDOWN };
    CHECK(sendToServer(server.queue, &shutdown));
    pthread_join(serverTid, NULL);
    CHECK(server.stats.faults == simulator.stats.faults);
    CHECK(server.stats.repairs == simulator.stats.repairs);
    for (int s = 0; s < server.numStations; s++) {
        for (int c = 0; c < server.stations[s].numChargers; c++) {
            CHECK(server.stations[s].chargers[c].status == AVAILABLE);
        }
    }
    shutdownServer(&server);
    queue = createServerQueue();
}

int main(void) {
    queue = createServerQueue();
    if (queue == -1) {
        return 1;
    }

    RUN_TEST(testConfigValidation);
    RUN_TEST(testFaultsHitDistinctChargersUpToLimit);
    RUN_TEST(testOnlyDueChargersAreRepaired);
    RUN_TEST(testStepFollowsSchedule);
    RUN_TEST(testFaultsAndRepairsReachServer);

    destroyMessageQueue(queue);
    return TEST_RESULT();
}
