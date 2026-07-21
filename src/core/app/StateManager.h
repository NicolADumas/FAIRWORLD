#pragma once
#include <memory>
#include <iostream>
#include "State.h"
#include "SharedContext.h"
#include "CacheManager.h"

class StateManager {
public:
    void SetSharedContext(SharedContext* context) { m_context = context; }

    void ChangeState(std::unique_ptr<State> newState) {
        // Mette in coda il cambio di stato
        m_pendingState = std::move(newState);
    }

    void ProcessTransitions() {
        if (m_pendingState) {
            // Memory Isolation: Distrugge esplicitamente lo stato corrente PRIMA di inizializzare il nuovo
            m_currentState.reset();
            
            // Invalidation automatica delle cache CPU & GPU durante la transizione
            if (m_context && m_context->cacheManager) {
                m_context->cacheManager->FlushGpuRenderCaches(m_context);
                m_context->cacheManager->FlushCpuTransientCaches(m_context);
            }

            // Applica il nuovo stato
            m_currentState = std::move(m_pendingState);
            
            bool result = m_currentState->Init();
            if (!result) {
                std::cerr << "[StateManager ERROR] Inizializzazione stato fallita.\n";
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

    State* GetCurrentState() const {
        return m_currentState.get();
    }

private:
    SharedContext* m_context = nullptr;
    std::unique_ptr<State> m_currentState = nullptr;
    std::unique_ptr<State> m_pendingState = nullptr;
};
