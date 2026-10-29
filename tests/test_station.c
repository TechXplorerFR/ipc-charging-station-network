#include "station.h"
#include "test_utils.h"

static void setStatus(struct Station* station, int chargerIndex, ChargerStatus status) {
    lockStation(station);
    setStationChargerStatus(station, chargerIndex, status);
    unlockStation(station);
}

static void testDefaultPowersAreVaried(void) {
    struct Station station;
    if (!initStation(&station, 2, 4)) {
        CHECK(false);
        return;
    }

    CHECK(station.id == 2);
    CHECK(station.numChargers == 4);
    CHECK(station.chargers[0].maxPower == 22);
    CHECK(station.chargers[1].maxPower == 50);
    CHECK(station.chargers[2].maxPower == 150);
    CHECK(station.chargers[3].maxPower == 22);
    for (int i = 0; i < 4; i++) {
        CHECK(station.chargers[i].id == i);
        CHECK(station.chargers[i].status == AVAILABLE);
        CHECK(station.chargers[i].session.vehicleId == NO_VEHICLE);
    }
    CHECK(countChargers(&station, AVAILABLE) == 4);
    CHECK(waitQueueLength(&station.waitQueue) == 0);

    destroyStation(&station);
}

static void testCustomPowers(void) {
    struct Station station;
    const int powers[] = { 7, 300 };
    if (!initStationWithPowers(&station, 0, powers, 2)) {
        CHECK(false);
        return;
    }
    CHECK(station.chargers[0].maxPower == 7);
    CHECK(station.chargers[1].maxPower == 300);
    destroyStation(&station);
}

static void testRejectsInvalidConfiguration(void) {
    struct Station station;
    CHECK(!initStation(&station, 0, 0));
    CHECK(!initStation(&station, 0, MAX_CHARGERS_PER_STATION + 1));

    const int powers[] = { 50, 0 };
    CHECK(!initStationWithPowers(&station, 0, powers, 2));
    CHECK(station.chargers == NULL);

    // Une station dont l'initialisation a échoué se détruit sans erreur
    destroyStation(&station);
}

static void testStatusChangesKeepAvailableCounter(void) {
    struct Station station;
    if (!initStation(&station, 0, 2)) {
        CHECK(false);
        return;
    }

    setStatus(&station, 0, OCCUPIED);
    CHECK(hasAvailableCharger(&station));
    setStatus(&station, 1, OUT_OF_SERVICE);
    CHECK(!hasAvailableCharger(&station));
    CHECK(countChargers(&station, OCCUPIED) == 1);
    CHECK(countChargers(&station, OUT_OF_SERVICE) == 1);
    CHECK(!isChargerAvailable(&station, 0));

    // Passer d'un état indisponible à un autre ne touche pas au compteur
    setStatus(&station, 1, MAINTENANCE);
    CHECK(!hasAvailableCharger(&station));

    setStatus(&station, 1, AVAILABLE);
    CHECK(hasAvailableCharger(&station));
    CHECK(isChargerAvailable(&station, 1));
    CHECK(!isChargerAvailable(&station, 2));

    destroyStation(&station);
}

static void testOccupancyRate(void) {
    struct Station station;
    if (!initStation(&station, 0, 2)) {
        CHECK(false);
        return;
    }
    CHECK(stationOccupancyRate(&station) < 0.01);

    // Station ouverte depuis 10 s, borne 0 occupée tout ce temps : 1 borne sur 2 occupée
    setStatus(&station, 0, OCCUPIED);
    station.openedAt.tv_sec -= 10;
    station.chargers[0].occupiedSince.tv_sec -= 10;
    double rate = stationOccupancyRate(&station);
    CHECK(rate > 0.49 && rate < 0.51);

    // Une fois libérée, le temps d'occupation est conservé
    setStatus(&station, 0, AVAILABLE);
    CHECK(station.chargers[0].occupiedSeconds >= 10.0 && station.chargers[0].occupiedSeconds < 11.0);

    // Une borne qui reste occupée (passage direct au véhicule suivant) n'est pas remise à zéro
    setStatus(&station, 1, OCCUPIED);
    struct timespec occupiedSince = station.chargers[1].occupiedSince;
    setStatus(&station, 1, OCCUPIED);
    CHECK(station.chargers[1].occupiedSince.tv_sec == occupiedSince.tv_sec);
    CHECK(station.chargers[1].occupiedSince.tv_nsec == occupiedSince.tv_nsec);

    destroyStation(&station);
}

static void testDestroyTwice(void) {
    struct Station station;
    if (!initStation(&station, 0, 1)) {
        CHECK(false);
        return;
    }
    destroyStation(&station);
    destroyStation(&station);
    CHECK(station.chargers == NULL);
    CHECK(station.semId == -1);
}

static void testPrintStation(void) {
    struct Station station;
    if (!initStation(&station, 1, 3)) {
        CHECK(false);
        return;
    }
    lockStation(&station);
    setStationChargerStatus(&station, 1, OCCUPIED);
    station.chargers[1].session.vehicleId = 42;
    clock_gettime(CLOCK_MONOTONIC, &station.chargers[1].session.start);
    setStationChargerStatus(&station, 2, OUT_OF_SERVICE);
    unlockStation(&station);

    printStation(&station);
    CHECK(countChargers(&station, AVAILABLE) == 1); // Le verrou a bien été rendu

    destroyStation(&station);
}

int main(void) {
    RUN_TEST(testDefaultPowersAreVaried);
    RUN_TEST(testCustomPowers);
    RUN_TEST(testRejectsInvalidConfiguration);
    RUN_TEST(testStatusChangesKeepAvailableCounter);
    RUN_TEST(testOccupancyRate);
    RUN_TEST(testDestroyTwice);
    RUN_TEST(testPrintStation);
    return TEST_RESULT();
}
