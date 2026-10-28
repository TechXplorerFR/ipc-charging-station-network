#include "allocator.h"
#include "test_utils.h"
#include <pthread.h>

// Station de test : bornes 0, 1 et 2 de 22, 50 et 150 kW
static bool initTestStation(struct Station* station) {
    if (!initStation(station, 0, 3)) {
        return false;
    }
    station->chargers[0].maxPower = 22;
    station->chargers[1].maxPower = 50;
    station->chargers[2].maxPower = 150;
    return true;
}

static struct Assignment request(struct Station* station, int vehicleId, int desiredPower, VehiclePriority priority) {
    struct WaitingVehicle vehicle = { .vehicleId = vehicleId, .desiredPower = desiredPower, .priority = priority };
    return requestCharger(station, &vehicle);
}

static void testPicksBestFittingCharger(void) {
    struct Station station;
    if (!initTestStation(&station)) {
        CHECK(false);
        return;
    }

    // Plus petite borne couvrant la demande
    CHECK(request(&station, 1, 40, PRIORITY_NORMAL).chargerIndex == 1);
    // Aucune borne libre ne couvre la demande : la plus puissante
    CHECK(request(&station, 2, 100, PRIORITY_NORMAL).chargerIndex == 2);
    CHECK(request(&station, 3, 100, PRIORITY_NORMAL).chargerIndex == 0);

    CHECK(station.chargers[1].vehicleId == 1);
    CHECK(station.chargers[2].vehicleId == 2);
    CHECK(station.chargers[0].vehicleId == 3);
    CHECK(!hasAvailableCharger(&station));

    destroyStation(&station);
}

static void testQueuesThenRefusesWhenFull(void) {
    struct Station station;
    if (!initTestStation(&station)) {
        CHECK(false);
        return;
    }
    for (int id = 0; id < 3; id++) {
        CHECK(request(&station, id, 50, PRIORITY_NORMAL).result == ASSIGN_OK);
    }

    struct Assignment queued = request(&station, 10, 50, PRIORITY_NORMAL);
    CHECK(queued.result == ASSIGN_QUEUED);
    CHECK(queued.queuePosition == 1);
    CHECK(queued.chargerIndex == -1);

    struct Assignment prioritary = request(&station, 11, 50, PRIORITY_HIGH);
    CHECK(prioritary.result == ASSIGN_QUEUED);
    CHECK(prioritary.queuePosition == 1);

    for (int id = 12; waitQueueLength(&station.waitQueue) < MAX_WAITING_VEHICLES; id++) {
        CHECK(request(&station, id, 50, PRIORITY_NORMAL).result == ASSIGN_QUEUED);
    }
    CHECK(request(&station, 1000, 50, PRIORITY_HIGH).result == ASSIGN_REFUSED);

    destroyStation(&station);
}

static void testReleasedChargerGoesToNextInQueue(void) {
    struct Station station;
    if (!initTestStation(&station)) {
        CHECK(false);
        return;
    }
    for (int id = 0; id < 3; id++) {
        request(&station, id, 50, PRIORITY_NORMAL);
    }
    request(&station, 10, 50, PRIORITY_NORMAL);
    request(&station, 11, 50, PRIORITY_HIGH);

    struct WaitingVehicle next;
    CHECK(releaseChargerAndServeQueue(&station, 1, &next) == HANDOVER_REASSIGNED);
    CHECK(next.vehicleId == 11);
    CHECK(station.chargers[1].status == OCCUPIED);
    CHECK(station.chargers[1].vehicleId == 11);
    CHECK(!hasAvailableCharger(&station));

    CHECK(releaseChargerAndServeQueue(&station, 1, &next) == HANDOVER_REASSIGNED);
    CHECK(next.vehicleId == 10);

    CHECK(releaseChargerAndServeQueue(&station, 1, &next) == HANDOVER_FREED);
    CHECK(station.chargers[1].status == AVAILABLE);
    CHECK(station.chargers[1].vehicleId == NO_VEHICLE);
    CHECK(hasAvailableCharger(&station));

    // La borne n'est plus occupée, et un indice inconnu est refusé
    CHECK(releaseChargerAndServeQueue(&station, 1, &next) == HANDOVER_INVALID);
    CHECK(releaseChargerAndServeQueue(&station, 3, &next) == HANDOVER_INVALID);

    destroyStation(&station);
}

