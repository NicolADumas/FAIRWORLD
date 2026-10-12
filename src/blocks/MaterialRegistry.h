#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <cstdint>
#include "ShapeRegistry.h"
#include "SurfaceDefinition.h"

namespace fw {

enum BlockBehavior : uint32_t {
    BLOCK_BEHAVIOR_NONE            = 0,
    BLOCK_BEHAVIOR_SEASONAL        = 1u << 0,
    BLOCK_BEHAVIOR_ANIMATED        = 1u << 1,
    BLOCK_BEHAVIOR_WIND_AFFECTED   = 1u << 2,
    BLOCK_BEHAVIOR_EMISSIVE        = 1u << 3,
};

// Struttura 16-byte allineata per l'SSBO GPU
struct BlockPropertiesGPU {
    glm::vec4 pbr;          // x: roughness, y: metallic, z: emissive, w: alpha
    uint32_t behaviors;     // Bitmask (BlockBehavior)
    uint32_t visualMode;
    uint32_t physicalFlags;
    uint32_t reserved;
};

struct PBRMaterialDef {
    uint8_t target_block_id = 0; // Maps back to the SimBlockDef id
    std::string albedoPath = "";
    std::string normalPath = "";
    std::string ormPath = "";
    
    // Fallback for missing textures (or for the old ForgeWorld / BlockMaker renderers)
    glm::vec3 baseColorFallback = glm::vec3(1.0f);
    float roughnessFallback = 1.0f;
    float metallicFallback = 0.0f;
    float emissiveStrength = 0.0f;
    float alphaFallback = 1.0f;

    uint32_t behaviors = BLOCK_BEHAVIOR_NONE;

    // Parametric Geometry (Per-Block)
    int shapeType = 0;          // 0 = Standard Voxel Cube, 1 = SuperSphere (|x|^n + |y|^n + |z|^n = 1)
    float superSphereN = 2.0f;  // Exponent n for SuperSphere
    
    // New Universal Shape System
    ShapeHandle sharedShape;

    // BM-C: UV & Surface System
    uint32_t revision = 1;
    UVTransform baseUVTransform;
    UVProjectionMode baseProjection = UVProjectionMode::Default;
    std::vector<SurfaceOverride> surfaces;

    const SurfaceOverride* FindSurfaceOverride(uint32_t surfaceId) const {
        for (const auto& s : surfaces) {
            if (s.surfaceId == surfaceId) return &s;
        }
        return nullptr;
    }

    SurfaceOverride& GetOrCreateSurfaceOverride(uint32_t surfaceId) {
        for (auto& s : surfaces) {
            if (s.surfaceId == surfaceId) return s;
        }
        SurfaceOverride newS;
        newS.surfaceId = surfaceId;
        surfaces.push_back(newS);
        return surfaces.back();
    }
};

class MaterialRegistry {
public:
    MaterialRegistry();
    ~MaterialRegistry() = default;

    void Initialize();
    bool LoadFromJson(const std::string& filepath);
    bool SaveToJson(const std::string& filepath);

    void UpdateMaterial(uint8_t blockId, const PBRMaterialDef& def);
    bool InitDefaultMaterial(uint8_t blockId);
    bool DuplicateMaterial(uint8_t sourceId, uint8_t targetId);
    bool ResetMaterial(uint8_t blockId);

    const PBRMaterialDef& GetMaterial(uint8_t blockId) const;
    PBRMaterialDef& GetMaterialMutable(uint8_t blockId);

    uint32_t CountShapeUsers(ShapeID id) const;

    const std::vector<PBRMaterialDef>& GetAllMaterials() const { return m_materials; }

private:
    void RegisterDefaultMaterials();

    std::vector<PBRMaterialDef> m_materials;
    PBRMaterialDef m_fallbackMaterial;
};

} // namespace fw
