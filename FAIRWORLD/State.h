#pragma once
#include <expected>
#include <string>

class State {
public:
    State() = default;
    virtual ~State() = default;

    // Strict Memory Safety: Niente copie
    State(const State&) = delete;
    State& operator=(const State&) = delete;

    virtual std::expected<void, std::string> Init() = 0;
    virtual void Update(float dt) = 0;
    virtual void Render() = 0;
};
