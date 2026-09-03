#pragma once
#include <glm/glm.hpp>
#include <cstdint>

// ============================================================
// Struct che DEVONO combaciare 1:1 col layout std430 dello shader.
// Attenzione all'allineamento: vec3 in std430 si allinea a 16 byte,
// quindi ogni ChunkData/OutputData è paddato per restare multiplo di 16.
// ============================================================

struct alignas(16) ChunkData {
    glm::vec3 center;
    float     radius;
    glm::vec4 p00;
    glm::vec4 p10;
    glm::vec4 p01;
    glm::vec4 p11;
    uint32_t  chunkID;
    uint32_t  _pad0 = 0;
    uint32_t  _pad1 = 0;
    uint32_t  _pad2 = 0;
};
static_assert(sizeof(ChunkData) == 96, "ChunkData deve essere 96 byte per matchare lo shader");

enum class ChunkState : uint32_t {
    Outside      = 0,
    Inside       = 1,
    Intersecting = 2
};

struct alignas(16) OutputData {
    uint32_t state;
    uint32_t lod;
    uint32_t _pad0 = 0;
    uint32_t _pad1 = 0;
};
static_assert(sizeof(OutputData) == 16, "OutputData deve essere 16 byte per matchare lo shader");

// Push constants passate ogni frame
struct alignas(16) CullingPushConstants {
    glm::vec3 cameraPos;
    float     safeRegionRadius;
    glm::vec3 safeRegionCenter;
    uint32_t  numChunks;
};
