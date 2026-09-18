#include "pch.h"
#include "TerrainSolver.h"
#include "components/ForgeComponents.h"
#include "world/MapWorldGenerator.h" // For PerlinNoise or similar utility
#include "world/CubeSphereMapping.h"

namespace fw {

// Fast pure hash function (e.g. MurmurHash3 or FNV-1a or similar simple hash)
uint32_t TerrainGenerationContext::GetDeterministicHash(const glm::vec3& worldPos, int salt) const {
    // Simple spatial hashing based on world coordinates and seed
    uint32_t h = planetSeed ^ (uint32_t)salt;
    
    // Discretize to avoid floating point inconsistencies
    int32_t ix = (int32_t)std::floor(worldPos.x * 1000.0f);
    int32_t iy = (int32_t)std::floor(worldPos.y * 1000.0f);
    int32_t iz = (int32_t)std::floor(worldPos.z * 1000.0f);
    
    // Hash mix
    h ^= ix; h *= 0x5bd1e995; h ^= h >> 15;
    h ^= iy; h *= 0x5bd1e995; h ^= h >> 15;
    h ^= iz; h *= 0x5bd1e995; h ^= h >> 15;
    
    return h;
}

void TerrainWorkspace::EnsureCapacity(size_t size2D, size_t size3D) {
    if (surfaceHeights.capacity() < size2D) {
        macroField.reserve(size2D);
        regionalField.reserve(size2D);
        detailField.reserve(size2D);
        ridgeField.reserve(size2D);
        valleyField.reserve(size2D);
        surfaceHeights.reserve(size2D);
    }
    
    if (caveDensity.capacity() < size3D) {
        caveDensity.reserve(size3D);
        waterLevel.reserve(size3D);
        layerIndices.reserve(size3D);
    }
}

void TerrainWorkspace::Reset(int width, int height, int depth) {
    size_t size2D = width * depth;
    size_t size3D = width * height * depth;
    EnsureCapacity(size2D, size3D);
    
    macroField.assign(size2D, 0.0f);
    regionalField.assign(size2D, 0.0f);
    detailField.assign(size2D, 0.0f);
    ridgeField.assign(size2D, 0.0f);
    valleyField.assign(size2D, 0.0f);
    surfaceHeights.assign(size2D, 0.0f);
    
    caveDensity.assign(size3D, 0.0f);
    waterLevel.assign(size3D, 0.0f);
    layerIndices.assign(size3D, 0);
}

glm::vec3 TerrainSolver::GetVoxelSpherePos(const TerrainGenerationContext& ctx, int x, int y, int z) const {
    // 1. Calculate global voxel coordinates on the face grid
    // For seamless connections, we map voxel centers.
    // However, to ensure boundaries perfectly match, we define the UV domain exactly.
    int globalX = ctx.chunkCoord.x * ctx.voxelResolutionX + x;
    int globalZ = ctx.chunkCoord.z * ctx.voxelResolutionZ + z;
    
    // 2. Map to UV [0, 1] across the face.
    // If faceGridResolution is the total number of voxels across a face (e.g. chunks * chunk_size)
    // we divide by faceGridResolution to get UV.
    float u = (float)globalX / (float)ctx.faceGridResolution;
    float v = (float)globalZ / (float)ctx.faceGridResolution;
    
    // 3. Convert Face+UV to spherical direction
    glm::vec3 pos = CubeSphereMapping::FaceUVToDirection(ctx.faceIndex, glm::vec2(u, v));
    
    return glm::normalize(pos);
}

void TerrainSolver::GenerateChunk(
    const TerrainGenerationContext& context,
    const ResolvedTerrainRules& rules,
    TerrainWorkspace& workspace,
    fw::VoxelChunkComponent& outputChunk) 
{
    // 1. Reset Workspace
    workspace.Reset(context.voxelResolutionX, context.voxelResolutionY, context.voxelResolutionZ);
    
    // 2. Evaluate Height Fields (Macro, Regional, Detail, Morphology)
    EvaluateHeightFields(context, rules, workspace);
    
    // 3. Evaluate volumetric fields (Caves, Water)
    EvaluateCavesAndWater(context, rules, workspace);
    
    // 4. Evaluate layers (relative depth based)
    EvaluateLayers(context, rules, workspace);
    
    // 5. Final Materialization (Voxel Classifier)
    ClassifyVoxels(context, rules, workspace, outputChunk);
}

void TerrainSolver::EvaluateHeightFields(const TerrainGenerationContext& ctx, const ResolvedTerrainRules& rules, TerrainWorkspace& ws) {
    // Deterministic coherent noise generators seeded by planet seed
    // Using simple prime numbers for salts
    uint32_t macroSeed = ctx.planetSeed ^ 0x1234567;
    uint32_t regionalSeed = ctx.planetSeed ^ 0x89ABCDEF;
    uint32_t detailSeed = ctx.planetSeed ^ 0xFEDCBA98;
    
    const float radius = 100.0f; // Planet base radius (should come from context/rules)
    
    for (int z = 0; z < ctx.voxelResolutionZ; ++z) {
        for (int x = 0; x < ctx.voxelResolutionX; ++x) {
            int idx2D = z * ctx.voxelResolutionX + x;
            
            glm::vec3 spherePos = GetVoxelSpherePos(ctx, x, 0, z);
            
            // In a real implementation we would sample Perlin/Simplex here
            // float macroNoise = SampleNoise(spherePos, macroSeed, rules.height.macroScale);
            // float regionalNoise = SampleNoise(spherePos, regionalSeed, rules.height.regionalScale);
            // float detailNoise = SampleNoise(spherePos, detailSeed, rules.height.detailScale);
            
            // For now, placeholder math
            float macroVal = 0.0f; 
            float regionalVal = 0.0f;
            float detailVal = 0.0f;
            
            ws.macroField[idx2D] = macroVal;
            ws.regionalField[idx2D] = regionalVal;
            ws.detailField[idx2D] = detailVal;
            
            // Morphology
            float ridgeVal = 1.0f - std::abs(regionalVal); // Example ridge
            ws.ridgeField[idx2D] = ridgeVal;
            
            float valleyVal = regionalVal * regionalVal; // Example valley
            ws.valleyField[idx2D] = valleyVal;
            
            // Compose final height
            float finalHeight = radius + rules.rules.height.baseHeight 
                + (macroVal * rules.rules.height.amplitude)
                + (regionalVal * rules.rules.height.amplitude * 0.5f)
                + (detailVal * rules.rules.height.amplitude * 0.1f);
                
            ws.surfaceHeights[idx2D] = finalHeight;
        }
    }
}

void TerrainSolver::EvaluateCavesAndWater(const TerrainGenerationContext& ctx, const ResolvedTerrainRules& rules, TerrainWorkspace& ws) {
    // Generate volumetric data for caves and water bodies
    for (int y = 0; y < ctx.voxelResolutionY; ++y) {
        for (int z = 0; z < ctx.voxelResolutionZ; ++z) {
            for (int x = 0; x < ctx.voxelResolutionX; ++x) {
                int idx3D = (y * ctx.voxelResolutionZ * ctx.voxelResolutionX) + (z * ctx.voxelResolutionX) + x;
                
                // glm::vec3 spherePos = GetVoxelSpherePos(ctx, x, y, z);
                
                ws.caveDensity[idx3D] = 0.0f; // No caves for now
                ws.waterLevel[idx3D] = 0.0f;
            }
        }
    }
}

void TerrainSolver::EvaluateLayers(const TerrainGenerationContext& ctx, const ResolvedTerrainRules& rules, TerrainWorkspace& ws) {
    for (int y = 0; y < ctx.voxelResolutionY; ++y) {
        // Voxel world Y (simplified, usually chunkY * size + y)
        float voxelY = (float)y;
        
        for (int z = 0; z < ctx.voxelResolutionZ; ++z) {
            for (int x = 0; x < ctx.voxelResolutionX; ++x) {
                int idx2D = z * ctx.voxelResolutionX + x;
                int idx3D = (y * ctx.voxelResolutionZ * ctx.voxelResolutionX) + (z * ctx.voxelResolutionX) + x;
                
                float surfaceY = ws.surfaceHeights[idx2D];
                float depth = surfaceY - voxelY;
                
                uint8_t layerIdx = 0; // 0 = Air/Core
                
                if (depth >= 0.0f) {
                    layerIdx = 255; // Core block by default
                    
                    // Check custom layers
                    for (size_t i = 0; i < rules.rules.layers.layers.size(); ++i) {
                        const auto& layer = rules.rules.layers.layers[i];
                        if (depth >= layer.minDepth && depth < layer.maxDepth) {
                            layerIdx = (uint8_t)(i + 1); // 1-indexed to differentiate from Core
                            break;
                        }
                    }
                }
                
                ws.layerIndices[idx3D] = layerIdx;
            }
        }
    }
}

void TerrainSolver::ClassifyVoxels(const TerrainGenerationContext& ctx, const ResolvedTerrainRules& rules, TerrainWorkspace& ws, fw::VoxelChunkComponent& output) {
    uint8_t coreBlockId = static_cast<uint8_t>(rules.resolvedCoreBlock);
    uint8_t waterBlockId = static_cast<uint8_t>(rules.resolvedWaterBlock);
    uint8_t airBlockId = 0;
    
    for (int y = 0; y < ctx.voxelResolutionY; ++y) {
        float voxelY = (float)y;
        
        for (int z = 0; z < ctx.voxelResolutionZ; ++z) {
            for (int x = 0; x < ctx.voxelResolutionX; ++x) {
                int idx3D = (y * ctx.voxelResolutionZ * ctx.voxelResolutionX) + (z * ctx.voxelResolutionX) + x;
                int idx2D = (z * ctx.voxelResolutionX) + x;
                
                uint8_t layerIdx = ws.layerIndices[idx3D];
                uint8_t finalBlock = airBlockId;
                
                if (layerIdx == 255) {
                    finalBlock = coreBlockId;
                } else if (layerIdx > 0) {
                    // It's a specific layer
                    if (layerIdx - 1 < rules.resolvedLayerBlocks.size()) {
                        finalBlock = static_cast<uint8_t>(rules.resolvedLayerBlocks[layerIdx - 1]);
                    } else {
                        finalBlock = coreBlockId;
                    }
                } else {
                    // Above surface
                    if (rules.rules.water.enabled && voxelY < rules.rules.water.globalLevel) {
                        finalBlock = waterBlockId;
                    }
                }
                
                // Cave pass
                if (ws.caveDensity[idx3D] > 0.5f) {
                    finalBlock = airBlockId;
                }
                
                if (ctx.diagnosticMode != TerrainDiagnosticMode::None) {
                    float val = 0.0f;
                    switch (ctx.diagnosticMode) {
                        case TerrainDiagnosticMode::MacroField: val = std::abs(ws.macroField[idx2D]); break;
                        case TerrainDiagnosticMode::RegionalField: val = std::abs(ws.regionalField[idx2D]); break;
                        case TerrainDiagnosticMode::DetailField: val = std::abs(ws.detailField[idx2D]); break;
                        case TerrainDiagnosticMode::RidgeField: val = std::abs(ws.ridgeField[idx2D]); break;
                        case TerrainDiagnosticMode::ValleyField: val = std::abs(ws.valleyField[idx2D]); break;
                        case TerrainDiagnosticMode::CaveDensity: val = ws.caveDensity[idx3D]; break;
                        case TerrainDiagnosticMode::WaterLevel: val = ws.waterLevel[idx3D]; break;
                        case TerrainDiagnosticMode::LayerIndex: val = (ws.layerIndices[idx3D] > 0) ? 1.0f : 0.0f; break;
                        default: break;
                    }
                    
                    // Visualizzazione: se il valore supera una soglia, mettiamo un blocco per vederne la forma.
                    // Poiche' non abbiamo una palette di colori, usiamo un trucco: disegniamo il blocco solo se (voxelY - baseHeight) < val * 50
                    // per campi 2D, cosi' vediamo il field come un'altitudine.
                    if (ctx.diagnosticMode == TerrainDiagnosticMode::LayerIndex || 
                        ctx.diagnosticMode == TerrainDiagnosticMode::CaveDensity ||
                        ctx.diagnosticMode == TerrainDiagnosticMode::WaterLevel) 
                    {
                        if (val > 0.5f) finalBlock = coreBlockId;
                        else finalBlock = airBlockId;
                    } 
                    else 
                    {
                        // Per i field 2D topografici
                        float renderHeight = rules.rules.height.baseHeight + (val * 30.0f); 
                        if (voxelY < renderHeight) finalBlock = coreBlockId;
                        else finalBlock = airBlockId;
                    }
                }
                
                output.blocks[x][y][z] = finalBlock;
            }
        }
    }
}

} // namespace fw
