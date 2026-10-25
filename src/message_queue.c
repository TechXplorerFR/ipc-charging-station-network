/*
 * message_queue.c -- Implementation du module IPC (files de messages System V)
 *
 * Lire d'abord message_queue.h : il explique l'architecture (deux files,
 * role du mtype) et la convention de retour des fonctions.
 *
 * Les 5 appels systeme utilises (voir "man 2 <nom>" sous Linux) :
 *   ftok()   : chemin + caractere  -> CLE (nombre partage entre processus)
 *   msgget() : CLE                 -> IDENTIFIANT de file (cree ou ouvre)
 *   msgsnd() : depose un message dans la file
 *   msgrcv() : retire un message de la file (avec filtrage sur le mtype)
 *   msgctl() : controle la file ; ici seulement IPC_RMID = suppression
 */

/*
 * Doit etre defini AVANT tout #include : active les declarations POSIX/XSI
 * (msgget, nanosleep, clock_gettime...) quand on compile en -std=c11 strict.
 */
#define _XOPEN_SOURCE 700

#include "message_queue.h"

#include <errno.h>      /* errno, EINTR, EEXIST, ENOMSG, EINVAL... */
#include <stddef.h>     /* NULL */
#include <sys/types.h>  /* key_t, ssize_t */
#include <sys/ipc.h>    /* ftok, IPC_CREAT, IPC_EXCL, IPC_NOWAIT, IPC_RMID */
#include <sys/msg.h>    /* msgget, msgsnd, msgrcv, msgctl */
#include <time.h>       /* clock_gettime, nanosleep */

/* ------------------------------------------------------------------------- */
/* Etat interne du module                                                    */
/* ------------------------------------------------------------------------- */

/*
 * Identifiants des deux files dans CE processus (-1 = pas encore ouverte).
 * "static" = visibles uniquement dans ce fichier : les autres modules n'y
 * touchent pas, ils passent par les fonctions msgq_*. C'est l'encapsulation.
 *
 * Remarque threads : ces variables sont ecrites une seule fois (a la
 * creation / ouverture) puis seulement lues. msgsnd/msgrcv etant sures
 * entre threads, plusieurs threads peuvent utiliser le module en parallele,
 * a condition d'avoir appele msgq_server_create() ou msgq_client_open()
 * AVANT de lancer les threads.
 */
static int g_request_qid  = -1;
static int g_response_qid = -1;

/* Droits d'acces de la file, comme pour un fichier : 0600 = lecture et
 * ecriture pour le meme utilisateur seulement. Tous nos processus tournent
 * sous le meme utilisateur, c'est suffisant (et plus sur que 0666). */
#define MSGQ_PERMISSIONS 0600

/* Pause entre deux essais dans msgq_receive_response_timeout(). */
#define MSGQ_POLL_INTERVAL_MS 10

/* ------------------------------------------------------------------------- */
/* Fonctions internes (static = privees a ce fichier)                        */
/* ------------------------------------------------------------------------- */

/*
 * Obtient l'identifiant d'une file a partir de son proj_id.
 * flags = 0           : ouvrir une file existante (echec ENOENT sinon)
 * flags = IPC_CREAT.. : creer la file
 */
static int get_queue(char proj_id, int flags)
{
    /* ETAPE 1 : fabriquer la cle. Le serveur et les clients font le meme
     * calcul avec les memes arguments, donc obtiennent la meme cle. */
    key_t key = ftok(MSGQ_KEY_PATH, proj_id);
    if (key == -1) {
        return -1;              /* errno positionne par ftok */
    }

    /* ETAPE 2 : demander au noyau l'identifiant de la file associee. */
    return msgget(key, flags | MSGQ_PERMISSIONS);
}

/*
 * Cree une file NEUVE et vide. Gere le cas ou une vieille file existe
 * encore parce qu'une execution precedente a plante sans faire le menage :
 * les files System V survivent a la fin des processus !
 */
static int create_fresh_queue(char proj_id)
{
    /* IPC_CREAT | IPC_EXCL = "cree-la, et echoue si elle existe deja".
     * Sans IPC_EXCL, on recupererait silencieusement la vieille file avec
     * les vieux messages encore dedans. */
    int qid = get_queue(proj_id, IPC_CREAT | IPC_EXCL);
    if (qid != -1) {
        return qid;             /* cas normal : rien n'existait */
    }
    if (errno != EEXIST) {
        return -1;              /* vraie erreur (droits, limite systeme...) */
    }

    /* Une vieille file existe : on l'ouvre, on la supprime... */
    int old_qid = get_queue(proj_id, 0);
    if (old_qid == -1) {
        return -1;
    }
    if (msgctl(old_qid, IPC_RMID, NULL) == -1) {
        return -1;
    }

    /* ... et on recommence la creation. */
    return get_queue(proj_id, IPC_CREAT | IPC_EXCL);
}

/*
 * Les messages qui doivent passer AVANT les demandes de recharge :
 * on ne veut pas qu'un arret ou une panne attende derriere 200 vehicules.
 */
