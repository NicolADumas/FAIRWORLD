#pragma once
#include <entt/entt.hpp>
#include <functional>
#include "world/TerrainSolver.h"

namespace fw {

class TerrainSolverSystem {
public:
    static TerrainDiagnosticMode s_DiagnosticMode;
    
    // The global dispatcher for the new Phase 5 terrain generation
    static int Update(entt::registry& registry, std::vector<entt::entity>& dirtyQueue, int maxChunksPerFrame, std::function<void(entt::entity)> markMeshDirty = nullptr, class BlockRegistry* blockRegistry = nullptr);
    
    // Explicit API for deterministic generation sharing the thread_local workspace
    static void GenerateChunk(const TerrainGenerationContext& context, const ResolvedTerrainRules& rules, VoxelChunkComponent& chunk);

    static bool s_enableVisualGateLog;
    
private:
    // Shared thread-local or pool-based workspace
    // For now we use a static thread-local to prevent allocations per chunk
    static thread_local TerrainWorkspace s_workspace;
};

} // namespace fw
