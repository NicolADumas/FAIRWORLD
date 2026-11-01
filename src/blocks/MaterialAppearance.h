#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <cstdint>
#include <algorithm>
#include <glm/glm.hpp>
#include "json.hpp"

namespace fw {

using AppearanceID = uint32_t;
inline constexpr AppearanceID INVALID_APPEARANCE_ID = 0;

// =============================================================================
// MA-02A: MATERIAL APPEARANCE MODEL
// Rappresentazione visiva PBR disaccoppiata dall'identità del blocco.
// Conforme ai vincoli di determinismo, invalidazione selettiva e Zero-Work.
// =============================================================================

struct MaterialAppearance {
    AppearanceID id = INVALID_APPEARANCE_ID;
    std::string canonicalId = "fairworld:appearance/unknown";
    std::string displayName = "Unknown Appearance";
    std::string description = "";

    // Mappe Texture PBR (percorsi relativi portabili)
    std::string albedoPath = "";
    std::string normalPath = "";
    std::string ormPath = "";
    std::string emissivePath = "";

    // Valori PBR di Fallback / Parametri Costanti
    glm::vec3 baseColorFallback = glm::vec3(1.0f);
    float roughnessFallback = 1.0f;
    float metallicFallback = 0.0f;
    float emissiveStrength = 0.0f;
    float alphaFallback = 1.0f;

    // Flag comportamentali GPU (BlockBehavior bitmask)
    uint32_t behaviors = 0;

    // Riferimento opzionale al catalogo scientifico (strictly advisory/consistenza)
    // NON duplica densità, composizione o proprietà termodinamiche
    uint32_t suggestedScientificMaterialId = 0;

    // Tracciamento revisione per invalidazione selettiva e Zero-Work
    uint64_t revision = 1;

    bool IsValid() const {
        return id != INVALID_APPEARANCE_ID && !canonicalId.empty();
    }

    uint64_t ComputeContentHash() const;
    bool Validate(std::string* outError = nullptr);

    bool operator==(const MaterialAppearance& other) const {
        return id == other.id &&
               canonicalId == other.canonicalId &&
               displayName == other.displayName &&
               description == other.description &&
               albedoPath == other.albedoPath &&
               normalPath == other.normalPath &&
               ormPath == other.ormPath &&
               emissivePath == other.emissivePath &&
               baseColorFallback == other.baseColorFallback &&
               roughnessFallback == other.roughnessFallback &&
               metallicFallback == other.metallicFallback &&
               emissiveStrength == other.emissiveStrength &&
               alphaFallback == other.alphaFallback &&
               behaviors == other.behaviors &&
               suggestedScientificMaterialId == other.suggestedScientificMaterialId;
    }

    bool operator!=(const MaterialAppearance& other) const {
        return !(*this == other);
    }
};

// Funzioni di conversione JSON per nlohmann::json
void to_json(nlohmann::json& j, const MaterialAppearance& a);
void from_json(const nlohmann::json& j, MaterialAppearance& a);

} // namespace fw
