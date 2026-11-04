#include "random.h"
#include <math.h>
#include <time.h>

uint64_t randomClockSeed(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t seed = (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
    return seed != 0 ? seed : 1;
}

uint64_t randomNext(uint64_t* state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

double randomUnit(uint64_t* state) {
    return (double)(randomNext(state) >> 11) * 0x1.0p-53;
}

double randomBetween(uint64_t* state, double min, double max) {
    return min + (max - min) * randomUnit(state);
}

int randomIndex(uint64_t* state, int count) {
    return (int)(randomUnit(state) * count);
}

double randomExponential(uint64_t* state, double rate) {
    // Inversion de la fonction de répartition : 1 - U est dans (0, 1], donc le logarithme est fini
    return -log(1.0 - randomUnit(state)) / rate;
}
