#include "pch.h"
#include "FAIRWORLD.h"
#include <iostream>

#include <windows.h>

HANDLE hServerProcess = NULL;

void StartAIServer() {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    char cmd[] = "cmd.exe /c \"python ../ai_server/server.py\"";
    
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        hServerProcess = pi.hProcess;
        CloseHandle(pi.hThread);
        std::cout << "[SYSTEM] AI Server Python avviato in background.\n";
    } else {
        std::cerr << "[WARNING] Impossibile avviare il Server Python. L'AI potrebbe non rispondere.\n";
    }
}

void StopAIServer() {
    if (hServerProcess) {
        TerminateProcess(hServerProcess, 0);
        CloseHandle(hServerProcess);
        std::cout << "[SYSTEM] AI Server Python chiuso.\n";
    }
}

int main() {
    std::cout << "==========================================\n";
    std::cout << "    FAIRWORLD ENGINE - BOOT SEQUENCE      \n";
    std::cout << "==========================================\n\n";

    StartAIServer();

    FairWorldEngine engine;

    if (engine.Init()) {
        std::cout << "\n[SYSTEM] Motore avviato. Entro nel loop principale...\n";
        
        // Esegue il programma finché non chiudi la finestra o il visore
        engine.Run(); 
        
        std::cout << "[SYSTEM] Chiusura del motore completata.\n";
    } else {
        std::cerr << "\n[ERROR] Errore critico durante l'inizializzazione." << std::endl;
        std::cin.get();
    }

    StopAIServer();
    return 0;
}
