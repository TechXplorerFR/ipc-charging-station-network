#ifndef SCENARIO_H
#define SCENARIO_H

#include <stdbool.h>
#include "vehicle.h"

#define MAX_SCENARIO_VEHICLES 32

struct ScenarioVehicle {
    VehicleType type;
    double capacity;
    double initialCharge;
    int maxPower;
    const char* destination;
    bool isPrioritary;
    int stationId;
};

typedef enum {
    SCENARIO_CANCEL_WAITING,
    SCENARIO_FAULT_AND_REPAIR
} ScenarioEventType;

struct ScenarioEvent {
    ScenarioEventType type;
    int stationId;
    int vehicleId;
};

struct Scenario {
    const char* name;
    const char* description;
    int numStations;
    int chargersPerStation;
    const struct ScenarioVehicle* vehicles;
    int numVehicles;
    const struct ScenarioEvent* events;
    int numEvents;
};

const struct Scenario* findScenario(const char* name);
void printScenarios(void);

#endif
