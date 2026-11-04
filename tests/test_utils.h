#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <stdio.h>

// Nombre de vérifications échouées dans le fichier de test courant
static int testFailures = 0;

// Vérification non fatale : affiche l'expression fausse et poursuit le test
#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) {                                                           \
            fprintf(stderr, "%s:%d : échec de « %s »\n", __FILE__, __LINE__, #condition); \
            testFailures++;                                                           \
        }                                                                             \
    } while (0)

// Lance une fonction de test et affiche son nom
#define RUN_TEST(test)              \
    do {                            \
        printf("- %s\n", #test);    \
        test();                     \
    } while (0)

// Code de sortie du programme de test (lu par ctest)
#define TEST_RESULT() (testFailures == 0 ? 0 : 1)

#endif // TEST_UTILS_H
