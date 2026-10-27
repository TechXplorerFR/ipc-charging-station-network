#!/bin/bash

# Usage : ./launch/run_linux.sh [--release] [--clean] [--test] [--no-run] [-- args de l'application]

usage() {
    echo "Usage : $0 [options] [-- arguments de l'application]"
    echo ""
    echo "  --release   Compilation en mode Release (Debug par défaut)"
    echo "  --clean     Supprime le dossier de compilation avant de compiler"
    echo "  --test      Exécute les tests (ctest) après la compilation"
    echo "  --no-run    Compile sans lancer l'application"
    echo "  -h, --help  Affiche cette aide"
}

BUILD_TYPE=Debug
CLEAN=0
RUN_TESTS=0
RUN_APP=1
APP_ARGS=()

while [ $# -gt 0 ]; do
    case "$1" in
        --release) BUILD_TYPE=Release ;;
        --clean)   CLEAN=1 ;;
        --test)    RUN_TESTS=1 ;;
        --no-run)  RUN_APP=0 ;;
        -h|--help) usage; exit 0 ;;
        --)        shift; APP_ARGS=("$@"); break ;;
        *)
            echo -e "\e[31m[ERREUR] Option inconnue : $1\e[0m"
            usage
            exit 1
            ;;
    esac
    shift
done

echo "========================================================"
echo "  Compilation et lancement du projet pour Linux"
echo "========================================================"
echo ""

# 1. Vérification stricte de la plateforme
if [ "$(uname -s)" != "Linux" ]; then
    echo -e "\e[31m[ERREUR] Ce script doit être exécuté sur un système Linux.\e[0m"
    exit 1
fi
echo "[INFO] Plateforme Linux détectée."

# 2. Vérification des outils (installés par setup/install_linux.sh)
for tool in cmake gcc; do
    if ! command -v "$tool" &> /dev/null; then
        echo -e "\e[31m[ERREUR] $tool est introuvable. Lancez d'abord setup/install_linux.sh.\e[0m"
        exit 1
    fi
done
echo "[OK] CMake et GCC sont disponibles."

# Chemins : le script fonctionne quel que soit le dossier courant
# (IPC_PROJECT_DIR est fourni par launch/run_win.cmd lors d'un lancement via WSL)
PROJECT_DIR="${IPC_PROJECT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
BUILD_DIR="$PROJECT_DIR/build/linux-$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"
APP="$BUILD_DIR/bin/ipc_station_app"

if [ "$CLEAN" -eq 1 ] && [ -d "$BUILD_DIR" ]; then
    echo "[INFO] Suppression de $BUILD_DIR..."
    rm -rf "$BUILD_DIR"
fi

# 3. Configuration CMake
echo ""
echo "[INFO] Configuration CMake ($BUILD_TYPE)..."
if ! cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"; then
    echo -e "\e[31m[ERREUR] La configuration CMake a échoué.\e[0m"
    exit 1
fi

# 4. Compilation
echo ""
echo "[INFO] Compilation..."
if ! cmake --build "$BUILD_DIR" --parallel "$(nproc)"; then
    echo -e "\e[31m[ERREUR] La compilation a échoué.\e[0m"
    exit 1
fi
echo -e "\e[32m[OK] Compilation réussie.\e[0m"

# 5. Tests (optionnel)
if [ "$RUN_TESTS" -eq 1 ]; then
    echo ""
    echo "========================================================"
    echo "  Exécution des tests"
    echo "========================================================"
    echo ""
    if ! (cd "$BUILD_DIR" && ctest --output-on-failure); then
        echo -e "\e[31m[ERREUR] Des tests ont échoué.\e[0m"
        exit 1
    fi
    echo -e "\e[32m[OK] Tous les tests sont passés.\e[0m"
fi

# 6. Lancement de l'application
if [ "$RUN_APP" -eq 1 ]; then
    echo ""
    echo "========================================================"
    echo "  Lancement de l'application"
    echo "========================================================"
    echo ""
    "$APP" "${APP_ARGS[@]}"
    exit $?
fi
