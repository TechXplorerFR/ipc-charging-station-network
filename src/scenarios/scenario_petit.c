#include "scenario.h"

static const struct ScenarioVehicle vehicles[] = {
    { CAR, 50.0, 46.0, 50, "Belfort", false, 0 },
    { VAN, 80.0, 76.0, 50, "Montbeliard", false, 0 },
    { BUS, 200.0, 196.0, 150, "Belfort", true, 0 },
};

const struct Scenario scenarioPetit = {
    .name = "petit",
    .description = "1 station, 3 bornes et 3 véhicules",
    .numStations = 1,
    .chargersPerStation = 3,
    .vehicles = vehicles,
    .numVehicles = (int)(sizeof(vehicles) / sizeof(vehicles[0])),
};
