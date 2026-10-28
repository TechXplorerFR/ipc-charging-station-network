#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stdbool.h>
#include "queue_manager.h"
#include "station.h"

// Toutes les fonctions prennent elles-mêmes le verrou de la station :
// la vérification, l'attribution et la mise à jour de l'état forment une seule section critique

typedef enum {
    ASSIGN_OK,     // Borne réservée
    ASSIGN_QUEUED, // Aucune borne libre : véhicule placé en file d'attente
    ASSIGN_REFUSED // Aucune borne libre et file d'attente pleine (ou véhicule déjà en attente)
} AssignResult;

struct Assignment {
    AssignResult result;
    int chargerIndex;  // Si ASSIGN_OK, -1 sinon
    int queuePosition; // Si ASSIGN_QUEUED (1 = prochain servi), -1 sinon
};

typedef enum {
    HANDOVER_INVALID,   // Borne inconnue ou dans un état inattendu : rien n'a changé
    HANDOVER_FREED,     // File d'attente vide : la borne est libre
    HANDOVER_REASSIGNED // Borne attribuée directement au prochain véhicule en attente
} HandoverResult;

// Réserve la borne libre la mieux adaptée à la puissance demandée,
// ou place le véhicule dans la file d'attente de la station
struct Assignment requestCharger(struct Station* station, const struct WaitingVehicle* vehicle);

// Fin de charge sur une borne occupée : elle passe au prochain véhicule en attente (copié dans *next),
// sinon elle redevient libre
HandoverResult releaseChargerAndServeQueue(struct Station* station, int chargerIndex, struct WaitingVehicle* next);

// Fin de réparation d'une borne OUT_OF_SERVICE ou MAINTENANCE : même principe
HandoverResult repairChargerAndServeQueue(struct Station* station, int chargerIndex, struct WaitingVehicle* next);

// Passe une borne à l'état OUT_OF_SERVICE ou MAINTENANCE ; renvoie l'identifiant du véhicule
// dont la charge est interrompue (à prévenir et rediriger), ou NO_VEHICLE
int takeChargerOutOfService(struct Station* station, int chargerIndex, ChargerStatus status);

// Retire un véhicule de la file d'attente ; renvoie false s'il n'y était pas
bool cancelWaitingVehicle(struct Station* station, int vehicleId);

#endif // ALLOCATOR_H
