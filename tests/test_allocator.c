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

// Occupe les 3 bornes avec les véhicules 0, 1 et 2 (50 kW) :
// le 0 prend la borne de 50 kW, le 1 celle de 150 kW, le 2 celle de 22 kW
static void fillStation(struct Station* station) {
    for (int id = 0; id < 3; id++) {
        request(station, id, 50, PRIORITY_NORMAL);
    }
}

static int sessionVehicle(const struct Station* station, int chargerIndex) {
    return station->chargers[chargerIndex].session.vehicleId;
}

static void testPicksBestFittingCharger(void) {
    struct Station station;
    if (!initTestStation(&station)) {
        CHECK(false);
        return;
    }

    // Plus petite borne couvrant la demande
    CHECK(request(&station, 1, 40, PRIORITY_HIGH).chargerIndex == 1);
    // Aucune borne libre ne couvre la demande : la plus puissante
    CHECK(request(&station, 2, 100, PRIORITY_NORMAL).chargerIndex == 2);
    CHECK(request(&station, 3, 100, PRIORITY_NORMAL).chargerIndex == 0);

    CHECK(sessionVehicle(&station, 1) == 1);
    CHECK(sessionVehicle(&station, 2) == 2);
    CHECK(sessionVehicle(&station, 0) == 3);
    CHECK(!hasAvailableCharger(&station));

    destroyStation(&station);
}

static void testSessionRecordsTheRequest(void) {
    struct Station station;
    if (!initTestStation(&station)) {
        CHECK(false);
        return;
    }

    struct timespec before;
    clock_gettime(CLOCK_MONOTONIC, &before);
    CHECK(request(&station, 7, 40, PRIORITY_HIGH).result == ASSIGN_OK);

    const struct ChargingSession* session = &station.chargers[1].session;
    CHECK(session->vehicleId == 7);
    CHECK(session->desiredPower == 40);
    CHECK(session->isPrioritary);
    CHECK(session->start.tv_sec > before.tv_sec
          || (session->start.tv_sec == before.tv_sec && session->start.tv_nsec >= before.tv_nsec));

    destroyStation(&station);
}

static void testQueuesThenRefusesWhenFull(void) {
    struct Station station;
    if (!initTestStation(&station)) {
        CHECK(false);
        return;
    }
    fillStation(&station);

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
    fillStation(&station);
    request(&station, 10, 50, PRIORITY_NORMAL);
    request(&station, 11, 30, PRIORITY_HIGH);

    struct ChargingSession finished;
    struct WaitingVehicle next;
    CHECK(releaseChargerAndServeQueue(&station, 1, &finished, &next) == HANDOVER_REASSIGNED);
    CHECK(finished.vehicleId == 0);
    CHECK(next.vehicleId == 11);
    CHECK(station.chargers[1].status == OCCUPIED);
    CHECK(sessionVehicle(&station, 1) == 11);
    CHECK(station.chargers[1].session.desiredPower == 30);
    CHECK(!hasAvailableCharger(&station));

    CHECK(releaseChargerAndServeQueue(&station, 1, &finished, &next) == HANDOVER_REASSIGNED);
    CHECK(finished.vehicleId == 11);
    CHECK(next.vehicleId == 10);

    CHECK(releaseChargerAndServeQueue(&station, 1, &finished, &next) == HANDOVER_FREED);
    CHECK(finished.vehicleId == 10);
    CHECK(station.chargers[1].status == AVAILABLE);
    CHECK(sessionVehicle(&station, 1) == NO_VEHICLE);
    CHECK(hasAvailableCharger(&station));

    // La borne n'est plus occupée, et un indice inconnu est refusé
    CHECK(releaseChargerAndServeQueue(&station, 1, &finished, &next) == HANDOVER_INVALID);
    CHECK(releaseChargerAndServeQueue(&station, 3, &finished, &next) == HANDOVER_INVALID);

    destroyStation(&station);
}

static void testFaultAndRepair(void) {
    struct Station station;
    if (!initTestStation(&station)) {
        CHECK(false);
        return;
    }
    fillStation(&station);
    request(&station, 10, 50, PRIORITY_NORMAL);

    struct ChargingSession interrupted;
    CHECK(takeChargerOutOfService(&station, 2, OUT_OF_SERVICE, &interrupted));
    CHECK(interrupted.vehicleId == 1);
    CHECK(interrupted.desiredPower == 50);
    CHECK(station.chargers[2].status == OUT_OF_SERVICE);
    CHECK(sessionVehicle(&station, 2) == NO_VEHICLE);

    CHECK(!takeChargerOutOfService(&station, 2, MAINTENANCE, &interrupted));
    CHECK(station.chargers[2].status == MAINTENANCE);
    CHECK(!takeChargerOutOfService(&station, 2, AVAILABLE, &interrupted));
    CHECK(station.chargers[2].status == MAINTENANCE);

    // Une borne en panne ne se libère pas comme une borne occupée
    struct ChargingSession finished;
    struct WaitingVehicle next;
    CHECK(releaseChargerAndServeQueue(&station, 2, &finished, &next) == HANDOVER_INVALID);

    CHECK(repairChargerAndServeQueue(&station, 2, &next) == HANDOVER_REASSIGNED);
    CHECK(next.vehicleId == 10);
    CHECK(sessionVehicle(&station, 2) == 10);
    CHECK(repairChargerAndServeQueue(&station, 2, &next) == HANDOVER_INVALID);

    // Panne d'une borne libre puis réparation sans file d'attente : la borne redevient disponible
    CHECK(releaseChargerAndServeQueue(&station, 0, &finished, &next) == HANDOVER_FREED);
    CHECK(!takeChargerOutOfService(&station, 0, OUT_OF_SERVICE, &interrupted));
    CHECK(!hasAvailableCharger(&station));
    CHECK(repairChargerAndServeQueue(&station, 0, &next) == HANDOVER_FREED);
    CHECK(hasAvailableCharger(&station));

    destroyStation(&station);
}

