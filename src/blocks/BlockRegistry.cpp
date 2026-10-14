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

std::string BlockRegistry::SanitizeStringId(const std::string& input) {
    std::string s = input;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();

    std::string prefix = "fairworld:";
    std::string body;
    if (s.rfind(prefix, 0) == 0) {
        body = s.substr(prefix.length());
    } else {
        body = s;
    }

    std::string cleanBody;
    for (char c : body) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            cleanBody += static_cast<char>(std::tolower(uc));
        } else if (c == '_' || c == '-') {
            cleanBody += c;
        } else if (std::isspace(uc)) {
            cleanBody += '_';
        }
    }

    if (cleanBody.empty()) {
        cleanBody = "custom_block";
    }

    return prefix + cleanBody;
}

bool BlockRegistry::ValidateStringId(const std::string& input, std::string* outError) {
    if (input.empty()) {
        if (outError) *outError = "String ID non puo' essere vuoto.";
        return false;
    }

    std::string prefix = "fairworld:";
    if (input.rfind(prefix, 0) != 0) {
        if (outError) *outError = "String ID deve iniziare con 'fairworld:'.";
        return false;
    }

    std::string body = input.substr(prefix.length());
    if (body.empty()) {
        if (outError) *outError = "Specificare un identificatore dopo 'fairworld:'.";
        return false;
    }

    if (body.length() > 64) {
        if (outError) *outError = "Lunghezza String ID superiore al limite consentito (64 caratteri).";
        return false;
    }

    for (char c : body) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '_' && c != '-') {
            if (outError) *outError = "Carattere non valido: sono ammessi solo caratteri alfanumerici, '_' e '-'.";
            return false;
        }
    }

    return true;
}

bool BlockRegistry::IsBlockValid(uint8_t id) const {
    if (id == 0) return true; // Air is reserved and valid
    if (id >= 256) return false;
    const auto& def = m_blocks[id];
    return !def.stringId.empty() && def.stringId != "fairworld:unknown";
}

bool BlockRegistry::IsReservedBlock(uint8_t id) const {
    return (id == 0); // Air is strictly reserved
}

bool BlockRegistry::IsStringIdTaken(const std::string& stringId, uint8_t excludeId) const {
    std::string low = toLowerStr(stringId);
    auto it = m_stringToIdMap.find(low);
    if (it != m_stringToIdMap.end() && it->second != excludeId) {
        return true;
    }
    it = m_stringToIdMap.find(stringId);
    if (it != m_stringToIdMap.end() && it->second != excludeId) {
        return true;
    }
    return false;
}

uint8_t BlockRegistry::FindFirstFreeSlot() const {
    // Search forward from cursor to mitigate immediate reuse of recently freed IDs
    for (int i = m_nextAllocSlot; i <= 255; ++i) {
        if (m_blocks[i].stringId.empty() || m_blocks[i].stringId == "fairworld:unknown") {
            return static_cast<uint8_t>(i);
        }
    }
    // Wrap around to start if not found forward
    for (int i = 1; i < m_nextAllocSlot; ++i) {
        if (m_blocks[i].stringId.empty() || m_blocks[i].stringId == "fairworld:unknown") {
            return static_cast<uint8_t>(i);
        }
    }
    return 0; // Registro pieno
}

uint32_t BlockRegistry::GetActiveBlockCount() const {
    uint32_t count = 0;
    for (int i = 1; i < 256; ++i) {
        if (IsBlockValid(static_cast<uint8_t>(i))) {
            count++;
        }
    }
    return count;
}

AssetOpResult BlockRegistry::CreateBlock(const std::string& stringId, const std::string& displayName) {
    std::string sanitized = SanitizeStringId(stringId);
    std::string valErr;
    if (!ValidateStringId(sanitized, &valErr)) {
        return { false, AssetValidationCode::InvalidCharacters, valErr, 0 };
    }

    if (IsStringIdTaken(sanitized)) {
        return { false, AssetValidationCode::DuplicateStringId, "String ID gia' in uso da un altro blocco: " + sanitized, 0 };
    }

    uint8_t freeId = FindFirstFreeSlot();
    if (freeId == 0) {
        return { false, AssetValidationCode::RegistryFull, "Capacita' registro blocchi esaurita (limite 255 blocchi).", 0 };
    }

    SimBlockDef& def = m_blocks[freeId];
    def.id = freeId;
    def.stringId = sanitized;
    def.displayName = displayName.empty() ? sanitized.substr(10) : displayName;

    // Inizializzazione coerente di default
    def.isSolid = true;
    def.isTransparent = false;
    def.physics.mass = 1.0f;
    def.physics.density = 1000.0f;
    def.physics.friction = 0.6f;
    def.physics.restitution = 0.0f;
    def.physics.collisionEnabled = true;
    def.physics.motionType = BlockMotionType::Static;
    def.physics.massMode = BlockMassMode::Manual;
    def.SyncLegacyPhysicsFields();

    def.thermal_resistance = 1.0f;
    def.thermal_capacity = 1.0f;
    def.lightEmissionLevel = 0.0f;

    def.localPosition = glm::vec3(0.0f);
    def.localRotation = glm::vec3(0.0f);
    def.localScale = glm::vec3(1.0f);
    def.pivotOffset = glm::vec3(0.0f);

    m_stringToIdMap[sanitized] = freeId;
    m_stringToIdMap[toLowerStr(sanitized)] = freeId;
    m_nextAllocSlot = (freeId >= 255) ? 1 : static_cast<uint8_t>(freeId + 1);

    return { true, AssetValidationCode::Success, "Blocco creato con successo.", freeId };
}

