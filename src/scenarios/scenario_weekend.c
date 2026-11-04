#include "scenario.h"

// Week-end : peu d'arrivées, mais des recharges plus longues (batteries moins pleines), sans urgence ni panne
static const struct GeneratorConfig generator = {
    .arrivalRate = 0.8,
    .maxVehicles = 16,
    .prioritaryRate = 0.0,
    .minInitialLevel = 0.85,
    .maxInitialLevel = 0.93,
    .seed = 2027,
};

const struct Scenario scenarioWeekend = {
    .name = "weekend",
    .description = "week-end : 3 stations de 4 bornes, 16 véhicules espacés, recharges plus longues",
    .numStations = 3,
    .chargersPerStation = 4,
    .generator = &generator,
};
