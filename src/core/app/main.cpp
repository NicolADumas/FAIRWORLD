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
#include "PlanetMapperState.h"
#include "ChunkEditorState.h"
#include "BlockMakerState.h"
#include "SolarSystemState.h"
#include "WorldProjectManager.h"
#include "DeviceManager.h"
#include "DiagnosticsManager.h"
#include "TimeManager.h"
#include "JoltPhysicsSystem.h"
#include "BlockRegistry.h"
#include "MaterialRegistry.h"
#include "CacheManager.h"

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
    std::cout << "0. HUB (Hub Principale)\n";
    std::cout << "1. FAIRWORLD (Mondo di Gioco, Esplorazione e Costruzione)\n";
    std::cout << "2. LA FORGE (Editor Strutture e Minivoxel)\n";
    std::cout << "3. PHYSICS LAB (Test Fisica e Materiali)\n";
    std::cout << "4. CHUNK EDITOR (Modellazione Terreni 2D/3D)\n";
    std::cout << "5. PLANET MAPPER (Configura Globo & Sistema)\n";
    std::cout << "6. BLOCK MAKER (Editor Blocchi e Materiali PBR)\n";
    std::cout << "7. SOLAR SYSTEM (Mappa Spaziale)\n";
    std::cout << "Scelta [0-7] (predefinito 0): ";
    
    int choice = 0;
    std::string input;
    std::getline(std::cin, input);
    if (!input.empty()) {
        try {
            choice = std::stoi(input);
        } catch (...) {
            choice = 0;
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
    stateManager.SetSharedContext(&context);
    context.stateManager = &stateManager;
    context.engine = &engine; // SharedContext come osservatore non-owning

    // 3. Inizializza i Servizi Fondamentali Prima Del Bootstrap degli Stati (per evitare che Init trovi puntatori nulli)
    DeviceManager deviceManager;
    fw::TimeManager timeManager;
    fw::DiagnosticsManager diagnosticsManager;
    fw::BlockRegistry blockRegistry;
    fw::MaterialRegistry materialRegistry;
    
    blockRegistry.Initialize();
    blockRegistry.LoadFromJson("assets/definitions/blocks.json");
    
    materialRegistry.Initialize();
    materialRegistry.LoadFromJson("assets/definitions/materials.json");
    
    fw::CacheManager cacheManager;
    cacheManager.Initialize(&context);

    fw::WorldProjectManager projectManager;
    projectManager.LoadProject("saves/map/world_map.json", &blockRegistry);
    
    context.deviceManager = &deviceManager;
    context.timeManager = &timeManager;
    context.diagnosticsManager = &diagnosticsManager;
    context.blockRegistry = &blockRegistry;
    context.materialRegistry = &materialRegistry;
    context.cacheManager = &cacheManager;
    context.projectManager = &projectManager;

    engine.SetSharedContext(&context);

    // 4. Bootstrap: Avvia lo stato selezionato con tutti i servizi connessi e sincronizzati
    if (choice == 1) {
        context.targetGameJsonPath = "saves/map/world_map.json"; // Forza caricamento mappa generata
        stateManager.ChangeState(std::make_unique<PlayState>(&context));
        engine.SetGameMode(GameMode::Play);
        std::cout << "[SYSTEM] Avviato PlayState...\n";
    } else if (choice == 2) {
        stateManager.ChangeState(std::make_unique<ForgeState>(&context));
        engine.SetGameMode(GameMode::Dev);
        std::cout << "[SYSTEM] Avviato ForgeState...\n";
    } else if (choice == 3) {
        stateManager.ChangeState(std::make_unique<PhysicsLabState>(&context));
        engine.SetGameMode(GameMode::PhysicsLab);
        std::cout << "[SYSTEM] Avviato PhysicsLabState...\n";
    } else if (choice == 4) {
        stateManager.ChangeState(std::make_unique<ChunkEditorState>(&context));
        engine.SetGameMode(GameMode::ChunkEditor);
        std::cout << "[SYSTEM] Avviato ChunkEditorState...\n";
    } else if (choice == 5) {
        stateManager.ChangeState(std::make_unique<PlanetMapperState>(&context));
        engine.SetGameMode(GameMode::PlanetMapper);
        std::cout << "[SYSTEM] Avviato PlanetMapperState...\n";
    } else if (choice == 6) {
        stateManager.ChangeState(std::make_unique<BlockMakerState>(&context));
        engine.SetGameMode(GameMode::BlockMaker);
        std::cout << "[SYSTEM] Avviato BlockMakerState...\n";
    } else if (choice == 7) {
        stateManager.ChangeState(std::make_unique<SolarSystemState>(&context));
        engine.SetGameMode(GameMode::SolarSystem);
        std::cout << "[SYSTEM] Avviato SolarSystemState...\n";
    } else {
        // Fallback default: HUB (Scelta 0 o non valida)
        stateManager.ChangeState(std::make_unique<HubState>(&context));
        engine.SetGameMode(GameMode::Hub);
        std::cout << "[SYSTEM] Avviato HubState...\n";
    }

    std::cout << "\n[SYSTEM] Entro nel main loop guidato dalla State Machine...\n";

// Setup Real Timing
auto lastTime = std::chrono::high_resolution_clock::now();
const float FIXED_DT = 1.0f / 60.0f; // 60 updates per second
float accumulator = 0.0f;

context.engine->SetRenderCallback([&]() {
    // Questo callback viene chiamato dal WindowManager durante il resize (WM_TIMER)
    // Evitiamo di chiamare Update() complesso, renderizziamo solo per non freezare l'immagine
    context.engine->BeginUIFrame();
    stateManager.Render();
    context.engine->EndUIFrame();
});


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
