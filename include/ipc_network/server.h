#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>
#include <time.h>
#include "message_queue.h"
#include "station.h"

#define MAX_STATIONS 30

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
    struct timespec lastMaintenance; // Horloge CLOCK_MONOTONIC
    struct timespec lastPing;
};

// Crée les stations et la file du serveur (en purgeant celle d'une exécution précédente)
bool initServer(struct Server* server, int numStations, int chargersPerStation);
// Supprime la file (les véhicules qui l'attendent encore reçoivent MSG_CLOSED) et libère les stations ;
// sans effet si c'est déjà fait
void shutdownServer(struct Server* server);

// Vérifie puis traite une requête et envoie les réponses aux véhicules concernés.
// Renvoie false sur REQ_SHUTDOWN : les véhicules connus ont alors reçu NOTIF_SHUTDOWN.
bool handleRequest(struct Server* server, const Message* msg);

// Refuse les attentes trop longues (MAX_WAIT_SECONDS) et, toutes les SERVER_PING_PERIOD_SECONDS,
// envoie NOTIF_PING aux véhicules en charge
void runServerMaintenance(struct Server* server);

// Boucle principale : traite les requêtes et la maintenance jusqu'à REQ_SHUTDOWN ou la suppression de la file
void runServer(struct Server* server);

// Rapport de fin de simulation : état des stations, recharges, énergie, temps d'attente, pannes
void printServerReport(struct Server* server);

#endif // SERVER_H
