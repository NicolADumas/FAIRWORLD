#pragma once
#include <entt/entt.hpp>
#include "PlanetComponents.h"

namespace fw {

class PlanetOrbitSystem {
public:
    static void Update(entt::registry& registry, float dt);
};

} // namespace fw
