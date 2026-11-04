#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stdbool.h>
#include "station.h"

// Réserve atomiquement une borne libre de la station ; renvoie son indice, ou -1 si aucune n'est libre
int assignChargerAtomically(struct Station* station, int vehicleId);

// Repasse une borne occupée à l'état libre ; renvoie false si elle n'était pas occupée
bool releaseCharger(struct Station* station, int chargerIndex);

#endif // ALLOCATOR_H
