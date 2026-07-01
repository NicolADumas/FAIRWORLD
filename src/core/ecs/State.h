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
    
    virtual entt::registry* GetRegistry() { return nullptr; }
};
