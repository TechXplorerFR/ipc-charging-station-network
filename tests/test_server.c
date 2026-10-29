#include "server.h"
#include "test_utils.h"
#include "vehicle.h"
#include <pthread.h>

// Utilise la vraie clé de la file du serveur : ne pas lancer pendant une simulation.
// Les bornes ont les puissances par défaut de initStation : 22, 50 puis 150 kW.
static struct Server server;

static Message makeRequest(RequestType request, int vehicleId, int stationId, int chargerId, int power) {
    return (Message){
        .request = request,
        .vehicle_id = vehicleId,
        .station_id = stationId,
        .charger_id = chargerId,
        .desired_power = power,
        .battery_level = 50,
    };
}

static bool handle(RequestType request, int vehicleId, int stationId, int chargerId, int power) {
    Message msg = makeRequest(request, vehicleId, stationId, chargerId, power);
    return handleRequest(&server, &msg);
}

static void demand(int vehicleId, int stationId, int power) {
    CHECK(handle(REQ_CHARGE_DEMAND, vehicleId, stationId, -1, power));
}

// Prochain message envoyé par le serveur au véhicule (request vaut REQ_SHUTDOWN s'il n'y en a pas)
static Message responseFor(int vehicleId) {
    Message msg = { .request = REQ_SHUTDOWN };
    CHECK(tryReceiveMessage(server.queue, &msg, vehicleMsgType(vehicleId)) == MSG_RECEIVED);
    return msg;
}

static bool hasNoResponse(int vehicleId) {
    Message msg;
    return tryReceiveMessage(server.queue, &msg, vehicleMsgType(vehicleId)) == MSG_NONE;
}

static bool setUp(int numStations, int chargersPerStation) {
    if (!initServer(&server, numStations, chargersPerStation)) {
        CHECK(false);
        return false;
    }
    return true;
}

static void testDemandsGetAssignedQueuedOrRefused(void) {
    if (!setUp(1, 2)) {
        return;
    }

    demand(1, 0, 40);
    Message assigned = responseFor(1);
    CHECK(assigned.request == RESP_ASSIGNED);
    CHECK(assigned.station_id == 0);
    CHECK(assigned.charger_id == 1);
    CHECK(assigned.desired_power == 40);

    demand(2, 0, 100);
    assigned = responseFor(2);
    CHECK(assigned.request == RESP_ASSIGNED);
    CHECK(assigned.charger_id == 0);
    CHECK(assigned.desired_power == 22); // Limitée par la borne

    demand(3, 0, 50);
    Message queued = responseFor(3);
    CHECK(queued.request == RESP_QUEUED);
    CHECK(queued.queue_position == 1);

    demand(4, 7, 50);
    CHECK(responseFor(4).request == RESP_REFUSED);

    CHECK(server.stats.assignedWithoutWaiting == 2);
    CHECK(server.stats.refused == 1);
    shutdownServer(&server);
}

static void testChargeEndServesNextInQueue(void) {
    if (!setUp(1, 1)) {
        return;
    }
    demand(1, 0, 50);
    demand(2, 0, 50);
    CHECK(responseFor(1).request == RESP_ASSIGNED);
    CHECK(responseFor(2).request == RESP_QUEUED);

    // Seul le véhicule en charge peut libérer la borne
    CHECK(handle(REQ_CHARGE_END, 2, 0, 0, 0));
    CHECK(hasNoResponse(2));
    CHECK(server.stats.completedCharges == 0);

    CHECK(handle(REQ_CHARGE_END, 1, 0, 0, 0));
    Message assigned = responseFor(2);
    CHECK(assigned.request == RESP_ASSIGNED);
    CHECK(assigned.charger_id == 0);
    CHECK(hasNoResponse(1));
    CHECK(server.stats.completedCharges == 1);

    shutdownServer(&server);
}

