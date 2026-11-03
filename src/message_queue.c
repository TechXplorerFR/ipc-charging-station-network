#include "message_queue.h"
#include <errno.h>
#include <stdio.h>

// Taille utile d'un message pour msgsnd/msgrcv : tout sauf le champ mtype
#define MSG_PAYLOAD_SIZE (sizeof(Message) - sizeof(long))

msgq_t initMessageQueue(key_t key) {
    msgq_t queue = msgget(key, 0600 | IPC_CREAT);
    if (queue == -1) {
        perror("Erreur msgget");
    }
    return queue;
}

msgq_t createServerQueue(void) {
    msgq_t stale = msgget(SERVER_QUEUE_KEY, 0600);
    if (stale != -1) {
        destroyMessageQueue(stale);
    } else if (errno != ENOENT) {
        perror("Erreur msgget");
        return -1;
    }

    // IPC_EXCL : échoue plutôt que de réutiliser une file qui contiendrait déjà des messages
    msgq_t queue = msgget(SERVER_QUEUE_KEY, 0600 | IPC_CREAT | IPC_EXCL);
    if (queue == -1) {
        perror("Erreur msgget");
    }
    return queue;
}

msgq_t openServerQueue(void) {
    return initMessageQueue(SERVER_QUEUE_KEY);
}

void destroyMessageQueue(msgq_t queue) {
    if (msgctl(queue, IPC_RMID, NULL) == -1) {
        perror("Erreur msgctl(IPC_RMID)");
    }
}

long vehicleMsgType(int vehicleId) {
    return (long)vehicleId + VEHICLE_MSG_TYPE_OFFSET;
}

bool sendMessage(msgq_t queue, const Message* msg) {
    int result;
    do {
        result = msgsnd(queue, msg, MSG_PAYLOAD_SIZE, 0);
    } while (result == -1 && errno == EINTR);

    if (result == -1) {
        perror("Erreur msgsnd");
        return false;
    }
    return true;
}

bool sendToServer(msgq_t queue, const Message* msg) {
    Message addressed = *msg;
    addressed.mtype = SERVER_MSG_TYPE;
    return sendMessage(queue, &addressed);
}

bool sendToVehicle(msgq_t queue, int vehicleId, const Message* msg) {
    Message addressed = *msg;
    addressed.mtype = vehicleMsgType(vehicleId);
    addressed.vehicle_id = vehicleId;
    return sendMessage(queue, &addressed);
}

// Statut d'un msgrcv qui a échoué ; la suppression de la file n'est pas une erreur
static ReceiveStatus failedReceiveStatus(void) {
    switch (errno) {
        case ENOMSG:
            return MSG_NONE;
        case EIDRM:
        case EINVAL: // File déjà supprimée avant l'appel
            return MSG_CLOSED;
        default:
            perror("Erreur msgrcv");
            return MSG_ERROR;
    }
}

ReceiveStatus receiveMessage(msgq_t queue, Message* msg, long mtype) {
    ssize_t result;
    do {
        result = msgrcv(queue, msg, MSG_PAYLOAD_SIZE, mtype, 0);
    } while (result == -1 && errno == EINTR);

    return result != -1 ? MSG_RECEIVED : failedReceiveStatus();
}

ReceiveStatus tryReceiveMessage(msgq_t queue, Message* msg, long mtype) {
    if (msgrcv(queue, msg, MSG_PAYLOAD_SIZE, mtype, IPC_NOWAIT) != -1) {
        return MSG_RECEIVED;
    }
    return failedReceiveStatus();
}

bool isWellFormedMessage(const Message* msg) {
    switch (msg->request) {
        case REQ_CHARGE_DEMAND:
            return msg->vehicle_id >= 0 && msg->station_id >= 0 && msg->desired_power > 0
                && msg->estimated_duration >= 0;

        case REQ_CHARGE_END:
            return msg->vehicle_id >= 0 && msg->station_id >= 0 && msg->charger_id >= 0;

        case REQ_ALERT: // Envoyées par le simulateur de pannes ou la CLI : pas de véhicule
        case REQ_REPAIR:
            return msg->station_id >= 0 && msg->charger_id >= 0;

        case REQ_CANCEL:
            return msg->vehicle_id >= 0 && msg->station_id >= 0;

        case REQ_PONG:
            return msg->vehicle_id >= 0 && msg->battery_level >= 0 && msg->battery_level <= 100;

        case RESP_ASSIGNED:
            return msg->vehicle_id >= 0 && msg->station_id >= 0 && msg->charger_id >= 0;

        case RESP_QUEUED:
            return msg->vehicle_id >= 0 && msg->station_id >= 0 && msg->queue_position >= 1;

        case RESP_REFUSED:
        case NOTIF_PING:
        case NOTIF_FAULT:
        case NOTIF_SHUTDOWN:
            return msg->vehicle_id >= 0;

        case REQ_SHUTDOWN:
            return true;
    }
    return false; // Valeur hors de l'énumération
}
