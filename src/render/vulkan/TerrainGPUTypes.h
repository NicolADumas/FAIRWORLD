#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

// Assicurarsi che le strutture GLSL (std430) abbiano lo stesso allineamento in C++

namespace fw {

struct MapRegionGPU {
    alignas(16) glm::vec3 centerNormal;  // offset 0
    alignas(4)  float angularRadius;     // offset 12
    alignas(16) glm::vec4 rectMinMax;    // offset 16 (gridX_min, gridY_min, gridX_max, gridY_max)
    alignas(4)  uint32_t shapeType;      // offset 32
    alignas(4)  uint32_t biomeType;      // offset 36
    alignas(4)  float perlinFreq;        // offset 40
    alignas(4)  float gravityMod;        // offset 44
    alignas(4)  uint32_t isGridAligned;  // offset 48
    alignas(4)  uint32_t faceIndex;      // offset 52 (0-5 per le 6 facce del cubo)
    alignas(4)  uint32_t surfaceBlock;   // offset 56
    alignas(4)  uint32_t subsurfaceBlock;// offset 60
};

} // namespace fw
