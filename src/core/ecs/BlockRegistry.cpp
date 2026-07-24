#include "pch.h"
#include "BlockRegistry.h"
#include "json.hpp"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

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
    // 0: Air
    m_blocks[0].id = 0;
    m_blocks[0].stringId = "fairworld:air";
    m_blocks[0].displayName = "Air";
    m_blocks[0].isSolid = false;
    m_blocks[0].isTransparent = true;
    m_stringToIdMap["fairworld:air"] = 0;

    // 1: Grass
    m_blocks[1].id = 1;
    m_blocks[1].stringId = "fairworld:grass";
    m_blocks[1].displayName = "Grass";
    m_stringToIdMap["fairworld:grass"] = 1;
    
    // 2: Dirt
    m_blocks[2].id = 2;
    m_blocks[2].stringId = "fairworld:dirt";
    m_blocks[2].displayName = "Dirt";
    m_stringToIdMap["fairworld:dirt"] = 2;

    // 3: Stone
    m_blocks[3].id = 3;
    m_blocks[3].stringId = "fairworld:stone";
    m_blocks[3].displayName = "Stone";
    m_blocks[3].mass = 5.0f; // Heavy
    m_stringToIdMap["fairworld:stone"] = 3;
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
            
            def.isSolid = jBlock.value("isSolid", true);
            def.isTransparent = jBlock.value("isTransparent", false);
            def.mass = jBlock.value("mass", 1.0f);
            def.friction = jBlock.value("friction", 0.6f);
            def.bounciness = jBlock.value("bounciness", 0.0f);
            
            def.thermal_resistance = jBlock.value("thermal_resistance", 1.0f);
            def.thermal_capacity = jBlock.value("thermal_capacity", 1.0f);
            def.lightEmissionLevel = jBlock.value("lightEmissionLevel", 0.0f);
            
            m_stringToIdMap[def.stringId] = id;
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
        const auto& def = m_blocks[i];
        if (i > 0 && def.stringId == "fairworld:unknown") continue; // Skip uninitialized blocks
        
        json jBlock;
        jBlock["id"] = def.id;
        jBlock["stringId"] = def.stringId;
        jBlock["displayName"] = def.displayName;
        
        jBlock["isSolid"] = def.isSolid;
        jBlock["isTransparent"] = def.isTransparent;
        jBlock["mass"] = def.mass;
        jBlock["friction"] = def.friction;
        jBlock["bounciness"] = def.bounciness;
        
        jBlock["thermal_resistance"] = def.thermal_resistance;
        jBlock["thermal_capacity"] = def.thermal_capacity;
        jBlock["lightEmissionLevel"] = def.lightEmissionLevel;
        
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
    return m_fallbackBlock;
}

SimBlockDef& BlockRegistry::GetBlockMutable(uint8_t id) {
    if (id < 256) return m_blocks[id];
    return m_fallbackBlock;
}

} // namespace fw
