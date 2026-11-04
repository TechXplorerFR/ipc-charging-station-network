#ifndef RANDOM_H
#define RANDOM_H

#include <stdint.h>

// Générateur pseudo-aléatoire SplitMix64. Chaque utilisateur (générateur de véhicules, simulateur de pannes)
// possède son propre état : reproductible à graine égale et sans état global partagé entre threads,
// contrairement à rand().

// Graine tirée de l'horloge monotone (jamais nulle)
uint64_t randomClockSeed(void);

uint64_t randomNext(uint64_t* state);

// Réel uniforme dans [0, 1)
double randomUnit(uint64_t* state);

// Réel uniforme dans [min, max)
double randomBetween(uint64_t* state, double min, double max);

// Entier uniforme dans [0, count), count > 0
int randomIndex(uint64_t* state, int count);

// Loi exponentielle de paramètre rate > 0 (moyenne 1 / rate) : délai entre deux événements d'un processus
// de Poisson
double randomExponential(uint64_t* state, double rate);

#endif // RANDOM_H
