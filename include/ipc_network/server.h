#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>
#include "message_queue.h"
#include "station.h"

#define MAX_STATIONS 30

struct Server {
    struct Station stations[MAX_STATIONS]; // L'identifiant d'une station est son indice
    int numStations;
};

bool initServer(struct Server* server, int numStations, int chargersPerStation);
void shutdownServer(struct Server* server);

// Traite une requête reçue ; renvoie false lorsque le serveur doit s'arrêter
bool handleRequest(struct Server* server, const Message* msg);

#endif // SERVER_H