static void testFaultNotifiesThenRepairServesQueue(void) {
    if (!setUp(1, 1)) {
        return;
    }
    demand(1, 0, 50);
    CHECK(responseFor(1).request == RESP_ASSIGNED);

    // Plus aucune borne : le véhicule interrompu repasse en tête de file
    CHECK(handle(REQ_ALERT, -1, 0, 0, 0));
    Message fault = responseFor(1);
    CHECK(fault.request == NOTIF_FAULT);
    CHECK(fault.charger_id == 0);
    Message queued = responseFor(1);
    CHECK(queued.request == RESP_QUEUED);
    CHECK(queued.queue_position == 1);
    CHECK(server.stats.faults == 1);
    CHECK(server.stats.interruptedCharges == 1);

    CHECK(handle(REQ_REPAIR, -1, 0, 0, 0));
    Message assigned = responseFor(1);
    CHECK(assigned.request == RESP_ASSIGNED);
    CHECK(assigned.charger_id == 0);
    CHECK(server.stats.repairs == 1);

    // Une borne en service ne se répare pas
    CHECK(handle(REQ_REPAIR, -1, 0, 0, 0));
    CHECK(server.stats.repairs == 1);

    shutdownServer(&server);
}

static void testFaultRedirectsToAnotherStationWhenQueueIsFull(void) {
    if (!setUp(2, 1)) {
        return;
    }
    demand(0, 0, 50);
    CHECK(responseFor(0).request == RESP_ASSIGNED);
    for (int id = 100; id < 100 + MAX_WAITING_VEHICLES; id++) {
        demand(id, 0, 50);
        CHECK(responseFor(id).request == RESP_QUEUED);
    }

    CHECK(handle(REQ_ALERT, -1, 0, 0, 0));
    CHECK(responseFor(0).request == NOTIF_FAULT);
    Message assigned = responseFor(0);
    CHECK(assigned.request == RESP_ASSIGNED);
    CHECK(assigned.station_id == 1);
    CHECK(assigned.charger_id == 0);
    CHECK(server.stats.redirections == 1);

    shutdownServer(&server);
}

static void testInvalidRequestsAreIgnored(void) {
    if (!setUp(1, 1)) {
        return;
    }

    CHECK(handle(REQ_CHARGE_DEMAND, 1, 0, -1, 0)); // Puissance nulle
    CHECK(hasNoResponse(1));
    CHECK(server.stats.malformed == 1);

    CHECK(handle(RESP_ASSIGNED, 1, 0, 0, 50)); // Message destiné aux véhicules
    CHECK(handle(REQ_ALERT, -1, 0, 5, 0));     // Borne inconnue
    CHECK(server.stats.faults == 0);
    CHECK(server.stats.requests == 3);

    shutdownServer(&server);
}

static void testCancelAndPong(void) {
    if (!setUp(1, 1)) {
        return;
    }
    demand(1, 0, 50);
    demand(2, 0, 50);
    CHECK(responseFor(2).request == RESP_QUEUED);

    CHECK(handle(REQ_CANCEL, 2, 0, -1, 0));
    CHECK(waitQueueLength(&server.stations[0].waitQueue) == 0);

    CHECK(handle(REQ_PONG, 1, -1, -1, 0));
    CHECK(server.stats.pongs == 1);

    shutdownServer(&server);
}

static void testMaintenanceExpiresWaitsAndPingsChargingVehicles(void) {
    if (!setUp(1, 1)) {
        return;
    }
    demand(1, 0, 50);
    demand(2, 0, 50);
    CHECK(responseFor(1).request == RESP_ASSIGNED);
    CHECK(responseFor(2).request == RESP_QUEUED);

    server.stations[0].waitQueue.vehicles[0].arrivalTime.tv_sec -= (time_t)MAX_WAIT_SECONDS + 1;
    server.lastPing.tv_sec -= (time_t)SERVER_PING_PERIOD_SECONDS + 1;
    runServerMaintenance(&server);

    CHECK(responseFor(2).request == RESP_REFUSED);
    CHECK(hasNoResponse(2));
    CHECK(responseFor(1).request == NOTIF_PING);
    CHECK(server.stats.refused == 1);

    // Ping trop récent : rien de plus
    runServerMaintenance(&server);
    CHECK(hasNoResponse(1));

    shutdownServer(&server);
}

