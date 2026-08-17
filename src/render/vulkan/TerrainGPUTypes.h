#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

// Assicurarsi che le strutture GLSL (std430) abbiano lo stesso allineamento in C++

namespace fw {

struct MapRegionGPU {
    alignas(16) glm::vec3 centerNormal;  // offset 0
    alignas(4)  float angularRadius;     // offset 12
    alignas(16) glm::vec4 rectMinMax;    // offset 16 (minX, minY, maxX, maxY)
    alignas(4)  uint32_t shapeType;      // offset 32 (0=Rect, 1=Circle, 2=Rhombus, 3=Star)
    alignas(4)  uint32_t biomeType;      // offset 36
    alignas(4)  float perlinFreq;        // offset 40
    alignas(4)  float gravityMod;        // offset 44
    alignas(4)  uint32_t isGridAligned;  // offset 48
    alignas(4)  uint32_t surfaceBlock;   // offset 52
    alignas(4)  uint32_t subsurfaceBlock;// offset 56
    alignas(4)  uint32_t _pad;           // offset 60
};

} // namespace fw
