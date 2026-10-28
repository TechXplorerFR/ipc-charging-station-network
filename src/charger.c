#include "charger.h"

void setChargerStatus(struct Charger* charger, ChargerStatus newStatus) {
    charger->status = newStatus;
}
