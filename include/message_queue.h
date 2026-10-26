/*
 * message_queue.h -- Contrat du module de communication interprocessus (IPC)
 *
 * ROLE DU MODULE
 * --------------
 * Ce module "encapsule les echanges interprocessus" (cf. sujet SR70).
 * Concretement : les vehicules, la CLI et le simulateur de pannes n'appellent
 * JAMAIS directement msgsnd()/msgrcv(). Ils passent par les fonctions msgq_*
 * declarees ici. Si un jour on change de mecanisme IPC (files POSIX, sockets...),
 * seul message_queue.c change : le reste du projet ne bouge pas.
 *
 * MECANISME CHOISI : files de messages System V
 * ---------------------------------------------
 * Une file de messages est une "boite aux lettres" geree par le NOYAU :
 *   - un processus depose un message (msgsnd),
 *   - un autre le retire (msgrcv), en attendant s'il n'y en a pas encore.
 * Chaque message commence obligatoirement par un champ "long mtype" (> 0).
 * msgrcv() sait filtrer sur ce mtype : c'est ce qui nous sert d'ADRESSE.
 *
 * ARCHITECTURE : DEUX FILES
 * -------------------------
 *
 *   vehicules / CLI / pannes                         serveur
 *        |                                             ^
 *        |   FILE DES REQUETES  (tout le monde -> serveur)
 *        +---------------------------------------------+
 *            mtype = 1 (urgent) ou 2 (normal)
 *
 *   vehicule n°17  <-------------------------------- serveur
 *            FILE DES REPONSES (serveur -> un vehicule precis)
 *            mtype = identifiant du destinataire (ici 17)
 *
 * - File des requetes : le mtype sert de PRIORITE. Le serveur lit avec
 *   msgtyp = -2, ce qui veut dire "donne-moi le plus petit mtype <= 2" :
 *   les messages urgents (pannes, commandes CLI) passent donc avant les
 *   demandes de recharge normales.
 * - File des reponses : le mtype sert d'ADRESSE. Le vehicule 17 lit avec
 *   msgtyp = 17 et ne recoit QUE les messages qui lui sont destines.
 *
 * CONVENTION DE RETOUR (la meme pour toutes les fonctions)
 * --------------------------------------------------------
 *    0               : succes
 *    MSGQ_NO_MESSAGE : pas de message disponible (mode non bloquant / timeout)
 *   -1               : erreur, errno indique la cause (utiliser perror()).
 *                      Cas particuliers a traiter par l'appelant :
 *                      - errno == EIDRM : la file a ete supprimee (le serveur
 *                        s'arrete) -> il faut quitter.
 *                      - errno == EINTR (receptions seulement) : un signal a
 *                        interrompu l'attente -> verifier son drapeau d'arret,
 *                        puis rappeler la fonction si on continue.
 */

#ifndef MESSAGE_QUEUE_H   /* "garde d'inclusion" : empeche que ce fichier soit */
#define MESSAGE_QUEUE_H   /* inclus deux fois dans le meme .c (double definition) */

/* ------------------------------------------------------------------------- */
/* Constantes                                                                */
/* ------------------------------------------------------------------------- */

/*
 * ftok() fabrique une CLE a partir d'un chemin de fichier EXISTANT et d'un
 * caractere. Deux programmes independants (serveur et vehicule) qui appellent
 * ftok() avec les memes arguments obtiennent la meme cle, donc la meme file.
 * /tmp existe toujours sous Linux, c'est pratique.
 */
#define MSGQ_KEY_PATH        "/tmp"
#define MSGQ_REQUEST_PROJ_ID 'R'   /* file des requetes */
#define MSGQ_RESPONSE_PROJ_ID 'A'  /* file des reponses ("Answers") */

/* Priorites dans la file des requetes (= valeur du mtype, toujours > 0). */
#define MSGQ_PRIO_URGENT 1L
#define MSGQ_PRIO_NORMAL 2L