static long priority_of(msgq_kind_t kind)
{
    switch (kind) {
    case MSG_FAULT_TRIGGER:
    case MSG_REPAIR_DONE:
    case MSG_CLI_STATUS:
    case MSG_CLI_ADD_VEHICLE:
    case MSG_SHUTDOWN:
        return MSGQ_PRIO_URGENT;
    default:
        return MSGQ_PRIO_NORMAL;
    }
}

/*
 * Envoi generique : emballe le payload dans un msgq_message_t (en ajoutant
 * le mtype) et le depose dans la file qid.
 * flags = 0 (bloquant si file pleine) ou IPC_NOWAIT (EAGAIN si file pleine).
 */
static int send_message(int qid, long mtype, const msgq_payload_t *payload,
                        int flags)
{
    /* Le noyau refuse un mtype <= 0 : on le verifie nous-memes pour donner
     * une erreur claire plutot qu'un comportement surprenant. */
    if (payload == NULL || mtype <= 0) {
        errno = EINVAL;
        return -1;
    }

    msgq_message_t msg;
    msg.mtype   = mtype;
    msg.payload = *payload;     /* copie de la structure entiere */

    /*
     * PIEGE N°1 : la taille passee a msgsnd est celle des DONNEES, SANS le
     * champ mtype. D'ou sizeof(msg.payload) et pas sizeof(msg).
     *
     * Boucle EINTR : si un signal arrive pendant que msgsnd attend de la
     * place, l'appel est interrompu (errno = EINTR) sans rien envoyer.
     * Ce n'est pas une vraie erreur : on recommence simplement.
     */
    for (;;) {
        if (msgsnd(qid, &msg, sizeof(msg.payload), flags) == 0) {
            return 0;
        }
        if (errno != EINTR) {
            return -1;          /* EAGAIN (file pleine), EIDRM, EINVAL... */
        }
    }
}

/*
 * Reception generique depuis la file qid.
 *
 * msgtyp choisit QUEL message on retire (voir "man 2 msgrcv") :
 *   msgtyp > 0  : le premier message dont le mtype vaut exactement msgtyp
 *                 -> sert d'ADRESSE (un vehicule ne lit que ses reponses)
 *   msgtyp < 0  : le message de plus petit mtype parmi ceux <= |msgtyp|
 *                 -> sert de PRIORITE (les urgents d'abord)
 *   msgtyp == 0 : le premier message de la file, quel que soit son mtype
 */
static int receive_message(int qid, long msgtyp, msgq_payload_t *out,
                           int blocking)
{
    if (out == NULL) {
        errno = EINVAL;
        return -1;
    }

    msgq_message_t msg;
    int flags = blocking ? 0 : IPC_NOWAIT;

    /* Meme piege qu'a l'envoi : taille des donnees seulement. */
    ssize_t n = msgrcv(qid, &msg, sizeof(msg.payload), msgtyp, flags);

    if (n == (ssize_t)sizeof(msg.payload)) {
        *out = msg.payload;     /* on rend seulement les donnees utiles */
        return 0;
    }
    if (n >= 0) {
        /* Message de taille inattendue : quelqu'un a ecrit dans notre file
         * avec un autre format. On le signale comme une erreur. */
        errno = EBADMSG;
        return -1;
    }
    if (errno == ENOMSG) {
        /* En mode IPC_NOWAIT, "file vide" se traduit par ENOMSG.
         * Ce n'est pas une erreur : on le signale avec une valeur dediee. */
        return MSGQ_NO_MESSAGE;
    }

    /*
     * Ici, contrairement a l'envoi, on NE recommence PAS sur EINTR : on rend
     * -1 avec errno = EINTR a l'appelant. Pourquoi ? Pour l'arret propre.
     * Le serveur passe sa vie bloque dans msgrcv. Quand on fait Ctrl+C, son
     * gestionnaire de SIGINT leve un drapeau "il faut s'arreter", et msgrcv
     * est interrompu. Si on relancait msgrcv ici, le serveur se rendormirait
     * sans jamais voir le drapeau. En rendant la main, la boucle principale
     * peut tester le drapeau, puis rappeler la reception si besoin.
     * (Valable si le gestionnaire est installe par sigaction SANS SA_RESTART.)
     */
    return -1;                  /* EINTR, EIDRM (file supprimee), EINVAL... */
}

/* Temps ecoule en millisecondes depuis *start (horloge monotone). */
static long elapsed_ms(const struct timespec *start)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start->tv_sec) * 1000L
         + (now.tv_nsec - start->tv_nsec) / 1000000L;
}

/* ------------------------------------------------------------------------- */
/* Cycle de vie des files                                                    */
/* ------------------------------------------------------------------------- */

