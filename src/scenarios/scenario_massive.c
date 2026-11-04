#include "scenario.h"

// Arrivée massive : 60 véhicules en quelques secondes ; les files d'attente absorbent le pic
static const struct GeneratorConfig generator = {
    .arrivalRate = 25.0,
    .maxVehicles = 60,
    .prioritaryRate = 0.05,
    .minInitialLevel = 0.93,
    .maxInitialLevel = 0.97,
    .seed = 2029,
};

const struct Scenario scenarioMassive = {
    .name = "massive",
    .description = "arrivée massive : 3 stations de 4 bornes, 60 véhicules en quelques secondes",
    .numStations = 3,
    .chargersPerStation = 4,
    .generator = &generator,
};
