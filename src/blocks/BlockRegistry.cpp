#include "pch.h"
#include "BlockRegistry.h"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>

using json = nlohmann::json;

namespace {
    std::string toLowerStr(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        return s;
    }

    glm::vec3 parseVec3Safe(const json& jBlock, const char* key, const glm::vec3& defaultVal) {
        if (!jBlock.contains(key) || !jBlock[key].is_array() || jBlock[key].size() != 3) {
            return defaultVal;
        }
        const auto& arr = jBlock[key];
        if (!arr[0].is_number() || !arr[1].is_number() || !arr[2].is_number()) {
            return defaultVal;
        }
        float x = arr[0].get<float>();
        float y = arr[1].get<float>();
        float z = arr[2].get<float>();
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            return defaultVal;
        }
        return glm::vec3(x, y, z);
    }
}


namespace fw {

BlockRegistry::BlockRegistry() {
    m_blocks.resize(256); // Support up to 256 blocks for now (matches uint8_t capacity)
    m_fallbackBlock.id = 255;
    m_fallbackBlock.stringId = "fairworld:error";
    m_fallbackBlock.displayName = "Error Block";
}

void BlockRegistry::Initialize() {
    RegisterDefaultBlocks();
}

void BlockRegistry::RegisterDefaultBlocks() {
    auto registerDef = [this](uint8_t id, const std::string& strId, const std::string& name, bool solid, bool transp) {
        m_blocks[id].id = id;
        m_blocks[id].stringId = strId;
        m_blocks[id].displayName = name;
        m_blocks[id].isSolid = solid;
        m_blocks[id].isTransparent = transp;
        m_blocks[id].physics.collisionEnabled = solid;
        m_blocks[id].physics.friction = 0.6f;
        m_blocks[id].physics.restitution = 0.0f;
        m_blocks[id].physics.density = 1000.0f;
        m_blocks[id].physics.mass = 1.0f;
        m_blocks[id].physics.motionType = BlockMotionType::Static;
        m_blocks[id].physics.massMode = BlockMassMode::Manual;
        m_blocks[id].SyncLegacyPhysicsFields();
        m_stringToIdMap[strId] = id;
        m_stringToIdMap[toLowerStr(strId)] = id;
    };

    registerDef(0,  "fairworld:air",        "Air",        false, true);
    m_blocks[0].physics.collisionEnabled = false;
    m_blocks[0].physics.density = 1.225f; // aria a livello del mare
    m_blocks[0].physics.mass = 0.001f;
    m_blocks[0].SyncLegacyPhysicsFields();

    registerDef(1,  "fairworld:grass",      "Grass",      true,  false);
    m_blocks[1].physics.density = 1200.0f;
    m_blocks[1].SyncLegacyPhysicsFields();

    registerDef(2,  "fairworld:dirt",       "Dirt",       true,  false);
    m_blocks[2].physics.density = 1500.0f;
    m_blocks[2].SyncLegacyPhysicsFields();

    registerDef(3,  "fairworld:stone",      "Stone",      true,  false);
    m_blocks[3].physics.mass = 5.0f;
    m_blocks[3].physics.density = 2600.0f;
    m_blocks[3].SyncLegacyPhysicsFields();

    registerDef(4,  "fairworld:wood",       "Wood",       true,  false);
    m_blocks[4].physics.density = 650.0f;
    m_blocks[4].SyncLegacyPhysicsFields();

    registerDef(5,  "fairworld:sand",       "Sand",       true,  false);
    m_blocks[5].physics.density = 1600.0f;
    m_blocks[5].physics.friction = 0.8f;
    m_blocks[5].SyncLegacyPhysicsFields();

    registerDef(6,  "fairworld:water",      "Water",      false, true);
    m_blocks[6].physics.collisionEnabled = false;
    m_blocks[6].physics.density = 1000.0f;
    m_blocks[6].SyncLegacyPhysicsFields();

    registerDef(7,  "fairworld:lava",       "Lava",       false, false);
    m_blocks[7].physics.collisionEnabled = false;
    m_blocks[7].physics.density = 3100.0f;
    m_blocks[7].SyncLegacyPhysicsFields();

    registerDef(8,  "fairworld:leaves",     "Leaves",     true,  true);
    m_blocks[8].physics.density = 200.0f;
    m_blocks[8].SyncLegacyPhysicsFields();

    registerDef(9,  "fairworld:mobspawner", "MobSpawner", true,  false);
    registerDef(10, "fairworld:lightsource","LightSource",true,  false);

    registerDef(13, "fairworld:ice",        "Ice",        true,  true);
    m_blocks[13].physics.friction = 0.05f;
    m_blocks[13].physics.density = 917.0f;
    m_blocks[13].SyncLegacyPhysicsFields();
}

bool BlockRegistry::LoadFromJson(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[BlockRegistry] File " << filepath << " not found. Using defaults.\n";
        return false;
    }

