#include "queue_manager.h"
#include "test_utils.h"

static bool enqueue(struct WaitQueue* queue, int vehicleId, VehiclePriority priority) {
    struct WaitingVehicle vehicle = { .vehicleId = vehicleId, .desiredPower = 50, .priority = priority };
    return enqueueVehicle(queue, &vehicle);
}

static int dequeueId(struct WaitQueue* queue) {
    struct WaitingVehicle next;
    return dequeueNextVehicle(queue, &next) ? next.vehicleId : -1;
}

static void testFirstComeFirstServedAtSamePriority(void) {
    struct WaitQueue queue;
    initWaitQueue(&queue);
    CHECK(enqueue(&queue, 1, PRIORITY_NORMAL));
    CHECK(enqueue(&queue, 2, PRIORITY_NORMAL));
    CHECK(enqueue(&queue, 3, PRIORITY_NORMAL));

    CHECK(dequeueId(&queue) == 1);
    CHECK(dequeueId(&queue) == 2);
    CHECK(dequeueId(&queue) == 3);
    CHECK(dequeueId(&queue) == -1);
}

static void testPrioritaryVehiclesGoFirst(void) {
    struct WaitQueue queue;
    initWaitQueue(&queue);
    CHECK(enqueue(&queue, 1, PRIORITY_NORMAL));
    CHECK(enqueue(&queue, 2, PRIORITY_HIGH));
    CHECK(enqueue(&queue, 3, PRIORITY_NORMAL));
    CHECK(enqueue(&queue, 4, PRIORITY_HIGH));

    CHECK(waitQueuePosition(&queue, 2) == 1);
    CHECK(waitQueuePosition(&queue, 4) == 2);
    CHECK(waitQueuePosition(&queue, 1) == 3);
    CHECK(waitQueuePosition(&queue, 3) == 4);

    CHECK(dequeueId(&queue) == 2);
    CHECK(dequeueId(&queue) == 4);
    CHECK(dequeueId(&queue) == 1);
    CHECK(dequeueId(&queue) == 3);
}

static void testRemoveWaitingVehicle(void) {
    struct WaitQueue queue;
    initWaitQueue(&queue);
    CHECK(enqueue(&queue, 1, PRIORITY_NORMAL));
    CHECK(enqueue(&queue, 2, PRIORITY_NORMAL));
    CHECK(enqueue(&queue, 3, PRIORITY_NORMAL));

    CHECK(removeWaitingVehicle(&queue, 2));
    CHECK(!removeWaitingVehicle(&queue, 2));
    CHECK(waitQueueLength(&queue) == 2);
    CHECK(waitQueuePosition(&queue, 2) == -1);
    CHECK(waitQueuePosition(&queue, 3) == 2);
}

static void testRejectsDuplicatesAndOverflow(void) {
    struct WaitQueue queue;
    initWaitQueue(&queue);
    CHECK(enqueue(&queue, 0, PRIORITY_NORMAL));
    CHECK(!enqueue(&queue, 0, PRIORITY_HIGH));

    for (int id = 1; id < MAX_WAITING_VEHICLES; id++) {
        CHECK(enqueue(&queue, id, PRIORITY_NORMAL));
    }
    CHECK(waitQueueLength(&queue) == MAX_WAITING_VEHICLES);
    CHECK(!enqueue(&queue, MAX_WAITING_VEHICLES, PRIORITY_HIGH));
}

static void testWaitingTimeStartsAtEnqueue(void) {
    struct WaitQueue queue;
    initWaitQueue(&queue);
    CHECK(enqueue(&queue, 1, PRIORITY_NORMAL));

    double waited = waitingTimeSeconds(&queue.vehicles[0]);
    CHECK(waited >= 0.0 && waited < 1.0);
}

int main(void) {
    RUN_TEST(testFirstComeFirstServedAtSamePriority);
    RUN_TEST(testPrioritaryVehiclesGoFirst);
    RUN_TEST(testRemoveWaitingVehicle);
    RUN_TEST(testRejectsDuplicatesAndOverflow);
    RUN_TEST(testWaitingTimeStartsAtEnqueue);
    return TEST_RESULT();
}