AssetOpResult BlockRegistry::DuplicateBlock(uint8_t sourceId, const std::string& newStringId, const std::string& newDisplayName) {
    if (!IsBlockValid(sourceId)) {
        return { false, AssetValidationCode::InvalidSourceId, "Blocco sorgente non valido o non inizializzato.", 0 };
    }

    const SimBlockDef& src = m_blocks[sourceId];
    std::string targetStrId = newStringId;
    if (targetStrId.empty()) {
        std::string base = src.stringId + "_copy";
        targetStrId = base;
        int copyIndex = 2;
        while (IsStringIdTaken(targetStrId)) {
            targetStrId = base + std::to_string(copyIndex++);
        }
    } else {
        targetStrId = SanitizeStringId(targetStrId);
    }

    std::string valErr;
    if (!ValidateStringId(targetStrId, &valErr)) {
        return { false, AssetValidationCode::InvalidCharacters, valErr, 0 };
    }

    if (IsStringIdTaken(targetStrId)) {
        return { false, AssetValidationCode::DuplicateStringId, "String ID clone gia' in uso: " + targetStrId, 0 };
    }

    uint8_t freeId = FindFirstFreeSlot();
    if (freeId == 0) {
        return { false, AssetValidationCode::RegistryFull, "Capacita' registro blocchi esaurita per la duplicazione.", 0 };
    }

    // Copia profonda indipendente dei dati
    SimBlockDef& clone = m_blocks[freeId];
    clone = src; // copia profonda di tutte le proprietà
    clone.id = freeId;
    clone.stringId = targetStrId;
    clone.displayName = newDisplayName.empty() ? (src.displayName + " (Copy)") : newDisplayName;
    clone.SyncLegacyPhysicsFields();

    m_stringToIdMap[targetStrId] = freeId;
    m_stringToIdMap[toLowerStr(targetStrId)] = freeId;
    m_nextAllocSlot = (freeId >= 255) ? 1 : static_cast<uint8_t>(freeId + 1);

    return { true, AssetValidationCode::Success, "Blocco duplicato con successo.", freeId };
}

AssetOpResult BlockRegistry::RenameBlock(uint8_t id, const std::string& newStringId, const std::string& newDisplayName) {
    if (id == 0) {
        return { false, AssetValidationCode::ReservedBlockId, "Impossibile rinominare il blocco riservato 'fairworld:air'.", 0 };
    }
    if (!IsBlockValid(id)) {
        return { false, AssetValidationCode::NotFound, "Blocco specificato non trovato o non inizializzato.", 0 };
    }

    std::string sanitized = SanitizeStringId(newStringId);
    std::string valErr;
    if (!ValidateStringId(sanitized, &valErr)) {
        return { false, AssetValidationCode::InvalidCharacters, valErr, 0 };
    }

    if (IsStringIdTaken(sanitized, id)) {
        return { false, AssetValidationCode::DuplicateStringId, "String ID gia' assegnato a un altro blocco: " + sanitized, 0 };
    }

    SimBlockDef& def = m_blocks[id];
    std::string oldStrId = def.stringId;

    m_stringToIdMap.erase(oldStrId);
    m_stringToIdMap.erase(toLowerStr(oldStrId));

    def.stringId = sanitized;
    def.displayName = newDisplayName.empty() ? sanitized.substr(10) : newDisplayName;

    m_stringToIdMap[sanitized] = id;
    m_stringToIdMap[toLowerStr(sanitized)] = id;

    return { true, AssetValidationCode::Success, "Blocco rinominato con successo.", id };
}

AssetOpResult BlockRegistry::DeleteBlock(uint8_t id) {
    if (id == 0) {
        return { false, AssetValidationCode::ReservedBlockId, "Impossibile eliminare il blocco riservato 'fairworld:air' (ID 0).", 0 };
    }
    if (!IsBlockValid(id)) {
        return { false, AssetValidationCode::NotFound, "Blocco non trovato o gia' non allocato.", 0 };
    }

    SimBlockDef& def = m_blocks[id];
    std::string oldStrId = def.stringId;

    m_stringToIdMap.erase(oldStrId);
    m_stringToIdMap.erase(toLowerStr(oldStrId));

    // Reset completo dello slot a fallback
    def = SimBlockDef();
    def.id = id;
    def.stringId = "fairworld:unknown";
    def.displayName = "Unknown Block";
    def.SyncLegacyPhysicsFields();

    return { true, AssetValidationCode::Success, "Blocco eliminato con successo.", id };
}

uint8_t BlockRegistry::CreateNewBlock(const std::string& stringId, const std::string& displayName) {
    auto res = CreateBlock(stringId, displayName);
    return res.allocatedId;
}

void BlockRegistry::UpdateBlock(uint8_t id, const SimBlockDef& def) {
    if (id > 0 && id < 256) {
        std::string oldStr = m_blocks[id].stringId;
        if (!oldStr.empty() && oldStr != def.stringId) {
            m_stringToIdMap.erase(oldStr);
            m_stringToIdMap.erase(toLowerStr(oldStr));
        }
        m_blocks[id] = def;
        m_blocks[id].id = id;
        m_stringToIdMap[def.stringId] = id;
        m_stringToIdMap[toLowerStr(def.stringId)] = id;
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

uint8_t BlockRegistry::GetBlockIdByString(const std::string& stringId) const {
    auto it = m_stringToIdMap.find(stringId);
    if (it != m_stringToIdMap.end()) {
        return it->second;
    }
    std::string low = toLowerStr(stringId);
    it = m_stringToIdMap.find(low);
    if (it != m_stringToIdMap.end()) {
        return it->second;
    }
    return 0;
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
