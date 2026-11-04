#include "history.h"
#include "sim_time.h"
#include <stdio.h>
#include <stdlib.h>

#define INITIAL_HISTORY_CAPACITY 32

static double secondsSince(const struct timespec* start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start->tv_sec) + (double)(now.tv_nsec - start->tv_nsec) / 1e9;
}

bool initHistory(struct History* history) {
    history->records = NULL;
    history->count = 0;
    history->capacity = 0;
    history->initialized = false;

    int error = pthread_mutex_init(&history->lock, NULL);
    if (error != 0) {
        fprintf(stderr, "Erreur : initialisation du verrou de l'historique (%d)\n", error);
        return false;
    }
    history->initialized = true;
    return true;
}

void destroyHistory(struct History* history) {
    if (!history->initialized) {
        return;
    }
    pthread_mutex_destroy(&history->lock);
    free(history->records);
    history->records = NULL;
    history->count = 0;
    history->capacity = 0;
    history->initialized = false;
}

bool addSessionRecord(struct History* history, const struct SessionRecord* record) {
    pthread_mutex_lock(&history->lock);
    if (history->count == history->capacity) {
        int newCapacity = history->capacity == 0 ? INITIAL_HISTORY_CAPACITY : history->capacity * 2;
        struct SessionRecord* grown = realloc(history->records, (size_t)newCapacity * sizeof(struct SessionRecord));
        if (grown == NULL) {
            pthread_mutex_unlock(&history->lock);
            perror("Erreur realloc de l'historique");
            return false;
        }
        history->records = grown;
        history->capacity = newCapacity;
    }

    struct SessionRecord* stored = &history->records[history->count++];
    *stored = *record;
    clock_gettime(CLOCK_MONOTONIC, &stored->endedAt);
    pthread_mutex_unlock(&history->lock);
    return true;
}

int historySize(struct History* history) {
    pthread_mutex_lock(&history->lock);
    int count = history->count;
    pthread_mutex_unlock(&history->lock);
    return count;
}

bool getSessionRecord(struct History* history, int index, struct SessionRecord* record) {
    pthread_mutex_lock(&history->lock);
    bool found = index >= 0 && index < history->count;
    if (found) {
        *record = history->records[index];
    }
    pthread_mutex_unlock(&history->lock);
    return found;
}

int countRecentSessions(struct History* history, int vehicleId, double windowSeconds) {
    pthread_mutex_lock(&history->lock);
    int count = 0;
    // Les enregistrements sont dans l'ordre chronologique : on remonte jusqu'au premier trop ancien
    for (int i = history->count - 1; i >= 0 && secondsSince(&history->records[i].endedAt) < windowSeconds; i--) {
        if (history->records[i].vehicleId == vehicleId) {
            count++;
        }
    }
    pthread_mutex_unlock(&history->lock);
    return count;
}

struct HistorySummary summarizeHistory(struct History* history, int stationId) {
    struct HistorySummary summary = { 0 };
    pthread_mutex_lock(&history->lock);
    for (int i = 0; i < history->count; i++) {
        const struct SessionRecord* record = &history->records[i];
        if (stationId != ALL_STATIONS && record->stationId != stationId) {
            continue;
        }
        summary.sessions++;
        summary.interruptedSessions += record->interrupted ? 1 : 0;
        summary.energyKWh += record->energyKWh;
        summary.revenue += record->cost;
    }
    pthread_mutex_unlock(&history->lock);
    return summary;
}

void printHistory(struct History* history, int maxRecords) {
    pthread_mutex_lock(&history->lock);
    int first = history->count > maxRecords ? history->count - maxRecords : 0;
    printf("Historique (%d dernière(s) session(s) sur %d) :\n", history->count - first, history->count);
    for (int i = first; i < history->count; i++) {
        const struct SessionRecord* record = &history->records[i];
        printf("  #%d véhicule %d%s, station %d borne %d : %.2f kWh à %d kW en %.1f min, %.2f €%s\n", i + 1,
               record->vehicleId, record->isPrioritary ? " (prioritaire)" : "", record->stationId,
               record->chargerId, record->energyKWh, record->power, simulatedMinutes(record->durationSeconds),
               record->cost, record->interrupted ? ", interrompue" : "");
    }
    pthread_mutex_unlock(&history->lock);
}

bool exportHistoryCsv(struct History* history, const char* path) {
    FILE* file = fopen(path, "w");
    if (file == NULL) {
        perror("Erreur d'ouverture du fichier d'historique");
        return false;
    }

    fprintf(file, "vehicule;station;borne;puissance_kW;energie_kWh;duree_min;cout_EUR;interrompue;prioritaire\n");
    pthread_mutex_lock(&history->lock);
    for (int i = 0; i < history->count; i++) {
        const struct SessionRecord* record = &history->records[i];
        fprintf(file, "%d;%d;%d;%d;%.3f;%.1f;%.2f;%d;%d\n", record->vehicleId, record->stationId,
                record->chargerId, record->power, record->energyKWh, simulatedMinutes(record->durationSeconds),
                record->cost, record->interrupted ? 1 : 0, record->isPrioritary ? 1 : 0);
    }
    pthread_mutex_unlock(&history->lock);

    bool written = !ferror(file);
    if (fclose(file) != 0) {
        written = false;
    }
    if (!written) {
        fprintf(stderr, "Erreur d'écriture du fichier d'historique %s\n", path);
    }
    return written;
}