/* Taille du champ texte libre dans un message (taille FIXE, pas de pointeur). */
#define MSGQ_TEXT_LEN 64

/* Valeur de retour "aucun message" (ce n'est pas une erreur). */
#define MSGQ_NO_MESSAGE 1

/* ------------------------------------------------------------------------- */
/* Types de messages                                                         */
/* ------------------------------------------------------------------------- */

/*
 * Le "type metier" du message. Attention a ne pas le confondre avec le mtype
 * System V : le mtype sert a la PRIORITE ou a l'ADRESSE, alors que ce champ
 * dit CE QUE CONTIENT le message. Chaque valeur correspond a une fleche des
 * diagrammes du sujet.
 */
typedef enum {
    /* --- vehicule -> serveur (diagramme 3) --- */
    MSG_CHARGE_REQUEST = 1,     /* demande de recharge */
    MSG_CANCEL_REQUEST,         /* le vehicule abandonne (attente trop longue) */
    MSG_SESSION_END,            /* recharge terminee, la borne peut etre liberee */

    /* --- serveur -> vehicule (diagrammes 3, 4, 6, 7) --- */
    MSG_CHARGER_ASSIGNED,       /* borne attribuee tout de suite */
    MSG_QUEUED,                 /* mis en file d'attente (queue_position renseigne) */
    MSG_REFUSED,                /* refus (pas de borne, pas de file) */
    MSG_ASSIGNED_FROM_QUEUE,    /* borne attribuee apres une attente */
    MSG_SESSION_INTERRUPTED,    /* la borne est tombee en panne pendant la recharge */

    /* --- CLI / simulateur de pannes -> serveur (diagrammes 7, 9) --- */
    MSG_CLI_STATUS,             /* demande d'affichage de l'etat du reseau */
    MSG_CLI_ADD_VEHICLE,        /* ajout manuel d'un vehicule */
    MSG_FAULT_TRIGGER,          /* declencher une panne sur une borne */
    MSG_REPAIR_DONE,            /* fin de reparation d'une borne */
    MSG_SHUTDOWN,               /* arret propre du serveur */

    /* --- serveur -> CLI --- */
    MSG_ACK                     /* accuse de reception generique */
} msgq_kind_t;

/* ------------------------------------------------------------------------- */
/* Structure des messages                                                    */
/* ------------------------------------------------------------------------- */

/*
 * Le CONTENU utile d'un message. Une seule structure pour tous les types :
 * selon "kind", certains champs sont utilises et d'autres restent a 0.
 * C'est plus simple qu'une structure par type et la taille reste fixe.
 *
 * REGLE D'OR : aucun pointeur ici ! Un pointeur est une adresse dans la
 * memoire du processus qui envoie : dans un autre processus, cette adresse
 * ne veut rien dire. On ne met donc que des nombres et des tableaux de taille
 * fixe, qui sont COPIES octet par octet dans la file par le noyau.
 */
typedef struct {
    msgq_kind_t kind;           /* ce que contient le message */
    int    sender_id;           /* qui envoie (id vehicule, PID pour la CLI...),
                                   toujours > 0 : le serveur s'en sert comme
                                   adresse de reponse (recipient_id) */
    int    vehicle_id;          /* vehicule concerne */
    int    station_id;          /* station demandee / concernee */
    int    charger_id;          /* borne attribuee / concernee (-1 si aucune) */
    int    priority;            /* 1 = prioritaire, 0 = normal */
    int    queue_position;      /* position dans la file d'attente (MSG_QUEUED) */
    double max_power_kw;        /* puissance maximale acceptee par le vehicule */
    double battery_capacity_kwh;
    double battery_level;       /* niveau actuel, en pourcentage */
    double energy_kwh;          /* energie delivree (fin / interruption de session) */
    double cost;                /* cout de la session */
    char   text[MSGQ_TEXT_LEN]; /* message libre (raison d'un refus, etc.) */
} msgq_payload_t;

