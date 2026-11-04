#include "billing.h"
#include <stdio.h>

const struct Tariff DEFAULT_TARIFF = {
    .pricePerKWh = 0.35,
    .fastChargePowerKW = 50,
    .fastChargeSurchargePerKWh = 0.15,
    .sessionFee = 1.00,
    .loyaltyMinSessions = 2,
    .loyaltyWindowSeconds = 600.0,
    .loyaltyDiscountRate = 0.10,
    .loyaltyPriority = true,
};

bool isValidTariff(const struct Tariff* tariff) {
    return tariff->pricePerKWh >= 0.0 && tariff->fastChargePowerKW >= 0 && tariff->fastChargeSurchargePerKWh >= 0.0
        && tariff->sessionFee >= 0.0 && tariff->loyaltyMinSessions >= 1 && tariff->loyaltyWindowSeconds >= 0.0
        && tariff->loyaltyDiscountRate >= 0.0 && tariff->loyaltyDiscountRate <= 1.0;
}

bool isLoyalCustomer(const struct Tariff* tariff, int recentSessions) {
    return recentSessions >= tariff->loyaltyMinSessions;
}

struct Invoice computeInvoice(const struct Tariff* tariff, const struct ChargingSession* session, bool interrupted,
                              int recentSessions) {
    struct Invoice invoice = {
        .vehicleId = session->vehicleId,
        .power = session->power,
        .energyKWh = sessionEnergyKWh(session),
        .durationSeconds = sessionDurationSeconds(session),
        .interrupted = interrupted,
    };

    bool fastCharge = session->power > tariff->fastChargePowerKW;
    invoice.unitPrice = tariff->pricePerKWh + (fastCharge ? tariff->fastChargeSurchargePerKWh : 0.0);
    invoice.energyCost = invoice.energyKWh * invoice.unitPrice;
    // La panne n'est pas du fait du client : seuls les kWh délivrés sont facturés
    invoice.sessionFee = interrupted ? 0.0 : tariff->sessionFee;

    double subtotal = invoice.energyCost + invoice.sessionFee;
    if (isLoyalCustomer(tariff, recentSessions)) {
        invoice.loyaltyDiscount = subtotal * tariff->loyaltyDiscountRate;
    }
    invoice.total = subtotal - invoice.loyaltyDiscount;
    return invoice;
}

void printInvoice(const struct Invoice* invoice) {
    printf("[FACTURE] Véhicule %d : %.2f kWh à %d kW en %.1f min × %.2f €/kWh = %.2f €", invoice->vehicleId,
           invoice->energyKWh, invoice->power, simulatedMinutes(invoice->durationSeconds), invoice->unitPrice,
           invoice->energyCost);
    if (invoice->sessionFee > 0.0) {
        printf(" + %.2f € de frais", invoice->sessionFee);
    }
    if (invoice->loyaltyDiscount > 0.0) {
        printf(" - %.2f € de remise fidélité", invoice->loyaltyDiscount);
    }
    printf(" = %.2f €%s\n", invoice->total, invoice->interrupted ? " (interrompue par une panne)" : "");
}
