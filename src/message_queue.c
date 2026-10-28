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
    return true;
}
