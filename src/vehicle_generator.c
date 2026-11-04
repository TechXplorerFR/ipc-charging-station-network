#include "vehicle_generator.h"
#include "random.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Pas de l'attente entre deux arrivées : *stop est relu à chaque pas
#define ARRIVAL_SLEEP_STEP_SECONDS 0.05

// Caractéristiques tirées pour chaque type de véhicule
struct VehicleProfile {
    VehicleType type;
    double weight;      // Proportion de ce type parmi les arrivées
    double minCapacity; // kWh
    double maxCapacity; // kWh
    int maxPower;       // kW
    bool alwaysPrioritary;
};

// Les bus (transport public) sont toujours prioritaires
static const struct VehicleProfile PROFILES[] = {
    { CAR, 0.70, 40.0, 80.0, 100, false },
    { VAN, 0.20, 60.0, 100.0, 50, false },
    { BUS, 0.10, 200.0, 350.0, 150, true },
};
#define NUM_PROFILES ((int)(sizeof(PROFILES) / sizeof(PROFILES[0])))

static const char* const DESTINATIONS[] = { "Belfort", "Montbéliard", "Besançon", "Mulhouse", "Héricourt", "Delle" };
#define NUM_DESTINATIONS ((int)(sizeof(DESTINATIONS) / sizeof(DESTINATIONS[0])))

const struct GeneratorConfig DEFAULT_GENERATOR_CONFIG = {
    .arrivalRate = 0.5,
    .numStations = 1,
    .maxVehicles = 20,
    .prioritaryRate = 0.05,
    .minInitialLevel = 0.10,
    .maxInitialLevel = 0.80,
    .seed = 0,
};

static const struct VehicleProfile* drawProfile(struct VehicleGenerator* generator) {
    double totalWeight = 0.0;
    for (int i = 0; i < NUM_PROFILES; i++) {
        totalWeight += PROFILES[i].weight;
    }

    double draw = randomUnit(&generator->rngState) * totalWeight;
    for (int i = 0; i < NUM_PROFILES - 1; i++) {
        if (draw < PROFILES[i].weight) {
            return &PROFILES[i];
        }
        draw -= PROFILES[i].weight;
    }
    return &PROFILES[NUM_PROFILES - 1];
}

bool isValidGeneratorConfig(const struct GeneratorConfig* config) {
    return config != NULL
        && config->arrivalRate > 0.0
        && config->numStations > 0
        && config->maxVehicles > 0 && config->maxVehicles <= MAX_GENERATED_VEHICLES
        && config->prioritaryRate >= 0.0 && config->prioritaryRate <= 1.0
        && config->minInitialLevel >= 0.0 && config->minInitialLevel <= config->maxInitialLevel
        && config->maxInitialLevel <= 1.0;
}

bool initVehicleGenerator(struct VehicleGenerator* generator, const struct GeneratorConfig* config, int firstId) {
    if (generator == NULL || !isValidGeneratorConfig(config) || firstId < 0) {
        fprintf(stderr, "[GENERATEUR] Erreur : configuration invalide.\n");
        return false;
    }

    generator->config = *config;
    generator->rngState = config->seed != 0 ? config->seed : randomClockSeed();
    generator->nextId = firstId;
    generator->generated = 0;
    return true;
}

double nextArrivalDelaySeconds(struct VehicleGenerator* generator) {
    return randomExponential(&generator->rngState, generator->config.arrivalRate);
}

struct Vehicle* generateVehicle(struct VehicleGenerator* generator, int* stationId) {
    if (generator->generated >= generator->config.maxVehicles) {
        return NULL;
    }

    const struct VehicleProfile* profile = drawProfile(generator);
    // Capacité arrondie au kWh pour des affichages lisibles
    double capacity = round(randomBetween(&generator->rngState, profile->minCapacity, profile->maxCapacity));
    double level = randomBetween(&generator->rngState, generator->config.minInitialLevel, generator->config.maxInitialLevel);
    bool prioritary = profile->alwaysPrioritary || randomUnit(&generator->rngState) < generator->config.prioritaryRate;
    const char* destination = DESTINATIONS[randomIndex(&generator->rngState, NUM_DESTINATIONS)];
    int station = randomIndex(&generator->rngState, generator->config.numStations);

    struct Vehicle* vehicle = createVehicle(generator->nextId, profile->type, capacity, capacity * level,
                                            profile->maxPower, destination, prioritary);
    if (vehicle == NULL) {
        return NULL;
    }

