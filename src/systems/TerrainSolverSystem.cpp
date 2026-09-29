#include "pch.h"
#include <string_view>
#include "TerrainSolverSystem.h"
#include "world/TerrainSolver.h"
#include "BiomeComponents.h"
#include "core/entities/Components.h"
#include "BlockRegistry.h"
#include "ForgeComponents.h"
#include "world/MapDocument.h"

namespace fw {

static uint64_t ComputeVoxelHash(const VoxelChunkComponent& chunk) {
    uint64_t hash = 14695981039346656037ull; // FNV_offset_basis
    const uint8_t* data = &chunk.blocks[0][0][0];
    const size_t size = sizeof(chunk.blocks);
    
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull; // FNV_prime
    }
    return hash;
}

bool TerrainSolverSystem::s_enableVisualGateLog = false;

TerrainDiagnosticMode TerrainSolverSystem::s_DiagnosticMode = TerrainDiagnosticMode::None;
thread_local TerrainWorkspace TerrainSolverSystem::s_workspace;

int TerrainSolverSystem::Update(entt::registry& registry, std::vector<entt::entity>& dirtyQueue, int maxChunksPerFrame, std::function<void(entt::entity)> markMeshDirty, BlockRegistry* blockRegistry) {
    if (dirtyQueue.empty()) return 0;
    
    int processed = 0;
    std::vector<entt::entity> remaining;
    
    for (auto entity : dirtyQueue) {
        if (processed >= maxChunksPerFrame) {
            remaining.push_back(entity);
            continue;
        }
        
        if (!registry.valid(entity) || !registry.all_of<VoxelChunkComponent, BiomeDataComponent>(entity)) {
            continue; // Entity destroyed or missing components
        }
        
        auto& chunk = registry.get<VoxelChunkComponent>(entity);
        auto& biomeData = registry.get<BiomeDataComponent>(entity);
        
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
        
        ctx.isFlat = biomeData.isFlat;
        ctx.planetRadius = PlanetMath::GetPlanetRadius(biomeData.planetSize);
        ctx.faceGridResolution = PlanetMath::GetFaceResolution(biomeData.planetSize) * ctx.voxelResolutionX;
        
        ctx.diagnosticMode = s_DiagnosticMode;
        // ruleHash will be computed below
        
        // Ottieni le regole finali risolvendo i nomi dei blocchi
        TerrainRuleOverrides emptyOverrides;
        ResolvedTerrainRules rules = ResolveTerrainRules(biomeData.baseTerrain.baseRules, emptyOverrides, 1.0f, blockRegistry);
        
        // --- FORZATURA PER IL TEST MOUNTAINS RIMOSSA ---
        // Adesso utilizza i valori originali da regole UI/JSON
        // ---------------------------------------
        
        ctx.ruleHash = ComputeRuleHash(rules);
        
        if (chunk.isGenerated && chunk.lastRuleHash == ctx.ruleHash) {
            // registry.remove<BiomeDataComponent>(entity); // Keep for editor live preview
            continue;
        }
        
        bool wasGenerated = chunk.isGenerated;
        uint64_t oldVoxelHash = chunk.voxelHash;
        
        // Esegui la generazione tramite il nuovo TerrainSolver
        GenerateChunk(ctx, rules, chunk);
        chunk.lastRuleHash = ctx.ruleHash;
        chunk.isGenerated = true;
        
        uint64_t newVoxelHash = ComputeVoxelHash(chunk);
        
        // Rimuovi il BiomeDataComponent per segnalare che la generazione voxel e' completata
        // registry.remove<BiomeDataComponent>(entity); // Keep for editor live preview
        
        // Segna come dirty per aggiornare la mesh SOLO se i voxel sono effettivamente cambiati
        if (!wasGenerated || oldVoxelHash != newVoxelHash) {
            chunk.voxelHash = newVoxelHash;
            if (markMeshDirty) {
                markMeshDirty(entity);
            }
        }
        if (s_enableVisualGateLog) {
            std::cout << "[TRACE] GENERATING chunk visualGate=TRUE\n";
        }
        processed++;
    }
    
    dirtyQueue = std::move(remaining);
    
    if (processed > 0 && s_enableVisualGateLog) {
        std::cout << "[TRACE] TerrainSolverSystem processed=" << processed << '\n';
    }
    return processed;
}

