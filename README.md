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

Pour ajouter un scénario, créez un fichier dans `src/scenarios/` avec sa
configuration de stations, de véhicules et d'événements, puis ajoutez-le à
`src/CMakeLists.txt` et enregistrez-le dans `src/scenarios.c`.

Pour compiler, exécuter tous les tests unitaires, puis lancer cette simulation :

```sh
./launch/run_linux.sh --test
```

Les tests seuls peuvent aussi être lancés après compilation avec
`ctest --test-dir build/linux-debug --output-on-failure`.
