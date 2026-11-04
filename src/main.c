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
    printf("        %s --compare [--all] [--quiet] [--seed N]   joue les scénarios et les compare\n", program);
    printf("        %s --list                                   liste les scénarios\n", program);
    printf("Sans scénario, « standard » est joué. --seed change les tirages aléatoires (0 : graine du scénario).\n");
    printf("--compare joue les scénarios à circulation aléatoire ; avec --all, aussi les scénarios scriptés.\n");
    printf("Avec --quiet, --compare n'affiche que le tableau comparatif, sans le journal ni le rapport de chaque "
           "scénario.\n");
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

// Comparaison en mode --quiet : le journal détaillé de chaque scénario est masqué, seul le tableau final compte
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

// Joue l'un après l'autre les scénarios à circulation aléatoire (tous avec includeScripted), chacun avec son
// journal et son rapport comme une exécution classique (sauf quiet), puis affiche le tableau comparatif.
// Seuls les scénarios aléatoires partagent le même réseau : les scriptés servent à démontrer un mécanisme.
static int compareScenarios(uint64_t seed, bool quiet, bool includeScripted) {
    const struct Scenario* compared[32];
    int total = 0;
    for (int i = 0; i < scenarioCount() && total < (int)(sizeof(compared) / sizeof(compared[0])); i++) {
        if (includeScripted || scenarioAt(i)->generator != NULL) {
            compared[total++] = scenarioAt(i);
        }
    }

    struct ScenarioResult results[32];
    bool allSucceeded = true;
    for (int i = 0; i < total; i++) {
        if (quiet) {
            fprintf(stderr, "[SIMULATION] Scénario %d/%d : %s en cours...\n", i + 1, total, compared[i]->name);
            int saved = silenceStdout();
            allSucceeded = runScenario(compared[i], seed, false, &results[i]) && allSucceeded;
            restoreStdout(saved);
        } else {
            printf("\n################ Scénario %d/%d : %s ################\n", i + 1, total, compared[i]->name);
            allSucceeded = runScenario(compared[i], seed, true, &results[i]) && allSucceeded;
            // Résultat de ce scénario au format du tableau final, pour le lire sans attendre la fin
            printScenarioComparison("Résultat du scénario", &results[i], 1);
        }
    }
    printScenarioComparison("Comparaison des scénarios", results, total);
    return allSucceeded ? 0 : 1;
}

int main(int argc, char* argv[]) {
    const char* scenarioName = "standard";
    bool compare = false;
    bool quiet = false;
    bool includeScripted = false;
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
        } else if (strcmp(argv[i], "--all") == 0 || strcmp(argv[i], "-a") == 0) {
            includeScripted = true;
        } else if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
            quiet = true;
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

    // --all sans --compare : la comparaison est sous-entendue
    if (compare || includeScripted) {
        return compareScenarios(seed, quiet, includeScripted);
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
