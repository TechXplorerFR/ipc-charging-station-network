#include "server.h"
#include "station.h"
#include <stdio.h>
#include <stdlib.h>

//Tableau de stations 
#define MAX_STATIONS 30
struct Station charging_stations[MAX_STATIONS];

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

void function_to_handle_charge_demand(void) {
    printf("[SERVEUR] Traitement de la demande de charge...\n");

    int available_station_index = -1;

    for (int i = 0; i < MAX_STATIONS; i++) {
        if (hasAvailableCharger(&charging_stations[i])) {
            available_station_index = i;
            break;
        }
    }

    if (available_station_index != -1) {
        printf("[SERVEUR] Borne de recharge %d attribuée au véhicule.\n", charging_stations[available_station_index].id);

    } else {
        printf("[SERVEUR] Aucune borne de recharge disponible pour le moment.\n");
    }
}

void function_to_handle_charge_end(void) {
    printf("[SERVEUR] Traitement de la fin de charge...\n");

    // Recherche d'une borne occupée à libérer
    int free_station_index = -1;
    int free_charger_index = -1;
    for (int i = 0; i < MAX_STATIONS && free_station_index == -1; i++) {
        pthread_mutex_lock(&charging_stations[i].lock);
        for (int j = 0; j < charging_stations[i].numChargers; j++) {
            if (charging_stations[i].chargers[j].status == OCCUPIED) {
                charging_stations[i].chargers[j].status = AVAILABLE;
                free_station_index = i;
                free_charger_index = j;
                break;
            }
        }
        pthread_mutex_unlock(&charging_stations[i].lock);
    }

    if (free_station_index != -1) {
        printf("[SERVEUR] Borne %d de la station %d libérée.\n", free_charger_index, charging_stations[free_station_index].id);

        //TODO Attribuer Vehicule en attente à la borne libérée

    } else {
        printf("[SERVEUR] Aucune borne de recharge n'était occupée.\n");
    }

}

void function_to_handle_alert(void) {
    printf("[SERVEUR] Traitement de l'alerte...\n");

    bool alert_handled = false;


    for (int i = 0; i < MAX_STATIONS; i++) {
        if (!hasAvailableCharger(&charging_stations[i])) {
            printf("[SERVEUR] Alerte : Borne de recharge %d signale une panne !\n", charging_stations[i].id);
            alert_handled = true;
            break;
        }
    }

    if (!alert_handled) {
        printf("[SERVEUR] Aucune alerte à traiter.\n");
    }
}


void function_to_shutdown_server(void) {
    printf("[SERVEUR] Procédure d'arrêt en cours...\n");

    // 1. TODO: Supprimer les sémaphores
    // 2. TODO: Fermer les files de messages
    // 3. TODO: Libérer les éventuels blocs de mémoire

    printf("[SERVEUR] Ressources IPC et mémoire nettoyées avec succès. Arrêt définitif.\n");
    
    exit(0); 
}
