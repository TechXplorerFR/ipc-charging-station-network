@echo off
setlocal EnableDelayedExpansion
echo ========================================================
echo   Compilation et lancement du projet pour Windows (WSL)
echo ========================================================
echo.

@REM Usage : launch\run_win.cmd [--release] [--clean] [--test] [--no-run] [-- args de l'application]
@REM Les options sont transmises telles quelles a launch/run_linux.sh.
@REM
@REM Le projet utilise les IPC System V (sys/msg.h) et les threads POSIX, qui
@REM n'existent pas sous Windows natif (MSVC, MinGW) : la compilation et
@REM l'execution se font donc dans une distribution Linux via WSL.
@REM Pour forcer une distribution : set IPC_WSL_DISTRO=Ubuntu

@REM 1. Verification de WSL
where wsl >nul 2>&1
IF %ERRORLEVEL% NEQ 0 (
    echo [ERREUR] WSL n'est pas installe.
    echo          Installez-le avec : wsl --install   ^(puis redemarrez^)
    exit /b 1
)
echo [OK] WSL est installe.

@REM 2. Choix de la distribution : IPC_WSL_DISTRO, sinon la premiere distribution
@REM    hors Docker Desktop (ses distributions ne contiennent pas d'outils de compilation)
SET WSL_UTF8=1
SET DISTRO=%IPC_WSL_DISTRO%
IF "%DISTRO%"=="" (
    FOR /F "usebackq delims=" %%D IN (`wsl -l -q`) DO (
        SET NAME=%%D
        IF "!DISTRO!"=="" IF /I NOT "!NAME:~0,14!"=="docker-desktop" SET DISTRO=%%D
    )
)
IF "%DISTRO%"=="" (
    echo [ERREUR] Aucune distribution Linux WSL trouvee.
    echo          Installez-en une avec : wsl --install -d Ubuntu
    exit /b 1
)
echo [INFO] Distribution WSL utilisee : %DISTRO%
echo.

@REM 3. Compilation et lancement via le script Linux
@REM    tr supprime les fins de ligne Windows (CRLF) eventuellement ajoutees par Git.
@REM    IPC_PROJECT_DIR indique au script la racine du projet (vue depuis WSL).
@REM    Le nom de distribution ne doit pas etre entre guillemets (wsl -d les refuse).
SET PROJECT_DIR=%~dp0..
wsl -d %DISTRO% --cd "%PROJECT_DIR%" -- bash -c "export IPC_PROJECT_DIR=$PWD; bash <(tr -d '\r' < launch/run_linux.sh) %*"
SET RESULT=%ERRORLEVEL%

echo.
IF %RESULT% NEQ 0 (
    echo [ERREUR] Le script s'est termine avec le code %RESULT%.
    echo          Si CMake ou GCC manquent dans WSL, executez dans la distribution :
    echo          sudo apt-get install -y build-essential cmake
)
exit /b %RESULT%