    json j;
    try {
        file >> j;
    } catch (const json::parse_error& e) {
        std::cerr << "[BlockRegistry] Error parsing JSON: " << e.what() << "\n";
        return false;
    }

    if (j.contains("blocks") && j["blocks"].is_array()) {
        for (const auto& jBlock : j["blocks"]) {
            uint8_t id = jBlock.value("id", 0);
            if (id == 0 && jBlock.value("stringId", "") != "fairworld:air") continue; // 0 is reserved for air unless explicit

            SimBlockDef& def = m_blocks[id];
            def.id = id;
            std::string legacyName = jBlock.value("name", "");
            std::string defaultStrId = legacyName.empty() ? "fairworld:unknown" : ("fairworld:" + legacyName);
            std::string defaultDispName = legacyName.empty() ? "Unknown" : legacyName;

            def.stringId = jBlock.value("stringId", defaultStrId);
            def.displayName = jBlock.value("displayName", defaultDispName);
            
            // Baseline and legacy compatibility values
            def.isSolid = jBlock.value("isSolid", true);
            def.isTransparent = jBlock.value("isTransparent", false);
            def.mass = jBlock.value("mass", 1.0f);
            def.friction = jBlock.value("friction", 0.6f);
            def.bounciness = jBlock.value("bounciness", 0.0f);

            // Typed physics contract with robust legacy defaults
            def.physics.mass = def.mass;
            def.physics.density = jBlock.value("density", 1000.0f);
            def.physics.friction = def.friction;
            def.physics.restitution = jBlock.value("restitution", def.bounciness);
            def.physics.collisionEnabled = jBlock.value("collisionEnabled", def.isSolid);

            int motionTypeInt = jBlock.value("motionType", static_cast<int>(BlockMotionType::Static));
            if (motionTypeInt >= 0 && motionTypeInt <= 2) {
                def.physics.motionType = static_cast<BlockMotionType>(motionTypeInt);
            } else {
                def.physics.motionType = BlockMotionType::Static;
            }

            int massModeInt = jBlock.value("massMode", static_cast<int>(BlockMassMode::Manual));
            if (massModeInt >= 0 && massModeInt <= 1) {
                def.physics.massMode = static_cast<BlockMassMode>(massModeInt);
            } else {
                def.physics.massMode = BlockMassMode::Manual;
            }

            def.thermal_resistance = jBlock.value("thermal_resistance", 1.0f);
            def.thermal_capacity = jBlock.value("thermal_capacity", 1.0f);
            def.lightEmissionLevel = jBlock.value("lightEmissionLevel", 0.0f);
            
            def.localPosition = parseVec3Safe(jBlock, "localPosition", glm::vec3(0.0f));
            def.localRotation = parseVec3Safe(jBlock, "localRotation", glm::vec3(0.0f));
            for (int c = 0; c < 3; ++c) {
                def.localRotation[c] = std::fmod(def.localRotation[c], 360.0f);
            }

            def.localScale = parseVec3Safe(jBlock, "localScale", glm::vec3(1.0f));
            for (int c = 0; c < 3; ++c) {
                if (std::abs(def.localScale[c]) < 1e-4f) {
                    def.localScale[c] = (def.localScale[c] < 0.0f) ? -1e-4f : 1e-4f;
                }
            }

            def.pivotOffset = parseVec3Safe(jBlock, "pivotOffset", glm::vec3(0.0f));
            
            // Sync legacy fields so mirrors are in lockstep
            def.SyncLegacyPhysicsFields();

            m_stringToIdMap[def.stringId] = id;
            m_stringToIdMap[toLowerStr(def.stringId)] = id;
        }
        std::cout << "[BlockRegistry] Loaded " << j["blocks"].size() << " blocks from " << filepath << "\n";
        return true;
    }
    
