#pragma once
#include <memory>
#include <iostream>
#include "State.h"

class StateManager {
public:
    void ChangeState(std::unique_ptr<State> newState) {
        // Mette in coda il cambio di stato
        m_pendingState = std::move(newState);
    }

    void ProcessTransitions() {
        if (m_pendingState) {
            // Memory Isolation: Distrugge esplicitamente lo stato corrente PRIMA di inizializzare il nuovo
            m_currentState.reset();
            
            // Applica il nuovo stato
            m_currentState = std::move(m_pendingState);
            
            auto result = m_currentState->Init();
            if (!result.has_value()) {
                std::cerr << "[StateManager ERROR] Inizializzazione stato fallita: " << result.error() << "\n";
                m_currentState.reset(); // Azzera in caso di fallimento critico
            }
        }
    }

    void Update(float dt) {
        if (m_currentState) m_currentState->Update(dt);
    }

    void Render() {
        if (m_currentState) m_currentState->Render();
    }

    bool IsRunning() const {
        return m_currentState != nullptr;
    }

    entt::registry* GetActiveRegistry() const {
        if (m_currentState) return m_currentState->GetRegistry();
        return nullptr;
    }

private:
    std::unique_ptr<State> m_currentState = nullptr;
    std::unique_ptr<State> m_pendingState = nullptr;
};
