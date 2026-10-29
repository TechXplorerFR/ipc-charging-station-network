#include "vehicle.h"
#include "test_utils.h"
#include <pthread.h>
#include <string.h>

// File privée au test, qui joue ici le rôle du serveur
static msgq_t queue;

static struct Vehicle* newVehicle(int id, double capacity, double charge, int maxPower) {
    return createVehicle(id, CAR, capacity, charge, maxPower, "Belfort", false);
}

static void startTrip(struct VehicleTrip* trip, pthread_t* thread, struct Vehicle* vehicle, msgq_t serverQueue) {
    *trip = (struct VehicleTrip){ .vehicle = vehicle, .serverQueue = serverQueue, .stationId = 0 };
    CHECK(pthread_create(thread, NULL, vehicleThread, trip) == 0);
}

static Message receiveFromVehicle(void) {
    Message msg = { .request = REQ_SHUTDOWN };
    CHECK(receiveMessage(queue, &msg, SERVER_MSG_TYPE) == MSG_RECEIVED);
    return msg;
}

static void reply(int vehicleId, RequestType request, int chargerId, int power) {
    Message msg = { .request = request, .station_id = 0, .charger_id = chargerId, .desired_power = power,
                    .queue_position = 1 };
    CHECK(sendToVehicle(queue, vehicleId, &msg));
}

static void testCreateVehicleValidatesParameters(void) {
    struct Vehicle* vehicle = createVehicle(1, BUS, 300.0, 120.0, 150, "Montbéliard", true);
    CHECK(vehicle != NULL);
    if (vehicle != NULL) {
        CHECK(vehicle->type == BUS);
        CHECK(vehicle->isPrioritary);
        destroyVehicle(vehicle);
    }

    CHECK(createVehicle(-1, CAR, 50.0, 10.0, 50, "Belfort", false) == NULL);
    CHECK(createVehicle(1, CAR, 0.0, 0.0, 50, "Belfort", false) == NULL);
    CHECK(createVehicle(1, CAR, 50.0, 60.0, 50, "Belfort", false) == NULL);
    CHECK(createVehicle(1, CAR, 50.0, 10.0, 0, "Belfort", false) == NULL);
    CHECK(createVehicle(1, CAR, 50.0, 10.0, 50, NULL, false) == NULL);
    destroyVehicle(NULL);
}

static void testBatteryAndEstimates(void) {
    struct Vehicle* vehicle = newVehicle(1, 60.0, 30.0, 100);
    if (vehicle == NULL) {
        CHECK(false);
        return;
    }
    CHECK(batteryLevelPercent(vehicle) == 50);
    CHECK(strcmp(vehicleTypeName(VAN), "camionnette") == 0);

    // 30 kWh à 50 kW = 36 min simulées = 36 s réelles ; au-delà de 100 kW, le véhicule limite la puissance
    CHECK(estimatedChargeDurationSeconds(vehicle, 50) == 36);
    CHECK(estimatedChargeDurationSeconds(vehicle, 200) == 18);

    double added = addCharge(vehicle, 50, 36.0);
    CHECK(added > 29.99 && added < 30.01);
    CHECK(isFullyCharged(vehicle));
    CHECK(addCharge(vehicle, 50, 10.0) == 0.0);
    CHECK(batteryLevelPercent(vehicle) == 100);
    CHECK(estimatedChargeDurationSeconds(vehicle, 50) == 0);

    destroyVehicle(vehicle);
}

static void testChargeRequestContent(void) {
    struct Vehicle* vehicle = createVehicle(3, BUS, 60.0, 30.0, 50, "Belfort", true);
    if (vehicle == NULL) {
        CHECK(false);
        return;
    }
    CHECK(sendChargeRequest(queue, vehicle, 2));

    Message msg = receiveFromVehicle();
    CHECK(msg.request == REQ_CHARGE_DEMAND);
    CHECK(msg.vehicle_id == 3);
    CHECK(msg.station_id == 2);
    CHECK(msg.desired_power == 50);
    CHECK(msg.estimated_duration == 36);
    CHECK(msg.battery_level == 50);
    CHECK(msg.is_prioritary);
    CHECK(isWellFormedMessage(&msg));

    destroyVehicle(vehicle);
}

static void testFullCycleAnswersPing(void) {
    struct Vehicle* vehicle = newVehicle(4, 50.0, 49.0, 50); // 1 kWh à 50 kW : environ 1,2 s
    if (vehicle == NULL) {
        CHECK(false);
        return;
    }
    struct VehicleTrip trip;
    pthread_t thread;
    startTrip(&trip, &thread, vehicle, queue);

    CHECK(receiveFromVehicle().request == REQ_CHARGE_DEMAND);
    reply(4, RESP_ASSIGNED, 1, 50);
    reply(4, NOTIF_PING, -1, 0);

    Message pong = receiveFromVehicle();
    CHECK(pong.request == REQ_PONG);
    CHECK(pong.vehicle_id == 4);
    CHECK(pong.battery_level >= 98 && pong.battery_level <= 100);

    Message end = receiveFromVehicle();
    CHECK(end.request == REQ_CHARGE_END);
    CHECK(end.station_id == 0);
    CHECK(end.charger_id == 1);
    CHECK(end.battery_level == 100);

    pthread_join(thread, NULL);
    CHECK(trip.outcome == VEHICLE_CHARGED);
    CHECK(isFullyCharged(vehicle));
    destroyVehicle(vehicle);
}

