#include "pch.h"
#include "MaterialAppearance.h"
#include <cctype>
#include <sstream>

namespace fw {

uint64_t MaterialAppearance::ComputeContentHash() const {
    uint64_t hash = 14695981039346656037ULL; // FNV offset basis
    auto hashCombine = [&hash](const void* data, size_t size) {
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            hash ^= static_cast<uint64_t>(ptr[i]);
            hash *= 1099511628211ULL; // FNV prime
        }
    };
    auto hashString = [&hashCombine](const std::string& str) {
        hashCombine(str.data(), str.size());
    };

    hashCombine(&id, sizeof(id));
    hashString(canonicalId);
    hashString(displayName);
    hashString(description);
    hashString(albedoPath);
    hashString(normalPath);
    hashString(ormPath);
    hashString(emissivePath);
    hashCombine(&baseColorFallback.x, sizeof(baseColorFallback.x));
    hashCombine(&baseColorFallback.y, sizeof(baseColorFallback.y));
    hashCombine(&baseColorFallback.z, sizeof(baseColorFallback.z));
    hashCombine(&roughnessFallback, sizeof(roughnessFallback));
    hashCombine(&metallicFallback, sizeof(metallicFallback));
    hashCombine(&emissiveStrength, sizeof(emissiveStrength));
    hashCombine(&alphaFallback, sizeof(alphaFallback));
    hashCombine(&behaviors, sizeof(behaviors));
    hashCombine(&suggestedScientificMaterialId, sizeof(suggestedScientificMaterialId));

    return hash;
}

bool MaterialAppearance::Validate(std::string* outError) {
    if (id == INVALID_APPEARANCE_ID) {
        if (outError) *outError = "Appearance ID cannot be 0 (INVALID_APPEARANCE_ID)";
        return false;
    }

    if (canonicalId.empty()) {
        if (outError) *outError = "Appearance canonicalId cannot be empty";
        return false;
    }

    // Caratteri consentiti in canonicalId: lowercase alnum, ':', '/', '_', '-', '.'
    for (char c : canonicalId) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && 
            c != ':' && c != '/' && c != '_' && c != '-' && c != '.') {
            if (outError) {
                *outError = "Invalid character '" + std::string(1, c) + "' in canonicalId: " + canonicalId;
            }
            return false;
        }
    }

    // Clamping dei valori PBR a intervalli fisicamente e computazionalmente validi
    roughnessFallback = std::clamp(roughnessFallback, 0.0f, 1.0f);
    metallicFallback = std::clamp(metallicFallback, 0.0f, 1.0f);
    alphaFallback = std::clamp(alphaFallback, 0.0f, 1.0f);
    if (emissiveStrength < 0.0f) emissiveStrength = 0.0f;

    baseColorFallback.x = std::clamp(baseColorFallback.x, 0.0f, 1.0f);
    baseColorFallback.y = std::clamp(baseColorFallback.y, 0.0f, 1.0f);
    baseColorFallback.z = std::clamp(baseColorFallback.z, 0.0f, 1.0f);

    return true;
}

void to_json(nlohmann::json& j, const MaterialAppearance& a) {
    j = nlohmann::json{
        {"id", a.id},
        {"canonicalId", a.canonicalId},
        {"displayName", a.displayName},
        {"description", a.description},
        {"albedoPath", a.albedoPath},
        {"normalPath", a.normalPath},
        {"ormPath", a.ormPath},
        {"emissivePath", a.emissivePath},
        {"baseColorFallback", {a.baseColorFallback.x, a.baseColorFallback.y, a.baseColorFallback.z}},
        {"roughnessFallback", a.roughnessFallback},
        {"metallicFallback", a.metallicFallback},
        {"emissiveStrength", a.emissiveStrength},
        {"alphaFallback", a.alphaFallback},
        {"behaviors", a.behaviors},
        {"suggestedScientificMaterialId", a.suggestedScientificMaterialId},
        {"revision", a.revision}
    };
}

void from_json(const nlohmann::json& j, MaterialAppearance& a) {
    a.id = j.value("id", INVALID_APPEARANCE_ID);
    a.canonicalId = j.value("canonicalId", "fairworld:appearance/unknown");
    a.displayName = j.value("displayName", "Unknown Appearance");
    a.description = j.value("description", "");
    a.albedoPath = j.value("albedoPath", "");
    a.normalPath = j.value("normalPath", "");
    a.ormPath = j.value("ormPath", "");
    a.emissivePath = j.value("emissivePath", "");

    if (j.contains("baseColorFallback") && j["baseColorFallback"].is_array() && j["baseColorFallback"].size() >= 3) {
        a.baseColorFallback = glm::vec3(
            j["baseColorFallback"][0].get<float>(),
            j["baseColorFallback"][1].get<float>(),
            j["baseColorFallback"][2].get<float>()
        );
    } else {
        a.baseColorFallback = glm::vec3(1.0f);
    }

    a.roughnessFallback = j.value("roughnessFallback", 1.0f);
    a.metallicFallback = j.value("metallicFallback", 0.0f);
    a.emissiveStrength = j.value("emissiveStrength", 0.0f);
    a.alphaFallback = j.value("alphaFallback", 1.0f);
    a.behaviors = j.value("behaviors", 0u);
    a.suggestedScientificMaterialId = j.value("suggestedScientificMaterialId", 0u);
    a.revision = j.value("revision", 1ULL);
}

} // namespace fw
