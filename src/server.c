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
    struct WaitingVehicle vehicle = {
        .vehicleId = msg->vehicle_id,
        .desiredPower = msg->desired_power,
        .priority = msg->is_prioritary ? PRIORITY_HIGH : PRIORITY_NORMAL,
    };
    struct Assignment assignment = requestCharger(station, &vehicle);
    switch (assignment.result) {
        case ASSIGN_OK:
            printf("[SERVEUR] Véhicule %d : borne %d attribuée.\n", msg->vehicle_id, assignment.chargerIndex);
            // TODO Envoyer RESP_ASSIGNED au véhicule
            break;

        case ASSIGN_QUEUED:
            printf("[SERVEUR] Aucune borne libre sur la station %d : véhicule %d en attente (position %d).\n",
                   station->id, msg->vehicle_id, assignment.queuePosition);
            // TODO Envoyer RESP_QUEUED au véhicule
            break;

        case ASSIGN_REFUSED:
            printf("[SERVEUR] Station %d saturée : demande du véhicule %d refusée.\n", station->id, msg->vehicle_id);
            // TODO Envoyer RESP_REFUSED au véhicule
            break;
    }
}

static void handleChargeEnd(struct Server* server, const Message* msg) {
    struct Station* station = findStation(server, msg->station_id);
    if (station == NULL) {
        return;
    }

    struct ChargingSession finished;
    struct WaitingVehicle next;
    HandoverResult result = releaseChargerAndServeQueue(station, msg->charger_id, &finished, &next);
    if (result == HANDOVER_INVALID) {
        printf("[SERVEUR] Erreur : la borne %d de la station %d n'était pas occupée.\n", msg->charger_id, station->id);
        return;
    }

    printf("[SERVEUR] Véhicule %d : recharge terminée sur la borne %d de la station %d.\n",
           finished.vehicleId, msg->charger_id, station->id);
    // TODO Facturer la session terminée (finished) et l'enregistrer dans l'historique

    if (result == HANDOVER_REASSIGNED) {
        printf("[SERVEUR] Borne %d de la station %d attribuée au véhicule %d (attente : %.1f s).\n",
               msg->charger_id, station->id, next.vehicleId, waitingTimeSeconds(&next));
        // TODO Envoyer RESP_ASSIGNED au véhicule next.vehicleId
    } else {
        printf("[SERVEUR] Borne %d de la station %d libérée.\n", msg->charger_id, station->id);
    }
}

static void handleCancel(struct Server* server, const Message* msg) {
    struct Station* station = findStation(server, msg->station_id);
    if (station == NULL) {
        return;
    }

    if (cancelWaitingVehicle(station, msg->vehicle_id)) {
        printf("[SERVEUR] Véhicule %d : quitte la file d'attente de la station %d.\n", msg->vehicle_id, station->id);
    } else {
        printf("[SERVEUR] Erreur : le véhicule %d n'attendait pas sur la station %d.\n", msg->vehicle_id, station->id);
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

    struct ChargingSession interrupted;
    bool wasCharging = takeChargerOutOfService(station, msg->charger_id, OUT_OF_SERVICE, &interrupted);

    printf("[SERVEUR] ALERTE : borne %d de la station %d en panne.\n", msg->charger_id, station->id);
    if (!wasCharging) {
        return;
    }

    // TODO Facturer la session interrompue et prévenir le véhicule (NOTIF_FAULT)
    struct Assignment redirection = redirectInterruptedVehicle(station, &interrupted);
    switch (redirection.result) {
        case ASSIGN_OK:
            printf("[SERVEUR] Véhicule %d : redirigé vers la borne %d.\n", interrupted.vehicleId,
                   redirection.chargerIndex);
            // TODO Envoyer RESP_ASSIGNED au véhicule
            break;

        case ASSIGN_QUEUED:
            printf("[SERVEUR] Véhicule %d : replacé en file d'attente (position %d).\n", interrupted.vehicleId,
                   redirection.queuePosition);
            // TODO Envoyer RESP_QUEUED au véhicule
            break;

        case ASSIGN_REFUSED:
            printf("[SERVEUR] Véhicule %d : aucune redirection possible sur la station %d.\n", interrupted.vehicleId,
                   station->id);
            // TODO Essayer une autre station, sinon envoyer RESP_REFUSED au véhicule
            break;
    }
}

bool initServer(struct Server* server, int numStations, int chargersPerStation) {
    server->numStations = 0;
    if (numStations > MAX_STATIONS) {
        printf("[SERVEUR] Erreur : %d stations demandées, maximum %d.\n", numStations, MAX_STATIONS);
        return false;
    }

    for (int i = 0; i < numStations; i++) {
        if (!initStation(&server->stations[i], i, chargersPerStation)) {
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

        case REQ_CANCEL:
            handleCancel(server, msg);
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
