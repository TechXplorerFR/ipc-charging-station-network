#include "queue_manager.h"
#include <stdio.h>
#include <string.h>

static bool arrivedBefore(const struct WaitingVehicle* a, const struct WaitingVehicle* b) {
    return a->arrivalTime.tv_sec < b->arrivalTime.tv_sec
        || (a->arrivalTime.tv_sec == b->arrivalTime.tv_sec && a->arrivalTime.tv_nsec < b->arrivalTime.tv_nsec);
}

// La place doit être garantie par l'appelant
static void insertAt(struct WaitQueue* queue, int index, const struct WaitingVehicle* vehicle) {
    memmove(&queue->vehicles[index + 1], &queue->vehicles[index],
            (size_t)(queue->count - index) * sizeof(struct WaitingVehicle));
    queue->vehicles[index] = *vehicle;
    queue->count++;
}

static void removeAt(struct WaitQueue* queue, int index) {
    memmove(&queue->vehicles[index], &queue->vehicles[index + 1],
            (size_t)(queue->count - index - 1) * sizeof(struct WaitingVehicle));
    queue->count--;
}

// Ajoute un véhicule qui arrive dans la file, derrière ceux de priorité supérieure ;
// aheadOfSamePriority le place devant ceux de même priorité au lieu de derrière
static bool enqueueArriving(struct WaitQueue* queue, const struct WaitingVehicle* vehicle, bool aheadOfSamePriority) {
    if (waitQueuePosition(queue, vehicle->vehicleId) != -1) {
        return false;
    }
    if (queue->count >= MAX_WAITING_VEHICLES) {
        queue->stats.rejected++;
        return false;
    }

    int index = 0;
    while (index < queue->count
           && (queue->vehicles[index].priority > vehicle->priority
               || (!aheadOfSamePriority && queue->vehicles[index].priority == vehicle->priority))) {
        index++;
    }

    struct WaitingVehicle arriving = *vehicle;
    clock_gettime(CLOCK_MONOTONIC, &arriving.arrivalTime);
    insertAt(queue, index, &arriving);
    return true;
}

void initWaitQueue(struct WaitQueue* queue) {
    queue->count = 0;
    queue->stats = (struct WaitQueueStats){ 0 };
}

bool enqueueVehicle(struct WaitQueue* queue, const struct WaitingVehicle* vehicle) {
    return enqueueArriving(queue, vehicle, false);
}

bool enqueueVehicleFirst(struct WaitQueue* queue, const struct WaitingVehicle* vehicle) {
    return enqueueArriving(queue, vehicle, true);
}

bool dequeueNextVehicle(struct WaitQueue* queue, struct WaitingVehicle* next) {
    if (queue->count == 0) {
        return false;
    }
    *next = queue->vehicles[0];
    removeAt(queue, 0);

    queue->stats.served++;
    queue->stats.totalServedWaitSeconds += waitingTimeSeconds(next);
    return true;
}

bool removeWaitingVehicle(struct WaitQueue* queue, int vehicleId) {
    int position = waitQueuePosition(queue, vehicleId);
    if (position == -1) {
        return false;
    }
    removeAt(queue, position - 1);
    queue->stats.cancelled++;
    return true;
}

int promoteLongWaitingVehicles(struct WaitQueue* queue, double agingSeconds) {
    int promoted = 0;
    for (int i = 0; i < queue->count; i++) {
        struct WaitingVehicle vehicle = queue->vehicles[i];
        if (vehicle.priority != PRIORITY_NORMAL || waitingTimeSeconds(&vehicle) < agingSeconds) {
            continue;
        }

        // Le véhicule remonte devant les prioritaires arrivés après lui : sa nouvelle place est avant i,
        // donc le suivant à examiner reste en i + 1
        removeAt(queue, i);
        vehicle.priority = PRIORITY_HIGH;
        int index = 0;
        while (index < queue->count && queue->vehicles[index].priority == PRIORITY_HIGH
               && arrivedBefore(&queue->vehicles[index], &vehicle)) {
            index++;
        }
        insertAt(queue, index, &vehicle);
        promoted++;
    }
    return promoted;
}

int removeExpiredVehicles(struct WaitQueue* queue, double maxWaitSeconds, struct WaitingVehicle* expired,
                          int capacity) {
    int removed = 0;
    int i = 0;
    while (i < queue->count && removed < capacity) {
        if (waitingTimeSeconds(&queue->vehicles[i]) >= maxWaitSeconds) {
            expired[removed++] = queue->vehicles[i];
            removeAt(queue, i);
            queue->stats.expired++;
        } else {
            i++;
        }
    }
    return removed;
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

double averageWaitSeconds(const struct WaitQueue* queue) {
    if (queue->stats.served == 0) {
        return 0.0;
    }
    return queue->stats.totalServedWaitSeconds / queue->stats.served;
}

void printWaitQueue(const struct WaitQueue* queue) {
    if (queue->count == 0) {
        printf("  File d'attente vide\n");
    } else {
        printf("  File d'attente (%d véhicule(s)) :\n", queue->count);
        for (int i = 0; i < queue->count; i++) {
            const struct WaitingVehicle* vehicle = &queue->vehicles[i];
            printf("    %d. véhicule %d, %d kW%s, attend depuis %.1f s\n", i + 1, vehicle->vehicleId,
                   vehicle->desiredPower, vehicle->priority == PRIORITY_HIGH ? ", prioritaire" : "",
                   waitingTimeSeconds(vehicle));
        }
    }

    const struct WaitQueueStats* stats = &queue->stats;
    printf("  Servis : %d (attente moyenne %.1f s), abandons : %d, attentes expirées : %d, refus : %d\n",
           stats->served, averageWaitSeconds(queue), stats->cancelled, stats->expired, stats->rejected);
}
