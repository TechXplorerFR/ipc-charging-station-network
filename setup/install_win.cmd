@echo off
echo ========================================================
echo   Installation des dependances C/C++ (GCC) pour Windows
echo ========================================================
echo.

@REM Detection de l'architecture du systeme
IF EXIST "%ProgramFiles(x86)%" (
    SET ARCHITECTURE=x64
) ELSE (
    SET ARCHITECTURE=x86
)
echo [INFO] Architecture detectee : %ARCHITECTURE%
echo.

@REM 1. Verification et installation de CMake
cmake --version >nul 2>&1
IF %ERRORLEVEL% NEQ 0 (
    echo [INFO] CMake n'est pas installe. Installation via winget...
    winget install -e --id Kitware.CMake --accept-source-agreements --accept-package-agreements
) ELSE (
    echo [OK] CMake est deja installe.
)

@REM 2. Verification de GCC et installation via MSYS2
gcc --version >nul 2>&1
IF %ERRORLEVEL% NEQ 0 (
    echo [INFO] GCC n'est pas detecte dans le PATH. Installation de MSYS2 et GCC...
    
    @REM Installation de MSYS2 si le dossier n'existe pas
    IF NOT EXIST "C:\msys64\usr\bin\bash.exe" (
        echo [INFO] Telechargement et installation de MSYS2 (Environnement de base)...
        winget install -e --id MSYS2.MSYS2 --accept-source-agreements --accept-package-agreements
    )
    
    echo [INFO] Mise a jour de MSYS2 et installation de GCC + Make (UCRT64)...
    @REM Execution de pacman (le gestionnaire de paquets) de maniere silencieuse via le bash de MSYS2
    C:\msys64\usr\bin\bash.exe -lc "pacman -Syu --noconfirm"
    C:\msys64\usr\bin\bash.exe -lc "pacman -S --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make"
    
    echo [INFO] Ajout automatique de GCC (C:\msys64\ucrt64\bin) au PATH Utilisateur Windows...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "[Environment]::SetEnvironmentVariable('Path', [Environment]::GetEnvironmentVariable('Path', 'User') + ';C:\msys64\ucrt64\bin', 'User')"
) ELSE (
    echo [OK] GCC est deja installe.
)

@REM 3. Etape de fin : Validation dans un environnement rafraichi via PowerShell
echo.
echo ========================================================
echo   Verification finale des versions (Actualisation PATH)
echo ========================================================
echo.

@REM Cette commande PowerShell fusionne les variables PATH (Systeme + Utilisateur), 
@REM prend en compte le nouveau chemin vers GCC installe, et affiche les versions de CMake et GCC.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$env:PATH = [Environment]::GetEnvironmentVariable('PATH', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('PATH', 'User'); Write-Host '--- Versions detectees ---' -ForegroundColor Cyan; Write-Host 'CMake :' -ForegroundColor Yellow; cmake --version; Write-Host ''; Write-Host 'GCC (via MSYS2) :' -ForegroundColor Yellow; gcc --version | Select-Object -First 1; Write-Host ''; Write-Host '>>> TOUT EST BIEN INSTALLE ET PRET A L''EMPLOI ! <<<' -ForegroundColor Green; Write-Host ''; Read-Host 'Appuyez sur Entree pour quitter'"

exit /b 0