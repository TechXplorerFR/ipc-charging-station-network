#include "scenario.h"
#include "test_utils.h"
#include <string.h>

// Utilise la vraie clé de la file du serveur : ne pas lancer pendant une simulation.

static void testRegisteredScenariosAreValid(void) {
    CHECK(scenarioCount() >= 8);
    CHECK(scenarioAt(-1) == NULL);
    CHECK(scenarioAt(scenarioCount()) == NULL);

    for (int i = 0; i < scenarioCount(); i++) {
        const struct Scenario* scenario = scenarioAt(i);
        CHECK(isValidScenario(scenario));
        CHECK(findScenario(scenario->name) == scenario);
        for (int j = 0; j < i; j++) {
            CHECK(strcmp(scenarioAt(j)->name, scenario->name) != 0);
        }
    }
    CHECK(findScenario("inexistant") == NULL);

    // Les scénarios du sujet : heure de pointe, week-end, panne généralisée, arrivée massive
    const char* const randomScenarios[] = { "pointe", "weekend", "panne-generale", "massive" };
    for (int i = 0; i < 4; i++) {
        const struct Scenario* scenario = findScenario(randomScenarios[i]);
        CHECK(scenario != NULL && scenario->generator != NULL);
    }
}

static const struct ScenarioVehicle oneVehicle[] = {
    { CAR, 50.0, 49.9, 50, "Belfort", false, 0 },
};

static void testInvalidScenariosAreRejected(void) {
    CHECK(!isValidScenario(NULL));

    struct Scenario scenario = {
        .name = "test",
        .description = "test",
        .numStations = 1,
        .chargersPerStation = 1,
        .vehicles = oneVehicle,
        .numVehicles = 1,
    };
    CHECK(isValidScenario(&scenario));

    scenario.numStations = 0;
    CHECK(!isValidScenario(&scenario));
    scenario.numStations = 1;

    // Ni véhicule scripté, ni arrivées aléatoires
    scenario.numVehicles = 0;
    CHECK(!isValidScenario(&scenario));
    scenario.numVehicles = 1;

    const struct ScenarioEvent faultOnUnknownVehicle[] = { { SCENARIO_FAULT_AND_REPAIR, 0, 5 } };
    scenario.events = faultOnUnknownVehicle;
    scenario.numEvents = 1;
    CHECK(!isValidScenario(&scenario));
    scenario.numEvents = 0;

    struct GeneratorConfig tooManyVehicles = DEFAULT_GENERATOR_CONFIG;
    tooManyVehicles.maxVehicles = MAX_GENERATED_VEHICLES + 1;
    scenario.generator = &tooManyVehicles;
    CHECK(!isValidScenario(&scenario));
    scenario.generator = NULL;

    // Plus de pannes simultanées que de bornes dans le réseau du scénario
    struct FaultSimulatorConfig tooManyFaults = DEFAULT_FAULT_SIMULATOR_CONFIG;
    tooManyFaults.maxConcurrentFaults = 2;
    scenario.faults = &tooManyFaults;
    CHECK(!isValidScenario(&scenario));

    struct ScenarioResult result;
    CHECK(!runScenario(&scenario, 0, false, &result));
    CHECK(!result.succeeded);
}

static void testScriptedScenarioChargesEveryVehicle(void) {
    struct ScenarioResult result;
    CHECK(runScenario(findScenario("affluence"), 0, false, &result));
    CHECK(result.succeeded);
    CHECK(strcmp(result.name, "affluence") == 0);
    CHECK(result.vehicles == 6);
    CHECK(result.charged == 6);
    CHECK(result.refused == 0);
    CHECK(result.faults == 0);
    CHECK(result.revenue > 0.0);
    CHECK(result.occupancyRate > 0.0 && result.occupancyRate <= 1.0);
}

static void testRandomScenarioWithFaults(void) {
    // Batteries presque pleines et réparations rapides : une à deux secondes au total
    const struct GeneratorConfig generator = {
        .arrivalRate = 20.0,
        .maxVehicles = 10,
        .prioritaryRate = 0.2,
        .minInitialLevel = 0.995,
        .maxInitialLevel = 0.998,
        .seed = 5,
    };
    const struct FaultSimulatorConfig faults = {
        .faultRate = 10.0,
        .minRepairSeconds = 0.05,
        .maxRepairSeconds = 0.1,
        .maxConcurrentFaults = 1,
        .seed = 6,
    };
    const struct ScenarioVehicle scripted[] = {
        { VAN, 80.0, 79.8, 50, "Belfort", false, 1 },
    };
    const struct Scenario scenario = {
        .name = "test-aleatoire",
        .description = "véhicules scriptés et aléatoires, pannes rapides",
        .numStations = 2,
        .chargersPerStation = 2,
        .vehicles = scripted,
        .numVehicles = 1,
        .generator = &generator,
        .faults = &faults,
    };

    struct ScenarioResult result;
    CHECK(runScenario(&scenario, 0, true, &result));
    CHECK(result.succeeded);
    CHECK(result.vehicles == 11);
    CHECK(result.charged + result.refused == 11);
    CHECK(result.charged >= 1);
    CHECK(result.interruptedCharges <= result.faults);
    CHECK(result.energyKWh > 0.0);
    CHECK(result.durationSeconds > 0.0);

    // Même graine, même suite d'arrivées : autant de véhicules
    struct ScenarioResult again;
    CHECK(runScenario(&scenario, 42, false, &again));
    CHECK(again.vehicles == 11);

    printScenarioComparison((struct ScenarioResult[]){ result, again }, 2);
}

int main(void) {
    RUN_TEST(testRegisteredScenariosAreValid);
    RUN_TEST(testInvalidScenariosAreRejected);
    RUN_TEST(testScriptedScenarioChargesEveryVehicle);
    RUN_TEST(testRandomScenarioWithFaults);
    return TEST_RESULT();
}
