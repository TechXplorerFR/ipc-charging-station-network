cmake_minimum_required(VERSION 3.10)

# Nom du projet et langage (C)
project(IpcChargingStationNetwork C)

# Utiliser le standard C11 (ou C99 selon vos besoins)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Inclure le dossier des codes sources
add_subdirectory(src)

# Activer les tests et inclure le dossier (optionnel)
enable_testing()
add_subdirectory(tests)