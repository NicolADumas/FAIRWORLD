#pragma once
#include <vector>
#include <cstdint>
#include <span>
#include <glm/glm.hpp>
#include "core/entities/Components.h"
#include "world/MapDocument.h"

namespace fw {

struct VoxelChunkComponent;

enum class TerrainDiagnosticMode {
    None,
    MacroField,
    RegionalField,
    DetailField,
    RidgeField,
    ValleyField,
    CaveDensity,
    WaterLevel,
    LayerIndex
};

// The context for deterministic terrain generation
struct TerrainGenerationContext {
    uint32_t planetSeed;
    ChunkCoord chunkCoord;
    int32_t layer = 0; // Canonical radial chunk layer
    glm::vec3 chunkCenterSphere; // Center of the chunk in normalized spherical coordinates
    int faceIndex = 0;
    int faceGridResolution = 100; // Valore di default
    int voxelResolutionX;
    int voxelResolutionY;
    int voxelResolutionZ;
    
    // Spatial Validity (Compact radial mask)
    int32_t firstValidRadialY = 0; 

    
    // Configurazione Architetturale del Mondo
    bool isFlat = false;
    float planetRadius = 1000.0f; // Usato se isFlat == false per scalare le frequenze
    float planetaryReliefScale = 1.0f; // Multiplier per il displacement morfologico planetario
    
    TerrainDiagnosticMode diagnosticMode = TerrainDiagnosticMode::None;
    uint64_t ruleHash; // For cache and invalidation purposes only
    uint64_t oldRuleHash; // For diagnostics
    
    // Pure deterministic hash function for spatial sampling
    uint32_t GetDeterministicHash(const glm::vec3& worldPos, int salt) const;
};

// Reusable workspace for chunk generation to avoid continuous memory allocation
struct TerrainWorkspace {
    std::vector<float> macroField;
    std::vector<float> regionalField;
    std::vector<float> detailField;
    std::vector<float> ridgeField;
    std::vector<float> valleyField;
    std::vector<float> surfaceHeights;
    std::vector<float> caveDensity;
    std::vector<float> waterLevel;
    std::vector<uint8_t> layerIndices; // Store layer evaluations for final classification
    std::vector<int> dominantRegionIndex; // 2D array mapping each (x,z) to the winning region index (-1 for base)
    
    void Reset(int width, int height, int depth);
    void EnsureCapacity(size_t size2D, size_t size3D);
};

class TerrainSolver {
public:
    TerrainSolver() = default;
    ~TerrainSolver() = default;

    // Main entry point for the Job System
    void GenerateChunk(
        const TerrainGenerationContext& context,
        const ResolvedTerrainRules& baseRules,
        const std::vector<std::pair<fw::MapRegion, ResolvedTerrainRules>>& regions,
        TerrainWorkspace& workspace,
        fw::VoxelChunkComponent& outputChunk
    );

    // Helpers
    glm::vec3 GetVoxelSpherePos(const TerrainGenerationContext& ctx, int x, int y, int z) const;

private:
    // Modular Generation Stages
    void EvaluateHeightFields(const TerrainGenerationContext& ctx, const ResolvedTerrainRules& baseRules, const std::vector<std::pair<fw::MapRegion, ResolvedTerrainRules>>& regions, TerrainWorkspace& ws);
    void EvaluateCavesAndWater(const TerrainGenerationContext& ctx, const ResolvedTerrainRules& baseRules, const std::vector<std::pair<fw::MapRegion, ResolvedTerrainRules>>& regions, TerrainWorkspace& ws);
    void EvaluateLayers(const TerrainGenerationContext& ctx, const ResolvedTerrainRules& baseRules, const std::vector<std::pair<fw::MapRegion, ResolvedTerrainRules>>& regions, TerrainWorkspace& ws);
    
    // Final classification step: Data to Voxel
    void ClassifyVoxels(const TerrainGenerationContext& ctx, const ResolvedTerrainRules& baseRules, const std::vector<std::pair<fw::MapRegion, ResolvedTerrainRules>>& regions, TerrainWorkspace& ws, fw::VoxelChunkComponent& output);
    
    // Helper to evaluate one specific algorithm into a workspace
    void EvaluateAlgorithmIntoWorkspace(const TerrainGenerationContext& ctx, const ResolvedTerrainRules& rules, TerrainWorkspace& ws);
};

} // namespace fw
