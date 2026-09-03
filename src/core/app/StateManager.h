#pragma once
#include <memory>
#include <vector>
#include <iostream>
#include "State.h"
#include "SharedContext.h"
#include "CacheManager.h"

class StateManager {
public:
    void SetSharedContext(SharedContext* context) { m_context = context; }

    void PushState(std::unique_ptr<State> newState) {
        m_pendingActions.push_back({ActionType::Push, std::move(newState)});
    }

    void PopState() {
        m_pendingActions.push_back({ActionType::Pop, nullptr});
    }

    void ChangeState(std::unique_ptr<State> newState) {
        m_pendingActions.push_back({ActionType::Change, std::move(newState)});
    }

    void ProcessTransitions() {
        for (auto& action : m_pendingActions) {
            if (action.type == ActionType::Change) {
                // Svuota lo stack
                while (!m_stateStack.empty()) {
                    m_stateStack.back()->OnExit();
                    m_stateStack.pop_back();
                }
                
                if (m_context && m_context->cacheManager) {
                    m_context->cacheManager->FlushGpuRenderCaches(m_context);
                    m_context->cacheManager->FlushCpuTransientCaches(m_context);
                }

                m_stateStack.push_back(std::move(action.newState));
                bool result = m_stateStack.back()->Init();
                m_stateStack.back()->OnEnter();
                if (!result) {
                    std::cerr << "[StateManager ERROR] Inizializzazione stato fallita.\n";
                    m_stateStack.pop_back();
                }
            }
            else if (action.type == ActionType::Push) {
                if (!m_stateStack.empty()) {
                    m_stateStack.back()->OnSuspend();
                }
                
                if (m_context && m_context->cacheManager) {
                    m_context->cacheManager->FlushCpuTransientCaches(m_context); // Soft flush
                }

                m_stateStack.push_back(std::move(action.newState));
                bool result = m_stateStack.back()->Init();
                m_stateStack.back()->OnEnter();
                if (!result) {
                    std::cerr << "[StateManager ERROR] Inizializzazione PushState fallita.\n";
                    m_stateStack.pop_back();
                    if (!m_stateStack.empty()) {
                        m_stateStack.back()->OnResume();
                    }
                }
            }
            else if (action.type == ActionType::Pop) {
                if (!m_stateStack.empty()) {
                    m_stateStack.back()->OnExit();
                    m_stateStack.pop_back();
                }
                if (!m_stateStack.empty()) {
                    if (m_context && m_context->cacheManager) {
                        m_context->cacheManager->FlushCpuTransientCaches(m_context);
                    }
                    m_stateStack.back()->OnResume();
                }
            }
        }
        m_pendingActions.clear();
    }

    void Update(float dt) {
        if (!m_stateStack.empty()) {
            m_stateStack.back()->Update(dt);
        }
    }

    void Render() {
        if (!m_stateStack.empty()) {
            m_stateStack.back()->Render();
        }
    }

    bool IsRunning() const {
        return !m_stateStack.empty();
    }

    entt::registry* GetActiveRegistry() const {
        if (!m_stateStack.empty()) return m_stateStack.back()->GetRegistry();
        return nullptr;
    }

    State* GetCurrentState() const {
        if (!m_stateStack.empty()) return m_stateStack.back().get();
        return nullptr;
    }

private:
    SharedContext* m_context = nullptr;
    std::vector<std::unique_ptr<State>> m_stateStack;

    enum class ActionType { Push, Pop, Change };
    struct PendingAction {
        ActionType type;
        std::unique_ptr<State> newState;
    };
    std::vector<PendingAction> m_pendingActions;
};
