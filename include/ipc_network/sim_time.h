#ifndef SIM_TIME_H
#define SIM_TIME_H

// Accélération de la simulation : une seconde réelle représente une minute simulée.
// Les durées sont mesurées en secondes réelles (horloge CLOCK_MONOTONIC) et affichées en minutes simulées :
// une recharge de 1,02 kWh à 22 kW dure 2,8 min simulées, soit 2,8 s réelles.
#define SIMULATED_SECONDS_PER_SECOND 60.0

// Durée simulée (min) correspondant à realSeconds secondes réelles
static inline double simulatedMinutes(double realSeconds) {
    return realSeconds * SIMULATED_SECONDS_PER_SECOND / 60.0;
}

#endif // SIM_TIME_H
