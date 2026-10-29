#include "server.h"
#include "allocator.h"
#include <stdio.h>

// Attente entre deux lectures de la file lorsqu'elle est vide
#define SERVER_POLL_INTERVAL_NANOSECONDS 20000000L // 20 ms

// Véhicules qu'une station peut connaître en même temps : un par borne et ceux de la file d'attente
#define MAX_VEHICLES_PER_STATION (MAX_CHARGERS_PER_STATION + MAX_WAITING_VEHICLES)

static double secondsSince(const struct timespec* start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start->tv_sec) + (double)(now.tv_nsec - start->tv_nsec) / 1e9;
}

// Renvoie la station visée par une requête, ou NULL si son identifiant est invalide
static struct Station* findStation(struct Server* server, int stationId) {
    if (stationId < 0 || stationId >= server->numStations) {
        printf("[SERVEUR] Erreur : station %d inconnue.\n", stationId);
        return NULL;
    }
    return &server->stations[stationId];
}

// Renvoie la station visée par une requête sur une borne, ou NULL si la station ou la borne est inconnue
static struct Station* findStationOfCharger(struct Server* server, int stationId, int chargerIndex) {
    struct Station* station = findStation(server, stationId);
    if (station != NULL && !isValidCharger(station, chargerIndex)) {
        printf("[SERVEUR] Erreur : borne %d inconnue sur la station %d.\n", chargerIndex, stationId);
        return NULL;
    }
    return station;
}

// Véhicule en charge sur la borne, ou NO_VEHICLE
static int chargingVehicle(struct Station* station, int chargerIndex) {
    lockStation(station);
    const struct Charger* charger = &station->chargers[chargerIndex];
    int vehicleId = charger->status == OCCUPIED ? charger->session.vehicleId : NO_VEHICLE;
    unlockStation(station);
    return vehicleId;
}

// Copie dans ids les véhicules en charge sur la station et, si includeWaiting, ceux de sa file d'attente ;
// renvoie leur nombre (au plus MAX_VEHICLES_PER_STATION)
static int collectVehicles(struct Station* station, bool includeWaiting, int* ids) {
    int count = 0;
    lockStation(station);
    for (int i = 0; i < station->numChargers; i++) {
        if (station->chargers[i].status == OCCUPIED) {
            ids[count++] = station->chargers[i].session.vehicleId;
        }
    }
    for (int i = 0; includeWaiting && i < station->waitQueue.count; i++) {
        ids[count++] = station->waitQueue.vehicles[i].vehicleId;
    }
    unlockStation(station);
    return count;
}

static void reply(struct Server* server, int vehicleId, const Message* msg) {
    if (!sendToVehicle(server->queue, vehicleId, msg)) {
        fprintf(stderr, "[SERVEUR] Erreur : message %d non transmis au véhicule %d.\n", msg->request, vehicleId);
    }
}

static void sendAssigned(struct Server* server, int vehicleId, const struct Station* station, int chargerIndex,
                         int desiredPower) {
    // La puissance d'une borne ne change plus après l'initialisation : lecture sans verrou
    int power = effectiveChargingPower(station->chargers[chargerIndex].maxPower, desiredPower);
    printf("[SERVEUR] Véhicule %d : borne %d de la station %d attribuée (%d kW).\n", vehicleId, chargerIndex,
           station->id, power);

    Message msg = {
        .request = RESP_ASSIGNED,
        .station_id = station->id,
        .charger_id = chargerIndex,
        .desired_power = power,
    };
    reply(server, vehicleId, &msg);
}

static void sendQueued(struct Server* server, int vehicleId, int stationId, int queuePosition) {
    printf("[SERVEUR] Véhicule %d : en attente sur la station %d (position %d).\n", vehicleId, stationId,
           queuePosition);
    Message msg = { .request = RESP_QUEUED, .station_id = stationId, .charger_id = -1, .queue_position = queuePosition };
    reply(server, vehicleId, &msg);
}

static void sendRefused(struct Server* server, int vehicleId, int stationId, const char* reason) {
    printf("[SERVEUR] Véhicule %d : demande refusée sur la station %d (%s).\n", vehicleId, stationId, reason);
    server->stats.refused++;
    Message msg = { .request = RESP_REFUSED, .station_id = stationId, .charger_id = -1 };
    reply(server, vehicleId, &msg);
}

