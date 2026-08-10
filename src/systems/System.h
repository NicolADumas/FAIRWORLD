#pragma once
#include <entt/entt.hpp>

struct SharedContext;

class System {
public:
    virtual ~System() = default;
    virtual void Update(entt::registry& registry, SharedContext* context, float dt) = 0;
};
