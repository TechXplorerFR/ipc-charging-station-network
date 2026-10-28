#ifndef QUEUE_MANAGER_H
#define QUEUE_MANAGER_H

#include <stdbool.h>
#include <time.h>

// Capacité de la file d'attente d'une station : au-delà, les demandes sont refusées
#define MAX_WAITING_VEHICLES 64

typedef enum {
    PRIORITY_NORMAL,
    PRIORITY_HIGH // Véhicule d'urgence, transport public
} VehiclePriority;

struct WaitingVehicle {
    int vehicleId;
    int desiredPower; // kW
    VehiclePriority priority;
    struct timespec arrivalTime; // Renseignée par enqueueVehicle()
};

// Règle de service : priorité la plus haute d'abord, puis premier arrivé, premier servi
struct WaitQueue {
    struct WaitingVehicle vehicles[MAX_WAITING_VEHICLES]; // Dans l'ordre de service : le prochain servi est en tête
    int count;
};

// Aucune de ces fonctions n'est protégée : les appeler verrou de la station acquis (lockStation)

void initWaitQueue(struct WaitQueue* queue);

// Place le véhicule derrière ceux de priorité supérieure ou égale ;
// renvoie false si la file est pleine ou si le véhicule y est déjà
bool enqueueVehicle(struct WaitQueue* queue, const struct WaitingVehicle* vehicle);

// Retire le prochain véhicule à servir et le copie dans *next ; renvoie false si la file est vide
bool dequeueNextVehicle(struct WaitQueue* queue, struct WaitingVehicle* next);

// Retire un véhicule qui abandonne l'attente ; renvoie false s'il n'était pas dans la file
bool removeWaitingVehicle(struct WaitQueue* queue, int vehicleId);

int waitQueueLength(const struct WaitQueue* queue);

// Position du véhicule (1 = prochain servi), ou -1 s'il n'est pas dans la file
int waitQueuePosition(const struct WaitQueue* queue, int vehicleId);

// Temps écoulé depuis l'entrée du véhicule dans la file
double waitingTimeSeconds(const struct WaitingVehicle* vehicle);

void printWaitQueue(const struct WaitQueue* queue);

#endif // QUEUE_MANAGER_H