static void notifyVehicle(struct Server* server, int vehicleId, RequestType notification, int stationId,
                          int chargerIndex) {
    Message msg = { .request = notification, .station_id = stationId, .charger_id = chargerIndex };
    reply(server, vehicleId, &msg);
}

// Envoie une notification à tous les véhicules en charge et, si includeWaiting, en attente
static void notifyAllVehicles(struct Server* server, RequestType notification, bool includeWaiting) {
    int ids[MAX_VEHICLES_PER_STATION];
    for (int s = 0; s < server->numStations; s++) {
        int count = collectVehicles(&server->stations[s], includeWaiting, ids);
        for (int i = 0; i < count; i++) {
            notifyVehicle(server, ids[i], notification, s, -1);
        }
    }
}

// Transmet au véhicule l'issue d'une demande ou d'une redirection
static void sendAssignment(struct Server* server, int vehicleId, const struct Station* station,
                           const struct Assignment* assignment, int desiredPower) {
    switch (assignment->result) {
        case ASSIGN_OK:
            sendAssigned(server, vehicleId, station, assignment->chargerIndex, desiredPower);
            break;
        case ASSIGN_QUEUED:
            sendQueued(server, vehicleId, station->id, assignment->queuePosition);
            break;
        case ASSIGN_REFUSED:
            sendRefused(server, vehicleId, station->id, "file d'attente pleine");
            break;
    }
}

// Session terminée ou interrompue : comptabilise l'énergie délivrée
static void recordSession(struct Server* server, const struct ChargingSession* session) {
    server->stats.deliveredEnergyKWh += sessionEnergyKWh(session);
    // TODO Facturer la session et l'enregistrer dans l'historique (modules billing et history)
}

// Redirige un véhicule dont la recharge a été interrompue : borne libre ou tête de file sur sa station,
// sinon sur la première autre station qui peut l'accueillir
static void redirectVehicle(struct Server* server, struct Station* origin, const struct ChargingSession* interrupted) {
    struct Station* target = origin;
    struct Assignment assignment = redirectInterruptedVehicle(origin, interrupted);
    for (int s = 0; assignment.result == ASSIGN_REFUSED && s < server->numStations; s++) {
        if (&server->stations[s] != origin) {
            target = &server->stations[s];
            assignment = redirectInterruptedVehicle(target, interrupted);
        }
    }

    if (assignment.result == ASSIGN_REFUSED) {
        target = origin;
    } else if (target != origin) {
        server->stats.redirections++;
    }
    sendAssignment(server, interrupted->vehicleId, target, &assignment, interrupted->desiredPower);
}

static void handleChargeDemand(struct Server* server, const Message* msg) {
    struct Station* station = findStation(server, msg->station_id);
    if (station == NULL) {
        sendRefused(server, msg->vehicle_id, msg->station_id, "station inconnue");
        return;
    }

    printf("[SERVEUR] Véhicule %d : demande de %d kW sur la station %d%s.\n", msg->vehicle_id, msg->desired_power,
           station->id, msg->is_prioritary ? " (prioritaire)" : "");
    struct WaitingVehicle vehicle = {
        .vehicleId = msg->vehicle_id,
        .desiredPower = msg->desired_power,
        .priority = msg->is_prioritary ? PRIORITY_HIGH : PRIORITY_NORMAL,
    };
    struct Assignment assignment = requestCharger(station, &vehicle);
    if (assignment.result == ASSIGN_OK) {
        server->stats.assignedWithoutWaiting++;
    }
    sendAssignment(server, msg->vehicle_id, station, &assignment, msg->desired_power);
}

