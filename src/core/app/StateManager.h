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

    void ReplaceState(std::unique_ptr<State> newState) {
        m_pendingActions.push_back({ActionType::Replace, std::move(newState)});
    }

    void Clear() {
        while (!m_stateStack.empty()) {
            m_stateStack.back()->OnExit();
            m_stateStack.pop_back();
        }
        m_pendingActions.clear();
        if (m_context && m_context->cacheManager) {
            m_context->cacheManager->FlushGpuRenderCaches(m_context);
            m_context->cacheManager->FlushCpuTransientCaches(m_context);
        }
    }

    void ProcessTransitions() {
        for (auto& action : m_pendingActions) {
            if (action.type == ActionType::Change) {
                if (!action.newState) {
                    std::cerr << "[StateManager ERROR] ChangeState called with null newState.\n";
                    continue;
                }
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
                if (m_stateStack.back()->Init()) {
                    m_stateStack.back()->OnEnter();
                } else {
                    std::cerr << "[StateManager ERROR] Inizializzazione ChangeState fallita. Rollback eseguito.\n";
                    m_stateStack.pop_back();
                }
            }
            else if (action.type == ActionType::Push) {
                if (!action.newState) {
                    std::cerr << "[StateManager ERROR] PushState called with null newState.\n";
                    continue;
                }
                if (!m_stateStack.empty()) {
                    m_stateStack.back()->OnSuspend();
                }
                
                if (m_context && m_context->cacheManager) {
                    m_context->cacheManager->FlushCpuTransientCaches(m_context); // Soft flush
                }

                m_stateStack.push_back(std::move(action.newState));
                if (m_stateStack.back()->Init()) {
                    m_stateStack.back()->OnEnter();
                } else {
                    std::cerr << "[StateManager ERROR] Inizializzazione PushState fallita. Rollback eseguito.\n";
                    m_stateStack.pop_back();
                    if (!m_stateStack.empty()) {
                        m_stateStack.back()->OnResume();
                    }
                }
            }
            else if (action.type == ActionType::Replace) {
                if (!action.newState) {
                    std::cerr << "[StateManager ERROR] ReplaceState called with null newState.\n";
                    continue;
                }
                if (!m_stateStack.empty()) {
                    m_stateStack.back()->OnExit();
                    m_stateStack.pop_back();
                }
                if (m_context && m_context->cacheManager) {
                    m_context->cacheManager->FlushCpuTransientCaches(m_context);
                }

                m_stateStack.push_back(std::move(action.newState));
                if (m_stateStack.back()->Init()) {
                    m_stateStack.back()->OnEnter();
                } else {
                    std::cerr << "[StateManager ERROR] Inizializzazione ReplaceState fallita. Rollback eseguito.\n";
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

    size_t GetStackSize() const {
        return m_stateStack.size();
    }

private:
    SharedContext* m_context = nullptr;
    std::vector<std::unique_ptr<State>> m_stateStack;

    enum class ActionType { Push, Pop, Change, Replace };
    struct PendingAction {
        ActionType type;
        std::unique_ptr<State> newState;
    };
    std::vector<PendingAction> m_pendingActions;
};
