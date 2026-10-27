cmake_minimum_required(VERSION 3.13)

# Nom du projet et langage (C)
project(IpcChargingStationNetwork VERSION 0.1.0 LANGUAGES C)

# Standard C11 (avec extensions GNU : strdup, sleep, fork...)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Options du projet
option(IPC_BUILD_TESTS "Compiler les tests unitaires" ON)
option(IPC_WARNINGS_AS_ERRORS "Traiter les warnings comme des erreurs" OFF)

# Type de compilation par défaut : Debug
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Type de compilation" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# Tous les exécutables sont placés dans <build>/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

# Export de compile_commands.json (pour clangd / VS Code)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Vérification de la plateforme : le projet repose sur les IPC System V
# (sys/msg.h) et les threads POSIX, absents sous Windows natif (MSVC, MinGW).
# ---------------------------------------------------------------------------
include(CheckIncludeFile)
check_include_file(sys/msg.h HAVE_SYS_MSG_H)
if(NOT HAVE_SYS_MSG_H)
    message(FATAL_ERROR
        "En-tête <sys/msg.h> introuvable : les IPC System V ne sont pas disponibles "
        "sur cette plateforme (${CMAKE_SYSTEM_NAME}, ${CMAKE_C_COMPILER_ID}).\n"
        "Sous Windows, compilez le projet dans WSL (launch/run_win.cmd s'en charge).")
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Dossier des en-têtes publics du projet
set(IPC_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include/ipc_network)

# Options de compilation communes à toutes les cibles du projet
function(ipc_configure_target target)
    target_include_directories(${target} PRIVATE ${IPC_INCLUDE_DIR})
    target_link_libraries(${target} PRIVATE Threads::Threads)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(IPC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# Codes sources de l'application
add_subdirectory(src)

# Tests (exécutés avec la commande 'ctest')
if(IPC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
