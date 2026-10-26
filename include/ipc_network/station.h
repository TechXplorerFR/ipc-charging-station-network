#include "charger.h"

#ifndef STATION_H
#define STATION_H

struct Station
{
    int id;
    struct Charger* chargers;
    int numChargers;
};

#endif // STATION_H