static void testQueuedThenAssigned(void) {
    struct Vehicle* vehicle = newVehicle(5, 50.0, 49.9, 50);
    if (vehicle == NULL) {
        CHECK(false);
        return;
    }
    struct VehicleTrip trip;
    pthread_t thread;
    startTrip(&trip, &thread, vehicle, queue);

    CHECK(receiveFromVehicle().request == REQ_CHARGE_DEMAND);
    reply(5, RESP_QUEUED, -1, 0);
    reply(5, RESP_ASSIGNED, 2, 50);
    CHECK(receiveFromVehicle().charger_id == 2);

    pthread_join(thread, NULL);
    CHECK(trip.outcome == VEHICLE_CHARGED);
    destroyVehicle(vehicle);
}

static void testRefused(void) {
    struct Vehicle* vehicle = newVehicle(6, 50.0, 10.0, 50);
    if (vehicle == NULL) {
        CHECK(false);
        return;
    }
    struct VehicleTrip trip;
    pthread_t thread;
    startTrip(&trip, &thread, vehicle, queue);

    CHECK(receiveFromVehicle().request == REQ_CHARGE_DEMAND);
    reply(6, RESP_REFUSED, -1, 0);

    pthread_join(thread, NULL);
    CHECK(trip.outcome == VEHICLE_REFUSED);
    destroyVehicle(vehicle);
}

static void testFaultThenRedirection(void) {
    struct Vehicle* vehicle = newVehicle(7, 50.0, 45.0, 150);
    if (vehicle == NULL) {
        CHECK(false);
        return;
    }
    struct VehicleTrip trip;
    pthread_t thread;
    startTrip(&trip, &thread, vehicle, queue);

    CHECK(receiveFromVehicle().request == REQ_CHARGE_DEMAND);
    reply(7, RESP_ASSIGNED, 1, 10);   // 5 kWh à 10 kW : 30 s, interrompu avant la fin
    reply(7, NOTIF_FAULT, 1, 0);
    reply(7, RESP_ASSIGNED, 2, 150);  // Redirection : environ 2 s

    Message end = receiveFromVehicle();
    CHECK(end.request == REQ_CHARGE_END);
    CHECK(end.charger_id == 2);

    pthread_join(thread, NULL);
    CHECK(trip.outcome == VEHICLE_CHARGED);
    destroyVehicle(vehicle);
}

static void testShutdownWhileQueuedOrCharging(void) {
    struct Vehicle* waiting = newVehicle(8, 50.0, 10.0, 50);
    struct Vehicle* charging = newVehicle(9, 50.0, 10.0, 50);
    if (waiting == NULL || charging == NULL) {
        CHECK(false);
        destroyVehicle(waiting);
        destroyVehicle(charging);
        return;
    }
    struct VehicleTrip waitingTrip, chargingTrip;
    pthread_t waitingThread, chargingThread;
    startTrip(&waitingTrip, &waitingThread, waiting, queue);
    startTrip(&chargingTrip, &chargingThread, charging, queue);

    CHECK(receiveFromVehicle().request == REQ_CHARGE_DEMAND);
    CHECK(receiveFromVehicle().request == REQ_CHARGE_DEMAND);
    reply(8, RESP_QUEUED, -1, 0);
    reply(9, RESP_ASSIGNED, 0, 50); // 40 kWh à 50 kW : 48 s, arrêté avant la fin
    reply(8, NOTIF_SHUTDOWN, -1, 0);
    reply(9, NOTIF_SHUTDOWN, -1, 0);

    pthread_join(waitingThread, NULL);
    pthread_join(chargingThread, NULL);
    CHECK(waitingTrip.outcome == VEHICLE_STOPPED);
    CHECK(chargingTrip.outcome == VEHICLE_STOPPED);
    CHECK(!isFullyCharged(charging));

    destroyVehicle(waiting);
    destroyVehicle(charging);
}

static void testClosedQueueStopsVehicle(void) {
    msgq_t serverQueue = initMessageQueue(IPC_PRIVATE);
    struct Vehicle* vehicle = newVehicle(10, 50.0, 10.0, 50);
    if (serverQueue == -1 || vehicle == NULL) {
        CHECK(false);
        destroyVehicle(vehicle);
        return;
    }
    struct VehicleTrip trip;
    pthread_t thread;
    startTrip(&trip, &thread, vehicle, serverQueue);

    Message demand;
    CHECK(receiveMessage(serverQueue, &demand, SERVER_MSG_TYPE) == MSG_RECEIVED);
    destroyMessageQueue(serverQueue);

    pthread_join(thread, NULL);
    CHECK(trip.outcome == VEHICLE_STOPPED);
    destroyVehicle(vehicle);
}

int main(void) {
    queue = initMessageQueue(IPC_PRIVATE);
    if (queue == -1) {
        return 1;
    }

    RUN_TEST(testCreateVehicleValidatesParameters);
    RUN_TEST(testBatteryAndEstimates);
    RUN_TEST(testChargeRequestContent);
    RUN_TEST(testFullCycleAnswersPing);
    RUN_TEST(testQueuedThenAssigned);
    RUN_TEST(testRefused);
    RUN_TEST(testFaultThenRedirection);
    RUN_TEST(testShutdownWhileQueuedOrCharging);
    RUN_TEST(testClosedQueueStopsVehicle);

    destroyMessageQueue(queue);
    return TEST_RESULT();
}
