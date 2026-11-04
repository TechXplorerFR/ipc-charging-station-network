#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include <stdbool.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

// Type (mtype) des messages destinés au serveur central
#define SERVER_MSG_TYPE 1

// Types de requêtes traitées par le serveur
typedef enum {
    REQ_CHARGE_DEMAND,
    REQ_CHARGE_END,
    REQ_ALERT,
    REQ_SHUTDOWN,
    REQ_CANCEL, // Le véhicule quitte la file d'attente
    REQ_PONG,   // Réponse à NOTIF_PING, avec le niveau de batterie
    REQ_REPAIR, // Fin de réparation d'une borne (station_id, charger_id)

    // Serveur -> véhicule
    RESP_ASSIGNED,  // Borne attribuée (station_id, charger_id, desired_power = puissance délivrée)
    RESP_QUEUED,    // Placé en file d'attente (queue_position)
    RESP_REFUSED,   // Aucune borne libre et file d'attente pleine
    NOTIF_PING,     // Demande de l'état de la charge
    NOTIF_FAULT,    // La borne utilisée par le véhicule est tombée en panne
    NOTIF_SHUTDOWN  // Fin de la simulation : le thread du véhicule doit se terminer
} RequestType;

typedef struct {
    long mtype;
    RequestType request;
    int vehicle_id;
    int station_id;
    int charger_id;         // Borne concernée (attribution, fin de charge, alerte), -1 sinon
    int desired_power;      // kW demandés (REQ_CHARGE_DEMAND) ou délivrés par la borne attribuée (RESP_ASSIGNED)
    int estimated_duration; // Durée de charge estimée (s)
    int battery_level;      // Niveau de batterie (%), renseigné dans REQ_PONG
    int queue_position;     // Position dans la file d'attente (1 = prochain servi), renseignée dans RESP_QUEUED
    bool is_prioritary;
} Message;

typedef int msgq_t;

// Renvoie l'identifiant de la file, ou -1 en cas d'erreur
msgq_t initMessageQueue(key_t key);
void destroyMessageQueue(msgq_t queue);
bool sendMessage(msgq_t queue, const Message* msg);
bool receiveMessage(msgq_t queue, Message* msg, long mtype);

#endif // MESSAGE_QUEUE_H
