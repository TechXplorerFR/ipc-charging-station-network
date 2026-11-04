#ifndef FAULT_SIMULATOR_H
#define FAULT_SIMULATOR_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include "message_queue.h"

// Nombre maximal de bornes mises en panne en même temps par le simulateur
#define MAX_SIMULATED_FAULTS 64

// Paramètres de la simulation des pannes
struct FaultSimulatorConfig {
    double faultRate;          // Pannes par seconde réelle sur tout le réseau (λ du processus de Poisson), > 0
    int numStations;           // Bornes visées : station 0 à numStations - 1,
    int chargersPerStation;    // borne 0 à chargersPerStation - 1 (comme initServer)
    double minRepairSeconds;   // Durée réelle d'une réparation, tirée uniformément entre ces deux bornes
    double maxRepairSeconds;
    int maxConcurrentFaults;   // Bornes en panne en même temps, de 1 à MAX_SIMULATED_FAULTS et au plus le total
    uint64_t seed;             // Graine du générateur pseudo-aléatoire ; 0 : graine tirée de l'horloge
};

// Une panne toutes les 20 s en moyenne sur une station de 3 bornes, réparée en 5 à 15 s (5 à 15 min simulées),
// une seule borne en panne à la fois
extern const struct FaultSimulatorConfig DEFAULT_FAULT_SIMULATOR_CONFIG;

// Borne mise en panne par le simulateur, en attente de réparation
struct SimulatedFault {
    int stationId;
    int chargerId;
    double repairAt; // Instant de la réparation (s, horloge monotone)
};

struct FaultSimulatorStats {
    int faults;  // REQ_ALERT envoyés
    int repairs; // REQ_REPAIR envoyés
    int errors;  // Échecs d'envoi
};

struct FaultSimulator {
    struct FaultSimulatorConfig config;
    uint64_t rngState;
    struct SimulatedFault faults[MAX_SIMULATED_FAULTS];
    int numFaults;
    double nextFaultAt; // Instant de la prochaine panne (s, horloge monotone)
    struct FaultSimulatorStats stats;
};

bool isValidFaultSimulatorConfig(const struct FaultSimulatorConfig* config);

// Tire l'instant de la première panne à partir de now ; renvoie false si la configuration est invalide
bool initFaultSimulator(struct FaultSimulator* simulator, const struct FaultSimulatorConfig* config, double now);

// Instant courant de l'horloge monotone, en secondes (référence des instants du simulateur)
double faultSimulatorNow(void);

// La borne a-t-elle été mise en panne par le simulateur et attend-elle sa réparation ?
bool isSimulatedFault(const struct FaultSimulator* simulator, int stationId, int chargerId);

// Met en panne une borne tirée parmi celles que le simulateur n'a pas déjà mises en panne (REQ_ALERT au serveur)
// et programme sa réparation. Renvoie false si maxConcurrentFaults est atteint ou si l'envoi échoue.
bool triggerRandomFault(struct FaultSimulator* simulator, msgq_t serverQueue, double now);

// Envoie REQ_REPAIR pour chaque borne dont la réparation est due à l'instant now (toutes si now est infini) ;
// renvoie le nombre de bornes réparées
int repairDueChargers(struct FaultSimulator* simulator, msgq_t serverQueue, double now);

// Un pas de simulation : réparations dues, puis panne si son instant est atteint (la suivante est alors tirée)
void stepFaultSimulator(struct FaultSimulator* simulator, msgq_t serverQueue, double now);

// Fait tomber en panne et répare des bornes jusqu'à ce que *stop passe à true, puis répare les bornes encore
// en panne pour rendre le réseau intact. Bloquant : à exécuter dans son propre thread, et à arrêter avant
// d'envoyer REQ_SHUTDOWN au serveur. Renvoie false si un envoi a échoué.
bool runFaultSimulator(struct FaultSimulator* simulator, msgq_t serverQueue, const atomic_bool* stop);

// Paramètres et résultat d'un thread simulateur de pannes
struct FaultSimulatorRun {
    struct FaultSimulator* simulator;
    msgq_t serverQueue;
    const atomic_bool* stop;
    bool succeeded; // Renseigné à la fin du thread
};

// Point d'entrée pour pthread_create, avec une struct FaultSimulatorRun* en argument
void* faultSimulatorThread(void* run);

void printFaultSimulatorReport(const struct FaultSimulator* simulator);

#endif // FAULT_SIMULATOR_H
