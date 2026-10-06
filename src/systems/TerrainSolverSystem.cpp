#include "pch.h"
#include <string_view>
#include "TerrainSolverSystem.h"
#include "world/TerrainSolver.h"
#include "BiomeComponents.h"
#include "core/entities/Components.h"
#include "BlockRegistry.h"
#include "ForgeComponents.h"
#include "world/MapDocument.h"
#include "world/PlanetRadialMapping.h"
#include "core/utils/ChunkDimensions.h"

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
        ctx.planetSeed = biomeData.baseTerrain.seed;
        
        if (chunk.planetCoord.planet.IsValid()) {
            ctx.faceIndex = static_cast<int>(chunk.planetCoord.face);
            ctx.chunkCoord = {chunk.planetCoord.col, chunk.planetCoord.row};
            ctx.layer = chunk.planetCoord.layer;
        } else {
            ctx.chunkCoord = {chunk.cx, chunk.cz};
            ctx.layer = 0;
        }
        
        // Centriamo nello spazio sferico (placeholder approssimato)
        ctx.chunkCenterSphere = glm::normalize(biomeData.chunkCenterWorld);
        if (glm::length(biomeData.chunkCenterWorld) < 0.1f) {
            ctx.chunkCenterSphere = glm::vec3(0.0f, 1.0f, 0.0f); // Fallback sicuro
        }
        
        ctx.voxelResolutionX = ChunkDimensions::VoxelsX;
        ctx.voxelResolutionY = ChunkDimensions::VoxelsY;
        ctx.voxelResolutionZ = ChunkDimensions::VoxelsZ;
        
        ctx.isFlat = biomeData.isFlat;
        ctx.planetRadius = PlanetMath::GetPlanetRadius(biomeData.planetSize);
        ctx.faceGridResolution = PlanetMath::GetFaceResolution(biomeData.planetSize) * ctx.voxelResolutionX;
        
        ctx.diagnosticMode = s_DiagnosticMode;
        
        // Setup Spatial Radial Validity Mask
        ctx.firstValidRadialY = 0;
        if (!ctx.isFlat) {
            for (int y = 0; y < ctx.voxelResolutionY; ++y) {
                if (PlanetRadialMapping::ClassifyCellRadialDomain(ctx.layer, y, ctx.planetRadius) == PlanetRadialMapping::RadialDomain::Valid) {
                    ctx.firstValidRadialY = y;
                    break;
                }
                if (y == ctx.voxelResolutionY - 1) {
                    ctx.firstValidRadialY = ctx.voxelResolutionY; // Entire chunk invalid
                }
            }
        }
        
        // ruleHash will be computed below
        
        // Ottieni le regole finali risolvendo i nomi dei blocchi per le base rules
        TerrainRuleOverrides emptyOverrides;
        ResolvedTerrainRules baseRules = ResolveTerrainRules(biomeData.baseTerrain.baseRules, emptyOverrides, 1.0f, blockRegistry);
        
        uint64_t combinedHash = ComputeRuleHash(baseRules);
        
        // Include base seed in hash
        uint64_t baseSeed = ctx.planetSeed;
        combinedHash ^= (baseSeed + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2));
        
        // Risolvi le regole per ogni regione e calcola l'hash composto
        std::vector<std::pair<fw::MapRegion, ResolvedTerrainRules>> resolvedRegions;
        resolvedRegions.reserve(biomeData.overlappingRegions.size());
        
        for (const auto& r : biomeData.overlappingRegions) {
            // Risolviamo usando la logica B4 esistente
            ResolvedTerrainRules rRules = ResolveTerrainRules(baseRules.rules, r.overrides, 1.0f, blockRegistry);
            resolvedRegions.push_back({r, rRules});
            
            // Hash dei dati WHAT (regole risultanti)
            uint64_t rHash = ComputeRuleHash(rRules);
            
            // Hash dei dati WHERE (impronta spaziale)
            uint64_t whereHash = 14695981039346656037ull;
            auto mix = [&whereHash](auto val) {
                const uint8_t* p = reinterpret_cast<const uint8_t*>(&val);
                for (size_t i = 0; i < sizeof(val); ++i) {
                    whereHash ^= p[i];
                    whereHash *= 1099511628211ull;
                }
            };
            mix(r.rectMin.x); mix(r.rectMin.y);
            mix(r.rectMax.x); mix(r.rectMax.y);
            mix((int)r.shape);
            mix(r.angularRadius);
            mix(r.influence); // falloff
            
            uint32_t effRegionSeed = r.seed.value_or(ctx.planetSeed);
            mix(effRegionSeed);
            
            // Combine hash
            combinedHash ^= (rHash + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2));
            combinedHash ^= (whereHash + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2));
        }
        
        ctx.ruleHash = combinedHash;
        
        if (chunk.isGenerated && chunk.lastRuleHash == ctx.ruleHash) {
            // registry.remove<BiomeDataComponent>(entity); // Keep for editor live preview
            continue;
        }
        
        bool wasGenerated = chunk.isGenerated;
        uint64_t oldVoxelHash = chunk.voxelHash;
        
        // Esegui la generazione tramite il nuovo TerrainSolver
        GenerateChunk(ctx, baseRules, resolvedRegions, chunk);
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

