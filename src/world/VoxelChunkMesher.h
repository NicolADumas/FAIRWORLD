#pragma once

#include "components/ForgeComponents.h"
#include "core/utils/PlanetMath.h"

#include <array>
#include <cstdint>
#include <vector>

namespace fw {

class MaterialRegistry;

struct VoxelChunkCpuMesh {
    std::vector<Vertex> vertices;
    // Sequential triangle indices describe the existing non-indexed vertex stream.
    // The production Vulkan upload continues to draw vertices directly.
    std::vector<uint32_t> indices;
    int solidVoxels = 0;
    int sourceChunkX = 0;
    int sourceChunkZ = 0;
    PlanetChunkCoord sourcePlanetCoord{};
};

class VoxelChunkMesher {
public:
    // Neighbor slots are row-major [xOffset + 1][zOffset + 1]; center is ignored.
    static VoxelChunkCpuMesh Build(
        const VoxelChunkComponent& chunk,
        const std::array<const VoxelChunkComponent*, 9>& neighbors,
        const MaterialRegistry& materials,
        bool isSpherical,
        PlanetSize planetSize);
};

} // namespace fw
