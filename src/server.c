#include "server.h"
#include <stdio.h>

//Tableau de stations 
#define MAX_STATIONS 30
ChargingStation charging_stations[MAX_STATIONS];

// Fonction pour diffrentes requetes du serveur
void handle_request(RequestType request) {
    switch (request) {
        case REQ_CHARGE_DEMAND:
            printf("[SERVEUR] Un véhicule demande une borne de recharge.\n");
            function_to_handle_charge_demand();
            break;

        case REQ_CHARGE_END:
            printf("[SERVEUR] Un véhicule a fini de charger et libère la borne.\n");
            function_to_handle_charge_end();
            break;

        case REQ_ALERT:
            printf("[SERVEUR] ALERTE : Une borne signale une panne !\n");
            function_to_handle_alert();
            break;

        case REQ_SHUTDOWN:
            printf("[SERVEUR] Arrêt propre du serveur demandé.\n");
            function_to_shutdown_server(); 
            break;

        default:
            printf("[SERVEUR] Erreur : Type de requête inconnu.\n");
            break;
    }
}



void function_to_handle_charge_demand() {
    printf("[SERVEUR] Traitement de la demande de charge...\n");

    //Décla Variables pour gérer les stations de charge
    typedef struct {
        int charge_station_id;
        bool is_available;
    } ChargingStation;

    int available_station_index = -1;

    for (int i = 0; i < MAX_STATIONS; i++) {
        if (charging_stations[i].is_available) {
            available_station_index = i;
            break;
        }
    }

    if (available_station_index != -1) {
        printf("[SERVEUR] Borne de recharge %d attribuée au véhicule.\n", charging_stations[available_station_index].charge_station_id);

    } else {
        printf("[SERVEUR] Aucune borne de recharge disponible pour le moment.\n");
    }


void function_to_handle_charge_end() {
    printf("[SERVEUR] Traitement de la fin de charge...\n");

    int free_station_index = -1;
    for (int i = 0; i < MAX_STATIONS; i++) {
        if (!charging_stations[i].is_available) {
            free_station_index = i;
            break;
        }
    }

    if (free_station_index != -1) {
        charging_stations[free_station_index].is_available = true;
        printf("[SERVEUR] Borne de recharge %d libérée.\n", charging_stations[free_station_index].charge_station_id);

        //TODO Attribuer Vehicule en attente à la borne libérée

    } else {
        printf("[SERVEUR] Aucune borne de recharge n'était occupée.\n");
    }

}

void function_to_handle_alert() {
    printf("[SERVEUR] Traitement de l'alerte...\n");

    bool alert_handled = false;


    for (int i = 0; i < MAX_STATIONS; i++) {
        if (!charging_stations[i].is_available) {
            printf("[SERVEUR] Alerte : Borne de recharge %d signale une panne !\n", charging_stations[i].charge_station_id);
            alert_handled = true;
            break;
        }
    }

    if (!alert_handled) {
        printf("[SERVEUR] Aucune alerte à traiter.\n");
    }
}


void function_to_shutdown_server() {
    printf("[SERVEUR] Procédure d'arrêt en cours...\n");

    // 1. TODO: Supprimer les sémaphores
    // 2. TODO: Fermer les files de messages
    // 3. TODO: Libérer les éventuels blocs de mémoire

    printf("[SERVEUR] Ressources IPC et mémoire nettoyées avec succès. Arrêt définitif.\n");
    
    exit(0); 
}
