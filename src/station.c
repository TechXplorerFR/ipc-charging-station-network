#include "station.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/sem.h>

// Sémaphores de l'ensemble associé à chaque station
#define SEM_LOCK      0 // Binaire : exclusion mutuelle sur l'état des bornes
#define SEM_AVAILABLE 1 // Compteur : nombre de bornes à l'état AVAILABLE
#define SEM_COUNT     2

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
    if (semop(semId, &op, 1) == -1) {
        perror("Erreur semop");
    }
}

bool initStation(struct Station* station, int id, int numChargers) {
    station->id = id;
    station->numChargers = numChargers;
    station->chargers = malloc(numChargers * sizeof(struct Charger));
    if (station->chargers == NULL) {
        perror("Erreur malloc des bornes");
        return false;
    }

    for (int i = 0; i < numChargers; i++) {
        station->chargers[i].id = i;
        station->chargers[i].maxPower = 50; // Exemple : 50 kW
        station->chargers[i].status = AVAILABLE;
        station->chargers[i].vehicleId = NO_VEHICLE;
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
        goto free_chargers;
    }

    return true;

free_chargers:
    free(station->chargers);
    station->chargers = NULL;
    return false;
}

void destroyStation(struct Station* station) {
    if (semctl(station->semId, 0, IPC_RMID) == -1) {
        perror("Erreur semctl(IPC_RMID)");
    }
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
