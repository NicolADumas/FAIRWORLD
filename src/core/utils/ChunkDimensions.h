#pragma once
#include <cstdint>

namespace fw {

struct ChunkDimensions {
    static constexpr int32_t VoxelsX = 16;
    static constexpr int32_t VoxelsY = 128;
    static constexpr int32_t VoxelsZ = 16;

    static_assert(VoxelsX > 0, "VoxelsX must be positive");
    static_assert(VoxelsY > 0, "VoxelsY must be positive");
    static_assert(VoxelsZ > 0, "VoxelsZ must be positive");
    static_assert(VoxelsX == VoxelsZ, "Horizontal chunk dimensions must be square");

    static constexpr float VoxelWorldSize = 1.0f;
    static_assert(VoxelWorldSize > 0.0f, "VoxelWorldSize must be positive");

    static constexpr float WorldExtentX = static_cast<float>(VoxelsX) * VoxelWorldSize;
    static constexpr float WorldExtentY = static_cast<float>(VoxelsY) * VoxelWorldSize;
    static constexpr float WorldExtentZ = static_cast<float>(VoxelsZ) * VoxelWorldSize;

    static constexpr float HorizontalWorldExtent = WorldExtentX;
    
    static constexpr float RadialOriginVoxel = static_cast<float>(VoxelsY) * 0.5f;
};

} // namespace fw
