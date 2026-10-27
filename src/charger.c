#include "charger.h"
#include <stdio.h>

void setChargerStatus(struct Charger* charger, AvailabiltyStatus newStatus) {
    charger->status = newStatus;
    // Les changements d'états (ex: EN PANNE, LIBRE) sont gérés ici[cite: 9]
}