static void testFaultAndRepair(void) {
    struct Station station;
    if (!initTestStation(&station)) {
        CHECK(false);
        return;
    }
    for (int id = 0; id < 3; id++) {
        request(&station, id, 50, PRIORITY_NORMAL);
    }
    request(&station, 10, 50, PRIORITY_NORMAL);

    // Pour 50 kW : le véhicule 0 prend la borne de 50 kW, le 1 celle de 150 kW, le 2 celle de 22 kW
    CHECK(takeChargerOutOfService(&station, 2, OUT_OF_SERVICE) == 1);
    CHECK(station.chargers[2].status == OUT_OF_SERVICE);
    CHECK(station.chargers[2].vehicleId == NO_VEHICLE);
    CHECK(takeChargerOutOfService(&station, 2, MAINTENANCE) == NO_VEHICLE);
    CHECK(takeChargerOutOfService(&station, 2, AVAILABLE) == NO_VEHICLE);
    CHECK(station.chargers[2].status == MAINTENANCE);

    // Une borne en panne ne se libère pas comme une borne occupée
    struct WaitingVehicle next;
    CHECK(releaseChargerAndServeQueue(&station, 2, &next) == HANDOVER_INVALID);

    CHECK(repairChargerAndServeQueue(&station, 2, &next) == HANDOVER_REASSIGNED);
    CHECK(next.vehicleId == 10);
    CHECK(station.chargers[2].vehicleId == 10);
    CHECK(repairChargerAndServeQueue(&station, 2, &next) == HANDOVER_INVALID);

    // Panne d'une borne libre puis réparation sans file d'attente : la borne redevient disponible
    CHECK(releaseChargerAndServeQueue(&station, 0, &next) == HANDOVER_FREED);
    CHECK(takeChargerOutOfService(&station, 0, OUT_OF_SERVICE) == NO_VEHICLE);
    CHECK(!hasAvailableCharger(&station));
    CHECK(repairChargerAndServeQueue(&station, 0, &next) == HANDOVER_FREED);
    CHECK(hasAvailableCharger(&station));

    destroyStation(&station);
}

static void testCancelWaitingVehicle(void) {
    struct Station station;
    if (!initTestStation(&station)) {
        CHECK(false);
        return;
    }
    for (int id = 0; id < 3; id++) {
        request(&station, id, 50, PRIORITY_NORMAL);
    }
    request(&station, 10, 50, PRIORITY_NORMAL);

    CHECK(cancelWaitingVehicle(&station, 10));
    CHECK(!cancelWaitingVehicle(&station, 10));

    struct WaitingVehicle next;
    CHECK(releaseChargerAndServeQueue(&station, 0, &next) == HANDOVER_FREED);

    destroyStation(&station);
}

// Nombre de véhicules (threads) qui demandent une borne en même temps
#define CONCURRENT_VEHICLES 32

struct ConcurrentRequest {
    struct Station* station;
    int vehicleId;
    struct Assignment assignment;
};

static void* requestFromThread(void* arg) {
    struct ConcurrentRequest* req = arg;
    req->assignment = request(req->station, req->vehicleId, 50, PRIORITY_NORMAL);
    return NULL;
}

static void testConcurrentRequestsNeverShareACharger(void) {
    struct Station station;
    if (!initTestStation(&station)) {
        CHECK(false);
        return;
    }

    pthread_t threads[CONCURRENT_VEHICLES];
    struct ConcurrentRequest requests[CONCURRENT_VEHICLES];
    for (int i = 0; i < CONCURRENT_VEHICLES; i++) {
        requests[i] = (struct ConcurrentRequest){ .station = &station, .vehicleId = i };
        CHECK(pthread_create(&threads[i], NULL, requestFromThread, &requests[i]) == 0);
    }
    for (int i = 0; i < CONCURRENT_VEHICLES; i++) {
        pthread_join(threads[i], NULL);
    }

    int assigned = 0;
    for (int i = 0; i < CONCURRENT_VEHICLES; i++) {
        if (requests[i].assignment.result == ASSIGN_OK) {
            assigned++;
            CHECK(station.chargers[requests[i].assignment.chargerIndex].vehicleId == requests[i].vehicleId);
        } else {
            CHECK(requests[i].assignment.result == ASSIGN_QUEUED);
        }
    }
    CHECK(assigned == station.numChargers);
    CHECK(waitQueueLength(&station.waitQueue) == CONCURRENT_VEHICLES - station.numChargers);

    destroyStation(&station);
}

int main(void) {
    RUN_TEST(testPicksBestFittingCharger);
    RUN_TEST(testQueuesThenRefusesWhenFull);
    RUN_TEST(testReleasedChargerGoesToNextInQueue);
    RUN_TEST(testFaultAndRepair);
    RUN_TEST(testCancelWaitingVehicle);
    RUN_TEST(testConcurrentRequestsNeverShareACharger);
    return TEST_RESULT();
}
