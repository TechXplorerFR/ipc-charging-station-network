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
    Message sent = {
        .mtype = SERVER_MSG_TYPE,
        .request = REQ_CHARGE_DEMAND,
        .vehicle_id = 12,
        .station_id = 2,
        .charger_id = -1,
        .desired_power = 50,
        .estimated_duration = 30,
        .is_prioritary = true,
    };
    CHECK(sendMessage(queue, &sent));

    Message received;
    CHECK(receiveMessage(queue, &received, SERVER_MSG_TYPE));
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

static void* vehicleThread(void* arg) {
    Message* received = arg;
    receiveMessage(queue, received, vehicleMsgType(4));
    return NULL;
}

static void testThreadWaitsForItsResponse(void) {
    Message received = { .request = REQ_SHUTDOWN };
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, vehicleThread, &received) == 0);

    Message response = { .request = RESP_QUEUED, .queue_position = 2 };
    CHECK(sendToVehicle(queue, 4, &response));
    pthread_join(thread, NULL);

    CHECK(received.request == RESP_QUEUED);
    CHECK(received.queue_position == 2);
}

int main(void) {
    queue = initMessageQueue(IPC_PRIVATE);
    if (queue == -1) {
        return 1;
    }

    RUN_TEST(testVehicleTypesNeverReachServer);
    RUN_TEST(testServerMessageRoundTrip);
    RUN_TEST(testResponsesReachOnlyTheirVehicle);
    RUN_TEST(testThreadWaitsForItsResponse);

    destroyMessageQueue(queue);
    return TEST_RESULT();
}
