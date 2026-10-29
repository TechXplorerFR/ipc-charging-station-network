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

// Simule une attente de seconds secondes en reculant l'heure d'arrivée du véhicule
static void age(struct WaitQueue* queue, int vehicleId, time_t seconds) {
    queue->vehicles[waitQueuePosition(queue, vehicleId) - 1].arrivalTime.tv_sec -= seconds;
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

static void testEnqueueFirstStaysBehindHigherPriority(void) {
    struct WaitQueue queue;
    initWaitQueue(&queue);
    CHECK(enqueue(&queue, 1, PRIORITY_NORMAL));
    CHECK(enqueue(&queue, 2, PRIORITY_HIGH));
    CHECK(enqueue(&queue, 3, PRIORITY_NORMAL));

    struct WaitingVehicle interrupted = { .vehicleId = 4, .desiredPower = 50, .priority = PRIORITY_NORMAL };
    CHECK(enqueueVehicleFirst(&queue, &interrupted));
    struct WaitingVehicle interruptedPrioritary = { .vehicleId = 5, .desiredPower = 50, .priority = PRIORITY_HIGH };
    CHECK(enqueueVehicleFirst(&queue, &interruptedPrioritary));
    CHECK(!enqueueVehicleFirst(&queue, &interrupted));

    CHECK(dequeueId(&queue) == 5);
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

static void testLongWaitingVehicleOvertakesLaterPrioritary(void) {
    struct WaitQueue queue;
    initWaitQueue(&queue);
    CHECK(enqueue(&queue, 1, PRIORITY_NORMAL));
    age(&queue, 1, 100);
    CHECK(enqueue(&queue, 2, PRIORITY_HIGH));
    CHECK(enqueue(&queue, 3, PRIORITY_NORMAL));

    CHECK(promoteLongWaitingVehicles(&queue, PRIORITY_AGING_SECONDS) == 1);
    CHECK(queue.vehicles[0].vehicleId == 1);
    CHECK(queue.vehicles[0].priority == PRIORITY_HIGH);
    CHECK(waitQueuePosition(&queue, 2) == 2);
    CHECK(waitQueuePosition(&queue, 3) == 3);
    CHECK(queue.vehicles[2].priority == PRIORITY_NORMAL);

    // Déjà prioritaire : pas de nouvelle promotion
    CHECK(promoteLongWaitingVehicles(&queue, PRIORITY_AGING_SECONDS) == 0);
}

static void testPromotedVehicleStaysBehindEarlierPrioritary(void) {
    struct WaitQueue queue;
    initWaitQueue(&queue);
    CHECK(enqueue(&queue, 1, PRIORITY_HIGH));
    CHECK(enqueue(&queue, 2, PRIORITY_NORMAL));
    CHECK(enqueue(&queue, 3, PRIORITY_NORMAL));
    age(&queue, 1, 200);
    age(&queue, 2, 100);
    age(&queue, 3, 90);

    CHECK(promoteLongWaitingVehicles(&queue, PRIORITY_AGING_SECONDS) == 2);
    CHECK(dequeueId(&queue) == 1);
    CHECK(dequeueId(&queue) == 2);
    CHECK(dequeueId(&queue) == 3);
}

static void testExpiredVehiclesAreRemoved(void) {
    struct WaitQueue queue;
    initWaitQueue(&queue);
    CHECK(enqueue(&queue, 1, PRIORITY_NORMAL));
    CHECK(enqueue(&queue, 2, PRIORITY_NORMAL));
    CHECK(enqueue(&queue, 3, PRIORITY_HIGH));
    age(&queue, 1, 400);
    age(&queue, 3, 400);

    // Capacité limitée à 1 : un seul retiré à la fois, le prochain servi en premier
    struct WaitingVehicle expired[MAX_WAITING_VEHICLES];
    CHECK(removeExpiredVehicles(&queue, MAX_WAIT_SECONDS, expired, 1) == 1);
    CHECK(expired[0].vehicleId == 3);

    CHECK(removeExpiredVehicles(&queue, MAX_WAIT_SECONDS, expired, MAX_WAITING_VEHICLES) == 1);
    CHECK(expired[0].vehicleId == 1);
    CHECK(waitQueueLength(&queue) == 1);
    CHECK(waitQueuePosition(&queue, 2) == 1);
    CHECK(queue.stats.expired == 2);
}

static void testStatsCountEveryExit(void) {
    struct WaitQueue queue;
    initWaitQueue(&queue);
    CHECK(averageWaitSeconds(&queue) == 0.0);

    CHECK(enqueue(&queue, 1, PRIORITY_NORMAL));
    CHECK(enqueue(&queue, 2, PRIORITY_NORMAL));
    CHECK(enqueue(&queue, 3, PRIORITY_NORMAL));
    age(&queue, 1, 10);
    age(&queue, 2, 30);

    CHECK(dequeueId(&queue) == 1);
    CHECK(dequeueId(&queue) == 2);
    CHECK(removeWaitingVehicle(&queue, 3));
    CHECK(queue.stats.served == 2);
    CHECK(queue.stats.cancelled == 1);
    double average = averageWaitSeconds(&queue);
    CHECK(average >= 20.0 && average < 21.0);

    for (int id = 0; id < MAX_WAITING_VEHICLES; id++) {
        CHECK(enqueue(&queue, id, PRIORITY_NORMAL));
    }
    CHECK(!enqueue(&queue, MAX_WAITING_VEHICLES, PRIORITY_NORMAL));
    CHECK(!enqueue(&queue, 0, PRIORITY_NORMAL)); // Doublon : pas compté comme un refus
    CHECK(queue.stats.rejected == 1);
}

int main(void) {
    RUN_TEST(testFirstComeFirstServedAtSamePriority);
    RUN_TEST(testPrioritaryVehiclesGoFirst);
    RUN_TEST(testEnqueueFirstStaysBehindHigherPriority);
    RUN_TEST(testRemoveWaitingVehicle);
    RUN_TEST(testRejectsDuplicatesAndOverflow);
    RUN_TEST(testWaitingTimeStartsAtEnqueue);
    RUN_TEST(testLongWaitingVehicleOvertakesLaterPrioritary);
    RUN_TEST(testPromotedVehicleStaysBehindEarlierPrioritary);
    RUN_TEST(testExpiredVehiclesAreRemoved);
    RUN_TEST(testStatsCountEveryExit);
    return TEST_RESULT();
}
