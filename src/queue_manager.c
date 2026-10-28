#include "queue_manager.h"
#include <stdio.h>
#include <string.h>

static void removeAt(struct WaitQueue* queue, int index) {
    memmove(&queue->vehicles[index], &queue->vehicles[index + 1],
            (size_t)(queue->count - index - 1) * sizeof(struct WaitingVehicle));
    queue->count--;
}

void initWaitQueue(struct WaitQueue* queue) {
    queue->count = 0;
}

bool enqueueVehicle(struct WaitQueue* queue, const struct WaitingVehicle* vehicle) {
    if (queue->count >= MAX_WAITING_VEHICLES || waitQueuePosition(queue, vehicle->vehicleId) != -1) {
        return false;
    }

    // Insertion devant le premier véhicule moins prioritaire : l'ordre d'arrivée est conservé à priorité égale
    int insertAt = 0;
    while (insertAt < queue->count && queue->vehicles[insertAt].priority >= vehicle->priority) {
        insertAt++;
    }

    memmove(&queue->vehicles[insertAt + 1], &queue->vehicles[insertAt],
            (size_t)(queue->count - insertAt) * sizeof(struct WaitingVehicle));
    queue->vehicles[insertAt] = *vehicle;
    clock_gettime(CLOCK_MONOTONIC, &queue->vehicles[insertAt].arrivalTime);
    queue->count++;
    return true;
}

bool dequeueNextVehicle(struct WaitQueue* queue, struct WaitingVehicle* next) {
    if (queue->count == 0) {
        return false;
    }
    *next = queue->vehicles[0];
    removeAt(queue, 0);
    return true;
}

bool removeWaitingVehicle(struct WaitQueue* queue, int vehicleId) {
    int position = waitQueuePosition(queue, vehicleId);
    if (position == -1) {
        return false;
    }
    removeAt(queue, position - 1);
    return true;
}

int waitQueueLength(const struct WaitQueue* queue) {
    return queue->count;
}

int waitQueuePosition(const struct WaitQueue* queue, int vehicleId) {
    for (int i = 0; i < queue->count; i++) {
        if (queue->vehicles[i].vehicleId == vehicleId) {
            return i + 1;
        }
    }
    return -1;
}

double waitingTimeSeconds(const struct WaitingVehicle* vehicle) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - vehicle->arrivalTime.tv_sec)
         + (double)(now.tv_nsec - vehicle->arrivalTime.tv_nsec) / 1e9;
}

void printWaitQueue(const struct WaitQueue* queue) {
    if (queue->count == 0) {
        printf("  File d'attente vide\n");
        return;
    }

    printf("  File d'attente (%d véhicule(s)) :\n", queue->count);
    for (int i = 0; i < queue->count; i++) {
        const struct WaitingVehicle* vehicle = &queue->vehicles[i];
        printf("    %d. véhicule %d, %d kW%s, attend depuis %.1f s\n", i + 1, vehicle->vehicleId,
               vehicle->desiredPower, vehicle->priority == PRIORITY_HIGH ? ", prioritaire" : "",
               waitingTimeSeconds(vehicle));
    }
}
