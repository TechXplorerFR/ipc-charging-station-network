#include "billing.h"
#include "test_utils.h"

#define SECONDS_PER_HOUR 3600.0

static bool closeTo(double value, double expected) {
    return value > expected - 0.001 && value < expected + 0.001;
}

// Session terminée de power kW pendant hours heures simulées
static struct ChargingSession makeSession(int power, double hours) {
    struct ChargingSession session = { .vehicleId = 7, .desiredPower = power, .power = power, .ended = true };
    session.end.tv_sec = 10000;
    session.start.tv_sec = 10000 - (time_t)(hours * SECONDS_PER_HOUR / SIMULATED_SECONDS_PER_SECOND);
    return session;
}

static void testDefaultTariffIsValid(void) {
    CHECK(isValidTariff(&DEFAULT_TARIFF));

    struct Tariff tariff = DEFAULT_TARIFF;
    tariff.pricePerKWh = -0.1;
    CHECK(!isValidTariff(&tariff));
    tariff = DEFAULT_TARIFF;
    tariff.loyaltyDiscountRate = 1.5;
    CHECK(!isValidTariff(&tariff));
    tariff = DEFAULT_TARIFF;
    tariff.loyaltyMinSessions = 0;
    CHECK(!isValidTariff(&tariff));
}

static void testStandardSession(void) {
    // 50 kW pendant 1 h = 50 kWh à 0,35 € + 1 € de frais
    struct ChargingSession session = makeSession(50, 1.0);
    struct Invoice invoice = computeInvoice(&DEFAULT_TARIFF, &session, false, 0);

    CHECK(invoice.vehicleId == 7);
    CHECK(closeTo(invoice.energyKWh, 50.0));
    CHECK(closeTo(invoice.unitPrice, 0.35));
    CHECK(closeTo(invoice.energyCost, 17.5));
    CHECK(closeTo(invoice.sessionFee, 1.0));
    CHECK(invoice.loyaltyDiscount == 0.0);
    CHECK(closeTo(invoice.total, 18.5));
    CHECK(!invoice.interrupted);
    printInvoice(&invoice);
}

static void testFastChargeSurcharge(void) {
    // 150 kW pendant 20 min = 50 kWh à 0,50 €
    struct ChargingSession session = makeSession(150, 1.0 / 3.0);
    struct Invoice invoice = computeInvoice(&DEFAULT_TARIFF, &session, false, 0);

    CHECK(closeTo(invoice.unitPrice, 0.50));
    CHECK(closeTo(invoice.energyCost, 25.0));
    CHECK(closeTo(invoice.total, 26.0));
}

static void testInterruptedSessionWaivesFee(void) {
    struct ChargingSession session = makeSession(50, 0.5);
    struct Invoice invoice = computeInvoice(&DEFAULT_TARIFF, &session, true, 0);

    CHECK(invoice.interrupted);
    CHECK(invoice.sessionFee == 0.0);
    CHECK(closeTo(invoice.total, 8.75));
    printInvoice(&invoice);
}

static void testLoyaltyDiscount(void) {
    struct ChargingSession session = makeSession(50, 1.0);

    // Une seule session récente : pas encore fidèle
    CHECK(computeInvoice(&DEFAULT_TARIFF, &session, false, 1).loyaltyDiscount == 0.0);

    struct Invoice invoice = computeInvoice(&DEFAULT_TARIFF, &session, false, 2);
    CHECK(closeTo(invoice.loyaltyDiscount, 1.85));
    CHECK(closeTo(invoice.total, 16.65));
    printInvoice(&invoice);
}

static void testCustomTariff(void) {
    struct Tariff tariff = DEFAULT_TARIFF;
    tariff.pricePerKWh = 0.20;
    tariff.sessionFee = 0.0;
    tariff.fastChargeSurchargePerKWh = 0.0;

    struct ChargingSession session = makeSession(150, 1.0 / 3.0);
    struct Invoice invoice = computeInvoice(&tariff, &session, false, 0);
    CHECK(closeTo(invoice.total, 10.0));
}

int main(void) {
    RUN_TEST(testDefaultTariffIsValid);
    RUN_TEST(testStandardSession);
    RUN_TEST(testFastChargeSurcharge);
    RUN_TEST(testInterruptedSessionWaivesFee);
    RUN_TEST(testLoyaltyDiscount);
    RUN_TEST(testCustomTariff);
    return TEST_RESULT();
}
