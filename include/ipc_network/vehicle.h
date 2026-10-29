#ifndef VEHICLE_H
#define VEHICLE_H

#include <stdbool.h>
#include "message_queue.h"

typedef enum {
    CAR,
    VAN,
    BUS
} VehicleType;

struct Vehicle {
    int id;
    VehicleType type;
    double capacity;      // kWh
    double currentCharge; // kWh, entre 0 et capacity
    int maxPower;         // kW acceptés au maximum
    char* destination;
    bool isPrioritary;    // Véhicule d'urgence ou transport public : passe devant dans la file d'attente
};

// Issue du cycle de vie d'un véhicule (runVehicle)
typedef enum {
    VEHICLE_CHARGED, // Recharge terminée et borne libérée
    VEHICLE_REFUSED, // Demande refusée par le serveur
    VEHICLE_STOPPED, // Fin de la simulation (NOTIF_SHUTDOWN ou file du serveur supprimée)
    VEHICLE_ERROR    // Échec d'envoi ou de réception
} VehicleOutcome;

// Renvoie NULL si un paramètre est invalide ou en cas d'échec d'allocation ; à libérer avec destroyVehicle()
struct Vehicle* createVehicle(int id, VehicleType type, double capacity, double initialCharge, int maxPower,
                              const char* destination, bool prioritary);
void destroyVehicle(struct Vehicle* vehicle);

const char* vehicleTypeName(VehicleType type);

// Niveau de batterie, de 0 à 100 %
int batteryLevelPercent(const struct Vehicle* vehicle);
bool isFullyCharged(const struct Vehicle* vehicle);

// Durée réelle (s, arrondie au-dessus) pour remplir la batterie à la puissance donnée,
// compte tenu de l'accélération de la simulation (SIMULATED_SECONDS_PER_SECOND)
int estimatedChargeDurationSeconds(const struct Vehicle* vehicle, int power);

// Ajoute l'énergie reçue pendant realSeconds à la puissance donnée (limitée par le véhicule), sans dépasser
// la capacité ; renvoie l'énergie réellement ajoutée (kWh)
double addCharge(struct Vehicle* vehicle, int power, double realSeconds);

// Demande de recharge à la puissance maximale du véhicule, avec la durée estimée
bool sendChargeRequest(msgq_t serverQueue, const struct Vehicle* vehicle, int stationId);
// Répond à un NOTIF_PING avec le niveau de batterie
bool respondToPing(msgq_t serverQueue, const struct Vehicle* vehicle);

// Cycle complet d'un véhicule : demande une borne à la station, attend son attribution (éventuellement
// après la file d'attente), recharge en répondant aux pings, puis libère la borne. Après une panne de borne,
// attend que le serveur le redirige. Bloquant : à exécuter dans le thread du véhicule.
VehicleOutcome runVehicle(struct Vehicle* vehicle, msgq_t serverQueue, int stationId);

// Paramètres et résultat d'un thread véhicule
struct VehicleTrip {
    struct Vehicle* vehicle;
    msgq_t serverQueue;
    int stationId;
    VehicleOutcome outcome; // Renseigné à la fin du thread
};

// Point d'entrée pour pthread_create, avec une struct VehicleTrip* en argument
void* vehicleThread(void* trip);

#endif // VEHICLE_H
