#include "message_queue.h"
#include <stdio.h>

msgq_t initMessageQueue(key_t key) {
    msgq_t queue = msgget(key, 0600 | IPC_CREAT);
    if (queue == -1) {
        perror("Erreur msgget");
    }
    return queue;
}

void destroyMessageQueue(msgq_t queue) {
    if (msgctl(queue, IPC_RMID, NULL) == -1) {
        perror("Erreur msgctl(IPC_RMID)");
    }
}

bool sendMessage(msgq_t queue, const Message* msg) {
    if (msgsnd(queue, msg, sizeof(Message) - sizeof(long), 0) == -1) {
        perror("Erreur msgsnd");
        return false;
    }
    return true;
}

bool receiveMessage(msgq_t queue, Message* msg, long mtype) {
    if (msgrcv(queue, msg, sizeof(Message) - sizeof(long), mtype, 0) == -1) {
        perror("Erreur msgrcv");
        return false;
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
