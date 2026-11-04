#include "scenario.h"

// Heure de pointe : arrivées soutenues sur tout le réseau, quelques véhicules d'urgence et de rares pannes
static const struct GeneratorConfig generator = {
    .arrivalRate = 3.0,
    .maxVehicles = 45,
    .prioritaryRate = 0.05,
    .minInitialLevel = 0.88,
    .maxInitialLevel = 0.96,
    .seed = 2026,
};

static const struct FaultSimulatorConfig faults = {
    .faultRate = 0.1,
    .minRepairSeconds = 3.0,
    .maxRepairSeconds = 6.0,
    .maxConcurrentFaults = 1,
    .seed = 70,
};

const struct Scenario scenarioPointe = {
    .name = "pointe",
    .description = "heure de pointe : 3 stations de 4 bornes, 45 véhicules aléatoires, pannes rares",
    .numStations = 3,
    .chargersPerStation = 4,
    .generator = &generator,
    .faults = &faults,
};
