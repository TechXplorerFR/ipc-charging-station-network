#include "scenario.h"
#include <stdio.h>
#include <string.h>

extern const struct Scenario scenarioAffluence;
extern const struct Scenario scenarioPanne;
extern const struct Scenario scenarioPetit;
extern const struct Scenario scenarioStandard;

static const struct Scenario* const scenarios[] = {
    &scenarioStandard,
    &scenarioPanne,
    &scenarioAffluence,
    &scenarioPetit,
};

const struct Scenario* findScenario(const char* name) {
    for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++) {
        if (strcmp(scenarios[i]->name, name) == 0) {
            return scenarios[i];
        }
    }
    return NULL;
}

void printScenarios(void) {
    printf("Scénarios disponibles :\n");
    for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++) {
        printf("  %-10s %s\n", scenarios[i]->name, scenarios[i]->description);
    }
}
