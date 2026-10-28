#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include <stdbool.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

// Clé de la file de messages du serveur central, commune à tous les threads
#define SERVER_QUEUE_KEY ((key_t)0x53523730) // "SR70"

// Type (mtype) des messages destinés au serveur central
#define SERVER_MSG_TYPE 1

// Les messages destinés à un véhicule ont pour type vehicleMsgType(id) :
// le décalage garantit un type strictement positif et différent de SERVER_MSG_TYPE
#define VEHICLE_MSG_TYPE_OFFSET 2

typedef enum {
    // Véhicule (ou simulateur) -> serveur
    REQ_CHARGE_DEMAND,
    REQ_CHARGE_END,
    REQ_ALERT,
    REQ_SHUTDOWN,
    REQ_CANCEL, // Le véhicule quitte la file d'attente
    REQ_PONG,   // Réponse à NOTIF_PING, avec le niveau de batterie

    // Serveur -> véhicule
    RESP_ASSIGNED, // Borne attribuée (charger_id)
    RESP_QUEUED,   // Placé en file d'attente (queue_position)
    RESP_REFUSED,  // Aucune borne libre et file d'attente pleine
    NOTIF_PING,    // Demande de l'état de la charge
    NOTIF_FAULT    // La borne utilisée par le véhicule est tombée en panne
} RequestType;

typedef struct {
    long mtype;
    RequestType request;
    int vehicle_id;
    int station_id;
    int charger_id;         // Borne concernée (attribution, fin de charge, alerte), -1 sinon
    int desired_power;      // kW
    int estimated_duration; // Durée de charge estimée (s)
    int battery_level;      // Niveau de batterie (%), renseigné dans REQ_PONG
    int queue_position;     // Position dans la file d'attente (1 = prochain servi), renseignée dans RESP_QUEUED
    bool is_prioritary;
} Message;

typedef int msgq_t;

typedef enum {
    MSG_RECEIVED,
    MSG_NONE, // File vide (réception non bloquante uniquement)
    MSG_ERROR
} ReceiveStatus;

// Renvoie l'identifiant de la file, ou -1 en cas d'erreur
msgq_t initMessageQueue(key_t key);
msgq_t openServerQueue(void);
void destroyMessageQueue(msgq_t queue);

// Type des messages destinés au véhicule vehicleId (>= 0)
long vehicleMsgType(int vehicleId);

// Envoi bloquant si la file est pleine ; un signal ne fait pas échouer l'envoi
bool sendMessage(msgq_t queue, const Message* msg);
// Envoie une copie de msg adressée au véhicule (mtype et vehicle_id renseignés)
bool sendToVehicle(msgq_t queue, int vehicleId, const Message* msg);

// Attend un message de type mtype ; un signal ne fait pas échouer l'attente
bool receiveMessage(msgq_t queue, Message* msg, long mtype);
// Récupère un message de type mtype s'il y en a un, sans attendre
ReceiveStatus tryReceiveMessage(msgq_t queue, Message* msg, long mtype);

#endif // MESSAGE_QUEUE_H
