#include "station.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sem.h>

// Sémaphores de l'ensemble associé à chaque station
#define SEM_LOCK      0 // Binaire : exclusion mutuelle sur l'état des bornes
#define SEM_AVAILABLE 1 // Compteur : nombre de bornes à l'état AVAILABLE
#define SEM_COUNT     2

// Puissances attribuées en boucle par initStation()
static const int DEFAULT_CHARGER_POWERS[] = { 22, 50, 150 };
#define DEFAULT_CHARGER_POWER_COUNT ((int)(sizeof(DEFAULT_CHARGER_POWERS) / sizeof(DEFAULT_CHARGER_POWERS[0])))

// glibc laisse l'appelant définir union semun (macOS la fournit déjà)
#ifdef _SEM_SEMUN_UNDEFINED
union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};
#endif

static void semAdd(int semId, unsigned short semNum, short delta) {
    struct sembuf op = { .sem_num = semNum, .sem_op = delta, .sem_flg = 0 };
    int result;
    // Un signal pendant l'attente ne doit pas laisser croire que le verrou est acquis
    do {
        result = semop(semId, &op, 1);
    } while (result == -1 && errno == EINTR);

    if (result == -1) {
        perror("Erreur semop");
    }
}

static double secondsSince(const struct timespec* start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start->tv_sec) + (double)(now.tv_nsec - start->tv_nsec) / 1e9;
}

// Verrou acquis : temps d'occupation cumulé de la borne, recharge en cours comprise
static double occupiedSeconds(const struct Charger* charger) {
    double total = charger->occupiedSeconds;
    if (charger->status == OCCUPIED) {
        total += secondsSince(&charger->occupiedSince);
    }
    return total;
}

bool initStationWithPowers(struct Station* station, int id, const int* chargerPowers, int numChargers) {
    station->chargers = NULL;
    station->numChargers = 0;
    station->semId = -1;
    if (numChargers <= 0 || numChargers > MAX_CHARGERS_PER_STATION) {
        fprintf(stderr, "Erreur : %d bornes demandées pour la station %d (1 à %d)\n", numChargers, id,
                MAX_CHARGERS_PER_STATION);
        return false;
    }
    for (int i = 0; i < numChargers; i++) {
        if (chargerPowers[i] <= 0) {
            fprintf(stderr, "Erreur : puissance %d kW invalide pour la borne %d\n", chargerPowers[i], i);
            return false;
        }
    }

    station->id = id;
    station->chargers = malloc((size_t)numChargers * sizeof(struct Charger));
    if (station->chargers == NULL) {
        perror("Erreur malloc des bornes");
        return false;
    }

    for (int i = 0; i < numChargers; i++) {
        station->chargers[i] = (struct Charger){
            .id = i,
            .maxPower = chargerPowers[i],
            .status = AVAILABLE,
            .session = { .vehicleId = NO_VEHICLE },
        };
    }

    initWaitQueue(&station->waitQueue);

    // Sémaphores propres au serveur : verrou libre, toutes les bornes disponibles
    station->semId = semget(IPC_PRIVATE, SEM_COUNT, 0600 | IPC_CREAT);
    if (station->semId == -1) {
        perror("Erreur semget");
        goto free_chargers;
    }

    unsigned short initialValues[SEM_COUNT] = {
        [SEM_LOCK] = 1,
        [SEM_AVAILABLE] = (unsigned short)numChargers,
    };
    union semun arg = { .array = initialValues };
    if (semctl(station->semId, 0, SETALL, arg) == -1) {
        perror("Erreur semctl(SETALL)");
        semctl(station->semId, 0, IPC_RMID);
        station->semId = -1;
        goto free_chargers;
    }

    station->numChargers = numChargers;
    clock_gettime(CLOCK_MONOTONIC, &station->openedAt);
    return true;

free_chargers:
    free(station->chargers);
    station->chargers = NULL;
    return false;
}