    generator->nextId++;
    generator->generated++;
    if (stationId != NULL) {
        *stationId = station;
    }
    return vehicle;
}

static bool isStopRequested(const atomic_bool* stop) {
    return stop != NULL && atomic_load(stop);
}

// Attend delay secondes par petits pas ; renvoie false si l'arrêt est demandé entre-temps
static bool waitForArrival(double delay, const atomic_bool* stop) {
    while (delay > 0.0) {
        if (isStopRequested(stop)) {
            return false;
        }
        double step = delay < ARRIVAL_SLEEP_STEP_SECONDS ? delay : ARRIVAL_SLEEP_STEP_SECONDS;
        struct timespec pause = { .tv_sec = 0, .tv_nsec = (long)(step * 1e9) };
        nanosleep(&pause, NULL);
        delay -= step;
    }
    return !isStopRequested(stop);
}

static void countOutcome(struct GeneratorReport* report, VehicleOutcome outcome) {
    switch (outcome) {
        case VEHICLE_CHARGED:
            report->charged++;
            break;
        case VEHICLE_REFUSED:
            report->refused++;
            break;
        case VEHICLE_STOPPED:
            report->stopped++;
            break;
        case VEHICLE_ERROR:
            report->errors++;
            break;
    }
}

bool runVehicleGenerator(struct VehicleGenerator* generator, msgq_t serverQueue, const atomic_bool* stop,
                         struct GeneratorReport* report) {
    *report = (struct GeneratorReport){ 0 };
    int capacity = generator->config.maxVehicles - generator->generated;
    if (capacity <= 0) {
        return true;
    }

    // Tableaux alloués une fois pour toutes : l'adresse de chaque trajet reste fixe pendant son thread
    struct VehicleTrip* trips = calloc((size_t)capacity, sizeof(struct VehicleTrip));
    pthread_t* threads = calloc((size_t)capacity, sizeof(pthread_t));
    if (trips == NULL || threads == NULL) {
        perror("[GENERATEUR] Erreur calloc");
        free(trips);
        free(threads);
        report->errors++;
        return false;
    }

    int started = 0;
    while (started < capacity && waitForArrival(nextArrivalDelaySeconds(generator), stop)) {
        int stationId = 0;
        struct Vehicle* vehicle = generateVehicle(generator, &stationId);
        if (vehicle == NULL) {
            report->errors++;
            break;
        }

        printf("[GENERATEUR] Arrivée du véhicule %d : %s de %.0f kWh (batterie %d %%) vers %s, station %d%s.\n",
               vehicle->id, vehicleTypeName(vehicle->type), vehicle->capacity, batteryLevelPercent(vehicle),
               vehicle->destination, stationId, vehicle->isPrioritary ? ", prioritaire" : "");

        trips[started] = (struct VehicleTrip){ .vehicle = vehicle, .serverQueue = serverQueue, .stationId = stationId };
        int error = pthread_create(&threads[started], NULL, vehicleThread, &trips[started]);
        if (error != 0) {
            fprintf(stderr, "[GENERATEUR] Impossible de démarrer le véhicule %d : %s.\n", vehicle->id, strerror(error));
            destroyVehicle(vehicle);
            report->errors++;
            break;
        }
        started++;
    }
    report->generated = started;

    for (int i = 0; i < started; i++) {
        int error = pthread_join(threads[i], NULL);
        if (error != 0) {
            // Le thread peut encore utiliser le véhicule : il n'est pas libéré
            fprintf(stderr, "[GENERATEUR] Impossible de rejoindre le véhicule %d : %s.\n", trips[i].vehicle->id,
                    strerror(error));
            report->errors++;
            continue;
        }
        countOutcome(report, trips[i].outcome);
        destroyVehicle(trips[i].vehicle);
    }

    free(trips);
    free(threads);
    return report->errors == 0;
}

void* vehicleGeneratorThread(void* arg) {
    struct GeneratorRun* run = arg;
    run->succeeded = runVehicleGenerator(run->generator, run->serverQueue, run->stop, &run->report);
    return NULL;
}

void printGeneratorReport(const struct GeneratorReport* report) {
    printf("[GENERATEUR] %d véhicules générés : %d rechargés, %d refusés, %d arrêtés, %d erreurs.\n",
           report->generated, report->charged, report->refused, report->stopped, report->errors);
}
