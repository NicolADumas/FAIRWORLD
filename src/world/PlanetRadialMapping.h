#pragma once

#include <cstdint>
#include "core/utils/ChunkDimensions.h"

namespace fw {

class PlanetRadialMapping {
public:
    enum class RadialDomain {
        Valid,           // OUTSIDE_CORE
        IntersectsCore,  // CORE_BOUNDARY
        BeyondCore       // INVALID
    };

    // Structural canonical reference shell offset inside layer 0
    static constexpr int32_t ReferenceVoxelOffset = ChunkDimensions::VoxelsY / 2;

    // A. Forward Mapping: (layer, localVertexY) -> global radial voxel offset g
    // Float overload for continuous coordinates (mesh vertices, raycasts)
    static float GetGlobalRadialOffset(int32_t layer, float localY);
    
    // Discrete overload for voxel cell logic
    static int32_t GetGlobalRadialOffsetDiscrete(int32_t layer, int32_t localY);

    // B. Forward Mapping: (g, PlanetRadius) -> radial distance r
    static float GetRadialDistance(float g, float planetRadius);

    // Radial distance to the continuous cell coordinate consumed by floor().
    // Voxel samples are centered on integer localY coordinates, so their cells
    // span [localY - 0.5, localY + 0.5); the returned value includes that half-cell.
    static float GetVoxelCellCoordinateFromDistance(float radialDistance, float planetRadius);

    // Radial center and outer face of a voxel cell, in world units.
    static float GetCellCenterRadius(int32_t layer, int32_t localCellY, float planetRadius);
    static float GetCellOuterRadius(int32_t layer, int32_t localCellY, float planetRadius);

    // C. Physical Domain validation
    static bool IsValidRadialDistance(float r);

    // Inverse Mapping: r -> g
    static float GetGlobalRadialOffsetFromDistance(float r, float planetRadius);

    // Inverse Mapping: g -> layer + localY
    // Continuous version preserves sub-voxel fraction
    static void GetLayerAndLocalY(float g, int32_t& outLayer, float& outLocalY);
    
    // Discrete version correctly handles negative integer division using floor semantics
    static void GetLayerAndLocalYDiscrete(int32_t g, int32_t& outLayer, int32_t& outLocalY);

    // D. Classification APIs
    static RadialDomain ClassifyChunkRadialDomain(int32_t layer, float planetRadius);
    static RadialDomain ClassifyCellRadialDomain(int32_t layer, int32_t localCellY, float planetRadius);
};

} // namespace fw
