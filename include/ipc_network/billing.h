#ifndef BILLING_H
#define BILLING_H

#include <stdbool.h>
#include "charger.h"

// Grille tarifaire du réseau (montants en euros)
struct Tariff {
    double pricePerKWh;               // Prix de base de l'énergie
    int fastChargePowerKW;            // Au-delà de cette puissance délivrée, la recharge est dite rapide
    double fastChargeSurchargePerKWh; // Supplément par kWh pour une recharge rapide
    double sessionFee;                // Frais fixes par session, offerts si une panne l'interrompt
    int loyaltyMinSessions;           // Sessions récentes nécessaires pour être client fidèle
    double loyaltyWindowSeconds;      // Fenêtre (temps réel) dans laquelle une session compte comme récente
    double loyaltyDiscountRate;       // Remise accordée aux clients fidèles (0 à 1)
};

// 0,35 €/kWh, +0,15 €/kWh au-delà de 50 kW, 1 € par session, 10 % de remise dès 2 sessions
// dans les 10 dernières minutes réelles (10 h simulées)
extern const struct Tariff DEFAULT_TARIFF;

// Détail du coût d'une session de recharge
struct Invoice {
    int vehicleId;
    int power;              // kW délivrés
    double energyKWh;
    double durationSeconds; // Durée réelle de la session
    double unitPrice;       // € par kWh appliqués, supplément de charge rapide compris
    double energyCost;
    double sessionFee;
    double loyaltyDiscount; // Montant remis
    double total;
    bool interrupted;       // Session interrompue par une panne
};

bool isValidTariff(const struct Tariff* tariff);

// Facture une session terminée (ou interrompue) ; recentSessions est le nombre de sessions du véhicule
// enregistrées dans la fenêtre de fidélité (voir countRecentSessions dans history.h)
struct Invoice computeInvoice(const struct Tariff* tariff, const struct ChargingSession* session, bool interrupted,
                              int recentSessions);

void printInvoice(const struct Invoice* invoice);

#endif // BILLING_H
