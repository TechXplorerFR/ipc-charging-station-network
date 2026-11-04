#include "scenario.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void printUsage(const char* program) {
    printf("Usage : %s [scénario] [--seed N]\n", program);
    printf("        %s --compare [--seed N]   compare les scénarios à circulation aléatoire\n", program);
    printf("        %s --list                 liste les scénarios\n", program);
    printf("Sans scénario, « standard » est joué. --seed change les tirages aléatoires (0 : graine du scénario).\n");
}

static bool parseSeed(const char* text, uint64_t* seed) {
    char* end;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || text[0] == '-') {
        return false;
    }
    *seed = (uint64_t)value;
    return true;
}

// Le journal détaillé de chaque scénario est masqué pendant la comparaison : seul le tableau final compte
static int silenceStdout(void) {
    fflush(stdout);
    int saved = dup(STDOUT_FILENO);
    int devNull = open("/dev/null", O_WRONLY);
    if (saved != -1 && devNull != -1) {
        dup2(devNull, STDOUT_FILENO);
    }
    if (devNull != -1) {
        close(devNull);
    }
    return saved;
}

static void restoreStdout(int saved) {
    fflush(stdout);
    if (saved != -1) {
        dup2(saved, STDOUT_FILENO);
        close(saved);
    }
}

static int compareScenarios(uint64_t seed) {
    struct ScenarioResult results[32];
    int count = 0;
    bool allSucceeded = true;
    for (int i = 0; i < scenarioCount() && count < (int)(sizeof(results) / sizeof(results[0])); i++) {
        const struct Scenario* scenario = scenarioAt(i);
        if (scenario->generator == NULL) {
            continue;
        }
        fprintf(stderr, "[SIMULATION] Scénario %s en cours...\n", scenario->name);
        int saved = silenceStdout();
        allSucceeded = runScenario(scenario, seed, false, &results[count]) && allSucceeded;
        restoreStdout(saved);
        count++;
    }
    printScenarioComparison(results, count);
    return allSucceeded ? 0 : 1;
}

int main(int argc, char* argv[]) {
    const char* scenarioName = "standard";
    bool compare = false;
    uint64_t seed = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0 || strcmp(argv[i], "-l") == 0) {
            printScenarios();
            return 0;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--compare") == 0) {
            compare = true;
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            if (!parseSeed(argv[++i], &seed)) {
                fprintf(stderr, "[SIMULATION] Graine invalide : %s\n", argv[i]);
                return 1;
            }
        } else if (argv[i][0] != '-') {
            scenarioName = argv[i];
        } else {
            fprintf(stderr, "[SIMULATION] Option inconnue : %s\n", argv[i]);
            printUsage(argv[0]);
            return 1;
        }
    }

    if (compare) {
        return compareScenarios(seed);
    }

    const struct Scenario* scenario = findScenario(scenarioName);
    if (scenario == NULL) {
        fprintf(stderr, "[SIMULATION] Scénario inconnu : %s\n", scenarioName);
        printScenarios();
        return 1;
    }
    if (seed != 0) {
        printf("[SIMULATION] Graine des tirages aléatoires : %" PRIu64 "\n", seed);
    }

    struct ScenarioResult result;
    return runScenario(scenario, seed, true, &result) ? 0 : 1;
}
