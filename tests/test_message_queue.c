#include "message_queue.h"
#include "test_utils.h"
#include <pthread.h>

// File privée au test : aucun conflit avec la file du serveur ou un autre test
static msgq_t queue;

static void testVehicleTypesNeverReachServer(void) {
    CHECK(vehicleMsgType(0) != SERVER_MSG_TYPE);
    CHECK(vehicleMsgType(0) > 0);
    CHECK(vehicleMsgType(1) != vehicleMsgType(2));
}

static void testServerMessageRoundTrip(void) {
    // mtype volontairement faux : sendToServer doit le corriger
    Message sent = {
        .mtype = 42,
        .request = REQ_CHARGE_DEMAND,
        .vehicle_id = 12,
        .station_id = 2,
        .charger_id = -1,
        .desired_power = 50,
        .estimated_duration = 30,
        .is_prioritary = true,
    };
    CHECK(sendToServer(queue, &sent));

    Message received;
    CHECK(receiveMessage(queue, &received, SERVER_MSG_TYPE) == MSG_RECEIVED);
    CHECK(received.mtype == SERVER_MSG_TYPE);
    CHECK(received.request == REQ_CHARGE_DEMAND);
    CHECK(received.vehicle_id == 12);
    CHECK(received.station_id == 2);
    CHECK(received.desired_power == 50);
    CHECK(received.estimated_duration == 30);
    CHECK(received.is_prioritary);
}

static void testResponsesReachOnlyTheirVehicle(void) {
    Message response = { .request = RESP_ASSIGNED, .charger_id = 3 };
    CHECK(sendToVehicle(queue, 7, &response));
    CHECK(sendToVehicle(queue, 8, &response));

    Message received;
    CHECK(tryReceiveMessage(queue, &received, vehicleMsgType(9)) == MSG_NONE);
    CHECK(tryReceiveMessage(queue, &received, SERVER_MSG_TYPE) == MSG_NONE);

    CHECK(tryReceiveMessage(queue, &received, vehicleMsgType(8)) == MSG_RECEIVED);
    CHECK(received.vehicle_id == 8);
    CHECK(received.charger_id == 3);

    CHECK(tryReceiveMessage(queue, &received, vehicleMsgType(7)) == MSG_RECEIVED);
    CHECK(received.vehicle_id == 7);
    CHECK(tryReceiveMessage(queue, &received, vehicleMsgType(7)) == MSG_NONE);
}

struct VehicleListener {
    msgq_t queue;
    int vehicleId;
    int messagesBeforeShutdown; // Messages reçus avant NOTIF_SHUTDOWN
    ReceiveStatus lastStatus;
};

// Boucle type d'un thread véhicule : traite ses messages jusqu'à l'arrêt ou la fermeture de la file
static void* vehicleListener(void* arg) {
    struct VehicleListener* listener = arg;
    Message msg;
    while ((listener->lastStatus = receiveMessage(listener->queue, &msg, vehicleMsgType(listener->vehicleId)))
           == MSG_RECEIVED) {
        if (msg.request == NOTIF_SHUTDOWN) {
            break;
        }
        listener->messagesBeforeShutdown++;
    }
    return NULL;
}

static void testShutdownNotificationStopsVehicle(void) {
    struct VehicleListener listener = { .queue = queue, .vehicleId = 4 };
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, vehicleListener, &listener) == 0);

    Message queued = { .request = RESP_QUEUED, .station_id = 0, .queue_position = 2 };
    Message shutdown = { .request = NOTIF_SHUTDOWN };
    CHECK(sendToVehicle(queue, 4, &queued));
    CHECK(sendToVehicle(queue, 4, &shutdown));
    pthread_join(thread, NULL);

    CHECK(listener.lastStatus == MSG_RECEIVED);
    CHECK(listener.messagesBeforeShutdown == 1);
}

static void testDestroyedQueueWakesBlockedVehicle(void) {
    struct VehicleListener listener = { .queue = initMessageQueue(IPC_PRIVATE), .vehicleId = 5 };
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, vehicleListener, &listener) == 0);

    destroyMessageQueue(listener.queue);
    pthread_join(thread, NULL);

    CHECK(listener.lastStatus == MSG_CLOSED);
    Message msg;
    CHECK(tryReceiveMessage(listener.queue, &msg, vehicleMsgType(5)) == MSG_CLOSED);
}

// Utilise la vraie clé du serveur : ne pas lancer pendant une simulation
static void testCreateServerQueuePurgesStaleMessages(void) {
    msgq_t stale = openServerQueue();
    Message leftover = { .request = REQ_CHARGE_DEMAND, .vehicle_id = 1, .station_id = 0, .desired_power = 50 };
    CHECK(sendToServer(stale, &leftover));

    msgq_t fresh = createServerQueue();
    CHECK(fresh != -1);
    Message msg;
    CHECK(tryReceiveMessage(fresh, &msg, SERVER_MSG_TYPE) == MSG_NONE);
    CHECK(openServerQueue() == fresh);

    destroyMessageQueue(fresh);
}

static void testWellFormedMessages(void) {
    Message demand = { .request = REQ_CHARGE_DEMAND, .vehicle_id = 1, .station_id = 0, .desired_power = 50 };
    CHECK(isWellFormedMessage(&demand));
    demand.desired_power = 0;
    CHECK(!isWellFormedMessage(&demand));

    Message end = { .request = REQ_CHARGE_END, .vehicle_id = 1, .station_id = 0, .charger_id = -1 };
    CHECK(!isWellFormedMessage(&end));
    end.charger_id = 2;
    CHECK(isWellFormedMessage(&end));

    Message alert = { .request = REQ_ALERT, .vehicle_id = -1, .station_id = 1, .charger_id = 0 };
    CHECK(isWellFormedMessage(&alert));

    Message pong = { .request = REQ_PONG, .vehicle_id = 3, .battery_level = 101 };
    CHECK(!isWellFormedMessage(&pong));
    pong.battery_level = 80;
    CHECK(isWellFormedMessage(&pong));

    Message queued = { .request = RESP_QUEUED, .vehicle_id = 3, .station_id = 0, .queue_position = 0 };
    CHECK(!isWellFormedMessage(&queued));

    Message unknown = { .request = (RequestType)99 };
    CHECK(!isWellFormedMessage(&unknown));
}

int main(void) {
    queue = initMessageQueue(IPC_PRIVATE);
    if (queue == -1) {
        return 1;
    }

    RUN_TEST(testVehicleTypesNeverReachServer);
    RUN_TEST(testServerMessageRoundTrip);
    RUN_TEST(testResponsesReachOnlyTheirVehicle);
    RUN_TEST(testShutdownNotificationStopsVehicle);
    RUN_TEST(testDestroyedQueueWakesBlockedVehicle);
    RUN_TEST(testCreateServerQueuePurgesStaleMessages);
    RUN_TEST(testWellFormedMessages);

    destroyMessageQueue(queue);
    return TEST_RESULT();
}
