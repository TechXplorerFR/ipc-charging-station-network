# ipc_add_test(<nom> <fichier de test> [modules src/ nécessaires...])
# Chaque test n'embarque que les modules qu'il teste : un module en cours
# d'écriture ne bloque pas la compilation des tests des autres modules.
function(ipc_add_test name test_source)
    set(module_sources "")
    foreach(module ${ARGN})
        list(APPEND module_sources ${PROJECT_SOURCE_DIR}/src/${module})
    endforeach()

    add_executable(${name} ${test_source} ${module_sources})
    ipc_configure_target(${name})
    add_test(NAME ${name} COMMAND ${name})
    # TIMEOUT : un test bloqué (msgrcv jamais servi) est tué au bout de 60 s
    # RUN_SERIAL : les tests partagent les files IPC du noyau, pas de parallélisme
    set_tests_properties(${name} PROPERTIES TIMEOUT 60 RUN_SERIAL ON)
endfunction()

ipc_add_test(test_message_queue test_message_queue.c message_queue.c)
ipc_add_test(test_queue_manager test_queue_manager.c queue_manager.c)
ipc_add_test(test_station test_station.c station.c charger.c queue_manager.c)
ipc_add_test(test_allocator test_allocator.c allocator.c station.c charger.c queue_manager.c)
