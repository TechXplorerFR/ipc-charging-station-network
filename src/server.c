#include "server.h"
#include "allocator.h"
#include <stdio.h>

// Renvoie la station visée par une requête, ou NULL si son identifiant est invalide
static struct Station* findStation(struct Server* server, int stationId) {
    if (stationId < 0 || stationId >= server->numStations) {
        printf("[SERVEUR] Erreur : station %d inconnue.\n", stationId);
        return NULL;
    }
    return &server->stations[stationId];
}

static void handleChargeDemand(struct Server* server, const Message* msg) {
    struct Station* station = findStation(server, msg->station_id);
    if (station == NULL) {
        return;
    }

    printf("[SERVEUR] Véhicule %d : demande de charge sur la station %d.\n", msg->vehicle_id, station->id);
    int chargerIndex = assignChargerAtomically(station, msg->vehicle_id);
    if (chargerIndex != -1) {
        // TODO Envoyer la confirmation d'attribution au véhicule
    } else {
        printf("[SERVEUR] Aucune borne libre sur la station %d.\n", station->id);
        // TODO Mettre le véhicule en file d'attente
    }
}

static void handleChargeEnd(struct Server* server, const Message* msg) {
    struct Station* station = findStation(server, msg->station_id);
    if (station == NULL) {
        return;
    }

    if (releaseCharger(station, msg->charger_id)) {
        printf("[SERVEUR] Borne %d de la station %d libérée.\n", msg->charger_id, station->id);
        // TODO Attribuer la borne libérée au prochain véhicule en attente
    } else {
        printf("[SERVEUR] Erreur : la borne %d de la station %d n'était pas occupée.\n", msg->charger_id, station->id);
    }
}

static void handleAlert(struct Server* server, const Message* msg) {
    struct Station* station = findStation(server, msg->station_id);
    if (station == NULL) {
        return;
    }
    if (!isValidCharger(station, msg->charger_id)) {
        printf("[SERVEUR] Erreur : borne %d inconnue sur la station %d.\n", msg->charger_id, station->id);
        return;
    }

    lockStation(station);
    bool wasOccupied = station->chargers[msg->charger_id].status == OCCUPIED;
    setStationChargerStatus(station, msg->charger_id, OUT_OF_SERVICE);
    unlockStation(station);

    printf("[SERVEUR] ALERTE : borne %d de la station %d en panne.\n", msg->charger_id, station->id);
    if (wasOccupied) {
        // TODO Notifier le véhicule en charge et le rediriger si possible
    }
}

bool initServer(struct Server* server, int numStations, int chargersPerStation, key_t firstQueueKey) {
    server->numStations = 0;
    if (numStations > MAX_STATIONS) {
        printf("[SERVEUR] Erreur : %d stations demandées, maximum %d.\n", numStations, MAX_STATIONS);
        return false;
    }

    for (int i = 0; i < numStations; i++) {
        if (!initStation(&server->stations[i], i, chargersPerStation, firstQueueKey + i)) {
            shutdownServer(server);
            return false;
        }
        server->numStations++;
    }
    return true;
}

void shutdownServer(struct Server* server) {
    printf("[SERVEUR] Libération des ressources IPC...\n");
    for (int i = 0; i < server->numStations; i++) {
        destroyStation(&server->stations[i]);
    }
    server->numStations = 0;
    // TODO Fermer la file de requêtes du serveur
}

bool handleRequest(struct Server* server, const Message* msg) {
    switch (msg->request) {
        case REQ_CHARGE_DEMAND:
            handleChargeDemand(server, msg);
            break;

        case REQ_CHARGE_END:
            handleChargeEnd(server, msg);
            break;

        case REQ_ALERT:
            handleAlert(server, msg);
            break;

        case REQ_SHUTDOWN:
            printf("[SERVEUR] Arrêt du serveur demandé.\n");
            return false;

        default:
            printf("[SERVEUR] Erreur : type de requête %d inconnu.\n", msg->request);
            break;
    }
    return true;
}
