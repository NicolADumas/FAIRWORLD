#include "pch.h"
#include "TerrainSolverSystem.h"
#include "world/TerrainSolver.h"
#include "BiomeComponents.h"
#include "core/entities/Components.h"
#include "BlockRegistry.h"
#include "ForgeComponents.h"
#include "world/MapDocument.h"

namespace fw {

TerrainDiagnosticMode TerrainSolverSystem::s_DiagnosticMode = TerrainDiagnosticMode::None;
thread_local TerrainWorkspace TerrainSolverSystem::s_workspace;

void TerrainSolverSystem::Update(entt::registry& registry, int maxChunksPerFrame, BlockRegistry* blockRegistry) {
    auto view = registry.view<VoxelChunkComponent, BiomeDataComponent>();
    int processed = 0;
    
    for (auto entity : view) {
        if (processed >= maxChunksPerFrame) break;
        
        auto& chunk = view.get<VoxelChunkComponent>(entity);
        auto& biomeData = view.get<BiomeDataComponent>(entity);
        
        // Costruisci il contesto di generazione dalla BiomeDataComponent
        TerrainGenerationContext ctx;
        ctx.planetSeed = 12345; // TODO: Fetch from actual global map data when available
        ctx.chunkCoord = {chunk.cx, chunk.cz};
        
        // Centriamo nello spazio sferico (placeholder approssimato)
        ctx.chunkCenterSphere = glm::normalize(biomeData.chunkCenterWorld);
        if (glm::length(biomeData.chunkCenterWorld) < 0.1f) {
            ctx.chunkCenterSphere = glm::vec3(0.0f, 1.0f, 0.0f); // Fallback sicuro
        }
        
        ctx.voxelResolutionX = 16;
        ctx.voxelResolutionY = 128;
        ctx.voxelResolutionZ = 16;
        
        ctx.diagnosticMode = s_DiagnosticMode;
        ctx.ruleHash = 0; // TODO: Implement rule hash per caching
        
        // Ottieni le regole finali risolvendo i nomi dei blocchi
        TerrainRuleOverrides emptyOverrides;
        ResolvedTerrainRules rules = ResolveTerrainRules(biomeData.baseTerrain.baseRules, emptyOverrides, 1.0f, blockRegistry);
        
        // Esegui la generazione tramite il nuovo TerrainSolver
        GenerateChunk(ctx, rules, chunk);
        
        // Rimuovi il BiomeDataComponent per segnalare che la generazione voxel e' completata
        registry.remove<BiomeDataComponent>(entity);
        
        // Segna come dirty per aggiornare la mesh (VulkanChunkBuffer)
        registry.emplace_or_replace<ChunkDirtyComponent>(entity);
        
        processed++;
    }
}

void TerrainSolverSystem::GenerateChunk(const TerrainGenerationContext& context, const ResolvedTerrainRules& rules, VoxelChunkComponent& chunk) {
    TerrainSolver solver;
    
    // Esegui la generazione passando il workspace thread_local
    solver.GenerateChunk(context, rules, s_workspace, chunk);
    
    // Il flag isGenerated indica inequivocabilmente che la pipeline procedurale è completa 
    // e i dati in chunk.blocks sono validi e pronti per il meshing o la persistenza.
    chunk.isGenerated = true;
}

} // namespace fw
