#ifndef VEHICLE_GENERATOR_H
#define VEHICLE_GENERATOR_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include "message_queue.h"
#include "vehicle.h"

// Nombre maximal de véhicules générés lors d'une même exécution (un thread par véhicule)
#define MAX_GENERATED_VEHICLES 256

// Paramètres de la génération aléatoire des véhicules
struct GeneratorConfig {
    double arrivalRate;     // Arrivées par seconde réelle (λ du processus de Poisson), > 0
    int numStations;        // Station de destination tirée uniformément entre 0 et numStations - 1
    int maxVehicles;        // Nombre de véhicules à générer, de 1 à MAX_GENERATED_VEHICLES
    double prioritaryRate;  // Probabilité qu'une voiture ou camionnette soit un véhicule d'urgence (prioritaire)
    double minInitialLevel; // Niveau de batterie initial, en fraction de la capacité, tiré entre ces deux bornes
    double maxInitialLevel;
    uint64_t seed;          // Graine du générateur pseudo-aléatoire ; 0 : graine tirée de l'horloge
};

// 20 véhicules sur une station, une arrivée toutes les 2 s en moyenne, batterie entre 10 et 80 %
extern const struct GeneratorConfig DEFAULT_GENERATOR_CONFIG;

struct VehicleGenerator {
    struct GeneratorConfig config;
    uint64_t rngState;
    int nextId;    // Identifiant du prochain véhicule
    int generated; // Véhicules déjà générés
};

// Bilan d'une exécution de runVehicleGenerator
struct GeneratorReport {
    int generated; // Véhicules dont le thread a démarré
    int charged;
    int refused;
    int stopped;
    int errors;    // Erreurs de messagerie, d'allocation ou de création de thread
};

bool isValidGeneratorConfig(const struct GeneratorConfig* config);

// Les identifiants des véhicules générés commencent à firstId ; renvoie false si la configuration est invalide
bool initVehicleGenerator(struct VehicleGenerator* generator, const struct GeneratorConfig* config, int firstId);

// Délai (s réelles) avant la prochaine arrivée : loi exponentielle de paramètre arrivalRate, de sorte que
// les arrivées forment un processus de Poisson
double nextArrivalDelaySeconds(struct VehicleGenerator* generator);

// Tire un véhicule aléatoire (type, capacité, batterie, priorité, destination) et sa station de recharge.
// Renvoie NULL une fois maxVehicles atteint ou en cas d'échec d'allocation ; à libérer avec destroyVehicle()
struct Vehicle* generateVehicle(struct VehicleGenerator* generator, int* stationId);

// Fait arriver les véhicules un par un (un thread runVehicle chacun) jusqu'à maxVehicles ou jusqu'à ce que
// *stop passe à true (stop peut valoir NULL), puis attend la fin de tous les threads démarrés.
// Bloquant : après avoir mis *stop à true, l'appelant doit arrêter le serveur (REQ_SHUTDOWN) pour que les
// véhicules encore en attente se terminent. Renvoie false si une erreur est survenue.
bool runVehicleGenerator(struct VehicleGenerator* generator, msgq_t serverQueue, const atomic_bool* stop,
                         struct GeneratorReport* report);

// Paramètres et résultat d'un thread générateur
struct GeneratorRun {
    struct VehicleGenerator* generator;
    msgq_t serverQueue;
    const atomic_bool* stop;
    struct GeneratorReport report; // Renseigné à la fin du thread
    bool succeeded;
};

// Point d'entrée pour pthread_create, avec une struct GeneratorRun* en argument
void* vehicleGeneratorThread(void* run);

void printGeneratorReport(const struct GeneratorReport* report);

#endif // VEHICLE_GENERATOR_H
