#pragma once
#include <entt/entt.hpp>

namespace fw {

    class PlanetTimeSystem {
    public:
        static void Update(entt::registry& registry, float dt);
    };

}
