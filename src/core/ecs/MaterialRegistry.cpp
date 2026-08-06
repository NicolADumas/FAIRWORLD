#include "pch.h"
#include "MaterialRegistry.h"
#include "json.hpp"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace fw {

MaterialRegistry::MaterialRegistry() {
    m_materials.resize(256); // Match the 256 block limit
    m_fallbackMaterial.target_block_id = 255;
    m_fallbackMaterial.baseColorFallback = glm::vec3(1.0f, 0.0f, 1.0f); // Magenta error
}

void MaterialRegistry::Initialize() {
    RegisterDefaultMaterials();
}

void MaterialRegistry::RegisterDefaultMaterials() {
    // 0: Air
    m_materials[0].target_block_id = 0;
    
    // 1: Grass
    m_materials[1].target_block_id = 1;
    m_materials[1].baseColorFallback = glm::vec3(0.2f, 0.8f, 0.2f);
    m_materials[1].roughnessFallback = 0.9f;

    // 2: Dirt
    m_materials[2].target_block_id = 2;
    m_materials[2].baseColorFallback = glm::vec3(0.5f, 0.3f, 0.1f);
    m_materials[2].roughnessFallback = 1.0f;

    // 3: Stone
    m_materials[3].target_block_id = 3;
    m_materials[3].baseColorFallback = glm::vec3(0.5f, 0.5f, 0.5f);
    m_materials[3].roughnessFallback = 0.7f;

    // 4: Wood
    m_materials[4].target_block_id = 4;
    m_materials[4].baseColorFallback = glm::vec3(0.6f, 0.4f, 0.2f);
    m_materials[4].roughnessFallback = 0.8f;

    // 5: Sand
    m_materials[5].target_block_id = 5;
    m_materials[5].baseColorFallback = glm::vec3(0.9f, 0.85f, 0.5f);
    m_materials[5].roughnessFallback = 0.9f;

    // 6: Water
    m_materials[6].target_block_id = 6;
    m_materials[6].baseColorFallback = glm::vec3(0.1f, 0.35f, 0.85f);
    m_materials[6].roughnessFallback = 0.02f; // Superficie marina specchiante e liscissima
    m_materials[6].metallicFallback = 0.25f;  // Riflesso solare (Fresnel PBR)

    // 7: Lava
    m_materials[7].target_block_id = 7;
    m_materials[7].baseColorFallback = glm::vec3(0.95f, 0.3f, 0.05f);
    m_materials[7].roughnessFallback = 0.9f;
    m_materials[7].emissiveStrength = 2.5f;

    // 8: Leaves
    m_materials[8].target_block_id = 8;
    m_materials[8].baseColorFallback = glm::vec3(0.15f, 0.6f, 0.15f);
    m_materials[8].roughnessFallback = 0.85f;

    // 9: MobSpawner
    m_materials[9].target_block_id = 9;
    m_materials[9].baseColorFallback = glm::vec3(0.2f, 0.2f, 0.25f);
    m_materials[9].roughnessFallback = 0.4f;
    m_materials[9].metallicFallback = 0.5f;

    // 10: LightSource
    m_materials[10].target_block_id = 10;
    m_materials[10].baseColorFallback = glm::vec3(1.0f, 0.95f, 0.8f);
    m_materials[10].roughnessFallback = 0.5f;
    m_materials[10].emissiveStrength = 3.0f;

    // 13: Ice
    m_materials[13].target_block_id = 13;
    m_materials[13].baseColorFallback = glm::vec3(0.75f, 0.88f, 0.95f);
    m_materials[13].roughnessFallback = 0.05f;
    m_materials[13].metallicFallback = 0.1f;
}

bool MaterialRegistry::LoadFromJson(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[MaterialRegistry] File " << filepath << " non trovato. Uso i default.\n";
        return false;
    }

    json j;
    try {
        file >> j;
    } catch (const json::parse_error& e) {
        std::cerr << "[MaterialRegistry] Errore parsing JSON: " << e.what() << "\n";
        return false;
    }

    if (j.contains("materials") && j["materials"].is_array()) {
        for (const auto& jMat : j["materials"]) {
            uint8_t id = jMat.value("target_block_id", 0);

            PBRMaterialDef& def = m_materials[id];
            def.target_block_id = id;
            def.albedoPath = jMat.value("albedoPath", "");
            def.normalPath = jMat.value("normalPath", "");
            def.ormPath = jMat.value("ormPath", "");
            
            if (jMat.contains("baseColorFallback")) {
                auto color = jMat["baseColorFallback"];
                def.baseColorFallback = glm::vec3(color[0], color[1], color[2]);
            }
            
            def.metallicFallback = jMat.value("metallicFallback", 0.0f);
            def.roughnessFallback = jMat.value("roughnessFallback", 1.0f);
            def.emissiveStrength = jMat.value("emissiveStrength", 0.0f);
            def.shapeType = jMat.value("shapeType", 0);
            def.superSphereN = jMat.value("superSphereN", 2.0f);
        }
        std::cout << "[MaterialRegistry] Caricati " << j["materials"].size() << " materiali da " << filepath << "\n";
        return true;
    }
    
    return false;
}

bool MaterialRegistry::SaveToJson(const std::string& filepath) {
    json j;
    json jMaterials = json::array();

    for (int i = 0; i < 256; ++i) {
        const auto& def = m_materials[i];
        if (i > 0 && def.target_block_id == 0) continue; // Skip uninitialized
        
        json jMat;
        jMat["target_block_id"] = def.target_block_id;
        jMat["albedoPath"] = def.albedoPath;
        jMat["normalPath"] = def.normalPath;
        jMat["ormPath"] = def.ormPath;
        
        jMat["baseColorFallback"] = {def.baseColorFallback.x, def.baseColorFallback.y, def.baseColorFallback.z};
        jMat["metallicFallback"] = def.metallicFallback;
        jMat["roughnessFallback"] = def.roughnessFallback;
        jMat["emissiveStrength"] = def.emissiveStrength;
        jMat["shapeType"] = def.shapeType;
        jMat["superSphereN"] = def.superSphereN;
        
        jMaterials.push_back(jMat);
    }
    
    j["materials"] = jMaterials;
    
    std::ofstream file(filepath);
    if (file.is_open()) {
        file << j.dump(4);
        std::cout << "[MaterialRegistry] Materiali salvati in " << filepath << "\n";
        return true;
    }
    return false;
}

void MaterialRegistry::UpdateMaterial(uint8_t blockId, const PBRMaterialDef& def) {
    if (blockId > 0 && blockId < 256) {
        m_materials[blockId] = def;
        m_materials[blockId].target_block_id = blockId;
    }
}

const PBRMaterialDef& MaterialRegistry::GetMaterial(uint8_t blockId) const {
    if (blockId < 256) return m_materials[blockId];
    return m_fallbackMaterial;
}

PBRMaterialDef& MaterialRegistry::GetMaterialMutable(uint8_t blockId) {
    if (blockId < 256) return m_materials[blockId];
    return m_fallbackMaterial;
}

} // namespace fw