int msgq_server_create(void)
{
    int request_qid = create_fresh_queue(MSGQ_REQUEST_PROJ_ID);
    if (request_qid == -1) {
        return -1;
    }

    int response_qid = create_fresh_queue(MSGQ_RESPONSE_PROJ_ID);
    if (response_qid == -1) {
        /* On ne laisse pas une file a moitie creee trainer dans le noyau.
         * msgctl peut modifier errno : on sauvegarde la cause d'origine
         * pour que l'appelant voie la vraie erreur. */
        int saved_errno = errno;
        msgctl(request_qid, IPC_RMID, NULL);
        errno = saved_errno;
        return -1;
    }

    /* On ne met a jour l'etat global qu'une fois TOUT reussi. */
    g_request_qid  = request_qid;
    g_response_qid = response_qid;
    return 0;
}

int msgq_client_open(void)
{
    /* flags = 0 : on ouvre SANS creer. Si le serveur n'est pas lance,
     * msgget echoue avec ENOENT : le client sait qu'il ne sert a rien
     * d'envoyer une demande. */
    int request_qid = get_queue(MSGQ_REQUEST_PROJ_ID, 0);
    if (request_qid == -1) {
        return -1;
    }

    int response_qid = get_queue(MSGQ_RESPONSE_PROJ_ID, 0);
    if (response_qid == -1) {
        return -1;
    }

    g_request_qid  = request_qid;
    g_response_qid = response_qid;
    return 0;
}

int msgq_destroy(void)
{
    int result = 0;

    /* On essaie de supprimer les deux files meme si la premiere echoue :
     * il vaut mieux en nettoyer une que zero. */
    if (g_request_qid != -1 && msgctl(g_request_qid, IPC_RMID, NULL) == -1) {
        result = -1;
    }
    if (g_response_qid != -1 && msgctl(g_response_qid, IPC_RMID, NULL) == -1) {
        result = -1;
    }

    g_request_qid  = -1;
    g_response_qid = -1;
    return result;
}

/* ------------------------------------------------------------------------- */
/* Cote clients                                                              */
/* ------------------------------------------------------------------------- */

int msgq_send_request(const msgq_payload_t *req)
{
    if (req == NULL) {
        errno = EINVAL;
        return -1;
    }
    /* flags = 0 : bloquant. Un vehicule peut se permettre d'attendre que
     * le serveur ait vide un peu la file. */
    return send_message(g_request_qid, priority_of(req->kind), req, 0);
}

int msgq_receive_response(int recipient_id, msgq_payload_t *out, int blocking)
{
    if (recipient_id <= 0) {
        errno = EINVAL;
        return -1;
    }
    /* msgtyp = recipient_id (> 0) : on ne retire QUE les messages dont le
     * mtype est notre identifiant. Les reponses des autres restent dans la
     * file pour leurs destinataires. */
    return receive_message(g_response_qid, recipient_id, out, blocking);
}

int msgq_receive_response_timeout(int recipient_id, msgq_payload_t *out,
                                  int timeout_ms)
{
    if (timeout_ms < 0) {
        errno = EINVAL;
        return -1;
    }

    /*
     * Les files System V n'ont pas de "msgrcv avec delai". Solution simple
     * choisie ici : le "polling". On essaie en non bloquant, et si rien
     * n'est arrive on dort un peu (10 ms) avant de reessayer, jusqu'a
     * depasser le delai.
     * Alternative possible : alarm() + reception bloquante interrompue par
     * SIGALRM (EINTR). Plus precis, mais un seul alarm() par processus et
     * plus delicat avec des threads : le polling est plus robuste ici.
     *
     * CLOCK_MONOTONIC plutot que l'heure "murale" : elle ne recule jamais,
     * meme si quelqu'un change l'heure du systeme pendant l'attente.
     */
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    const struct timespec pause = { 0, MSGQ_POLL_INTERVAL_MS * 1000000L };

    for (;;) {
        int rc = msgq_receive_response(recipient_id, out, 0);
        if (rc != MSGQ_NO_MESSAGE) {
            return rc;          /* message recu (0) ou erreur (-1) */
        }
        if (elapsed_ms(&start) >= timeout_ms) {
            return MSGQ_NO_MESSAGE;     /* delai depasse */
        }
        nanosleep(&pause, NULL);
    }
}

/* ------------------------------------------------------------------------- */
/* Cote serveur                                                              */
/* ------------------------------------------------------------------------- */

int msgq_receive_request(msgq_payload_t *out, int blocking)
{
    /* msgtyp = -2 : "le plus petit mtype <= 2". Les messages urgents
     * (mtype 1) sortent donc avant les normaux (mtype 2), et a priorite
     * egale l'ordre d'arrivee (FIFO) est respecte. */
    return receive_message(g_request_qid, -MSGQ_PRIO_NORMAL, out, blocking);
}

int msgq_send_response(int recipient_id, const msgq_payload_t *resp)
{
    /* IPC_NOWAIT : le serveur ne doit jamais rester bloque a cause d'un
     * seul destinataire. Si la file est pleine, errno = EAGAIN et c'est au
     * serveur de decider (journaliser, reessayer plus tard...). */
    return send_message(g_response_qid, recipient_id, resp, IPC_NOWAIT);
}
