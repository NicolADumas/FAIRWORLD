@echo off
echo ==========================================
echo Sincronizzazione DUMARM con GitHub
echo ==========================================
echo.
cd /d "%~dp0"
echo 1. Controllo stato...
git status
echo.
echo 2. Aggiunta di tutte le modifiche...
git add .
echo.
set /p commitMsg="Inserisci un messaggio per descrivere le modifiche (es. 'aggiornamento gui'): "
echo.
echo 3. Salvataggio locale (Commit)...
git commit -m "%commitMsg%"
echo.
echo 4. Caricamento online (Push)...
git push origin main
echo.
if %errorlevel% equ 0 (
    echo ==========================================
    echo SUCCESSO! Tutto aggiornato online.
    echo ==========================================
) else (
    echo ==========================================
    echo ERRORE! Qualcosa. Controlla messaggi sopra.
    echo ==========================================
)
pause
