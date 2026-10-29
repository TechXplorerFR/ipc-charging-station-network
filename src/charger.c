#include "charger.h"

#define SECONDS_PER_HOUR 3600.0

void setChargerStatus(struct Charger* charger, ChargerStatus newStatus) {
    charger->status = newStatus;
}

const char* chargerStatusName(ChargerStatus status) {
    switch (status) {
        case AVAILABLE:
            return "libre";
        case OCCUPIED:
            return "occupée";
        case OUT_OF_SERVICE:
            return "en panne";
        case MAINTENANCE:
            return "en maintenance";
    }
    return "inconnu";
}

bool isChargerOutOfOrder(ChargerStatus status) {
    return status == OUT_OF_SERVICE || status == MAINTENANCE;
}

int effectiveChargingPower(int chargerPower, int desiredPower) {
    return desiredPower < chargerPower ? desiredPower : chargerPower;
}

void startChargingSession(struct Charger* charger, int vehicleId, int desiredPower, bool isPrioritary) {
    charger->session = (struct ChargingSession){
        .vehicleId = vehicleId,
        .desiredPower = desiredPower,
        .power = effectiveChargingPower(charger->maxPower, desiredPower),
        .isPrioritary = isPrioritary,
    };
    clock_gettime(CLOCK_MONOTONIC, &charger->session.start);
}

void clearChargingSession(struct Charger* charger) {
    charger->session = (struct ChargingSession){ .vehicleId = NO_VEHICLE };
}

void endChargingSession(struct ChargingSession* session) {
    if (session->ended) {
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &session->end);
    session->ended = true;
}

double sessionDurationSeconds(const struct ChargingSession* session) {
    struct timespec end = session->end;
    if (!session->ended) {
        clock_gettime(CLOCK_MONOTONIC, &end);
    }
    return (double)(end.tv_sec - session->start.tv_sec) + (double)(end.tv_nsec - session->start.tv_nsec) / 1e9;
}

double sessionEnergyKWh(const struct ChargingSession* session) {
    double simulatedHours = sessionDurationSeconds(session) * SIMULATED_SECONDS_PER_SECOND / SECONDS_PER_HOUR;
    return session->power * simulatedHours;
}
