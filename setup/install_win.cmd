@echo off
setlocal EnableDelayedExpansion
echo ========================================================
echo   Installation des dependances C (GCC, CMake) pour Windows
echo ========================================================
echo.

@REM Le projet utilise les IPC System V (sys/msg.h) et les threads POSIX, qui
@REM n'existent pas sous Windows natif (MSVC, MinGW) : il se compile et s'execute
@REM dans une distribution Linux via WSL (voir launch\run_win.cmd). Ce script
@REM verifie WSL puis installe GCC et CMake dans la distribution.
@REM Pour forcer une distribution : set IPC_WSL_DISTRO=Ubuntu

@REM 1. Verification de WSL
where wsl >nul 2>&1
IF %ERRORLEVEL% NEQ 0 (
    echo [ERREUR] WSL n'est pas installe.
    echo          Installez-le avec : wsl --install -d Ubuntu   ^(puis redemarrez^)
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

@REM 3. Installation dans la distribution par le script Linux (le mot de passe sudo peut etre demande).
@REM    tr supprime les fins de ligne Windows (CRLF) d'une copie de travail anterieure a .gitattributes.
FOR %%I IN ("%~dp0..") DO SET "PROJECT_DIR=%%~fI"
wsl -d %DISTRO% --cd "%PROJECT_DIR%" -- bash -c "bash <(tr -d '\r' < setup/install_linux.sh)"
SET RESULT=%ERRORLEVEL%

echo.
IF %RESULT% NEQ 0 (
    echo [ERREUR] L'installation s'est terminee avec le code %RESULT%.
    exit /b %RESULT%
)
echo Lancez ensuite le projet avec : launch\run_win.cmd
exit /b 0
