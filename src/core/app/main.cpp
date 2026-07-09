#include "pch.h"
#include "FAIRWORLD.h"
#include <iostream>
#include <windows.h>
#include <chrono>

#include "SharedContext.h"
#include "StateManager.h"
#include "HubState.h"
#include "ForgeState.h"
#include "PlayState.h"
#include "PhysicsLabState.h"
#include "MapState.h"
#include "DeviceManager.h"
#include "DiagnosticsManager.h"
#include "TimeManager.h"
#include "JoltPhysicsSystem.h"
#include "BlockRegistry.h"

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
    std::cout << "    FAIRWORLD ENGINE - BOOT SEQUENCE   \n";
    std::cout << "==========================================\n\n";

    fw::JoltPhysicsSystem::InitializeGlobals();

    StartAIServer();

    std::cout << "\nSeleziona la modalita' di avvio:\n";
    std::cout << "1. FAIRWORLD (Mondo di Gioco, Esplorazione e Costruzione)\n";
    std::cout << "2. FORGE (Editor Strutture e Minivoxel)\n";
    std::cout << "3. HUB (Hub Principale)\n";
    std::cout << "4. LAB (Test Fisica e Materiali)\n";
    std::cout << "5. MAP EDITOR (Planet Mapper)\n";
    std::cout << "Scelta [1-5] (predefinito 1): ";
    
    int choice = 1;
    std::string input;
    std::getline(std::cin, input);
    if (!input.empty()) {
        try {
            choice = std::stoi(input);
        } catch (...) {
            choice = 1;
        }
    }

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

    // 3. Bootstrap: Avvia lo stato selezionato
    if (choice == 2) {
        stateManager.ChangeState(std::make_unique<ForgeState>(&context));
        engine.SetGameMode(GameMode::Dev);
        std::cout << "[SYSTEM] Avviato ForgeState...\n";
    } else if (choice == 3) {
        stateManager.ChangeState(std::make_unique<HubState>(&context));
        engine.SetGameMode(GameMode::Hub);
        std::cout << "[SYSTEM] Avviato HubState...\n";
    } else if (choice == 4) {
        stateManager.ChangeState(std::make_unique<PhysicsLabState>(&context));
        engine.SetGameMode(GameMode::PhysicsLab);
        std::cout << "[SYSTEM] Avviato PhysicsLabState...\n";
    } else if (choice == 5) {
        stateManager.ChangeState(std::make_unique<MapState>(&context));
        engine.SetGameMode(GameMode::Map);
        std::cout << "[SYSTEM] Avviato MapState...\n";
    } else {
        stateManager.ChangeState(std::make_unique<PlayState>(&context));
        engine.SetGameMode(GameMode::Play);
        std::cout << "[SYSTEM] Avviato PlayState...\n";
    }

    // Configurazione DeviceManager, TimeManager e DiagnosticsManager
    DeviceManager deviceManager;
    fw::TimeManager timeManager;
    fw::DiagnosticsManager diagnosticsManager;
    fw::BlockRegistry blockRegistry;
    
    // Inizializza o carica i blocchi da file
    blockRegistry.Initialize();
    blockRegistry.LoadFromJson("../assets/blocks.json");
    
    context.deviceManager = &deviceManager;
    context.timeManager = &timeManager;
    context.diagnosticsManager = &diagnosticsManager;
    context.blockRegistry = &blockRegistry;

    // 5. Collega il Bus Dati dell'OS al motore (per l'Action Mapping)
    engine.SetSharedContext(&context);

    std::cout << "\n[SYSTEM] Entro nel main loop guidato dalla State Machine...\n";

// Setup Real Timing
auto lastTime = std::chrono::high_resolution_clock::now();
const float FIXED_DT = 1.0f / 60.0f; // 60 updates per second
float accumulator = 0.0f;

while (context.engine->IsRunning()) {
    // Poll hardware (window or VR)
    context.engine->PollHardwareEvents();

    // Update hardware bus
    deviceManager.Update(&context);

    // Calculate frame time
    auto currentTime = std::chrono::high_resolution_clock::now();
    float frameTime = std::chrono::duration<float>(currentTime - lastTime).count();
    lastTime = currentTime;
    // Clamp to avoid spiral of death
    if (frameTime > 0.25f) frameTime = 0.25f;
    accumulator += frameTime;

    // Fixed‑timestep updates
    while (accumulator >= FIXED_DT) {
        // Process state transitions
        stateManager.ProcessTransitions();
        if (!stateManager.IsRunning()) {
            // Exit main loop cleanly
            accumulator = 0.0f;
            break;
        }
        // Update game logic with fixed dt
        stateManager.Update(FIXED_DT);
        accumulator -= FIXED_DT;
    }

    // Calcola l'alpha per l'interpolazione del render:
    // quanta frazione del FIXED_DT è già 'avanzata' nel tempo rimanente.
    context.interpolationAlpha = accumulator / FIXED_DT;

    // Rendering (V-Sync limits the rate)
    context.engine->BeginUIFrame();
    stateManager.Render();
    context.engine->EndUIFrame();
    
    // Telemetria (Diagnostics)
    fw::FrameMetrics metrics;
    metrics.frameTimeMs   = frameTime * 1000.0f;
    metrics.physicsTimeMs = 0.0f;   // TODO: tracciare fisica
    metrics.entityCountF  = 0.0f;   // TODO: contare entità reali dal registry
    diagnosticsManager.PushFrame(metrics);
}

    std::cout << "[SYSTEM] Chiusura del motore completata.\n";
    engine.Shutdown();
    StopAIServer();
    fw::JoltPhysicsSystem::ShutdownGlobals();
    return 0;
}
