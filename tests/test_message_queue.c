/*
 * test_message_queue.c -- Tests du module message_queue
 *
 * Lancement :  make test
 *
 * PRINCIPE
 * --------
 * Ce programme joue a la fois le role du SERVEUR et des VEHICULES :
 *   - le processus principal est le "serveur" (il cree les files) ;
 *   - fork() cree des processus fils qui jouent les "vehicules".
 * Comme dans le vrai projet, ce sont donc bien des PROCESSUS DIFFERENTS
 * qui communiquent par les files du noyau.
 *
 * Chaque test est une fonction qui renvoie 0 (reussi) ou 1 (echoue).
 * Un fils "vehicule" signale son resultat par son CODE DE SORTIE
 * (0 = tout va bien), que le pere recupere avec waitpid().
 *
 * On n'utilise QUE les fonctions de message_queue.h, comme le feront les
 * autres modules : si ces tests passent, le contrat est respecte.
 */

#define _XOPEN_SOURCE 700       /* fork, waitpid, alarm, sleep... */

#include "message_queue.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* ------------------------------------------------------------------------- */
/* Mini "framework" de test                                                  */
/* ------------------------------------------------------------------------- */

/*
 * CHECK(condition) : si la condition est fausse, affiche le fichier, la
 * ligne, la condition et errno, puis fait echouer le test en cours.
 * "#cond" transforme l'expression en chaine de caracteres (operateur # du
 * preprocesseur) : on voit exactement quelle verification a rate.
 * Le do { } while (0) permet d'utiliser la macro comme une instruction
 * normale, suivie d'un ";", meme dans un if sans accolades.
 */
#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("    ECHEC %s:%d : %s (errno = %s)\n",                   \
                   __FILE__, __LINE__, #cond, strerror(errno));             \
            return 1;                                                       \
        }                                                                   \
    } while (0)

/* Fabrique une demande de recharge du vehicule "id". Le " = {0}" met tous
 * les champs a zero : pas de valeurs aleatoires envoyees dans la file. */
static msgq_payload_t make_request(msgq_kind_t kind, int id)
{
    msgq_payload_t p = {0};
    p.kind = kind;
    p.sender_id = id;
    p.vehicle_id = id;
    p.station_id = 1;
    p.max_power_kw = 50.0;
    return p;
}

/*
 * Attend la fin de n processus fils et verifie qu'ils ont TOUS reussi.
 * WIFEXITED : le fils s'est termine normalement (pas tue par un signal).
 * WEXITSTATUS : son code de sortie (l'argument de _exit).
 */
