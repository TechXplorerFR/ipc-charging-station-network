#ifndef QUEUE_MANAGER_H
#define QUEUE_MANAGER_H

#include <stdbool.h>
#include <time.h>

// Capacité de la file d'attente d'une station : au-delà, les demandes sont refusées
#define MAX_WAITING_VEHICLES 64

// Attente au-delà de laquelle un véhicule normal devient prioritaire, pour qu'un flot continu
// de véhicules prioritaires ne le fasse pas attendre indéfiniment
#define PRIORITY_AGING_SECONDS 60.0

// Attente maximale conseillée avant d'abandonner une demande (voir removeExpiredVehicles)
#define MAX_WAIT_SECONDS 300.0

typedef enum {
    PRIORITY_NORMAL,
    PRIORITY_HIGH // Véhicule d'urgence, transport public, ou véhicule normal promu après une longue attente
} VehiclePriority;

struct WaitingVehicle {
    int vehicleId;
    int desiredPower; // kW
    VehiclePriority priority;
    struct timespec arrivalTime; // Renseignée à l'entrée dans la file (horloge CLOCK_MONOTONIC)
};

// Compteurs cumulés depuis l'initialisation de la file, pour le rapport de fin de simulation
struct WaitQueueStats {
    int served;                    // Véhicules sortis de la file pour une borne
    double totalServedWaitSeconds; // Somme de leurs temps d'attente
    int cancelled;                 // Véhicules qui ont abandonné l'attente
    int expired;                   // Véhicules retirés après une attente trop longue
    int rejected;                  // Véhicules refusés car la file était pleine
};

// Règle de service : priorité la plus haute d'abord, puis premier arrivé, premier servi.
// Un véhicule interrompu par une panne passe devant ceux de même priorité (enqueueVehicleFirst).
struct WaitQueue {
    struct WaitingVehicle vehicles[MAX_WAITING_VEHICLES]; // Dans l'ordre de service : le prochain servi est en tête
    int count;
    struct WaitQueueStats stats;
};

// Aucune de ces fonctions n'est protégée : les appeler verrou de la station acquis (lockStation)

void initWaitQueue(struct WaitQueue* queue);

// Place le véhicule derrière ceux de priorité supérieure ou égale ;
// renvoie false si la file est pleine ou si le véhicule y est déjà
bool enqueueVehicle(struct WaitQueue* queue, const struct WaitingVehicle* vehicle);

// Comme enqueueVehicle, mais devant les véhicules de même priorité (toujours derrière ceux de priorité
// supérieure) : pour un véhicule dont la recharge a été interrompue par une panne
bool enqueueVehicleFirst(struct WaitQueue* queue, const struct WaitingVehicle* vehicle);

// Retire le prochain véhicule à servir et le copie dans *next ; renvoie false si la file est vide
bool dequeueNextVehicle(struct WaitQueue* queue, struct WaitingVehicle* next);

// Retire un véhicule qui abandonne l'attente ; renvoie false s'il n'était pas dans la file
bool removeWaitingVehicle(struct WaitQueue* queue, int vehicleId);

// Rend prioritaires les véhicules normaux qui attendent depuis au moins agingSeconds ; chacun se place
// parmi les prioritaires selon son heure d'arrivée. Renvoie le nombre de véhicules promus.
int promoteLongWaitingVehicles(struct WaitQueue* queue, double agingSeconds);

// Retire au plus capacity véhicules qui attendent depuis au moins maxWaitSeconds et les copie dans
// expired (à prévenir par un refus) ; renvoie leur nombre
int removeExpiredVehicles(struct WaitQueue* queue, double maxWaitSeconds, struct WaitingVehicle* expired,
                          int capacity);

int waitQueueLength(const struct WaitQueue* queue);

// Position du véhicule (1 = prochain servi), ou -1 s'il n'est pas dans la file
int waitQueuePosition(const struct WaitQueue* queue, int vehicleId);

// Temps écoulé depuis l'entrée du véhicule dans la file
double waitingTimeSeconds(const struct WaitingVehicle* vehicle);

// Temps d'attente moyen des véhicules servis, 0 si aucun ne l'a été
double averageWaitSeconds(const struct WaitQueue* queue);

void printWaitQueue(const struct WaitQueue* queue);

#endif // QUEUE_MANAGER_H
