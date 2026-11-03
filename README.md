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

## Facturation

Chaque session terminée ou interrompue est facturée puis enregistrée dans
l'historique ; le rapport de fin donne le chiffre d'affaires par station et
les dernières sessions. Tarif par défaut (`DEFAULT_TARIFF`, `billing.c`) :

- 0,35 €/kWh, plus 0,15 €/kWh pour une recharge rapide (au-delà de 50 kW) ;
- 1 € de frais fixes par session, offerts si une panne interrompt la recharge ;
- 10 % de remise pour un client fidèle (au moins 2 sessions dans les
  10 dernières minutes réelles, soit 10 h simulées).

Le temps est accéléré : une seconde réelle représente une minute de recharge.
