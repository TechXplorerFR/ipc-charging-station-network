# Plan — message_queue, allocator, queue_manager (modèle threads)

## Contexte
Répartition de l'équipe :
- véhicules, stations, bornes : `vehicle`, `station`, `charger` ;
- files de messages, allocation, file d'attente : `message_queue`, `allocator`, `queue_manager` ;
- point d'entrée et serveur : `main`, `server`.

Décision d'équipe : **les véhicules sont des threads** (un seul processus). Les files de messages System V restent le canal véhicule ↔ serveur (exigé par le sujet). La file d'attente devient donc une structure **en mémoire**, protégée par le verrou de station existant (`lockStation` / `unlockStation`).

## 1. `message_queue.h/.c`
- Conserver `RequestType` et le champ `request` (utilisés par `server.c`), et ajouter :
  - véhicule → serveur : `REQ_PONG`, `REQ_CANCEL` (abandon de la file) ;
  - serveur → véhicule : `RESP_ASSIGNED`, `RESP_QUEUED`, `RESP_REFUSED`, `RESP_PING`, `RESP_FAULT`.
- Nouveaux champs de `Message` : `estimated_duration` (s), `battery_level` (%), `queue_position`.
- Adressage : `SERVER_MSG_TYPE 1`, `VEHICLE_MSG_TYPE_OFFSET 2`, `long vehicleMsgType(int vehicleId)`.
- `SERVER_QUEUE_KEY` et `msgq_t openServerQueue(void)`.
- `bool sendToVehicle(msgq_t queue, int vehicleId, Message* msg)` : fixe le `mtype` du destinataire.
- `receiveMessage` : relance l'attente sur `EINTR`.
- `ReceiveStatus tryReceiveMessage(...)` : réception non bloquante (`IPC_NOWAIT`) ; `ENOMSG` → `MSG_NONE`, sans message d'erreur.

## 2. `queue_manager.h/.c` (nouveau)
- `struct WaitingVehicle { vehicleId, desiredPower, priority, arrivalTime }`.
- `struct WaitQueue { vehicles[MAX_WAITING_VEHICLES]; count; }`.
- Règle de priorité : priorité décroissante, puis premier arrivé premier servi à priorité égale (insertion triée stable).
- API, à appeler **verrou de la station acquis** :
  `initWaitQueue`, `enqueueVehicle` (false si pleine), `dequeueNextVehicle`, `removeWaitingVehicle`,
  `waitQueueLength`, `waitQueuePosition`, `waitingTimeSeconds`, `printWaitQueue`.

## 3. `allocator.h/.c`
- Choix de la borne : la plus petite puissance ≥ puissance demandée, sinon la plus puissante libre.
- `struct Assignment requestCharger(station, const WaitingVehicle*)` → résultat `ASSIGN_OK` / `ASSIGN_QUEUED` / `ASSIGN_REFUSED`, avec l'indice de la borne ou la position dans la file : recherche et mise en file dans **une seule** section critique (logigramme 4).
- `releaseChargerAndServeQueue` / `repairChargerAndServeQueue` : la borne passe directement au suivant de la file sous le même verrou, sinon elle redevient libre (logigrammes 6 et 7). Résultat : `HANDOVER_INVALID`, `HANDOVER_FREED` ou `HANDOVER_REASSIGNED` (véhicule servi copié dans `*next`).
- `takeChargerOutOfService(station, chargerIndex, status)` : renvoie l'identifiant du véhicule à prévenir, ou `NO_VEHICLE`.
- `cancelWaitingVehicle(station, vehicleId)` : un véhicule quitte la file.
- `assignChargerAtomically` et `releaseCharger` sont supprimées : elles permettaient de prendre ou libérer une borne sans passer par la file d'attente.

## 4. Retouches minimales dans les autres modules
- `charger.h` : champ `int vehicleId` (`-1` si aucun véhicule).
- `station.h/.c` : `msgq_t waitQueue` → `struct WaitQueue waitQueue` ; suppression du paramètre `key_t mqKey` devenu inutile ; `vehicleId = -1` à l'initialisation.
- `server.c` : `initServer` sans clé ; demande, fin de charge et alerte passent par les nouvelles fonctions de l'allocateur ; traitement de `REQ_CANCEL`. Les TODO restants (envoi des réponses aux véhicules, `REQ_PONG`, file de requêtes) restent à la charge du responsable du serveur.

## 5. Tests
`tests/test_message_queue.c`, `tests/test_queue_manager.c`, `tests/test_allocator.c`, enregistrés dans `tests/CMakeLists.txt`.

## Vérification
Compilation et `ctest` sous WSL (`launch/run_win.cmd --test --no-run`), sans aucun warning avec `-Wall -Wextra -Wpedantic`.
