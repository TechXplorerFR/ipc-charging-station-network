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

static const struct ScenarioEvent events[] = {
    { SCENARIO_CANCEL_WAITING, 0, -1 },
    { SCENARIO_FAULT_AND_REPAIR, 0, 0 },
};

const struct Scenario scenarioPanne = {
    .name = "panne",
    .description = "2 stations, 2 bornes par station, annulation et panne/réparation",
    .numStations = 2,
    .chargersPerStation = 2,
    .vehicles = vehicles,
    .numVehicles = (int)(sizeof(vehicles) / sizeof(vehicles[0])),
    .events = events,
    .numEvents = (int)(sizeof(events) / sizeof(events[0])),
};