void TerrainSolverSystem::GenerateChunk(const TerrainGenerationContext& context, const ResolvedTerrainRules& rules, VoxelChunkComponent& chunk) {
    TerrainSolver solver;
    
    // Esegui la generazione passando il workspace thread_local
    solver.GenerateChunk(context, rules, s_workspace, chunk);
    
    if (s_enableVisualGateLog) {
        std::cout << "[TRACE] ENTER VISUAL GATE BLOCK\n";
        
        // Calcola min/max per tutti i field
        auto getMinMax = [](const std::vector<float>& field, float& minOut, float& maxOut, float& avgOut) {
            minOut = 9999.0f;
            maxOut = -9999.0f;
            avgOut = 0.0f;
            for (int i = 0; i < 256; ++i) {
                float v = field[i];
                if (v < minOut) minOut = v;
                if (v > maxOut) maxOut = v;
                avgOut += v;
            }
            avgOut /= 256.0f;
        };
        
        float hMin, hMax, hAvg;
        getMinMax(s_workspace.surfaceHeights, hMin, hMax, hAvg);
        
        float mMin, mMax, mAvg;
        getMinMax(s_workspace.macroField, mMin, mMax, mAvg);
        
        float rMin, rMax, rAvg;
        getMinMax(s_workspace.regionalField, rMin, rMax, rAvg);
        
        float dMin, dMax, dAvg;
        getMinMax(s_workspace.detailField, dMin, dMax, dAvg);
        
        float riMin, riMax, riAvg;
        getMinMax(s_workspace.ridgeField, riMin, riMax, riAvg);

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
        std::cout << "[TerrainVisualGate][FIELDS]\n";
        std::cout << "Algorithm: " << (int)rules.rules.height.algorithm << "\n";
        std::cout << "Amplitude: " << rules.rules.height.amplitude << "\n";
        std::cout << "MacroScale: " << rules.rules.height.macroScale << "\n";
        std::cout << "Frequency: " << rules.rules.height.frequency << "\n";
        std::cout << "Octaves: " << rules.rules.height.octaves << "\n";
        std::cout << "Persistence: " << rules.rules.height.persistence << "\n";
        std::cout << "Lacunarity: " << rules.rules.height.lacunarity << "\n\n";
        
        std::cout << "Coordinates:\n";
        std::cout << "Chunk X: " << context.chunkCoord.x << "\n";
        std::cout << "Chunk Z: " << context.chunkCoord.z << "\n\n";
        
        std::cout << "RAW NOISE (MacroField):\n";
        std::cout << "Min: " << mMin << "\nMax: " << mMax << "\nDelta: " << (mMax - mMin) << "\n\n";

        std::cout << "REGIONAL FIELD:\n";
        std::cout << "Min: " << rMin << "\nMax: " << rMax << "\nDelta: " << (rMax - rMin) << "\n\n";
        
        std::cout << "DETAIL FIELD:\n";
        std::cout << "Min: " << dMin << "\nMax: " << dMax << "\nDelta: " << (dMax - dMin) << "\n\n";
        
        std::cout << "MORPHOLOGY (RidgeField):\n";
        std::cout << "Min: " << riMin << "\nMax: " << riMax << "\nDelta: " << (riMax - riMin) << "\n\n";
        
        std::cout << "FINAL HEIGHT:\n";
        std::cout << "Min: " << hMin << "\nMax: " << hMax << "\nDelta: " << (hMax - hMin) << "\n\n";
        
        std::cout << "[TerrainVisualGate][HEIGHT]\n";
        std::cout << "Min: " << hMin << "\n";
        std::cout << "Max: " << hMax << "\n";
        std::cout << "Delta: " << (hMax - hMin) << "\n";
        std::cout << "Average: " << hAvg << "\n\n";
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
    }

    // Il flag isGenerated indica inequivocabilmente che la pipeline procedurale è completa 
    // e i dati in chunk.blocks sono validi e pronti per il meshing o la persistenza.
    chunk.isGenerated = true;
}

} // namespace fw
