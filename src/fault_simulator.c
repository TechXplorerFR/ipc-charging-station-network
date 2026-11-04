#include "fault_simulator.h"
#include "random.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

// Intervalle entre deux pas de simulation : *stop est relu à chaque pas
#define FAULT_STEP_NANOSECONDS 50000000L // 50 ms

const struct FaultSimulatorConfig DEFAULT_FAULT_SIMULATOR_CONFIG = {
    .faultRate = 0.05,
    .numStations = 1,
    .chargersPerStation = 3,
    .minRepairSeconds = 5.0,
    .maxRepairSeconds = 15.0,
    .maxConcurrentFaults = 1,
    .seed = 0,
};

static int totalChargers(const struct FaultSimulatorConfig* config) {
    return config->numStations * config->chargersPerStation;
}

bool isValidFaultSimulatorConfig(const struct FaultSimulatorConfig* config) {
    return config != NULL
        && config->faultRate > 0.0
        && config->numStations > 0
        && config->chargersPerStation > 0
        && config->minRepairSeconds >= 0.0 && config->minRepairSeconds <= config->maxRepairSeconds
        && config->maxConcurrentFaults > 0 && config->maxConcurrentFaults <= MAX_SIMULATED_FAULTS
        && config->maxConcurrentFaults <= totalChargers(config);
}

double faultSimulatorNow(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

static void scheduleNextFault(struct FaultSimulator* simulator, double now) {
    simulator->nextFaultAt = now + randomExponential(&simulator->rngState, simulator->config.faultRate);
}

bool initFaultSimulator(struct FaultSimulator* simulator, const struct FaultSimulatorConfig* config, double now) {
    if (simulator == NULL || !isValidFaultSimulatorConfig(config)) {
        fprintf(stderr, "[PANNES] Erreur : configuration invalide.\n");
        return false;
    }

    simulator->config = *config;
    simulator->rngState = config->seed != 0 ? config->seed : randomClockSeed();
    simulator->numFaults = 0;
    simulator->stats = (struct FaultSimulatorStats){ 0 };
    scheduleNextFault(simulator, now);
    return true;
}

bool isSimulatedFault(const struct FaultSimulator* simulator, int stationId, int chargerId) {
    for (int i = 0; i < simulator->numFaults; i++) {
        if (simulator->faults[i].stationId == stationId && simulator->faults[i].chargerId == chargerId) {
            return true;
        }
    }
    return false;
}

// Signalement envoyé comme le ferait un capteur de borne : sans véhicule associé
static bool sendChargerEvent(struct FaultSimulator* simulator, msgq_t serverQueue, RequestType request,
                             int stationId, int chargerId) {
    Message msg = { .request = request, .vehicle_id = -1, .station_id = stationId, .charger_id = chargerId };
    if (!sendToServer(serverQueue, &msg)) {
        simulator->stats.errors++;
        return false;
    }
    return true;
}

bool triggerRandomFault(struct FaultSimulator* simulator, msgq_t serverQueue, double now) {
    const struct FaultSimulatorConfig* config = &simulator->config;
    if (simulator->numFaults >= config->maxConcurrentFaults) {
        return false;
    }

    // Tirage uniforme parmi les bornes encore en service (maxConcurrentFaults <= total : il en reste une)
    int remaining = randomIndex(&simulator->rngState, totalChargers(config) - simulator->numFaults);
    int stationId = 0;
    int chargerId = 0;
    for (int index = 0; index < totalChargers(config); index++) {
        stationId = index / config->chargersPerStation;
        chargerId = index % config->chargersPerStation;
        if (isSimulatedFault(simulator, stationId, chargerId)) {
            continue;
        }
        if (remaining == 0) {
            break;
        }
        remaining--;
    }

    if (!sendChargerEvent(simulator, serverQueue, REQ_ALERT, stationId, chargerId)) {
        return false;
    }

    double repairDelay = randomBetween(&simulator->rngState, config->minRepairSeconds, config->maxRepairSeconds);
    simulator->faults[simulator->numFaults++] = (struct SimulatedFault){
        .stationId = stationId,
        .chargerId = chargerId,
        .repairAt = now + repairDelay,
    };
    simulator->stats.faults++;
    printf("[PANNES] Panne de la borne %d de la station %d, réparation dans %.1f s.\n", chargerId, stationId,
           repairDelay);
    return true;
}

int repairDueChargers(struct FaultSimulator* simulator, msgq_t serverQueue, double now) {
    int repaired = 0;
    int i = 0;
    while (i < simulator->numFaults) {
        struct SimulatedFault fault = simulator->faults[i];
        if (fault.repairAt > now) {
            i++;
            continue;
        }
        // En cas d'échec d'envoi, la borne est oubliée : la renvoyer à chaque pas inonderait le journal
        if (sendChargerEvent(simulator, serverQueue, REQ_REPAIR, fault.stationId, fault.chargerId)) {
            simulator->stats.repairs++;
            repaired++;
            printf("[PANNES] Borne %d de la station %d réparée.\n", fault.chargerId, fault.stationId);
        }
        simulator->faults[i] = simulator->faults[--simulator->numFaults];
    }
    return repaired;
}

void stepFaultSimulator(struct FaultSimulator* simulator, msgq_t serverQueue, double now) {
    repairDueChargers(simulator, serverQueue, now);
    if (now < simulator->nextFaultAt) {
        return;
    }
    // Réseau déjà au maximum de pannes : cette panne n'a pas lieu, la suivante est tirée normalement
    triggerRandomFault(simulator, serverQueue, now);
    scheduleNextFault(simulator, now);
}

bool runFaultSimulator(struct FaultSimulator* simulator, msgq_t serverQueue, const atomic_bool* stop) {
    int initialErrors = simulator->stats.errors;
    const struct timespec pause = { .tv_sec = 0, .tv_nsec = FAULT_STEP_NANOSECONDS };
    while (!atomic_load(stop)) {
        stepFaultSimulator(simulator, serverQueue, faultSimulatorNow());
        nanosleep(&pause, NULL);
    }

    repairDueChargers(simulator, serverQueue, INFINITY);
    return simulator->stats.errors == initialErrors;
}

void* faultSimulatorThread(void* arg) {
    struct FaultSimulatorRun* run = arg;
    run->succeeded = runFaultSimulator(run->simulator, run->serverQueue, run->stop);
    return NULL;
}

void printFaultSimulatorReport(const struct FaultSimulator* simulator) {
    printf("[PANNES] %d pannes simulées, %d réparations, %d erreurs d'envoi.\n", simulator->stats.faults,
           simulator->stats.repairs, simulator->stats.errors);
}
