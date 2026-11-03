#include "history.h"
#include "test_utils.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct SessionRecord makeRecord(int vehicleId, int stationId, double energyKWh, double cost, bool interrupted) {
    return (struct SessionRecord){
        .vehicleId = vehicleId,
        .stationId = stationId,
        .chargerId = 0,
        .power = 50,
        .energyKWh = energyKWh,
        .durationSeconds = 12.0,
        .cost = cost,
        .interrupted = interrupted,
    };
}

static bool add(struct History* history, int vehicleId, int stationId, double energyKWh, double cost,
                bool interrupted) {
    struct SessionRecord record = makeRecord(vehicleId, stationId, energyKWh, cost, interrupted);
    return addSessionRecord(history, &record);
}

static void testRecordsAndSummaries(void) {
    struct History history;
    if (!initHistory(&history)) {
        CHECK(false);
        return;
    }
    CHECK(historySize(&history) == 0);
    CHECK(summarizeHistory(&history, ALL_STATIONS).sessions == 0);

    CHECK(add(&history, 1, 0, 10.0, 4.50, false));
    CHECK(add(&history, 2, 1, 20.0, 8.00, false));
    CHECK(add(&history, 1, 0, 5.0, 1.75, true));
    CHECK(historySize(&history) == 3);

    struct SessionRecord record;
    CHECK(getSessionRecord(&history, 1, &record));
    CHECK(record.vehicleId == 2 && record.stationId == 1);
    CHECK(!getSessionRecord(&history, 3, &record));
    CHECK(!getSessionRecord(&history, -1, &record));

    struct HistorySummary network = summarizeHistory(&history, ALL_STATIONS);
    CHECK(network.sessions == 3);
    CHECK(network.interruptedSessions == 1);
    CHECK(network.energyKWh > 34.99 && network.energyKWh < 35.01);
    CHECK(network.revenue > 14.24 && network.revenue < 14.26);

    struct HistorySummary station0 = summarizeHistory(&history, 0);
    CHECK(station0.sessions == 2);
    CHECK(station0.revenue > 6.24 && station0.revenue < 6.26);
    CHECK(summarizeHistory(&history, 5).sessions == 0);

    printHistory(&history, 2);
    destroyHistory(&history);
    destroyHistory(&history); // Sans effet la seconde fois
}

static void testRecentSessionsForLoyalty(void) {
    struct History history;
    if (!initHistory(&history)) {
        CHECK(false);
        return;
    }
    CHECK(add(&history, 1, 0, 10.0, 4.0, false));
    CHECK(add(&history, 2, 0, 10.0, 4.0, false));
    CHECK(add(&history, 1, 0, 10.0, 4.0, false));
    CHECK(countRecentSessions(&history, 1, 600.0) == 2);
    CHECK(countRecentSessions(&history, 2, 600.0) == 1);
    CHECK(countRecentSessions(&history, 3, 600.0) == 0);

    // Les deux premières sessions remontent à 1000 s : hors de la fenêtre
    history.records[0].endedAt.tv_sec -= 1000;
    history.records[1].endedAt.tv_sec -= 1000;
    CHECK(countRecentSessions(&history, 1, 600.0) == 1);
    CHECK(countRecentSessions(&history, 2, 600.0) == 0);

    destroyHistory(&history);
}

static void testGrowsBeyondInitialCapacity(void) {
    struct History history;
    if (!initHistory(&history)) {
        CHECK(false);
        return;
    }
    for (int i = 0; i < 100; i++) {
        CHECK(add(&history, i, i % 3, 1.0, 0.5, false));
    }
    CHECK(historySize(&history) == 100);

    struct SessionRecord record;
    CHECK(getSessionRecord(&history, 99, &record));
    CHECK(record.vehicleId == 99);
    CHECK(summarizeHistory(&history, ALL_STATIONS).sessions == 100);

    destroyHistory(&history);
}

static void testExportCsv(void) {
    struct History history;
    if (!initHistory(&history)) {
        CHECK(false);
        return;
    }
    CHECK(add(&history, 1, 0, 10.0, 4.5, false));
    CHECK(add(&history, 2, 1, 2.5, 0.88, true));

    char path[] = "/tmp/test_history_XXXXXX";
    int fd = mkstemp(path);
    if (fd == -1) {
        CHECK(false);
        destroyHistory(&history);
        return;
    }
    close(fd);
    CHECK(exportHistoryCsv(&history, path));

    FILE* file = fopen(path, "r");
    char line[256];
    int lines = 0;
    while (file != NULL && fgets(line, sizeof(line), file) != NULL) {
        if (lines == 0) {
            CHECK(strncmp(line, "vehicule;station;borne", 22) == 0);
        } else if (lines == 2) {
            CHECK(strcmp(line, "2;1;0;50;2.500;12.0;0.88;1;0\n") == 0);
        }
        lines++;
    }
    CHECK(lines == 3);
    if (file != NULL) {
        fclose(file);
    }
    unlink(path);

    CHECK(!exportHistoryCsv(&history, "/dossier/inexistant/historique.csv"));
    destroyHistory(&history);
}

// Threads qui enregistrent des sessions en même temps
#define WRITER_THREADS 4
#define RECORDS_PER_WRITER 50

static void* writeRecords(void* arg) {
    struct History* history = arg;
    for (int i = 0; i < RECORDS_PER_WRITER; i++) {
        add(history, i, 0, 1.0, 1.0, false);
    }
    return NULL;
}

static void testConcurrentWriters(void) {
    struct History history;
    if (!initHistory(&history)) {
        CHECK(false);
        return;
    }

    pthread_t threads[WRITER_THREADS];
    for (int i = 0; i < WRITER_THREADS; i++) {
        CHECK(pthread_create(&threads[i], NULL, writeRecords, &history) == 0);
    }
    for (int i = 0; i < WRITER_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    CHECK(historySize(&history) == WRITER_THREADS * RECORDS_PER_WRITER);
    struct HistorySummary summary = summarizeHistory(&history, ALL_STATIONS);
    CHECK(summary.revenue > WRITER_THREADS * RECORDS_PER_WRITER - 0.01);

    destroyHistory(&history);
}

int main(void) {
    RUN_TEST(testRecordsAndSummaries);
    RUN_TEST(testRecentSessionsForLoyalty);
    RUN_TEST(testGrowsBeyondInitialCapacity);
    RUN_TEST(testExportCsv);
    RUN_TEST(testConcurrentWriters);
    return TEST_RESULT();
}
