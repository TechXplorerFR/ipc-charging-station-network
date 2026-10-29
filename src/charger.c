#include "charger.h"

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
