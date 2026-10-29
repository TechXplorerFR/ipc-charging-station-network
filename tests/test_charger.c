#include "charger.h"
#include "test_utils.h"
#include <string.h>

static void testStatusNames(void) {
    CHECK(strcmp(chargerStatusName(AVAILABLE), "libre") == 0);
    CHECK(strcmp(chargerStatusName(OCCUPIED), "occupée") == 0);
    CHECK(strcmp(chargerStatusName(OUT_OF_SERVICE), "en panne") == 0);
    CHECK(strcmp(chargerStatusName(MAINTENANCE), "en maintenance") == 0);
    CHECK(strcmp(chargerStatusName((ChargerStatus)99), "inconnu") == 0);
}

static void testOutOfOrderStatuses(void) {
    CHECK(!isChargerOutOfOrder(AVAILABLE));
    CHECK(!isChargerOutOfOrder(OCCUPIED));
    CHECK(isChargerOutOfOrder(OUT_OF_SERVICE));
    CHECK(isChargerOutOfOrder(MAINTENANCE));
}

static void testEffectivePowerIsCappedByCharger(void) {
    CHECK(effectiveChargingPower(50, 30) == 30);
    CHECK(effectiveChargingPower(50, 150) == 50);
    CHECK(effectiveChargingPower(22, 22) == 22);
}

static void testStartAndClearSession(void) {
    struct Charger charger = { .id = 0, .maxPower = 50, .status = AVAILABLE };
    startChargingSession(&charger, 7, 120, true);

    CHECK(charger.session.vehicleId == 7);
    CHECK(charger.session.desiredPower == 120);
    CHECK(charger.session.power == 50);
    CHECK(charger.session.isPrioritary);
    CHECK(!charger.session.ended);
    double duration = sessionDurationSeconds(&charger.session);
    CHECK(duration >= 0.0 && duration < 1.0);

    clearChargingSession(&charger);
    CHECK(charger.session.vehicleId == NO_VEHICLE);
    CHECK(charger.session.power == 0);
}

static void testEndedSessionStopsCounting(void) {
    struct Charger charger = { .maxPower = 150 };
    startChargingSession(&charger, 1, 50, false);
    charger.session.start.tv_sec -= 60; // Session commencée il y a 60 s

    endChargingSession(&charger.session);
    struct timespec end = charger.session.end;
    endChargingSession(&charger.session); // Une seconde fin ne déplace pas la première
    CHECK(charger.session.end.tv_sec == end.tv_sec && charger.session.end.tv_nsec == end.tv_nsec);

    double duration = sessionDurationSeconds(&charger.session);
    CHECK(duration >= 60.0 && duration < 61.0);
}

static void testEnergyUsesSimulatedTime(void) {
    // 60 s réelles = 1 h simulée : 50 kW pendant 1 h = 50 kWh
    struct ChargingSession session = { .vehicleId = 1, .power = 50, .ended = true };
    session.end.tv_sec = 1000;
    session.start.tv_sec = 1000 - (time_t)(3600.0 / SIMULATED_SECONDS_PER_SECOND);

    double energy = sessionEnergyKWh(&session);
    CHECK(energy > 49.99 && energy < 50.01);
}

int main(void) {
    RUN_TEST(testStatusNames);
    RUN_TEST(testOutOfOrderStatuses);
    RUN_TEST(testEffectivePowerIsCappedByCharger);
    RUN_TEST(testStartAndClearSession);
    RUN_TEST(testEndedSessionStopsCounting);
    RUN_TEST(testEnergyUsesSimulatedTime);
    return TEST_RESULT();
}