void TerrainSolverSystem::GenerateChunk(const TerrainGenerationContext& context, const ResolvedTerrainRules& baseRules, const std::vector<std::pair<fw::MapRegion, ResolvedTerrainRules>>& regions, VoxelChunkComponent& chunk) {
    TerrainSolver solver;
    
    // Esegui la generazione passando il workspace thread_local
    solver.GenerateChunk(context, baseRules, regions, s_workspace, chunk);
    
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
                    else if (block == baseRules.resolvedWaterBlock && baseRules.rules.water.enabled) waterVoxels++;
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
        std::cout << "[TerrainVisualGate][BASE]\n";
        std::cout << "Algorithm: " << (int)baseRules.rules.height.algorithm << "\n";
        std::cout << "Amplitude: " << baseRules.rules.height.common.amplitude << "\n";
        std::cout << "Frequency: " << baseRules.rules.height.common.frequency << "\n";
        std::cout << "MacroScale: " << baseRules.rules.height.common.macroScale << "\n\n";
        
        for (size_t i = 0; i < regions.size(); ++i) {
            const auto& r = regions[i].first;
            const auto& rRules = regions[i].second;
            std::cout << "[TerrainVisualGate][REGION]\n";
            std::cout << "Region index: " << i << "\n";
            std::cout << "Biome/type: " << (int)r.type << "\n";
            std::cout << "Algorithm: " << (int)rRules.rules.height.algorithm << "\n";
            std::cout << "Amplitude: " << rRules.rules.height.common.amplitude << "\n";
            std::cout << "Frequency: " << rRules.rules.height.common.frequency << "\n";
            std::cout << "MacroScale: " << rRules.rules.height.common.macroScale << "\n";
            std::cout << "Influence / relevant spatial data: " << r.influence << " (Rect: " << r.rectMin.x << "," << r.rectMin.y << " to " << r.rectMax.x << "," << r.rectMax.y << ")\n\n";
        }
        
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
        
        bool isHillsActive = (baseRules.rules.height.algorithm == fw::TerrainAlgorithmType::Hills);
        for (const auto& r : regions) {
            if (r.second.rules.height.algorithm == fw::TerrainAlgorithmType::Hills) {
                isHillsActive = true;
            }
        }
        
        if (isHillsActive) {
            std::cout << "[TerrainVisualGate][HILLS]\n";
            std::cout << "BaseHeight: " << baseRules.rules.height.common.baseHeight << "\n";
            std::cout << "Amplitude: " << baseRules.rules.height.common.amplitude << "\n";
            std::cout << "Frequency: " << baseRules.rules.height.common.frequency << "\n";
            std::cout << "MacroScale: " << baseRules.rules.height.common.macroScale << "\n";
            
            if (auto* spec = std::get_if<fw::HillsRules>(&baseRules.rules.height.specialized)) {
                std::cout << "Octaves: " << spec->octaves << "\n";
                std::cout << "Roundness: " << spec->roundness << "\n\n";
            } else {
                std::cout << "Octaves: N/A\nRoundness: N/A\n\n";
            }
            
            std::cout << "Raw/Noise Min: " << dMin << "\n";
            std::cout << "Raw/Noise Max: " << dMax << "\n\n";
            std::cout << "Response Min: " << mMin << "\n";
            std::cout << "Response Max: " << mMax << "\n\n";
            std::cout << "Final Height Min: " << hMin << "\n";
            std::cout << "Final Height Max: " << hMax << "\n";
            std::cout << "Final Height Delta: " << (hMax - hMin) << "\n\n";
        }
        
        bool isMountainsActive = (baseRules.rules.height.algorithm == fw::TerrainAlgorithmType::Mountains);
        for (const auto& r : regions) {
            if (r.second.rules.height.algorithm == fw::TerrainAlgorithmType::Mountains) {
                isMountainsActive = true;
            }
        }
        
        if (isMountainsActive) {
            std::cout << "[TerrainVisualGate][MOUNTAINS]\n";
            std::cout << "BaseHeight: " << baseRules.rules.height.common.baseHeight << "\n";
            std::cout << "Amplitude: " << baseRules.rules.height.common.amplitude << "\n";
            std::cout << "Frequency: " << baseRules.rules.height.common.frequency << "\n";
            std::cout << "MacroScale: " << baseRules.rules.height.common.macroScale << "\n";
            
            if (auto* spec = std::get_if<fw::MountainRules>(&baseRules.rules.height.specialized)) {
                std::cout << "Octaves: " << spec->octaves << "\n";
                std::cout << "Persistence: " << spec->persistence << "\n";
                std::cout << "RidgeStrength: " << spec->ridgeStrength << "\n\n";
            } else {
                std::cout << "Octaves: N/A\nPersistence: N/A\nRidgeStrength: N/A\n\n";
            }
            
            std::cout << "Mountain Mass Min: " << mMin << "\n";
            std::cout << "Mountain Mass Max: " << mMax << "\n\n";
            std::cout << "Ridge Response Min: " << riMin << "\n";
            std::cout << "Ridge Response Max: " << riMax << "\n\n";
            std::cout << "Final Response Min: " << dMin << "\n";
            std::cout << "Final Response Max: " << dMax << "\n\n";
            std::cout << "Final Height Min: " << hMin << "\n";
            std::cout << "Final Height Max: " << hMax << "\n";
            std::cout << "Final Height Delta: " << (hMax - hMin) << "\n\n";
        }
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