static void testRedirectToAnotherFreeCharger(void) {
    struct Station station;
    if (!initTestStation(&station)) {
        CHECK(false);
        return;
    }
    request(&station, 0, 50, PRIORITY_NORMAL); // Borne de 50 kW
    request(&station, 1, 50, PRIORITY_NORMAL); // Borne de 150 kW

    // Panne de la borne de 50 kW : le véhicule 0 part sur la seule borne libre (22 kW)
    struct ChargingSession interrupted;
    CHECK(takeChargerOutOfService(&station, 1, OUT_OF_SERVICE, &interrupted));
    CHECK(interrupted.vehicleId == 0);

    struct Assignment redirection = redirectInterruptedVehicle(&station, &interrupted);
    CHECK(redirection.result == ASSIGN_OK);
    CHECK(redirection.chargerIndex == 0);
    CHECK(sessionVehicle(&station, 0) == 0);

    destroyStation(&station);
}

static void testRedirectAheadOfSamePriority(void) {
    struct Station station;
    if (!initTestStation(&station)) {
        CHECK(false);
        return;
    }
    fillStation(&station);
    request(&station, 10, 50, PRIORITY_NORMAL);
    request(&station, 11, 50, PRIORITY_HIGH);

    // Véhicule normal interrompu : derrière le prioritaire, devant les autres véhicules normaux
    struct ChargingSession interrupted;
    CHECK(takeChargerOutOfService(&station, 1, OUT_OF_SERVICE, &interrupted));
    struct Assignment redirection = redirectInterruptedVehicle(&station, &interrupted);
    CHECK(redirection.result == ASSIGN_QUEUED);
    CHECK(redirection.queuePosition == 2);
    CHECK(waitQueuePosition(&station.waitQueue, 11) == 1);
    CHECK(waitQueuePosition(&station.waitQueue, 10) == 3);

    // File pleine : redirection impossible sur cette station
    for (int id = 20; waitQueueLength(&station.waitQueue) < MAX_WAITING_VEHICLES; id++) {
        request(&station, id, 50, PRIORITY_NORMAL);
    }
    CHECK(takeChargerOutOfService(&station, 2, OUT_OF_SERVICE, &interrupted));
    CHECK(redirectInterruptedVehicle(&station, &interrupted).result == ASSIGN_REFUSED);

    destroyStation(&station);
}

static void testCancelWaitingVehicle(void) {
    struct Station station;
    if (!initTestStation(&station)) {
        CHECK(false);
        return;
    }
    fillStation(&station);
    request(&station, 10, 50, PRIORITY_NORMAL);

    CHECK(cancelWaitingVehicle(&station, 10));
    CHECK(!cancelWaitingVehicle(&station, 10));

    struct ChargingSession finished;
    struct WaitingVehicle next;
    CHECK(releaseChargerAndServeQueue(&station, 0, &finished, &next) == HANDOVER_FREED);

    destroyStation(&station);
}

static void testLongWaitingVehicleServedBeforeLaterPrioritary(void) {
    struct Station station;
    if (!initTestStation(&station)) {
        CHECK(false);
        return;
    }
    fillStation(&station);
    request(&station, 10, 50, PRIORITY_NORMAL);
    station.waitQueue.vehicles[0].arrivalTime.tv_sec -= 100; // Attente simulée de 100 s
    request(&station, 11, 50, PRIORITY_HIGH);

    struct ChargingSession finished;
    struct WaitingVehicle next;
    CHECK(releaseChargerAndServeQueue(&station, 1, &finished, &next) == HANDOVER_REASSIGNED);
    CHECK(next.vehicleId == 10);
    CHECK(waitQueuePosition(&station.waitQueue, 11) == 1);

    destroyStation(&station);
}

static void testExpireWaitingVehicles(void) {
    struct Station station;
    if (!initTestStation(&station)) {
        CHECK(false);
        return;
    }
    fillStation(&station);
    request(&station, 10, 50, PRIORITY_NORMAL);
    request(&station, 11, 50, PRIORITY_NORMAL);
    station.waitQueue.vehicles[0].arrivalTime.tv_sec -= 400;

    struct WaitingVehicle expired[MAX_WAITING_VEHICLES];
    CHECK(expireWaitingVehicles(&station, MAX_WAIT_SECONDS, expired, MAX_WAITING_VEHICLES) == 1);
    CHECK(expired[0].vehicleId == 10);
    CHECK(waitQueuePosition(&station.waitQueue, 11) == 1);

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
            CHECK(sessionVehicle(&station, requests[i].assignment.chargerIndex) == requests[i].vehicleId);
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
    RUN_TEST(testSessionRecordsTheRequest);
    RUN_TEST(testQueuesThenRefusesWhenFull);
    RUN_TEST(testReleasedChargerGoesToNextInQueue);
    RUN_TEST(testFaultAndRepair);
    RUN_TEST(testRedirectToAnotherFreeCharger);
    RUN_TEST(testRedirectAheadOfSamePriority);
    RUN_TEST(testCancelWaitingVehicle);
    RUN_TEST(testLongWaitingVehicleServedBeforeLaterPrioritary);
    RUN_TEST(testExpireWaitingVehicles);
    RUN_TEST(testConcurrentRequestsNeverShareACharger);
    return TEST_RESULT();
}
