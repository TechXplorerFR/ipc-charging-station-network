#include "server.h"
#include "test_utils.h"
#include "vehicle_generator.h"
#include <pthread.h>

// Utilise la vraie clé de la file du serveur : ne pas lancer pendant une simulation.

static struct GeneratorConfig testConfig(void) {
    struct GeneratorConfig config = DEFAULT_GENERATOR_CONFIG;
    config.seed = 42;
    return config;
}

static void testConfigValidation(void) {
    struct GeneratorConfig config = testConfig();
    struct VehicleGenerator generator;
    CHECK(isValidGeneratorConfig(&config));
    CHECK(initVehicleGenerator(&generator, &config, 0));
    CHECK(!initVehicleGenerator(&generator, &config, -1));
    CHECK(!isValidGeneratorConfig(NULL));

    config = testConfig();
    config.arrivalRate = 0.0;
    CHECK(!isValidGeneratorConfig(&config));

    config = testConfig();
    config.numStations = 0;
    CHECK(!isValidGeneratorConfig(&config));

    config = testConfig();
    config.maxVehicles = MAX_GENERATED_VEHICLES + 1;
    CHECK(!isValidGeneratorConfig(&config));

    config = testConfig();
    config.prioritaryRate = 1.5;
    CHECK(!isValidGeneratorConfig(&config));

    config = testConfig();
    config.minInitialLevel = 0.9;
    config.maxInitialLevel = 0.5;
    CHECK(!isValidGeneratorConfig(&config));
}

static void testVehiclesRespectConfig(void) {
    struct GeneratorConfig config = testConfig();
    config.numStations = 3;
    config.maxVehicles = MAX_GENERATED_VEHICLES;
    config.prioritaryRate = 0.0;
    struct VehicleGenerator generator;
    if (!initVehicleGenerator(&generator, &config, 10)) {
        CHECK(false);
        return;
    }

    bool stationSeen[3] = { false, false, false };
    bool typeSeen[3] = { false, false, false };
    for (int i = 0; i < config.maxVehicles; i++) {
        int stationId = -1;
        struct Vehicle* vehicle = generateVehicle(&generator, &stationId);
        CHECK(vehicle != NULL);
        if (vehicle == NULL) {
            return;
        }
        CHECK(vehicle->id == 10 + i);
        CHECK(stationId >= 0 && stationId < 3);
        if (stationId >= 0 && stationId < 3) {
            stationSeen[stationId] = true;
        }
        typeSeen[vehicle->type] = true;
        double level = vehicle->currentCharge / vehicle->capacity;
        CHECK(level >= config.minInitialLevel - 1e-9 && level <= config.maxInitialLevel + 1e-9);
        CHECK(vehicle->maxPower > 0);
        // Sans urgence tirée, seuls les bus sont prioritaires
        CHECK(vehicle->isPrioritary == (vehicle->type == BUS));
        destroyVehicle(vehicle);
    }
    CHECK(stationSeen[0] && stationSeen[1] && stationSeen[2]);
    CHECK(typeSeen[CAR] && typeSeen[VAN] && typeSeen[BUS]);

    // maxVehicles atteint
    CHECK(generateVehicle(&generator, NULL) == NULL);
    CHECK(generator.generated == config.maxVehicles);
}

static void testSameSeedGivesSameSequence(void) {
    struct GeneratorConfig config = testConfig();
    struct VehicleGenerator first;
    struct VehicleGenerator second;
    if (!initVehicleGenerator(&first, &config, 0) || !initVehicleGenerator(&second, &config, 0)) {
        CHECK(false);
        return;
    }

    for (int i = 0; i < 5; i++) {
        CHECK(nextArrivalDelaySeconds(&first) == nextArrivalDelaySeconds(&second));
        int firstStation = -1;
        int secondStation = -1;
        struct Vehicle* a = generateVehicle(&first, &firstStation);
        struct Vehicle* b = generateVehicle(&second, &secondStation);
        CHECK(a != NULL && b != NULL);
        if (a != NULL && b != NULL) {
            CHECK(a->type == b->type);
            CHECK(a->capacity == b->capacity);
            CHECK(a->currentCharge == b->currentCharge);
            CHECK(firstStation == secondStation);
        }
        destroyVehicle(a);
        destroyVehicle(b);
    }
}

static void testArrivalDelaysFollowExponentialLaw(void) {
    struct GeneratorConfig config = testConfig();
    config.arrivalRate = 4.0;
    struct VehicleGenerator generator;
    if (!initVehicleGenerator(&generator, &config, 0)) {
        CHECK(false);
        return;
    }

    // Moyenne attendue : 1 / λ = 0,25 s
    const int samples = 20000;
    double sum = 0.0;
    for (int i = 0; i < samples; i++) {
        double delay = nextArrivalDelaySeconds(&generator);
        CHECK(delay >= 0.0);
        sum += delay;
    }
    double mean = sum / samples;
    CHECK(mean > 0.24 && mean < 0.26);
}

static void testStopBeforeFirstArrival(void) {
    struct GeneratorConfig config = testConfig();
    config.arrivalRate = 0.001; // Première arrivée dans ~1000 s : seul l'arrêt peut terminer le test
    struct VehicleGenerator generator;
    if (!initVehicleGenerator(&generator, &config, 0)) {
        CHECK(false);
        return;
    }

    atomic_bool stop = true;
    struct GeneratorReport report;
    CHECK(runVehicleGenerator(&generator, -1, &stop, &report));
    CHECK(report.generated == 0);
    CHECK(generator.generated == 0);
}

static void* serverThread(void* server) {
    runServer(server);
    return NULL;
}

static void testGeneratedVehiclesChargeThroughServer(void) {
    struct Server server;
    if (!initServer(&server, 2, 3)) {
        CHECK(false);
        return;
    }
    pthread_t serverTid;
    CHECK(pthread_create(&serverTid, NULL, serverThread, &server) == 0);

    // Batteries presque pleines : chaque recharge dure moins d'une seconde
    struct GeneratorConfig config = testConfig();
    config.arrivalRate = 20.0;
    config.numStations = 2;
    config.maxVehicles = 8;
    config.minInitialLevel = 0.998;
    config.maxInitialLevel = 0.999;
    struct VehicleGenerator generator;
    CHECK(initVehicleGenerator(&generator, &config, 0));

    struct GeneratorRun run = { .generator = &generator, .serverQueue = server.queue, .stop = NULL };
    pthread_t generatorTid;
    CHECK(pthread_create(&generatorTid, NULL, vehicleGeneratorThread, &run) == 0);
    pthread_join(generatorTid, NULL);

    CHECK(run.succeeded);
    CHECK(run.report.generated == 8);
    CHECK(run.report.charged == 8);
    CHECK(run.report.errors == 0);
    printGeneratorReport(&run.report);

    Message shutdown = { .request = REQ_SHUTDOWN };
    CHECK(sendToServer(server.queue, &shutdown));
    pthread_join(serverTid, NULL);
    CHECK(server.stats.completedCharges == 8);
    shutdownServer(&server);
}

int main(void) {
    RUN_TEST(testConfigValidation);
    RUN_TEST(testVehiclesRespectConfig);
    RUN_TEST(testSameSeedGivesSameSequence);
    RUN_TEST(testArrivalDelaysFollowExponentialLaw);
    RUN_TEST(testStopBeforeFirstArrival);
    RUN_TEST(testGeneratedVehiclesChargeThroughServer);
    return TEST_RESULT();
}
