#pragma once
#include <entt/entt.hpp>

namespace fw {

class BiomeTerrainSystem {
public:
    // Dispatcher Globale
    static void Update(entt::registry& registry, int maxChunksPerFrame = 10, class BlockRegistry* blockRegistry = nullptr);
};

class BiomeDecoratorSystem {
public:
    // Dispatcher Globale
    static void Update(entt::registry& registry, int maxChunksPerFrame = 10, class BlockRegistry* blockRegistry = nullptr);
};

// --- Sistemi Specifici per Bioma (Terrain) ---
class ForestTerrainSystem {
public:
    static void Update(entt::registry& registry, int maxChunksPerFrame, class BlockRegistry* blockRegistry);
};

class DesertTerrainSystem {
public:
    static void Update(entt::registry& registry, int maxChunksPerFrame, class BlockRegistry* blockRegistry);
};

class OceanTerrainSystem {
public:
    static void Update(entt::registry& registry, int maxChunksPerFrame, class BlockRegistry* blockRegistry);
};

class VolcanoTerrainSystem {
public:
    static void Update(entt::registry& registry, int maxChunksPerFrame, class BlockRegistry* blockRegistry);
};

class TundraTerrainSystem {
public:
    static void Update(entt::registry& registry, int maxChunksPerFrame, class BlockRegistry* blockRegistry);
};

class FlatTerrainSystem {
public:
    static void Update(entt::registry& registry, int maxChunksPerFrame, class BlockRegistry* blockRegistry);
};

// --- Sistemi Specifici per Bioma (Decorator) ---
class ForestDecoratorSystem {
public:
    static void Update(entt::registry& registry, int maxChunksPerFrame, class BlockRegistry* blockRegistry);
};

class DesertDecoratorSystem {
public:
    static void Update(entt::registry& registry, int maxChunksPerFrame, class BlockRegistry* blockRegistry);
};

class FlatDecoratorSystem {
public:
    static void Update(entt::registry& registry, int maxChunksPerFrame, class BlockRegistry* blockRegistry);
};

} // namespace fw
