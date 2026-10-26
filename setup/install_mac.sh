#!/bin/bash

echo "========================================================"
echo "  Installation des dépendances C/C++ pour macOS"
echo "========================================================"
echo ""

# 1. Vérification stricte de la plateforme
if [ "$(uname -s)" != "Darwin" ]; then
    echo -e "\033[31m[ERREUR] Ce script doit être exécuté sur macOS.\033[0m"
    exit 1
fi
echo "[INFO] Plateforme macOS détectée."

# 2. Vérification et installation d'Apple Clang (Command Line Tools)
if xcode-select -p &> /dev/null; then
    echo "[OK] Apple Command Line Tools (Clang) déjà installés."
else
    echo "[INFO] Les outils de compilation Apple ne sont pas installés."
    echo "[ACTION] Lancement de l'installation..."
    xcode-select --install
    echo -e "\033[33m[ATTENTION] Une fenêtre Apple s'est ouverte. Terminez l'installation via cette fenêtre, puis relancez ce script.\033[0m"
    exit 0
fi

# 3. Vérification de Homebrew (nécessaire pour récupérer CMake proprement)
if ! command -v brew &> /dev/null; then
    echo "[INFO] Homebrew n'est pas installé. Installation en cours..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    
    # Ajout au PATH temporaire pour la suite du script (Cas Apple Silicon)
    if [ -f "/opt/homebrew/bin/brew" ]; then
        eval "$(/opt/homebrew/bin/brew shellenv)"
    fi
else
    echo "[OK] Homebrew est déjà installé."
fi

# 4. Vérification et installation de CMake
if ! command -v cmake &> /dev/null; then
    echo "[INFO] CMake n'est pas installé. Installation via Homebrew..."
    brew install cmake
else
    echo "[OK] CMake est déjà installé."
fi

# 5. Étape de fin : Vérification finale des versions
echo ""
echo "========================================================"
echo "  Vérification finale des versions"
echo "========================================================"
echo ""

echo -e "\033[33mCMake :\033[0m"
cmake --version | head -n 1
echo ""

echo -e "\033[33mApple Clang (Compilateur C par défaut) :\033[0m"
clang --version | head -n 1
echo ""

echo -e "\033[32m>>> TOUT EST BIEN INSTALLÉ ET PRÊT À L'EMPLOI ! <<<\033[0m"