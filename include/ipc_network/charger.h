#include <stdbool.h>

#ifndef CHARGER_H
#define CHARGER_H

typedef enum
{
    AVAILABLE,
    OCCUPIED,
    OUT_OF_SERVICE,
    MAINTENANCE
} AvailabiltyStatus;

struct Charger
{
    int id;
    int maxPower;
    AvailabiltyStatus status;
};



#endif // CHARGER_H