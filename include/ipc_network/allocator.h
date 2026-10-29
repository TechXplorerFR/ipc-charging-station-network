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

// Réserve la borne libre la mieux adaptée à la puissance demandée et y ouvre une session de recharge,
// ou place le véhicule dans la file d'attente de la station
struct Assignment requestCharger(struct Station* station, const struct WaitingVehicle* vehicle);

// Fin de charge sur une borne occupée : la session terminée est copiée dans *finished (pour la facturation),
// puis la borne passe au prochain véhicule en attente (copié dans *next), sinon elle redevient libre.
// Avant de choisir, les véhicules qui attendent depuis PRIORITY_AGING_SECONDS deviennent prioritaires
// (de même pour repairChargerAndServeQueue).
HandoverResult releaseChargerAndServeQueue(struct Station* station, int chargerIndex,
                                           struct ChargingSession* finished, struct WaitingVehicle* next);

// Fin de réparation d'une borne OUT_OF_SERVICE ou MAINTENANCE : elle passe au prochain véhicule
// en attente (copié dans *next), sinon elle redevient libre
HandoverResult repairChargerAndServeQueue(struct Station* station, int chargerIndex, struct WaitingVehicle* next);

// Passe une borne à l'état OUT_OF_SERVICE ou MAINTENANCE. Renvoie true si une recharge était en cours :
// sa session interrompue est copiée dans *interrupted (à facturer, et véhicule à rediriger).
// Renvoie false sans rien changer si la borne ou l'état demandé est invalide.
bool takeChargerOutOfService(struct Station* station, int chargerIndex, ChargerStatus status,
                             struct ChargingSession* interrupted);

// Redirige le véhicule d'une session interrompue vers la borne libre la mieux adaptée de la station,
// sinon le place dans la file d'attente devant les véhicules de même priorité.
// La station peut être celle de la panne ou une autre.
struct Assignment redirectInterruptedVehicle(struct Station* station, const struct ChargingSession* interrupted);

// Retire un véhicule de la file d'attente ; renvoie false s'il n'y était pas
bool cancelWaitingVehicle(struct Station* station, int vehicleId);

// À appeler périodiquement (par exemple avec MAX_WAIT_SECONDS) : retire au plus capacity véhicules
// qui attendent depuis trop longtemps et les copie dans expired (à prévenir par RESP_REFUSED) ;
// renvoie leur nombre
int expireWaitingVehicles(struct Station* station, double maxWaitSeconds, struct WaitingVehicle* expired,
                          int capacity);

#endif // ALLOCATOR_H
