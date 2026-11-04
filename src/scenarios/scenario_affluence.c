#include "scenario.h"

static const struct ScenarioVehicle vehicles[] = {
    { CAR, 50.0, 49.5, 50, "Belfort", false, 0 },
    { VAN, 80.0, 79.5, 50, "Montbeliard", false, 0 },
    { CAR, 50.0, 49.5, 50, "Belfort", true, 0 },
    { BUS, 200.0, 199.5, 150, "Montbeliard", true, 0 },
    { CAR, 50.0, 49.5, 50, "Belfort", false, 0 },
    { VAN, 80.0, 79.5, 50, "Montbeliard", false, 0 },
};

static const struct ScenarioEvent events[] = {
    { SCENARIO_CANCEL_WAITING, 0, -1 },
};

const struct Scenario scenarioAffluence = {
    .name = "affluence",
    .description = "1 station, 1 borne, 6 véhicules et une annulation en file",
    .numStations = 1,
    .chargersPerStation = 1,
    .vehicles = vehicles,
    .numVehicles = (int)(sizeof(vehicles) / sizeof(vehicles[0])),
    .events = events,
    .numEvents = (int)(sizeof(events) / sizeof(events[0])),
};