bool initStation(struct Station* station, int id, int numChargers) {
    int powers[MAX_CHARGERS_PER_STATION];
    for (int i = 0; i < numChargers && i < MAX_CHARGERS_PER_STATION; i++) {
        powers[i] = DEFAULT_CHARGER_POWERS[i % DEFAULT_CHARGER_POWER_COUNT];
    }
    return initStationWithPowers(station, id, powers, numChargers);
}

void destroyStation(struct Station* station) {
    if (station->semId != -1 && semctl(station->semId, 0, IPC_RMID) == -1) {
        perror("Erreur semctl(IPC_RMID)");
    }
    station->semId = -1;
    free(station->chargers);
    station->chargers = NULL;
    station->numChargers = 0;
}

void lockStation(struct Station* station) {
    semAdd(station->semId, SEM_LOCK, -1);
}

void unlockStation(struct Station* station) {
    semAdd(station->semId, SEM_LOCK, 1);
}

void setStationChargerStatus(struct Station* station, int chargerIndex, ChargerStatus status) {
    struct Charger* charger = &station->chargers[chargerIndex];
    bool wasAvailable = charger->status == AVAILABLE;
    bool isAvailable = status == AVAILABLE;
    bool wasOccupied = charger->status == OCCUPIED;
    bool isOccupied = status == OCCUPIED;

    if (!wasOccupied && isOccupied) {
        clock_gettime(CLOCK_MONOTONIC, &charger->occupiedSince);
    } else if (wasOccupied && !isOccupied) {
        charger->occupiedSeconds += secondsSince(&charger->occupiedSince);
    }

    setChargerStatus(charger, status);

    // Sous verrou, le compteur vaut au moins 1 quand une borne quitte l'état AVAILABLE : pas de blocage
    if (wasAvailable && !isAvailable) {
        semAdd(station->semId, SEM_AVAILABLE, -1);
    } else if (!wasAvailable && isAvailable) {
        semAdd(station->semId, SEM_AVAILABLE, 1);
    }
}

bool isValidCharger(const struct Station* station, int chargerIndex) {
    return chargerIndex >= 0 && chargerIndex < station->numChargers;
}

bool hasAvailableCharger(struct Station* station) {
    // Lecture sans verrou : valeur indicative, seule une réservation sous verrou fait foi
    return semctl(station->semId, SEM_AVAILABLE, GETVAL) > 0;
}

bool isChargerAvailable(struct Station* station, int chargerIndex) {
    if (!isValidCharger(station, chargerIndex)) {
        return false;
    }
    lockStation(station);
    bool available = station->chargers[chargerIndex].status == AVAILABLE;
    unlockStation(station);
    return available;
}

int countChargers(struct Station* station, ChargerStatus status) {
    lockStation(station);
    int count = 0;
    for (int i = 0; i < station->numChargers; i++) {
        if (station->chargers[i].status == status) {
            count++;
        }
    }
    unlockStation(station);
    return count;
}

double stationOccupancyRate(struct Station* station) {
    lockStation(station);
    double occupied = 0.0;
    for (int i = 0; i < station->numChargers; i++) {
        occupied += occupiedSeconds(&station->chargers[i]);
    }
    double available = secondsSince(&station->openedAt) * station->numChargers;
    unlockStation(station);

    return available > 0.0 ? occupied / available : 0.0;
}

void printStation(struct Station* station) {
    double occupancy = stationOccupancyRate(station);

    lockStation(station);
    int availableCount = 0;
    for (int i = 0; i < station->numChargers; i++) {
        if (station->chargers[i].status == AVAILABLE) {
            availableCount++;
        }
    }
    printf("Station %d : %d/%d bornes libres, taux d'occupation %.1f %%\n", station->id, availableCount,
           station->numChargers, occupancy * 100.0);

    for (int i = 0; i < station->numChargers; i++) {
        const struct Charger* charger = &station->chargers[i];
        printf("  Borne %d (%d kW) : %s", charger->id, charger->maxPower, chargerStatusName(charger->status));
        if (charger->status == OCCUPIED) {
            printf(", véhicule %d depuis %.1f s", charger->session.vehicleId, secondsSince(&charger->session.start));
        }
        printf("\n");
    }
    printWaitQueue(&station->waitQueue);
    unlockStation(station);
}
