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
    REQ_SHUTDOWN
} RequestType;

typedef struct {
    long mtype;
    RequestType request;
    int vehicle_id;
    int station_id;
    int charger_id; // Borne concernée (fin de charge, alerte), -1 sinon
    int desired_power;
    bool is_prioritary;
} Message;

typedef int msgq_t;

// Renvoie l'identifiant de la file, ou -1 en cas d'erreur
msgq_t initMessageQueue(key_t key);
void destroyMessageQueue(msgq_t queue);
bool sendMessage(msgq_t queue, const Message* msg);
bool receiveMessage(msgq_t queue, Message* msg, long mtype);

#endif // MESSAGE_QUEUE_H
