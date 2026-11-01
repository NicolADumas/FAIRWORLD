#include "pch.h"
#include "MaterialAppearanceRegistry.h"
#include "MaterialRegistry.h"
#include "BlockRegistry.h"
#include "ScientificMaterialRegistry.h"
#include <fstream>
#include <iostream>
#include <algorithm>

namespace fw {

MaterialAppearanceRegistry::MaterialAppearanceRegistry() {
    m_fallbackAppearance.id = INVALID_APPEARANCE_ID;
    m_fallbackAppearance.canonicalId = "fairworld:appearance/fallback_error";
    m_fallbackAppearance.displayName = "Fallback Error Appearance";
    m_fallbackAppearance.baseColorFallback = glm::vec3(1.0f, 0.0f, 1.0f); // Magenta error
    m_fallbackAppearance.roughnessFallback = 1.0f;
    m_fallbackAppearance.metallicFallback = 0.0f;
    m_fallbackAppearance.emissiveStrength = 0.0f;
    m_fallbackAppearance.alphaFallback = 1.0f;
}

void MaterialAppearanceRegistry::Initialize() {
    BootstrapLegacyBlockAppearances(nullptr);
}

void MaterialAppearanceRegistry::Clear() {
    m_appearances.clear();
    m_canonicalIndex.clear();
    m_nextAppearanceId = 1000;
}

bool MaterialAppearanceRegistry::RegisterAppearance(const MaterialAppearance& def, bool allowUpdate, std::string* outError) {
    MaterialAppearance validated = def;
    if (!validated.Validate(outError)) {
        return false;
    }

    if (!allowUpdate) {
        if (m_appearances.find(validated.id) != m_appearances.end()) {
            if (outError) {
                *outError = "[ERROR] Collision: Duplicate appearance ID " + std::to_string(validated.id);
            }
            return false;
        }

        if (m_canonicalIndex.find(validated.canonicalId) != m_canonicalIndex.end()) {
            if (outError) {
                *outError = "[ERROR] Collision: Duplicate canonicalId '" + validated.canonicalId + "'";
            }
            return false;
        }
    } else {
        auto itCanon = m_canonicalIndex.find(validated.canonicalId);
        if (itCanon != m_canonicalIndex.end() && itCanon->second != validated.id) {
            if (outError) {
                *outError = "[ERROR] Conflict: canonicalId '" + validated.canonicalId + 
                            "' is already assigned to ID " + std::to_string(itCanon->second);
            }
            return false;
        }

        auto itId = m_appearances.find(validated.id);
        if (itId != m_appearances.end()) {
            // Zero-Work: se il contenuto e' identico, preserva la revisione senza invalidazioni a cascata
            if (itId->second.ComputeContentHash() == validated.ComputeContentHash()) {
                return true;
            }
            validated.revision = itId->second.revision + 1;
            // Se il canonicalId e' cambiato, rimuovi la vecchia voce dall'indice
            if (itId->second.canonicalId != validated.canonicalId) {
                m_canonicalIndex.erase(itId->second.canonicalId);
            }
        }
    }

    if (validated.id >= m_nextAppearanceId) {
        m_nextAppearanceId = validated.id + 1;
    }

    m_appearances[validated.id] = validated;
    m_canonicalIndex[validated.canonicalId] = validated.id;
    return true;
}

bool MaterialAppearanceRegistry::HasAppearance(AppearanceID id) const {
    if (id == INVALID_APPEARANCE_ID) return false;
    return m_appearances.find(id) != m_appearances.end();
}

bool MaterialAppearanceRegistry::HasAppearance(const std::string& canonicalId) const {
    if (canonicalId.empty()) return false;
    return m_canonicalIndex.find(canonicalId) != m_canonicalIndex.end();
}

const MaterialAppearance& MaterialAppearanceRegistry::GetAppearance(AppearanceID id) const {
    auto it = m_appearances.find(id);
    if (it != m_appearances.end()) {
        return it->second;
    }
    return m_fallbackAppearance;
}

const MaterialAppearance& MaterialAppearanceRegistry::GetAppearance(const std::string& canonicalId) const {
    auto it = m_canonicalIndex.find(canonicalId);
    if (it != m_canonicalIndex.end()) {
        return GetAppearance(it->second);
    }
    return m_fallbackAppearance;
}

const MaterialAppearance& MaterialAppearanceRegistry::GetAppearanceByCanonicalId(const std::string& canonicalId) const {
    return GetAppearance(canonicalId);
}

const MaterialAppearance& MaterialAppearanceRegistry::GetAppearanceByCanonical(const std::string& canonicalId) const {
    return GetAppearance(canonicalId);
}

MaterialAppearance* MaterialAppearanceRegistry::GetAppearanceMutable(AppearanceID id) {
    auto it = m_appearances.find(id);
    if (it != m_appearances.end()) {
        return &it->second;
    }
    return nullptr;
}

MaterialAppearance* MaterialAppearanceRegistry::GetAppearanceMutable(const std::string& canonicalId) {
    auto it = m_canonicalIndex.find(canonicalId);
    if (it != m_canonicalIndex.end()) {
        return GetAppearanceMutable(it->second);
    }
    return nullptr;
}

MaterialAppearance* MaterialAppearanceRegistry::GetAppearanceMutableByCanonicalId(const std::string& canonicalId) {
    return GetAppearanceMutable(canonicalId);
}

AppearanceID MaterialAppearanceRegistry::GetAppearanceIdByCanonical(const std::string& canonicalId) const {
    auto it = m_canonicalIndex.find(canonicalId);
    if (it != m_canonicalIndex.end()) {
        return it->second;
    }
    return INVALID_APPEARANCE_ID;
}

AppearanceID MaterialAppearanceRegistry::GetAppearanceIdByCanonicalId(const std::string& canonicalId) const {
    return GetAppearanceIdByCanonical(canonicalId);
}

AppearanceID MaterialAppearanceRegistry::AllocateNextAppearanceId() {
    return m_nextAppearanceId++;
}

size_t MaterialAppearanceRegistry::GetActiveAppearanceCount() const {
    return m_appearances.size();
}

std::vector<AppearanceID> MaterialAppearanceRegistry::GetAllAppearanceIDs() const {
    std::vector<AppearanceID> ids;
    ids.reserve(m_appearances.size());
    for (const auto& kv : m_appearances) {
        ids.push_back(kv.first);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool MaterialAppearanceRegistry::LoadFromJson(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string err;
    return LoadFromJsonString(content, &err);
}

bool MaterialAppearanceRegistry::SaveToJson(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    file << SaveToJsonString();
    return true;
}

bool MaterialAppearanceRegistry::LoadFromJson(const nlohmann::json& j, std::string* outError) {
    if (!j.contains("appearances") || !j["appearances"].is_array()) {
        if (outError) *outError = "Missing 'appearances' array in JSON root";
        return false;
    }

    for (const auto& item : j["appearances"]) {
        MaterialAppearance a;
        from_json(item, a);
        std::string regErr;
        if (!RegisterAppearance(a, true, &regErr)) {
            if (outError) *outError = regErr;
            return false;
        }
    }

    return true;
}

nlohmann::json MaterialAppearanceRegistry::SaveToJson() const {
    nlohmann::json j;
    j["schemaVersion"] = 1;
    nlohmann::json jArr = nlohmann::json::array();

    auto sortedIds = GetAllAppearanceIDs();
    for (AppearanceID id : sortedIds) {
        const auto& a = m_appearances.at(id);
        nlohmann::json jItem;
        to_json(jItem, a);
        jArr.push_back(jItem);
    }

    j["appearances"] = jArr;
    return j;
}

bool MaterialAppearanceRegistry::LoadFromJsonString(const std::string& jsonContent, std::string* outError) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(jsonContent);
    } catch (const nlohmann::json::parse_error& e) {
        if (outError) *outError = std::string("JSON parse error: ") + e.what();
        return false;
    }

    return LoadFromJson(j, outError);
}

std::string MaterialAppearanceRegistry::SaveToJsonString() const {
    return SaveToJson().dump(4);
}

AppearanceValidationReport MaterialAppearanceRegistry::ValidateScientificReferences(
    const ScientificMaterialRegistry& sciRegistry) const 
{
    AppearanceValidationReport rep;
    rep.totalAppearancesChecked = m_appearances.size();

    for (const auto& kv : m_appearances) {
        const auto& a = kv.second;
        if (a.suggestedScientificMaterialId == 0) {
            rep.unassociatedAppearances++;
        } else {
            if (sciRegistry.HasMaterial(a.suggestedScientificMaterialId)) {
                rep.validAssociations++;
            } else {
                AppearanceScientificDiagnostic diag;
                diag.appearanceId = a.id;
                diag.canonicalId = a.canonicalId;
                diag.unresolvedScientificId = a.suggestedScientificMaterialId;
                diag.message = "Appearance [" + a.canonicalId + "] references unresolved ScientificMaterialID " +
                               std::to_string(a.suggestedScientificMaterialId);
                rep.warnings.push_back(diag);
            }
        }
    }

    return rep;
}

std::string MaterialAppearanceRegistry::GetLegacyCanonicalIdForBlock(uint8_t blockId) {
    return "fairworld:appearance/legacy_block_" + std::to_string(static_cast<int>(blockId));
}

AppearanceID MaterialAppearanceRegistry::ResolveAppearanceId(uint8_t blockId, AppearanceID explicitAppearanceId) const {
    if (explicitAppearanceId != INVALID_APPEARANCE_ID && HasAppearance(explicitAppearanceId)) {
        return explicitAppearanceId;
    }

    if (blockId > 0 && HasAppearance(static_cast<AppearanceID>(blockId))) {
        return static_cast<AppearanceID>(blockId);
    }

    return INVALID_APPEARANCE_ID;
}

const MaterialAppearance& MaterialAppearanceRegistry::ResolveBlockAppearance(
    uint8_t blockId, AppearanceID explicitAppearanceId) const 
{
    AppearanceID resolved = ResolveAppearanceId(blockId, explicitAppearanceId);
    if (resolved != INVALID_APPEARANCE_ID) {
        return GetAppearance(resolved);
    }
    return m_fallbackAppearance;
}

const MaterialAppearance& MaterialAppearanceRegistry::ResolveBlockAppearance(const SimBlockDef& blockDef) const {
    return ResolveBlockAppearance(blockDef.id, blockDef.appearanceId);
}

void MaterialAppearanceRegistry::BootstrapLegacyBlockAppearances(const MaterialRegistry* legacyMatRegistry) {
    if (legacyMatRegistry) {
        for (int i = 1; i < 256; ++i) {
            const auto& mat = legacyMatRegistry->GetMaterial(static_cast<uint8_t>(i));
            if (mat.target_block_id > 0 || i <= 16) {
                MaterialAppearance a;
                a.id = static_cast<AppearanceID>(i);
                a.canonicalId = GetLegacyCanonicalIdForBlock(static_cast<uint8_t>(i));
                a.displayName = "Legacy Block " + std::to_string(i) + " Appearance";
                a.albedoPath = mat.albedoPath;
                a.normalPath = mat.normalPath;
                a.ormPath = mat.ormPath;
                a.baseColorFallback = mat.baseColorFallback;
                a.roughnessFallback = mat.roughnessFallback;
                a.metallicFallback = mat.metallicFallback;
                a.emissiveStrength = mat.emissiveStrength;
                a.alphaFallback = mat.alphaFallback;
                a.behaviors = mat.behaviors;
                RegisterAppearance(a, true);
            }
        }
    } else {
        // Fallback autonomo sui 17 blocchi canonici storici
        struct DefaultEntry {
            uint8_t id;
            const char* name;
            glm::vec3 color;
            float rough;
            float metal;
            float emissive;
            float alpha;
        };

        static const DefaultEntry DEFAULTS[] = {
            { 1,  "Grass",       {0.20f, 0.80f, 0.20f}, 0.90f, 0.00f, 0.0f, 1.0f },
            { 2,  "Dirt",        {0.50f, 0.30f, 0.10f}, 1.00f, 0.00f, 0.0f, 1.0f },
            { 3,  "Stone",       {0.50f, 0.50f, 0.50f}, 0.70f, 0.00f, 0.0f, 1.0f },
            { 4,  "Wood",        {0.60f, 0.40f, 0.20f}, 0.80f, 0.00f, 0.0f, 1.0f },
            { 5,  "Sand",        {0.90f, 0.85f, 0.50f}, 0.90f, 0.00f, 0.0f, 1.0f },
            { 6,  "Water",       {0.10f, 0.35f, 0.85f}, 0.02f, 0.25f, 0.0f, 0.8f },
            { 7,  "Lava",        {0.95f, 0.30f, 0.05f}, 0.90f, 0.00f, 2.5f, 1.0f },
            { 8,  "Leaves",      {0.15f, 0.60f, 0.15f}, 0.85f, 0.00f, 0.0f, 1.0f },
            { 9,  "MobSpawner",  {0.20f, 0.20f, 0.25f}, 0.40f, 0.50f, 0.0f, 1.0f },
            { 10, "LightSource", {1.00f, 0.95f, 0.80f}, 0.50f, 0.00f, 3.0f, 1.0f },
            { 11, "Mushroom",    {0.10f, 0.90f, 0.80f}, 0.50f, 0.00f, 1.0f, 1.0f },
            { 12, "Ore",         {0.85f, 0.20f, 0.20f}, 0.60f, 0.30f, 0.0f, 1.0f },
            { 13, "Ice",         {0.75f, 0.88f, 0.95f}, 0.05f, 0.10f, 0.0f, 0.85f},
            { 14, "StargateFrame", {0.15f, 0.15f, 0.20f}, 0.30f, 0.70f, 0.0f, 1.0f },
            { 15, "StargatePortal", {0.10f, 0.80f, 1.00f}, 0.10f, 0.00f, 4.0f, 0.5f },
            { 16, "Obsidian",    {0.10f, 0.08f, 0.12f}, 0.30f, 0.10f, 0.0f, 1.0f }
        };

        for (const auto& e : DEFAULTS) {
            MaterialAppearance a;
            a.id = static_cast<AppearanceID>(e.id);
            a.canonicalId = GetLegacyCanonicalIdForBlock(e.id);
            a.displayName = std::string("Legacy ") + e.name + " Appearance";
            a.baseColorFallback = e.color;
            a.roughnessFallback = e.rough;
            a.metallicFallback = e.metal;
            a.emissiveStrength = e.emissive;
            a.alphaFallback = e.alpha;
            RegisterAppearance(a, true);
        }
    }
}

} // namespace fw