    return false;
}

bool BlockRegistry::SaveToJson(const std::string& filepath) {
    json j;
    json jBlocks = json::array();

    for (int i = 0; i < 256; ++i) {
        auto& def = m_blocks[i];
        if (i > 0 && (def.stringId.empty() || def.stringId == "fairworld:unknown")) continue; // Skip uninitialized blocks
        
        // Ensure legacy fields reflect authoritative physics definition before writing
        def.SyncLegacyPhysicsFields();

        json jBlock;
        jBlock["id"] = def.id > 0 ? def.id : (uint8_t)i;
        jBlock["stringId"] = def.stringId;
        jBlock["displayName"] = def.displayName;
        
        // Legacy fields for backward compatibility
        jBlock["isSolid"] = def.isSolid;
        jBlock["isTransparent"] = def.isTransparent;
        jBlock["mass"] = def.mass;
        jBlock["friction"] = def.friction;
        jBlock["bounciness"] = def.bounciness;

        // Authoritative Typed Physics Contract
        jBlock["density"] = def.physics.density;
        jBlock["restitution"] = def.physics.restitution;
        jBlock["collisionEnabled"] = def.physics.collisionEnabled;
        jBlock["motionType"] = static_cast<int>(def.physics.motionType);
        jBlock["massMode"] = static_cast<int>(def.physics.massMode);
        
        jBlock["thermal_resistance"] = def.thermal_resistance;
        jBlock["thermal_capacity"] = def.thermal_capacity;
        jBlock["lightEmissionLevel"] = def.lightEmissionLevel;
        
        jBlock["localPosition"] = { def.localPosition.x, def.localPosition.y, def.localPosition.z };
        jBlock["localRotation"] = { def.localRotation.x, def.localRotation.y, def.localRotation.z };
        jBlock["localScale"] = { def.localScale.x, def.localScale.y, def.localScale.z };
        jBlock["pivotOffset"] = { def.pivotOffset.x, def.pivotOffset.y, def.pivotOffset.z };
        
        jBlocks.push_back(jBlock);
    }
    
    j["blocks"] = jBlocks;
    
    std::ofstream file(filepath);
    if (file.is_open()) {
        file << j.dump(4);
        std::cout << "[BlockRegistry] Saved blocks to " << filepath << "\n";
        return true;
    }
    return false;
}

uint8_t BlockRegistry::CreateNewBlock(const std::string& stringId, const std::string& displayName) {
    // Trova il primo slot libero (da 1 a 255)
    for (uint8_t i = 1; i < 256; ++i) {
        if (m_blocks[i].stringId == "fairworld:unknown") {
            m_blocks[i].id = i;
            m_blocks[i].stringId = stringId;
            m_blocks[i].displayName = displayName;
            m_stringToIdMap[stringId] = i;
            return i;
        }
    }
    return 0; // Fallimento, registro pieno
}

void BlockRegistry::UpdateBlock(uint8_t id, const SimBlockDef& def) {
    if (id > 0 && id < 256) {
        m_blocks[id] = def;
        m_blocks[id].id = id;
        m_stringToIdMap[def.stringId] = id;
    }
}

const SimBlockDef& BlockRegistry::GetBlock(uint8_t id) const {
    if (id < 256) return m_blocks[id];
    return m_fallbackBlock;
}

const SimBlockDef& BlockRegistry::GetBlock(const std::string& stringId) const {
    auto it = m_stringToIdMap.find(stringId);
    if (it != m_stringToIdMap.end()) {
        return m_blocks[it->second];
    }
    std::string low = toLowerStr(stringId);
    it = m_stringToIdMap.find(low);
    if (it != m_stringToIdMap.end()) {
        return m_blocks[it->second];
    }
    return m_fallbackBlock;
}

SimBlockDef& BlockRegistry::GetBlockMutable(uint8_t id) {
    if (id < 256) {
        if (m_blocks[id].id == 0 && id > 0) {
            m_blocks[id].id = id;
        }
        return m_blocks[id];
    }
    return m_fallbackBlock;
}

} // namespace fw
