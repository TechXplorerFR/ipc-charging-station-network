#ifndef HISTORY_H
#define HISTORY_H

#include <pthread.h>
#include <stdbool.h>
#include <time.h>

// Valeur de stationId pour résumer tout le réseau
#define ALL_STATIONS (-1)

// Session de recharge facturée
struct SessionRecord {
    int vehicleId;
    int stationId;
    int chargerId;
    int power; // kW délivrés
    double energyKWh;
    double durationSeconds;
    double cost; // € facturés
    bool interrupted;
    bool isPrioritary;
    struct timespec endedAt; // Horloge CLOCK_MONOTONIC, renseignée par addSessionRecord()
};

// Historique de toutes les sessions, consultable pendant la simulation (CLI) : toutes les fonctions
// prennent elles-mêmes le verrou
struct History {
    struct SessionRecord* records; // Dans l'ordre d'enregistrement
    int count;
    int capacity;
    pthread_mutex_t lock;
    bool initialized;
};

struct HistorySummary {
    int sessions;
    int interruptedSessions;
    double energyKWh;
    double revenue; // €
};

bool initHistory(struct History* history);
// Sans effet si l'historique n'est pas initialisé ou déjà détruit
void destroyHistory(struct History* history);

// Enregistre une session terminée maintenant ; renvoie false en cas d'échec d'allocation
bool addSessionRecord(struct History* history, const struct SessionRecord* record);

int historySize(struct History* history);
// Copie l'enregistrement n° index (0 = le plus ancien) ; renvoie false si l'indice est invalide
bool getSessionRecord(struct History* history, int index, struct SessionRecord* record);

// Nombre de sessions du véhicule terminées depuis moins de windowSeconds (pour la fidélité)
int countRecentSessions(struct History* history, int vehicleId, double windowSeconds);

// Totaux d'une station, ou de tout le réseau avec ALL_STATIONS
struct HistorySummary summarizeHistory(struct History* history, int stationId);

// Affiche les maxRecords dernières sessions
void printHistory(struct History* history, int maxRecords);

// Écrit l'historique au format CSV (séparateur « ; ») ; renvoie false si le fichier ne peut pas être écrit
bool exportHistoryCsv(struct History* history, const char* path);

#endif // HISTORY_H
