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

bool sendToVehicle(msgq_t queue, int vehicleId, const Message* msg) {
    Message addressed = *msg;
    addressed.mtype = vehicleMsgType(vehicleId);
    addressed.vehicle_id = vehicleId;
    return sendMessage(queue, &addressed);
}

bool receiveMessage(msgq_t queue, Message* msg, long mtype) {
    ssize_t result;
    do {
        result = msgrcv(queue, msg, MSG_PAYLOAD_SIZE, mtype, 0);
    } while (result == -1 && errno == EINTR);

    if (result == -1) {
        perror("Erreur msgrcv");
        return false;
    }
    return true;
}

ReceiveStatus tryReceiveMessage(msgq_t queue, Message* msg, long mtype) {
    if (msgrcv(queue, msg, MSG_PAYLOAD_SIZE, mtype, IPC_NOWAIT) != -1) {
        return MSG_RECEIVED;
    }
    if (errno == ENOMSG) {
        return MSG_NONE;
    }
    perror("Erreur msgrcv");
    return MSG_ERROR;
}
