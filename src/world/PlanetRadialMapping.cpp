#include "pch.h"
#include "PlanetRadialMapping.h"
#include <cmath>

namespace fw {

float PlanetRadialMapping::GetGlobalRadialOffset(int32_t layer, float localY) {
    return static_cast<float>(layer * ChunkDimensions::VoxelsY) + localY - static_cast<float>(ReferenceVoxelOffset);
}

int32_t PlanetRadialMapping::GetGlobalRadialOffsetDiscrete(int32_t layer, int32_t localY) {
    return (layer * ChunkDimensions::VoxelsY) + localY - ReferenceVoxelOffset;
}

float PlanetRadialMapping::GetRadialDistance(float g, float planetRadius) {
    return planetRadius + (g * ChunkDimensions::VoxelWorldSize);
}

bool PlanetRadialMapping::IsValidRadialDistance(float r) {
    return r > 0.0f;
}

float PlanetRadialMapping::GetGlobalRadialOffsetFromDistance(float r, float planetRadius) {
    return (r - planetRadius) / ChunkDimensions::VoxelWorldSize;
}

void PlanetRadialMapping::GetLayerAndLocalY(float g, int32_t& outLayer, float& outLocalY) {
    float shifted_g = g + static_cast<float>(ReferenceVoxelOffset);
    float layer_f = std::floor(shifted_g / static_cast<float>(ChunkDimensions::VoxelsY));
    outLayer = static_cast<int32_t>(layer_f);
    outLocalY = shifted_g - (layer_f * static_cast<float>(ChunkDimensions::VoxelsY));
}

void PlanetRadialMapping::GetLayerAndLocalYDiscrete(int32_t g, int32_t& outLayer, int32_t& outLocalY) {
    int32_t shifted_g = g + ReferenceVoxelOffset;
    // C++ division truncates towards zero. We must explicitly handle negative floor division.
    outLayer = (shifted_g >= 0) 
        ? (shifted_g / ChunkDimensions::VoxelsY) 
        : ((shifted_g - ChunkDimensions::VoxelsY + 1) / ChunkDimensions::VoxelsY);
    outLocalY = shifted_g - (outLayer * ChunkDimensions::VoxelsY);
}

PlanetRadialMapping::RadialDomain PlanetRadialMapping::ClassifyChunkRadialDomain(int32_t layer, float planetRadius) {
    float gMin = GetGlobalRadialOffset(layer, 0.0f);
    float gMax = GetGlobalRadialOffset(layer, static_cast<float>(ChunkDimensions::VoxelsY));

    float rMin = GetRadialDistance(gMin, planetRadius);
    float rMax = GetRadialDistance(gMax, planetRadius);

    if (rMin > 0.0f) {
        return RadialDomain::Valid;
    } else if (rMax > 0.0f) {
        return RadialDomain::IntersectsCore;
    } else {
        return RadialDomain::BeyondCore;
    }
}

PlanetRadialMapping::RadialDomain PlanetRadialMapping::ClassifyCellRadialDomain(int32_t layer, int32_t localCellY, float planetRadius) {
    float g0 = GetGlobalRadialOffset(layer, static_cast<float>(localCellY));
    float g1 = GetGlobalRadialOffset(layer, static_cast<float>(localCellY + 1));

    float r0 = GetRadialDistance(g0, planetRadius);
    float r1 = GetRadialDistance(g1, planetRadius);

    if (r0 > 0.0f && r1 > 0.0f) {
        return RadialDomain::Valid;
    } else if (r0 <= 0.0f && r1 > 0.0f) {
        return RadialDomain::IntersectsCore;
    } else {
        return RadialDomain::BeyondCore;
    }
}

} // namespace fw
