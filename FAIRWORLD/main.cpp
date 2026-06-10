#include "pch.h"
#include "FAIRWORLD.h"
#include <iostream>
#include <windows.h>
#include <chrono>

#include "SharedContext.h"
#include "StateManager.h"
#include "HubState.h"
#include "DeviceManager.h"

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
    std::cout << "    FAIRWORLD ENGINE - BOOT SEQUENCE V2   \n";
    std::cout << "==========================================\n\n";

    StartAIServer();

    // 1. Inizializza il VERO motore grafico (Incapsulato come Servitore)
    FairWorldEngine engine;
    if (!engine.Init()) {
        std::cerr << "\n[ERROR] Errore critico durante l'inizializzazione grafica." << std::endl;
        StopAIServer();
        return -1;
    }

    // 2. Istanzia Context e StateManager
    SharedContext context;
    StateManager stateManager;
    context.stateManager = &stateManager;
    context.engine = &engine; // SharedContext come osservatore non-owning

    // 3. Bootstrap (Isolamento Memoria)
    stateManager.ChangeState(std::make_unique<HubState>(&context));

    // 4. Collega il Bus Dati dell'OS al motore (per l'Action Mapping)
    engine.SetSharedContext(&context);

    std::cout << "\n[SYSTEM] Entro nel main loop guidato dalla State Machine...\n";

    DeviceManager deviceManager; // Il nostro Demone hardware

    // Setup Real Timing
    auto lastTime = std::chrono::high_resolution_clock::now();

    // 4. Main Loop
    while (context.engine->IsRunning()) {
        
        // Polling hardware (Finestra o VR)
        context.engine->PollHardwareEvents();

        // 1. IL DEMONE AGGIORNA IL BUS HARDWARE
        deviceManager.Update(&context);

        // Calcolo Real Timing Delta (dt)
        auto currentTime = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        // A. Transizioni (Distrugge vecchi stati prima di inizializzare i nuovi)
        stateManager.ProcessTransitions();

        if (!stateManager.IsRunning()) {
            break; 
        }

        // B. Update Data-Driven (che a sua volta pilota l'Update dell'Engine)
        stateManager.Update(dt);
        
        // C. Render Hardware (che a sua volta pilota il Render dell'Engine)
        context.engine->BeginUIFrame();
        stateManager.Render();
        context.engine->EndUIFrame();
    }

    std::cout << "[SYSTEM] Chiusura del motore completata.\n";
    engine.Shutdown();
    StopAIServer();
    return 0;
}