static void handleChargeEnd(struct Server* server, const Message* msg) {
    struct Station* station = findStationOfCharger(server, msg->station_id, msg->charger_id);
    if (station == NULL) {
        return;
    }
    // Le serveur est le seul à modifier les bornes : le véhicule ne peut pas changer entre ce contrôle et la libération
    if (chargingVehicle(station, msg->charger_id) != msg->vehicle_id) {
        printf("[SERVEUR] Erreur : le véhicule %d ne charge pas sur la borne %d de la station %d.\n",
               msg->vehicle_id, msg->charger_id, station->id);
        return;
    }

    struct ChargingSession finished;
    struct WaitingVehicle next;
    HandoverResult result = releaseChargerAndServeQueue(station, msg->charger_id, &finished, &next);
    if (result == HANDOVER_INVALID) {
        printf("[SERVEUR] Erreur : la borne %d de la station %d n'était pas occupée.\n", msg->charger_id, station->id);
        return;
    }

    server->stats.completedCharges++;
    recordSession(server, &finished);
    printf("[SERVEUR] Véhicule %d : recharge terminée sur la borne %d de la station %d (%.1f kWh en %.1f s).\n",
           finished.vehicleId, msg->charger_id, station->id, sessionEnergyKWh(&finished),
           sessionDurationSeconds(&finished));

    if (result == HANDOVER_REASSIGNED) {
        sendAssigned(server, next.vehicleId, station, msg->charger_id, next.desiredPower);
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
    struct Station* station = findStationOfCharger(server, msg->station_id, msg->charger_id);
    if (station == NULL) {
        return;
    }

    struct ChargingSession interrupted;
    bool wasCharging = takeChargerOutOfService(station, msg->charger_id, OUT_OF_SERVICE, &interrupted);
    server->stats.faults++;
    printf("[SERVEUR] ALERTE : borne %d de la station %d en panne.\n", msg->charger_id, station->id);
    if (!wasCharging) {
        return;
    }

    server->stats.interruptedCharges++;
    recordSession(server, &interrupted);
    printf("[SERVEUR] Véhicule %d : recharge interrompue après %.1f kWh.\n", interrupted.vehicleId,
           sessionEnergyKWh(&interrupted));
    notifyVehicle(server, interrupted.vehicleId, NOTIF_FAULT, station->id, msg->charger_id);
    redirectVehicle(server, station, &interrupted);
}

static void handleRepair(struct Server* server, const Message* msg) {
    struct Station* station = findStationOfCharger(server, msg->station_id, msg->charger_id);
    if (station == NULL) {
        return;
    }

    struct WaitingVehicle next;
    HandoverResult result = repairChargerAndServeQueue(station, msg->charger_id, &next);
    if (result == HANDOVER_INVALID) {
        printf("[SERVEUR] Erreur : la borne %d de la station %d n'était pas hors service.\n", msg->charger_id,
               station->id);
        return;
    }

    server->stats.repairs++;
    printf("[SERVEUR] Borne %d de la station %d remise en service.\n", msg->charger_id, station->id);
    if (result == HANDOVER_REASSIGNED) {
        sendAssigned(server, next.vehicleId, station, msg->charger_id, next.desiredPower);
    }
}

static void handlePong(struct Server* server, const Message* msg) {
    server->stats.pongs++;
    printf("[SERVEUR] Véhicule %d : batterie à %d %%.\n", msg->vehicle_id, msg->battery_level);
}

bool initServer(struct Server* server, int numStations, int chargersPerStation) {
    server->numStations = 0;
    server->queue = -1;
    server->stats = (struct ServerStats){ 0 };
    if (numStations <= 0 || numStations > MAX_STATIONS) {
        printf("[SERVEUR] Erreur : %d stations demandées (1 à %d).\n", numStations, MAX_STATIONS);
        return false;
    }

    for (int i = 0; i < numStations; i++) {
        if (!initStation(&server->stations[i], i, chargersPerStation)) {
            shutdownServer(server);
            return false;
        }
        server->numStations++;
    }

    server->queue = createServerQueue();
    if (server->queue == -1) {
        shutdownServer(server);
        return false;
    }

    clock_gettime(CLOCK_MONOTONIC, &server->lastMaintenance);
    server->lastPing = server->lastMaintenance;
    return true;
}

void shutdownServer(struct Server* server) {
    if (server->queue != -1) {
        destroyMessageQueue(server->queue);
        server->queue = -1;
    }
    for (int i = 0; i < server->numStations; i++) {
        destroyStation(&server->stations[i]);
    }
    server->numStations = 0;
}

bool handleRequest(struct Server* server, const Message* msg) {
    server->stats.requests++;
    if (!isWellFormedMessage(msg)) {
        server->stats.malformed++;
        printf("[SERVEUR] Erreur : requête %d incohérente ignorée (véhicule %d).\n", msg->request, msg->vehicle_id);
        return true;
    }

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

        case REQ_REPAIR:
            handleRepair(server, msg);
            break;

        case REQ_CANCEL:
            handleCancel(server, msg);
            break;

        case REQ_PONG:
            handlePong(server, msg);
            break;

        case REQ_SHUTDOWN:
            printf("[SERVEUR] Arrêt du serveur demandé.\n");
            notifyAllVehicles(server, NOTIF_SHUTDOWN, true);
            return false;

        case RESP_ASSIGNED:
        case RESP_QUEUED:
        case RESP_REFUSED:
        case NOTIF_PING:
        case NOTIF_FAULT:
        case NOTIF_SHUTDOWN:
            printf("[SERVEUR] Erreur : le message %d est destiné aux véhicules, pas au serveur.\n", msg->request);
            break;
    }
    return true;
}

