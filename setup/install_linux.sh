#!/bin/bash

echo "========================================================"
echo "  Installation des dépendances C/C++ pour Linux"
echo "========================================================"
echo ""

# 1. Vérification stricte de la plateforme
if [ "$(uname -s)" != "Linux" ]; then
    echo -e "\e[31m[ERREUR] Ce script doit être exécuté sur un système Linux.\e[0m"
    exit 1
fi
echo "[INFO] Plateforme Linux détectée."

# 2. Détection du gestionnaire de paquets et installation
if command -v apt-get &> /dev/null; then
    echo "[INFO] Gestionnaire de paquets APT (Debian/Ubuntu) détecté."
    sudo apt-get update
    sudo apt-get install -y build-essential cmake
elif command -v dnf &> /dev/null; then
    echo "[INFO] Gestionnaire de paquets DNF (Fedora/RHEL) détecté."
    sudo dnf groupinstall -y "Development Tools"
    sudo dnf install -y cmake
elif command -v pacman &> /dev/null; then
    echo "[INFO] Gestionnaire de paquets Pacman (Arch Linux) détecté."
    sudo pacman -S --noconfirm base-devel cmake
else
    echo -e "\e[31m[ERREUR] Gestionnaire de paquets non supporté. Installez GCC et CMake manuellement.\e[0m"
    exit 1
fi

# 3. Étape de fin : Vérification finale des versions
echo ""
echo "========================================================"
echo "  Vérification finale des versions"
echo "========================================================"
echo ""

echo -e "\e[33mCMake :\e[0m"
cmake --version | head -n 1
echo ""

echo -e "\e[33mGCC (Compilateur C) :\e[0m"
gcc --version | head -n 1
echo ""

echo -e "\e[32m>>> TOUT EST BIEN INSTALLÉ ET PRÊT À L'EMPLOI ! <<<\e[0m"