#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>
#include <time.h>
#include "billing.h"
#include "history.h"
#include "message_queue.h"
#include "station.h"

#define MAX_STATIONS 30

// Nombre de sessions de l'historique reprises dans le rapport de fin
#define REPORT_HISTORY_RECORDS 10

// Intervalle entre deux passes de maintenance (expiration des attentes trop longues, ping)
#define SERVER_MAINTENANCE_PERIOD_SECONDS 1.0
// Intervalle entre deux pings des véhicules en charge
#define SERVER_PING_PERIOD_SECONDS 5.0

// Compteurs cumulés pour le rapport de fin de simulation
struct ServerStats {
    int requests;               // Requêtes reçues
    int malformed;              // Requêtes ignorées car incohérentes
    int assignedWithoutWaiting; // Demandes servies immédiatement, sans passer par une file
    int completedCharges;       // Recharges menées à terme
    int interruptedCharges;     // Recharges interrompues par une panne
    double deliveredEnergyKWh;  // Énergie délivrée, recharges interrompues comprises
    int faults;                 // Pannes signalées
    int repairs;                // Bornes remises en service
    int redirections;           // Véhicules redirigés vers une autre station après une panne
    int refused;                // Refus envoyés (file pleine, station inconnue, attente expirée)
    int pongs;                  // Réponses aux pings
};

struct Server {
    struct Station stations[MAX_STATIONS]; // L'identifiant d'une station est son indice
    int numStations;
    msgq_t queue; // File des requêtes et des réponses aux véhicules, -1 si fermée
    struct ServerStats stats;
    struct Tariff tariff;     // Appliqué à chaque session terminée ou interrompue
    struct History history;   // Sessions facturées
    struct timespec lastMaintenance; // Horloge CLOCK_MONOTONIC
    struct timespec lastPing;
};

// Crée les stations, l'historique et la file du serveur (en purgeant celle d'une exécution précédente).
// Le tarif vaut DEFAULT_TARIFF ; il peut être changé avant de lancer runServer().
bool initServer(struct Server* server, int numStations, int chargersPerStation);
// Supprime la file (les véhicules qui l'attendent encore reçoivent MSG_CLOSED), libère les stations
// et l'historique ; sans effet si c'est déjà fait
void shutdownServer(struct Server* server);

// Vérifie puis traite une requête et envoie les réponses aux véhicules concernés.
// Renvoie false sur REQ_SHUTDOWN : les véhicules connus ont alors reçu NOTIF_SHUTDOWN.
bool handleRequest(struct Server* server, const Message* msg);

// Refuse les attentes trop longues (MAX_WAIT_SECONDS) et, toutes les SERVER_PING_PERIOD_SECONDS,
// envoie NOTIF_PING aux véhicules en charge
void runServerMaintenance(struct Server* server);

// Boucle principale : traite les requêtes et la maintenance jusqu'à REQ_SHUTDOWN ou la suppression de la file
void runServer(struct Server* server);

// Indicateurs de performance du réseau, pour le rapport et la comparaison des scénarios
struct ServerSummary {
    int servedVehicles;            // Véhicules arrivés à une borne, directement ou après la file d'attente
    double averageWaitSeconds;     // Attente réelle moyenne des véhicules servis (nulle s'ils l'ont été tout de suite)
    double occupancyRate;          // Taux d'occupation moyen des stations depuis leur ouverture (0 à 1)
    struct HistorySummary history; // Sessions, énergie facturée et chiffre d'affaires de tout le réseau
};

// À appeler serveur arrêté, ou depuis le thread du serveur
struct ServerSummary summarizeServer(struct Server* server);

// Rapport de fin de simulation : état des stations, recharges, énergie, chiffre d'affaires, temps d'attente,
// pannes et dernières sessions de l'historique
void printServerReport(struct Server* server);

#endif // SERVER_H