void runServerMaintenance(struct Server* server) {
    clock_gettime(CLOCK_MONOTONIC, &server->lastMaintenance);

    struct WaitingVehicle expired[MAX_WAITING_VEHICLES];
    for (int s = 0; s < server->numStations; s++) {
        int count = expireWaitingVehicles(&server->stations[s], MAX_WAIT_SECONDS, expired, MAX_WAITING_VEHICLES);
        for (int i = 0; i < count; i++) {
            sendRefused(server, expired[i].vehicleId, s, "attente trop longue");
        }
    }

    if (secondsSince(&server->lastPing) >= SERVER_PING_PERIOD_SECONDS) {
        clock_gettime(CLOCK_MONOTONIC, &server->lastPing);
        notifyAllVehicles(server, NOTIF_PING, false);
    }
}

void runServer(struct Server* server) {
    const struct timespec pollInterval = { .tv_sec = 0, .tv_nsec = SERVER_POLL_INTERVAL_NANOSECONDS };
    printf("[SERVEUR] En service : %d station(s), en attente des requêtes.\n", server->numStations);

    while (true) {
        if (secondsSince(&server->lastMaintenance) >= SERVER_MAINTENANCE_PERIOD_SECONDS) {
            runServerMaintenance(server);
        }

        Message msg;
        switch (tryReceiveMessage(server->queue, &msg, SERVER_MSG_TYPE)) {
            case MSG_RECEIVED:
                if (!handleRequest(server, &msg)) {
                    return;
                }
                break;

            case MSG_NONE:
                nanosleep(&pollInterval, NULL);
                break;

            case MSG_CLOSED:
            case MSG_ERROR:
                fprintf(stderr, "[SERVEUR] File de requêtes indisponible : arrêt.\n");
                return;
        }
    }
}

void printServerReport(struct Server* server) {
    printf("========== Rapport du réseau ==========\n");
    int servedFromQueue = 0;
    double totalWaitSeconds = 0.0;
    for (int s = 0; s < server->numStations; s++) {
        struct Station* station = &server->stations[s];
        printStation(station);

        lockStation(station);
        servedFromQueue += station->waitQueue.stats.served;
        totalWaitSeconds += station->waitQueue.stats.totalServedWaitSeconds;
        unlockStation(station);
    }

    // Les véhicules servis immédiatement comptent pour une attente nulle
    const struct ServerStats* stats = &server->stats;
    int servedVehicles = servedFromQueue + stats->assignedWithoutWaiting;
    printf("Recharges terminées : %d, interrompues par une panne : %d\n", stats->completedCharges,
           stats->interruptedCharges);
    printf("Énergie délivrée : %.1f kWh\n", stats->deliveredEnergyKWh);
    printf("Temps d'attente moyen : %.1f s sur %d véhicule(s) servi(s)\n",
           servedVehicles > 0 ? totalWaitSeconds / servedVehicles : 0.0, servedVehicles);
    printf("Pannes : %d, réparations : %d, redirections vers une autre station : %d\n", stats->faults,
           stats->repairs, stats->redirections);
    printf("Refus : %d, requêtes reçues : %d (dont %d incohérentes)\n", stats->refused, stats->requests,
           stats->malformed);
    // TODO Chiffre d'affaires (module billing)
}
