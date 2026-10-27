// message_queue.h
#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define SERVER_MSG_TYPE 1

typedef struct {
    long mtype;
    int vehicle_id;
    int station_id;
    int desired_power;
    int is_prioritary;
} Message;

typedef int msgq_t;

msgq_t initMessageQueue(int key);
void sendMessage(msgq_t queue, Message* msg);
void receiveMessage(msgq_t queue, Message* msg, long mtype);

#endif // MESSAGE_QUEUE_H