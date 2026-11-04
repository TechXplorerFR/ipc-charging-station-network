#include "scenario.h"

static const struct ScenarioVehicle vehicles[] = {
    { CAR, 50.0, 46.0, 50, "Belfort", false, 0 },
    { VAN, 80.0, 76.0, 50, "Montbeliard", false, 0 },
    { BUS, 200.0, 196.0, 150, "Belfort", false, 0 },
    { CAR, 50.0, 46.0, 50, "Montbeliard", true, 0 },
    { CAR, 50.0, 46.0, 50, "Belfort", false, 1 },
    { VAN, 80.0, 76.0, 50, "Montbeliard", false, 1 },
    { BUS, 200.0, 196.0, 150, "Belfort", false, 1 },
    { CAR, 50.0, 46.0, 50, "Montbeliard", true, 1 },
};

const struct Scenario scenarioStandard = {
    .name = "standard",
    .description = "2 stations, 2 bornes par station, 8 véhicules",
    .numStations = 2,
    .chargersPerStation = 2,
    .vehicles = vehicles,
    .numVehicles = (int)(sizeof(vehicles) / sizeof(vehicles[0])),
};
