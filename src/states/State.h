#pragma once

#include <string>
#include <entt/entt.hpp>

class State {
public:
    State() = default;
    virtual ~State() = default;

    // Strict Memory Safety: Niente copie
    State(const State&) = delete;
    State& operator=(const State&) = delete;

    virtual bool Init() = 0;
    virtual void Update(float dt) = 0;
    virtual void Render() = 0;

    // Lifecycle hooks for multitasking (Stack StateManager)
    virtual void OnEnter() {}
    virtual void OnSuspend() {}
    virtual void OnResume() {}
    virtual void OnExit() {}
    
    // OPT-02.1: Activity-Aware capability query (true per stati 3D/fisica/animati, false per stati statici/Hub)
    virtual bool RequiresContinuousUpdate() const { return true; }

    virtual entt::registry* GetRegistry() { return nullptr; }
};
