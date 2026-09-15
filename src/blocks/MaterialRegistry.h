#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <cstdint>

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
};

class MaterialRegistry {
public:
    MaterialRegistry();
    ~MaterialRegistry() = default;

    void Initialize();
    bool LoadFromJson(const std::string& filepath);
    bool SaveToJson(const std::string& filepath);

    void UpdateMaterial(uint8_t blockId, const PBRMaterialDef& def);

    const PBRMaterialDef& GetMaterial(uint8_t blockId) const;
    PBRMaterialDef& GetMaterialMutable(uint8_t blockId);

    const std::vector<PBRMaterialDef>& GetAllMaterials() const { return m_materials; }

private:
    void RegisterDefaultMaterials();

    std::vector<PBRMaterialDef> m_materials;
    PBRMaterialDef m_fallbackMaterial;
};

} // namespace fw