/*
 * Le message tel que le NOYAU le voit : obligatoirement un "long mtype" en
 * premier, puis les donnees. On garde les donnees dans une sous-structure :
 * comme ca, la taille a donner a msgsnd()/msgrcv() est simplement
 * sizeof(msgq_payload_t). (Piege classique : cette taille ne doit PAS
 * inclure le mtype.)
 *
 * Les autres modules n'ont pas besoin de manipuler ce type : les fonctions
 * ci-dessous prennent et rendent un msgq_payload_t, le mtype est gere ici.
 */
typedef struct {
    long           mtype;
    msgq_payload_t payload;
} msgq_message_t;

/* ------------------------------------------------------------------------- */
/* Cycle de vie des files                                                    */
/* ------------------------------------------------------------------------- */

/*
 * Cree les deux files. A appeler UNE fois, par le serveur, au demarrage
 * (diagramme 2 : "Initialiser les files de messages IPC").
 * Si des files d'une execution precedente (qui a plante) existent encore,
 * elles sont supprimees puis recreees vides.
 */
int msgq_server_create(void);

/*
 * Ouvre les deux files deja creees par le serveur. A appeler par chaque
 * processus client (vehicule, CLI, simulateur de pannes) avant d'envoyer.
 * Echoue (errno = ENOENT) si le serveur n'est pas lance : ne cree rien.
 */
int msgq_client_open(void);

/*
 * Supprime les deux files du noyau. A appeler par le serveur a l'arret.
 * Effet de bord voulu : tout processus bloque dans une reception est
 * reveille avec errno = EIDRM, ce qui lui signale de se terminer.
 */
int msgq_destroy(void);

/* ------------------------------------------------------------------------- */
/* Cote clients : vehicule, CLI, simulateur de pannes                        */
/* ------------------------------------------------------------------------- */

/*
 * Envoie une requete au serveur. La priorite (mtype) est deduite de
 * req->kind : pannes, reparations, commandes CLI et arret sont urgents.
 * BLOQUANT si la file est pleine (le client attend qu'il y ait de la place).
 */
int msgq_send_request(const msgq_payload_t *req);

/*
 * Recoit une reponse adressee a recipient_id (en general le vehicle_id).
 *   blocking != 0 : attend jusqu'a l'arrivee d'un message.
 *   blocking == 0 : rend la main tout de suite (MSGQ_NO_MESSAGE si rien).
 * recipient_id doit etre > 0.
 */
int msgq_receive_response(int recipient_id, msgq_payload_t *out, int blocking);

/*
 * Comme msgq_receive_response(), mais attend au plus timeout_ms
 * millisecondes. Rend MSGQ_NO_MESSAGE si rien n'est arrive a temps
 * (diagramme 3 : branche "Reponse recue ? Non").
 */
int msgq_receive_response_timeout(int recipient_id, msgq_payload_t *out,
                                  int timeout_ms);

/* ------------------------------------------------------------------------- */
/* Cote serveur                                                              */
/* ------------------------------------------------------------------------- */

/*
 * Recoit la prochaine requete, les urgentes en premier.
 *   blocking != 0 : attend jusqu'a l'arrivee d'un message.
 *   blocking == 0 : rend MSGQ_NO_MESSAGE si la file est vide.
 */
int msgq_receive_request(msgq_payload_t *out, int blocking);

/*
 * Envoie une reponse au destinataire recipient_id (> 0).
 * NON BLOQUANT : si la file des reponses est pleine, renvoie -1 avec
 * errno = EAGAIN au lieu de figer le serveur (sinon TOUS les vehicules
 * seraient bloques a cause d'un seul qui ne lit pas ses messages).
 */
int msgq_send_response(int recipient_id, const msgq_payload_t *resp);

#endif /* MESSAGE_QUEUE_H */
