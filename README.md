# Réseau de bornes de recharge

## Lancer la simulation

Sous Linux (ou WSL), lancez depuis la racine du projet :

```sh
./launch/run_linux.sh
```

Le programme démarre un serveur, deux stations et huit véhicules. Les demandes
sont traitées en parallèle ; certaines passent par la file d'attente. Un rapport
du réseau est affiché à la fin.

Pour compiler, exécuter tous les tests unitaires, puis lancer cette simulation :

```sh
./launch/run_linux.sh --test
```

Les tests seuls peuvent aussi être lancés après compilation avec
`ctest --test-dir build/linux-debug --output-on-failure`.