static int wait_children(int n)
{
    int all_ok = 1;
    for (int i = 0; i < n; i++) {
        int status;
        if (wait(&status) == -1) {
            return 0;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            printf("    un fils a echoue (code %d)\n",
                   WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            all_ok = 0;
        }
    }
    return all_ok;
}

/* ------------------------------------------------------------------------- */
/* Tests                                                                     */
/* ------------------------------------------------------------------------- */

/*
 * TEST 1 : un client ne peut pas ouvrir les files si le serveur ne les a
 * pas creees (msgq_client_open ne doit jamais creer de file).
 */
static int test_open_without_server(void)
{
    /* On cree puis detruit, pour etre sur de partir d'un etat propre meme
     * si une execution precedente a plante en laissant des files. */
    CHECK(msgq_server_create() == 0);
    CHECK(msgq_destroy() == 0);

    CHECK(msgq_client_open() == -1);
    CHECK(errno == ENOENT);     /* "No such file or directory" : pas de file */
    return 0;
}

/*
 * TEST 2 : une creation remplace une vieille file. On simule un serveur
 * qui a plante : il reste un message dans la file. Une nouvelle creation
 * doit repartir d'une file VIDE.
 */
static int test_create_replaces_stale_queue(void)
{
    CHECK(msgq_server_create() == 0);
    msgq_payload_t req = make_request(MSG_CHARGE_REQUEST, 1);
    CHECK(msgq_send_request(&req) == 0);

    /* "Crash" : on ne detruit pas, on recree directement. */
    CHECK(msgq_server_create() == 0);

    msgq_payload_t out;
    CHECK(msgq_receive_request(&out, 0) == MSGQ_NO_MESSAGE);
    return 0;
}

/*
 * TEST 3 : un aller-retour simple, dans le meme processus.
 * Verifie que le contenu arrive intact (copie octet par octet).
 */
static int test_round_trip(void)
{
    msgq_payload_t req = make_request(MSG_CHARGE_REQUEST, 7);
    strcpy(req.text, "bonjour serveur");
    CHECK(msgq_send_request(&req) == 0);

    msgq_payload_t got;
    CHECK(msgq_receive_request(&got, 1) == 0);
    CHECK(got.kind == MSG_CHARGE_REQUEST);
    CHECK(got.vehicle_id == 7);
    CHECK(got.max_power_kw == 50.0);
    CHECK(strcmp(got.text, "bonjour serveur") == 0);

    msgq_payload_t resp = {0};
    resp.kind = MSG_CHARGER_ASSIGNED;
    resp.charger_id = 3;
    CHECK(msgq_send_response(got.sender_id, &resp) == 0);

    msgq_payload_t answer;
    CHECK(msgq_receive_response(7, &answer, 1) == 0);
    CHECK(answer.kind == MSG_CHARGER_ASSIGNED);
    CHECK(answer.charger_id == 3);
    return 0;
}

/*
 * TEST 4 : priorites. On envoie une demande NORMALE puis un arret URGENT.
 * Le serveur doit recevoir l'arret en premier, alors qu'il est arrive
 * en dernier (grace a msgtyp = -2 dans msgq_receive_request).
 */
static int test_urgent_first(void)
{
    msgq_payload_t normal = make_request(MSG_CHARGE_REQUEST, 1);
    msgq_payload_t urgent = make_request(MSG_SHUTDOWN, 2);
    CHECK(msgq_send_request(&normal) == 0);
    CHECK(msgq_send_request(&urgent) == 0);

    msgq_payload_t got;
    CHECK(msgq_receive_request(&got, 1) == 0);
    CHECK(got.kind == MSG_SHUTDOWN);
    CHECK(msgq_receive_request(&got, 1) == 0);
    CHECK(got.kind == MSG_CHARGE_REQUEST);

    /* Plus rien : en non bloquant, on doit avoir MSGQ_NO_MESSAGE. */
    CHECK(msgq_receive_request(&got, 0) == MSGQ_NO_MESSAGE);
    return 0;
}

/*
 * TEST 5 : routage avec plusieurs vehicules (le test le plus important).
 * N fils envoient chacun une demande puis attendent LEUR reponse. Le
 * serveur repond a chacun avec une borne differente (id * 100). Chaque
 * fils verifie qu'il a recu la sienne et pas celle d'un autre.
 *
 * Pourquoi pas 1000 vehicules ? Une file a une capacite limitee
 * (/proc/sys/kernel/msgmnb, souvent 16 Ko). Avec ~140 octets par message,
 * 50 reponses tiennent sans probleme ; au-dela, msgq_send_response (non
 * bloquant) pourrait renvoyer EAGAIN si les fils lisent trop lentement.
 */
#define NB_VEHICLES 50

static int test_routing_many_vehicles(void)
{
    for (int id = 1; id <= NB_VEHICLES; id++) {
        pid_t pid = fork();
        CHECK(pid != -1);

        if (pid == 0) {
            /* ===== Code du FILS (un vehicule) =====
             * Les identifiants de file sont herites du pere, mais un vrai
             * vehicule (programme separe) devrait ouvrir les files : on le
             * fait pour etre realiste.
             * On sort avec _exit() et pas exit() : dans un fils issu de
             * fork(), _exit() evite de vider une deuxieme fois les tampons
             * de printf herites du pere (affichages en double). */
            if (msgq_client_open() != 0) _exit(10);

            msgq_payload_t req = make_request(MSG_CHARGE_REQUEST, id);
            if (msgq_send_request(&req) != 0) _exit(11);

            msgq_payload_t answer;
            if (msgq_receive_response(id, &answer, 1) != 0) _exit(12);
            if (answer.vehicle_id != id) _exit(13);
            if (answer.charger_id != id * 100) _exit(14);
            _exit(0);
        }
        /* ===== Le PERE continue la boucle et cree le fils suivant ===== */
    }

    /* Le pere joue le serveur : il traite les N demandes dans l'ordre
     * ou elles arrivent (forcement melange, les fils tournent en parallele). */
    for (int i = 0; i < NB_VEHICLES; i++) {
        msgq_payload_t req;
        CHECK(msgq_receive_request(&req, 1) == 0);

        msgq_payload_t resp = {0};
        resp.kind = MSG_CHARGER_ASSIGNED;
        resp.vehicle_id = req.vehicle_id;
        resp.charger_id = req.vehicle_id * 100;
        /* L'adresse de reponse est le sender_id de la demande. */
        CHECK(msgq_send_response(req.sender_id, &resp) == 0);
    }

    CHECK(wait_children(NB_VEHICLES));
    return 0;
}

/*
 * TEST 6 : reception avec delai. Personne n'ecrit au vehicule 42 : on doit
 * recuperer MSGQ_NO_MESSAGE apres environ 200 ms, sans bloquer pour toujours.
 */
static int test_timeout(void)
{
    msgq_payload_t out;
    CHECK(msgq_receive_response_timeout(42, &out, 200) == MSGQ_NO_MESSAGE);

    /* Et si un message est deja la, il est rendu tout de suite. */
    msgq_payload_t resp = {0};
    resp.kind = MSG_QUEUED;
    resp.queue_position = 3;
    CHECK(msgq_send_response(42, &resp) == 0);
    CHECK(msgq_receive_response_timeout(42, &out, 200) == 0);
    CHECK(out.queue_position == 3);
    return 0;
}

/*
 * TEST 7 : parametres invalides. Un mtype <= 0 est interdit par le noyau :
 * notre module doit refuser proprement avec EINVAL.
 */
static int test_invalid_arguments(void)
{
    msgq_payload_t p = {0};
    CHECK(msgq_send_response(0, &p) == -1 && errno == EINVAL);
    CHECK(msgq_send_response(-5, &p) == -1 && errno == EINVAL);
    CHECK(msgq_receive_response(0, &p, 0) == -1 && errno == EINVAL);
    CHECK(msgq_send_request(NULL) == -1 && errno == EINVAL);
    CHECK(msgq_receive_request(NULL, 0) == -1 && errno == EINVAL);
    return 0;
}

/*
 * TEST 8 : arret du serveur pendant qu'un vehicule attend. Le fils se
 * bloque dans une reception ; le pere detruit les files ; le fils doit
 * etre reveille avec errno = EIDRM ("identifier removed").
 */
static int test_destroy_wakes_waiting_vehicle(void)
{
    pid_t pid = fork();
    CHECK(pid != -1);

    if (pid == 0) {
        msgq_payload_t out;
        int rc = msgq_receive_response(99, &out, 1);   /* bloque ici */
        _exit(rc == -1 && errno == EIDRM ? 0 : 20);
    }

    /* On laisse au fils le temps d'arriver dans msgrcv. Un sleep dans un
     * test n'est pas parfait (c'est une "course"), mais 1 s est largement
     * suffisant. Si le fils n'etait pas encore bloque, il trouverait la
     * file deja supprimee (EINVAL) et le test le signalerait. */
    sleep(1);
    CHECK(msgq_destroy() == 0);
    CHECK(wait_children(1));
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Programme principal                                                       */
/* ------------------------------------------------------------------------- */

/* Associe un nom lisible a chaque fonction de test (pointeur de fonction). */
typedef struct {
    const char *name;
    int (*run)(void);
} test_case_t;

int main(void)
{
    /*
     * Garde-fou : si un test reste bloque (par exemple un msgrcv qui
     * n'est jamais servi), SIGALRM tue le programme au bout de 30 s au
     * lieu de le laisser fige pour toujours.
     */
    alarm(30);

    /* Les tests 1 et 2 gerent eux-memes la creation des files.
     * Les tests suivants supposent que les files existent (creees au 2). */
    const test_case_t tests[] = {
        { "ouverture sans serveur",       test_open_without_server },
        { "recreation d'une vieille file", test_create_replaces_stale_queue },
        { "aller-retour simple",          test_round_trip },
        { "urgents traites en premier",   test_urgent_first },
        { "routage vers 50 vehicules",    test_routing_many_vehicles },
        { "reception avec delai",         test_timeout },
        { "arguments invalides",          test_invalid_arguments },
        { "arret pendant une attente",    test_destroy_wakes_waiting_vehicle },
    };
    const int nb_tests = (int)(sizeof(tests) / sizeof(tests[0]));

    int failures = 0;
    for (int i = 0; i < nb_tests; i++) {
        printf("[%d/%d] %s\n", i + 1, nb_tests, tests[i].name);
        /* Vider le tampon de printf AVANT un eventuel fork() dans le test,
         * sinon le texte non encore affiche serait duplique dans les fils. */
        fflush(stdout);
        if (tests[i].run() != 0) {
            failures++;
        }
    }

    /* Quoi qu'il arrive, on ne laisse pas de files dans le noyau.
     * (Ne fait rien si le test 8 les a deja supprimees.) */
    msgq_destroy();

    if (failures == 0) {
        printf("OK : %d tests reussis\n", nb_tests);
        return 0;
    }
    printf("ECHEC : %d test(s) sur %d\n", failures, nb_tests);
    return 1;
}
