#pragma once
#include <entt/entt.hpp>

namespace fw {

class BiomeTerrainSystem {
public:
    static void Update(entt::registry& registry, int maxChunksPerFrame = 10, class BlockRegistry* blockRegistry = nullptr);
};

class BiomeDecoratorSystem {
public:
    static void Update(entt::registry& registry, int maxChunksPerFrame = 10, class BlockRegistry* blockRegistry = nullptr);
};

} // namespace fw
