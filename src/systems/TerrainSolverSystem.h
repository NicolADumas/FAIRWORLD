#pragma once
#include <entt/entt.hpp>
#include "world/TerrainSolver.h"

namespace fw {

class TerrainSolverSystem {
public:
    static TerrainDiagnosticMode s_DiagnosticMode;
    
    // The global dispatcher for the new Phase 5 terrain generation
    static void Update(entt::registry& registry, int maxChunksPerFrame, class BlockRegistry* blockRegistry = nullptr);
    
    // Explicit API for deterministic generation sharing the thread_local workspace
    static void GenerateChunk(const TerrainGenerationContext& context, const ResolvedTerrainRules& rules, VoxelChunkComponent& chunk);
    
private:
    // Shared thread-local or pool-based workspace
    // For now we use a static thread-local to prevent allocations per chunk
    static thread_local TerrainWorkspace s_workspace;
};

} // namespace fw
