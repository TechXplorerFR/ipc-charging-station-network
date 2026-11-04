#include "random.h"
#include "test_utils.h"
#include <stdbool.h>

static void testSameSeedGivesSameSequence(void) {
    uint64_t first = 123;
    uint64_t second = 123;
    uint64_t other = 124;
    bool differs = false;
    for (int i = 0; i < 100; i++) {
        uint64_t value = randomNext(&first);
        CHECK(value == randomNext(&second));
        differs = differs || value != randomNext(&other);
    }
    CHECK(differs);
    CHECK(randomClockSeed() != 0);
}

static void testDrawsStayInBounds(void) {
    uint64_t state = 42;
    bool indexSeen[5] = { false, false, false, false, false };
    for (int i = 0; i < 10000; i++) {
        double unit = randomUnit(&state);
        CHECK(unit >= 0.0 && unit < 1.0);

        double between = randomBetween(&state, -2.0, 3.0);
        CHECK(between >= -2.0 && between < 3.0);

        int index = randomIndex(&state, 5);
        CHECK(index >= 0 && index < 5);
        if (index >= 0 && index < 5) {
            indexSeen[index] = true;
        }
    }
    for (int i = 0; i < 5; i++) {
        CHECK(indexSeen[i]);
    }
}

static void testExponentialMean(void) {
    uint64_t state = 7;
    // Moyenne attendue : 1 / λ = 0,25
    const int samples = 50000;
    double sum = 0.0;
    for (int i = 0; i < samples; i++) {
        double delay = randomExponential(&state, 4.0);
        CHECK(delay >= 0.0);
        sum += delay;
    }
    double mean = sum / samples;
    CHECK(mean > 0.24 && mean < 0.26);
}

int main(void) {
    RUN_TEST(testSameSeedGivesSameSequence);
    RUN_TEST(testDrawsStayInBounds);
    RUN_TEST(testExponentialMean);
    return TEST_RESULT();
}
