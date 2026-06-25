#pragma once
#include "State.h"
#include "entt/entt.hpp"
#include <vector>
#include <memory>
#include "Systems.h"

struct SharedContext;

class PlayState : public State {
public:
    explicit PlayState(SharedContext* context);
    ~PlayState() override;

    std::expected<void, std::string> Init() override;
    void Update(float dt) override;
    void Render() override;

private:
    SharedContext* m_context;
    entt::registry m_registry;
    std::vector<std::unique_ptr<fw::ISystem>> m_systems;
};
