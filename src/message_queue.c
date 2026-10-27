#include "message_queue.h"
#include <stdio.h>
#include <stdlib.h>

msgq_t initMessageQueue(int key) {
    msgq_t queue = msgget(key, 0666 | IPC_CREAT);
    if (queue == -1) {
        perror("Erreur lors de la création de la file de messages");
        exit(1);
    }
    return queue;
}

void sendMessage(msgq_t queue, Message* msg) {
    if (msgsnd(queue, msg, sizeof(Message) - sizeof(long), 0) == -1) {
        perror("Erreur msgsnd");
    }
}

void receiveMessage(msgq_t queue, Message* msg, long mtype) {
    if (msgrcv(queue, msg, sizeof(Message) - sizeof(long), mtype, 0) == -1) {
        perror("Erreur msgrcv");
    }
}