static void testShutdownNotifiesKnownVehicles(void) {
    if (!setUp(1, 1)) {
        return;
    }
    demand(1, 0, 50);
    demand(2, 0, 50);
    CHECK(responseFor(1).request == RESP_ASSIGNED);
    CHECK(responseFor(2).request == RESP_QUEUED);

    CHECK(!handle(REQ_SHUTDOWN, -1, -1, -1, 0));
    CHECK(responseFor(1).request == NOTIF_SHUTDOWN);
    CHECK(responseFor(2).request == NOTIF_SHUTDOWN);

    shutdownServer(&server);
    shutdownServer(&server); // Sans effet la seconde fois
    CHECK(server.queue == -1);
}

static void* serverThread(void* arg) {
    (void)arg;
    runServer(&server);
    return NULL;
}

// Nombre de véhicules (threads) lancés sur une station de 2 bornes
#define END_TO_END_VEHICLES 4

static void testEndToEndWithVehicleThreads(void) {
    if (!setUp(1, 2)) {
        return;
    }
    pthread_t serverTid;
    CHECK(pthread_create(&serverTid, NULL, serverThread, NULL) == 0);

    // 0,5 kWh à recharger chacun : quelques secondes au total, avec une file d'attente
    struct Vehicle* vehicles[END_TO_END_VEHICLES];
    struct VehicleTrip trips[END_TO_END_VEHICLES];
    pthread_t vehicleTids[END_TO_END_VEHICLES];
    for (int i = 0; i < END_TO_END_VEHICLES; i++) {
        vehicles[i] = createVehicle(i, CAR, 50.0, 49.5, 50, "Belfort", i == END_TO_END_VEHICLES - 1);
        trips[i] = (struct VehicleTrip){ .vehicle = vehicles[i], .serverQueue = server.queue, .stationId = 0 };
        CHECK(pthread_create(&vehicleTids[i], NULL, vehicleThread, &trips[i]) == 0);
    }
    for (int i = 0; i < END_TO_END_VEHICLES; i++) {
        pthread_join(vehicleTids[i], NULL);
        CHECK(trips[i].outcome == VEHICLE_CHARGED);
        CHECK(isFullyCharged(vehicles[i]));
        destroyVehicle(vehicles[i]);
    }

    Message shutdown = { .request = REQ_SHUTDOWN };
    CHECK(sendToServer(server.queue, &shutdown));
    pthread_join(serverTid, NULL);

    CHECK(server.stats.completedCharges == END_TO_END_VEHICLES);
    CHECK(server.stats.malformed == 0);
    // 4 × 0,5 kWh, à quelques millisecondes de latence près
    CHECK(server.stats.deliveredEnergyKWh > 1.9 && server.stats.deliveredEnergyKWh < 2.5);
    printServerReport(&server);

    shutdownServer(&server);
}

int main(void) {
    RUN_TEST(testDemandsGetAssignedQueuedOrRefused);
    RUN_TEST(testChargeEndServesNextInQueue);
    RUN_TEST(testFaultNotifiesThenRepairServesQueue);
    RUN_TEST(testFaultRedirectsToAnotherStationWhenQueueIsFull);
    RUN_TEST(testInvalidRequestsAreIgnored);
    RUN_TEST(testCancelAndPong);
    RUN_TEST(testMaintenanceExpiresWaitsAndPingsChargingVehicles);
    RUN_TEST(testShutdownNotifiesKnownVehicles);
    RUN_TEST(testEndToEndWithVehicleThreads);
    return TEST_RESULT();
}
