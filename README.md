# Réseau de bornes de recharge

## Lancer la simulation

Sous Linux (ou WSL), lancez depuis la racine du projet :

```sh
./launch/run_linux.sh
```

Le scénario `standard` démarre deux stations, deux bornes par station et huit
véhicules. Pour afficher les scénarios disponibles :

```sh
./launch/run_linux.sh -- --list
```

Choisissez un scénario en passant son nom après `--` :

```sh
./launch/run_linux.sh -- petit
./launch/run_linux.sh -- affluence
./launch/run_linux.sh -- panne
```

`petit` utilise une station et trois bornes pour trois véhicules.
`affluence` rassemble six véhicules autour d'une seule borne et montre une
annulation en file d'attente. `panne` utilise deux stations, huit véhicules,
une annulation et une panne suivie d'une réparation. Les demandes sont traitées
en parallèle et un rapport du réseau est affiché à la fin.

### Scénarios de circulation aléatoire

Les scénarios suivants utilisent 3 stations de 4 bornes. Les véhicules y
arrivent selon un processus de Poisson (type, batterie, station et priorité
tirés au hasard) et certains simulent des pannes aléatoires avec réparation :

| Scénario | Situation |
|---|---|
| `pointe` | heure de pointe : 40 véhicules à 3 arrivées/s, pannes rares |
| `weekend` | week-end : 16 véhicules espacés, recharges plus longues |
| `panne-generale` | jusqu'à 8 bornes sur 12 en panne en même temps |
| `massive` | arrivée massive : 60 véhicules en quelques secondes |

Les tirages sont reproductibles : chaque scénario a sa graine, que `--seed`
remplace pour obtenir une autre circulation.

```sh
./launch/run_linux.sh -- pointe
./launch/run_linux.sh -- pointe --seed 12
```

Pour comparer l'effet de la charge et des pannes sur les performances,
`--compare` joue tous ces scénarios l'un après l'autre (environ 3 minutes).
Chacun s'affiche comme une exécution classique (journal, rapport du réseau,
bilan des arrivées et des pannes), suivi de sa ligne de résultat. Un tableau
final les compare : véhicules rechargés et refusés, pannes, recharges
interrompues, attente moyenne, taux d'occupation, chiffre d'affaires.
`--quiet` n'affiche que ce tableau.

Avec `--all`, la comparaison joue aussi les 4 scénarios scriptés (`standard`,
`panne`, `affluence`, `petit`), marqués « scripté » dans la colonne Type. Leurs
réseaux et leurs véhicules diffèrent : ils démontrent un mécanisme (file,
annulation, panne) et leurs chiffres ne se comparent pas directement à ceux
des scénarios aléatoires, qui partagent tous le même réseau.

```sh
./launch/run_linux.sh -- --compare
./launch/run_linux.sh -- --compare --quiet
./launch/run_linux.sh -- --compare --all
```

### Ajouter un scénario

Créez un fichier dans `src/scenarios/` avec sa configuration de stations, de
véhicules et d'événements scriptés, et/ou une configuration d'arrivées
aléatoires (`struct GeneratorConfig`) et de pannes (`struct
FaultSimulatorConfig`). Ajoutez-le ensuite à `src/CMakeLists.txt` et
enregistrez-le dans `src/scenarios.c`.

Pour compiler, exécuter tous les tests unitaires, puis lancer cette simulation :

```sh
./launch/run_linux.sh --test
```

Les tests seuls peuvent aussi être lancés après compilation avec
`ctest --test-dir build/linux-debug --output-on-failure`.

## Facturation

Chaque session terminée ou interrompue est facturée puis enregistrée dans
l'historique ; le rapport de fin donne le chiffre d'affaires par station et
les dernières sessions. Tarif par défaut (`DEFAULT_TARIFF`, `billing.c`) :

- 0,35 €/kWh, plus 0,15 €/kWh pour une recharge rapide (au-delà de 50 kW) ;
- 1 € de frais fixes par session, offerts si une panne interrompt la recharge ;
- client fidèle : au moins 2 sessions dans les 10 dernières minutes réelles
  (soit 10 h simulées). Il bénéficie de 10 % de remise et passe devant les
  véhicules normaux dans la file d'attente, derrière les véhicules
  prioritaires (urgences, bus).

Le temps est accéléré : une seconde réelle représente une minute simulée.
Toutes les durées affichées (recharges, attentes, historique, export CSV)
sont en minutes simulées : 1,02 kWh à 22 kW prend 2,8 min simulées, soit
2,8 s réelles.
