# Modules du réseau de stations (tout sauf le point d'entrée)
set(IPC_MODULE_SOURCES
    allocator.c
    billing.c
    charger.c
    fault_simulator.c
    history.c
    message_queue.c
    queue_manager.c
    random.c
    server.c
    station.c
    vehicle.c
    vehicle_generator.c
)

# Bibliothèque statique regroupant les modules, réutilisable par les tests
add_library(ipc_network STATIC ${IPC_MODULE_SOURCES})
ipc_configure_target(ipc_network)
target_include_directories(ipc_network PUBLIC ${IPC_INCLUDE_DIR})
# m : log() (random) et round() (vehicle_generator)
target_link_libraries(ipc_network PUBLIC Threads::Threads m)

# Exécutable final
add_executable(ipc_station_app main.c)
ipc_configure_target(ipc_station_app)
target_link_libraries(ipc_station_app PRIVATE ipc_network)
