#pragma once
#include <entt/entt.hpp>

namespace fw {

class BiomeTerrainSystem {
public:
    static void Update(entt::registry& registry, int maxChunksPerFrame = 10);
};

class BiomeDecoratorSystem {
public:
    static void Update(entt::registry& registry, int maxChunksPerFrame = 10);
};

} // namespace fw
