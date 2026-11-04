#include "scenario.h"

// Panne généralisée : jusqu'aux deux tiers des bornes en panne en même temps ; les véhicules interrompus
// sont redirigés et les files s'allongent
static const struct GeneratorConfig generator = {
    .arrivalRate = 1.5,
    .maxVehicles = 30,
    .prioritaryRate = 0.05,
    .minInitialLevel = 0.94,
    .maxInitialLevel = 0.98,
    .seed = 2028,
};

static const struct FaultSimulatorConfig faults = {
    .faultRate = 1.5,
    .minRepairSeconds = 3.0,
    .maxRepairSeconds = 6.0,
    .maxConcurrentFaults = 8,
    .seed = 71,
};

const struct Scenario scenarioPanneGenerale = {
    .name = "panne-generale",
    .description = "panne généralisée : 3 stations de 4 bornes, jusqu'à 8 bornes en panne à la fois",
    .numStations = 3,
    .chargersPerStation = 4,
    .generator = &generator,
    .faults = &faults,
};
