#ifndef SCENARIO_H
#define SCENARIO_H

#include <stdbool.h>
#include <stdint.h>
#include "fault_simulator.h"
#include "vehicle.h"
#include "vehicle_generator.h"

#define MAX_SCENARIO_VEHICLES 32

// Véhicule écrit en dur dans un scénario scripté
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
    SCENARIO_CANCEL_WAITING,  // Un véhicule temporaire se met en file sur stationId puis annule
    SCENARIO_FAULT_AND_REPAIR // Panne puis réparation de la borne du véhicule vehicleId sur stationId
} ScenarioEventType;

struct ScenarioEvent {
    ScenarioEventType type;
    int stationId;
    int vehicleId;
};

// Un scénario combine des véhicules et événements scriptés (reproductibles, pour démontrer un mécanisme)
// et une circulation aléatoire : arrivées selon un processus de Poisson et pannes aléatoires. Les champs
// numStations et chargersPerStation de generator et faults sont remplacés par ceux du scénario.
struct Scenario {
    const char* name;
    const char* description;
    int numStations;
    int chargersPerStation;
    const struct ScenarioVehicle* vehicles; // Véhicules scriptés, identifiants 0 à numVehicles - 1
    int numVehicles;
    const struct ScenarioEvent* events;     // Joués dans l'ordre une fois les véhicules scriptés lancés
    int numEvents;
    const struct GeneratorConfig* generator; // Arrivées aléatoires ; NULL : aucune
    const struct FaultSimulatorConfig* faults; // Pannes aléatoires pendant les arrivées ; NULL : aucune
};

// Mesures d'une exécution, pour comparer l'effet de la charge et des pannes sur les performances
struct ScenarioResult {
    const char* name;
    bool succeeded;            // Pas d'erreur, et tous les véhicules scriptés ont terminé leur recharge
    int vehicles;              // Véhicules lancés, scriptés et aléatoires
    int charged;               // Véhicules repartis batterie pleine
    int refused;               // Véhicules repartis sans recharge (file pleine, attente trop longue)
    int interruptedCharges;    // Recharges interrompues par une panne
    int faults;
    int redirections;          // Véhicules redirigés vers une autre station après une panne
    double averageWaitMinutes; // Attente moyenne avant d'obtenir une borne (minutes simulées)
    double occupancyRate;      // Taux d'occupation moyen des bornes (0 à 1)
    double energyKWh;
    double revenue;            // €
    double durationSeconds;    // Durée réelle de la simulation
};

int scenarioCount(void);
// Scénario n° index dans l'ordre d'affichage, NULL si l'indice est invalide
const struct Scenario* scenarioAt(int index);
const struct Scenario* findScenario(const char* name);
void printScenarios(void);

// Vérifie la cohérence du scénario (tailles, stations des véhicules et des événements, configurations)
bool isValidScenario(const struct Scenario* scenario);

// Joue le scénario de bout en bout : serveur, véhicules scriptés et aléatoires, événements et pannes dans
// leurs threads, puis rapport du serveur si printReport. seed remplace la graine des tirages aléatoires
// (0 : celle du scénario). Renvoie result->succeeded.
bool runScenario(const struct Scenario* scenario, uint64_t seed, bool printReport, struct ScenarioResult* result);

// Tableau des résultats de plusieurs scénarios, sous le titre donné
void printScenarioComparison(const char* title, const struct ScenarioResult* results, int count);

#endif
