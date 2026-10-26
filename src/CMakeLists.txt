# Lister les fichiers sources (.c)
set(SOURCES
    main.c
    allocator.c
    charger.c
    message_queue.c
    queue_manager.c
    server.c
    station.c
    vehicle.c
)

# Créer l'exécutable final
add_executable(ipc_station_app ${SOURCES})

# Indiquer au compilateur où trouver les fichiers .h
# PROJECT_SOURCE_DIR pointe vers la racine du projet
target_include_directories(ipc_station_app PRIVATE ${PROJECT_SOURCE_DIR}/include)

# Ajouter des flags de compilation stricts (recommandé avec GCC)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ipc_station_app PRIVATE -Wall -Wextra -Wpedantic)
endif()