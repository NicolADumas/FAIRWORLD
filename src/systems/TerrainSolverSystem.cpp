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

int TerrainSolverSystem::Update(entt::registry& registry, int maxChunksPerFrame, BlockRegistry* blockRegistry) {
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
        // ruleHash will be computed below
        
        // Ottieni le regole finali risolvendo i nomi dei blocchi
        TerrainRuleOverrides emptyOverrides;
        ResolvedTerrainRules rules = ResolveTerrainRules(biomeData.baseTerrain.baseRules, emptyOverrides, 1.0f, blockRegistry);
        
        ctx.ruleHash = ComputeRuleHash(rules);
        
        if (chunk.isGenerated && chunk.lastRuleHash == ctx.ruleHash) {
            registry.remove<BiomeDataComponent>(entity);
            processed++;
            continue;
        }
        
        // Esegui la generazione tramite il nuovo TerrainSolver
        GenerateChunk(ctx, rules, chunk);
        chunk.lastRuleHash = ctx.ruleHash;
        
        // Rimuovi il BiomeDataComponent per segnalare che la generazione voxel e' completata
        registry.remove<BiomeDataComponent>(entity);
        
        // Segna come dirty per aggiornare la mesh (VulkanChunkBuffer)
        registry.emplace_or_replace<ChunkDirtyComponent>(entity);
        
        processed++;
    }
    
    return processed;
}

void TerrainSolverSystem::GenerateChunk(const TerrainGenerationContext& context, const ResolvedTerrainRules& rules, VoxelChunkComponent& chunk) {
    TerrainSolver solver;
    
    // Esegui la generazione passando il workspace thread_local
    solver.GenerateChunk(context, rules, s_workspace, chunk);
    
    if (context.diagnosticMode == TerrainDiagnosticMode::MacroField) {
        float minHeight = 9999.0f;
        float maxHeight = -9999.0f;
        float avgHeight = 0.0f;
        for (int i = 0; i < 256; ++i) {
            float h = s_workspace.surfaceHeights[i];
            if (h < minHeight) minHeight = h;
            if (h > maxHeight) maxHeight = h;
            avgHeight += h;
        }
        avgHeight /= 256.0f;
        
        int solidVoxels = 0;
        int airVoxels = 0;
        int waterVoxels = 0;
        
        // Find actual solid height for 5 specific columns
        int solidH_0_0 = 0;
        int solidH_4_4 = 0;
        int solidH_8_8 = 0;
        int solidH_12_12 = 0;
        int solidH_15_15 = 0;

        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                int colSolidCount = 0;
                for (int y = 0; y < 128; ++y) {
                    uint8_t block = chunk.blocks[x][y][z];
                    if (block == 0) airVoxels++;
                    else if (block == rules.resolvedWaterBlock && rules.rules.water.enabled) waterVoxels++;
                    else {
                        solidVoxels++;
                        colSolidCount++;
                    }
                }
                
                if (x == 0 && z == 0) solidH_0_0 = colSolidCount;
                if (x == 4 && z == 4) solidH_4_4 = colSolidCount;
                if (x == 8 && z == 8) solidH_8_8 = colSolidCount;
                if (x == 12 && z == 12) solidH_12_12 = colSolidCount;
                if (x == 15 && z == 15) solidH_15_15 = colSolidCount;
            }
        }
        
        std::cout << "\n=======================================================\n";
        std::cout << "[TerrainVisualGate][HEIGHT]\n";
        std::cout << "Algorithm: " << (int)rules.rules.height.algorithm << "\n";
        std::cout << "Amplitude: " << rules.rules.height.amplitude << "\n";
        std::cout << "MacroScale: " << rules.rules.height.macroScale << "\n\n";
        std::cout << "Min: " << minHeight << "\n";
        std::cout << "Max: " << maxHeight << "\n";
        std::cout << "Delta: " << (maxHeight - minHeight) << "\n";
        std::cout << "Average: " << avgHeight << "\n\n";
        std::cout << "Column (0,0): " << s_workspace.surfaceHeights[0 * 16 + 0] << "\n";
        std::cout << "Column (4,4): " << s_workspace.surfaceHeights[4 * 16 + 4] << "\n";
        std::cout << "Column (8,8): " << s_workspace.surfaceHeights[8 * 16 + 8] << "\n";
        std::cout << "Column (12,12): " << s_workspace.surfaceHeights[12 * 16 + 12] << "\n";
        std::cout << "Column (15,15): " << s_workspace.surfaceHeights[15 * 16 + 15] << "\n";
        
        std::cout << "\n[TerrainVisualGate][CLASSIFIER]\n";
        std::cout << "Total: " << (airVoxels + solidVoxels + waterVoxels) << "\n";
        std::cout << "Solid: " << solidVoxels << "\n";
        std::cout << "Air: " << airVoxels << "\n";
        std::cout << "Water: " << waterVoxels << "\n\n";
        std::cout << "Column (0,0) solidHeight: " << solidH_0_0 << "\n";
        std::cout << "Column (4,4) solidHeight: " << solidH_4_4 << "\n";
        std::cout << "Column (8,8) solidHeight: " << solidH_8_8 << "\n";
        std::cout << "Column (12,12) solidHeight: " << solidH_12_12 << "\n";
        std::cout << "Column (15,15) solidHeight: " << solidH_15_15 << "\n";
        std::cout << "=======================================================\n";
    } else {
        std::cout << "[TerrainDiagnostic] GenerateChunk completato, ma diagnosticMode=" << (int)context.diagnosticMode << "\n";
    }

    // Il flag isGenerated indica inequivocabilmente che la pipeline procedurale è completa 
    // e i dati in chunk.blocks sono validi e pronti per il meshing o la persistenza.
    chunk.isGenerated = true;
}

} // namespace fw
