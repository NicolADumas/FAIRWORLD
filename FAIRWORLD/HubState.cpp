#include "pch.h"
#include "HubState.h"
#include "SharedContext.h"
#include "StateManager.h"
#include "PlayState.h"
#include "FAIRWORLD.h"
#include <iostream>

HubState::HubState(SharedContext* context) : m_context(context), m_simulatedTimeAccumulator(0.0f) {
    std::cout << "[HubState] Costruito.\n";
}

HubState::~HubState() {
    std::cout << "[HubState] Distrutto. Isolamento memoria garantito.\n";
}

std::expected<void, std::string> HubState::Init() {
    std::cout << "[HubState] Inizializzazione completata. Mostro il Menu Principale ImGui.\n";
    return {};
}

void HubState::Update(float dt) {
    // Fai aggiornare l'engine (che ignorerà la fisica essendo in MAIN_MENU)
    m_context->engine->Update(dt);
    
    // Intercettiamo il cambio di stato dell'engine (es. l'utente preme "Nuova Partita" in ImGui)
    if (m_context->engine->GetCurrentState() == GameState::PLAYING) {
        std::cout << "[HubState] L'utente ha premuto 'Gioca' nell'interfaccia. Innesco transizione...\n";
        
        // 1. Iniettiamo la cartuccia Data-Driven
        m_context->targetGameJsonPath = "projects/game_config.json";
        
        // 2. Chiamiamo il ChangeState (che userà unique_ptr direttamente)
        m_context->stateManager->ChangeState(std::make_unique<PlayState>(m_context));
    }
}

void HubState::Render() {
    // Fai disegnare all'engine (che renderizzerà renderMainMenu di ImGui)
    m_context->engine->Render();
}
