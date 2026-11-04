#include "scenario.h"
#include <stdio.h>
#include <string.h>

extern const struct Scenario scenarioAffluence;
extern const struct Scenario scenarioMassive;
extern const struct Scenario scenarioPanne;
extern const struct Scenario scenarioPanneGenerale;
extern const struct Scenario scenarioPetit;
extern const struct Scenario scenarioPointe;
extern const struct Scenario scenarioStandard;
extern const struct Scenario scenarioWeekend;

// Scénarios scriptés (démonstration d'un mécanisme), puis circulation aléatoire (comparaison des performances)
static const struct Scenario* const scenarios[] = {
    &scenarioStandard,
    &scenarioPanne,
    &scenarioAffluence,
    &scenarioPetit,
    &scenarioPointe,
    &scenarioWeekend,
    &scenarioPanneGenerale,
    &scenarioMassive,
};
#define NUM_SCENARIOS ((int)(sizeof(scenarios) / sizeof(scenarios[0])))

int scenarioCount(void) {
    return NUM_SCENARIOS;
}

const struct Scenario* scenarioAt(int index) {
    return index >= 0 && index < NUM_SCENARIOS ? scenarios[index] : NULL;
}

const struct Scenario* findScenario(const char* name) {
    for (int i = 0; i < NUM_SCENARIOS; i++) {
        if (strcmp(scenarios[i]->name, name) == 0) {
            return scenarios[i];
        }
    }
    return NULL;
}

void printScenarios(void) {
    printf("Scénarios disponibles :\n");
    for (int i = 0; i < NUM_SCENARIOS; i++) {
        printf("  %-15s %s\n", scenarios[i]->name, scenarios[i]->description);
    }
}
