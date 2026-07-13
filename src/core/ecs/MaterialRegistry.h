#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <cstdint>

namespace fw {

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
