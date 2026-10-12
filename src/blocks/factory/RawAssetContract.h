#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <glm/glm.hpp>
#include "json.hpp"
#include "ShapeRegistry.h"
#include "SurfaceDefinition.h"
#include "BlockRegistry.h"
#include "ForgeComponents.h"

namespace fw {

// Persistent asset identifier (canonical string ID, e.g. "fairworld:custom_brick")
using RawAssetID = std::string;

// Monotonic revision counter for the raw asset
using RawAssetRevision = uint64_t;

// Production Lifecycle Stage
enum class RawAssetStage : uint8_t {
    Draft = 0,       // Modificabile, lavorazione in corso nelle stazioni
    Validated = 1,   // Ha superato il Quality Control (QC)
    Packaged = 2,    // Pacchetto compilato e pronto per la pubblicazione
    Published = 3,   // Attivo e consumabile da PlayState o altri sistemi
    Deprecated = 4   // Dismesso o sostituito
};

// Dependency declaration between assets or derived artifacts
struct RawAssetDependency {
    std::string dependencyType; // "shape", "texture", "material", "parent_asset"
    std::string targetId;       // ID o URI della risorsa dipendente
    uint64_t requiredRevision = 0;
    bool isOptional = false;

    bool operator==(const RawAssetDependency& other) const {
        return dependencyType == other.dependencyType &&
               targetId == other.targetId &&
               requiredRevision == other.requiredRevision &&
               isOptional == other.isOptional;
    }
};

// Validation report produced by Quality Control and station checks
struct RawAssetValidation {
    bool isValid = false;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    void AddError(const std::string& err) {
        errors.push_back(err);
        isValid = false;
    }

    void AddWarning(const std::string& warn) {
        warnings.push_back(warn);
    }

    void Reset() {
        isValid = true;
        errors.clear();
        warnings.clear();
    }
};

// Metadata and provenance manifest for the raw asset
struct RawAssetManifest {
    RawAssetID assetId;
    std::string displayName = "New Raw Asset";
    std::string author = "LocalAuthor";
    uint32_t schemaVersion = 1;
    RawAssetRevision revision = 1;
    RawAssetStage stage = RawAssetStage::Draft;

    uint64_t creationTimestamp = 0;
    uint64_t lastModifiedTimestamp = 0;

    std::vector<RawAssetDependency> dependencies;

    // Backward-compatibility slot (0 = unmapped, 1..255 = legacy BlockRegistry ID)
    uint8_t targetRuntimeVoxelId = 0;
};

// =============================================================================
// RAW ASSET DRAFT — Rappresentazione Modificabile dell'Asset in Fabbrica
// =============================================================================
struct RawAssetDraft {
    RawAssetManifest manifest;

    // 1. Stazione Shape: Geometria parametrica o definita
    ShapeDefinition shape;

    // 2. Trasformazioni di base e Pivot nello spazio modello dell'asset
    glm::vec3 localPosition{0.0f, 0.0f, 0.0f};
    glm::vec3 localRotation{0.0f, 0.0f, 0.0f}; // Gradi Euler (Pitch, Yaw, Roll)
    glm::vec3 localScale{1.0f, 1.0f, 1.0f};
    glm::vec3 pivotOffset{0.0f, 0.0f, 0.0f};

    // 3. Stazione SurfaceUV: Mappatura, proiezioni e superfici semantiche
    UVTransform baseUVTransform;
    UVProjectionMode baseProjection = UVProjectionMode::Default;
    std::vector<SurfaceOverride> surfaces;

    // 4. Stazione Material & Texture: Parametri visivi PBR e mappe
    glm::vec3 baseColorFallback{1.0f, 1.0f, 1.0f};
    float roughnessFallback = 1.0f;
    float metallicFallback = 0.0f;
    float emissiveStrength = 0.0f;
    float alphaFallback = 1.0f;

    std::string albedoPath;
    std::string normalPath;
    std::string ormPath;
    uint32_t behaviors = 0;

    // 5. Stazione RawPhysics: Proprietà fisiche e termiche indipendenti
    BlockPhysicsDef physics;
    float thermalResistance = 1.0f;
    float thermalCapacity = 1.0f;
    float lightEmission = 0.0f;

    // Stato di lavorazione interno
    bool isDirty = false;
    uint32_t completedStationsMask = 0; // Bitmask delle stazioni completate

    void MarkDirty() {
        isDirty = true;
        manifest.revision++;
        manifest.lastModifiedTimestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
    }
};

// =============================================================================
// RAW ASSET PACKAGE — Rappresentazione Compilata, Immutabile e Pubblicabile
// =============================================================================
struct RawAssetPackage {
    RawAssetManifest manifest;
    RawAssetValidation validationReport;

    // Geometria compilata (senza handle VRAM GPU o puntatori volatili)
    struct CompiledGeometry {
        std::vector<Vertex> vertices;
        std::vector<Face> faces;
        AABB bounds;
        uint32_t vertexCount = 0;
        uint32_t triangleCount = 0;
    } geometry;

    // Definizione fisica compilata per il runtime
    BlockPhysicsDef compiledPhysics;

    // Assegnazioni materiali compilate
    struct CompiledMaterialBinding {
        uint32_t surfaceId = 0;
        uint32_t materialLayer = 0;
        glm::vec4 pbrParams{1.0f, 0.0f, 0.0f, 1.0f}; // rough, metal, emissive, alpha
        glm::vec3 baseColor{1.0f};
    };
    std::vector<CompiledMaterialBinding> materialBindings;

    // Mappe texture referenziate
    std::string albedoMapUri;
    std::string normalMapUri;
    std::string ormMapUri;

    // Trasformazione standard congelata
    glm::vec3 packagePosition{0.0f};
    glm::vec3 packageRotation{0.0f};
    glm::vec3 packageScale{1.0f};
    glm::vec3 packagePivot{0.0f};

    // Impronta digitale univoca (hash di validazione)
    uint64_t packageFingerprint = 0;

    bool IsPublishable() const {
        return validationReport.isValid &&
               !geometry.vertices.empty() &&
               manifest.stage >= RawAssetStage::Validated;
    }
};

// =============================================================================
// JSON SERIALIZATION HELPERS (Retrocompatibili e versionati)
// =============================================================================

inline void to_json(nlohmann::json& j, const RawAssetDependency& d) {
    j = nlohmann::json{
        {"type", d.dependencyType},
        {"targetId", d.targetId},
        {"requiredRevision", d.requiredRevision},
        {"isOptional", d.isOptional}
    };
}

inline void from_json(const nlohmann::json& j, RawAssetDependency& d) {
    d.dependencyType = j.value("type", "");
    d.targetId = j.value("targetId", "");
    d.requiredRevision = j.value("requiredRevision", static_cast<uint64_t>(0));
    d.isOptional = j.value("isOptional", false);
}

inline void to_json(nlohmann::json& j, const RawAssetManifest& m) {
    j = nlohmann::json{
        {"assetId", m.assetId},
        {"displayName", m.displayName},
        {"author", m.author},
        {"schemaVersion", m.schemaVersion},
        {"revision", m.revision},
        {"stage", static_cast<uint8_t>(m.stage)},
        {"creationTimestamp", m.creationTimestamp},
        {"lastModifiedTimestamp", m.lastModifiedTimestamp},
        {"targetRuntimeVoxelId", m.targetRuntimeVoxelId}
    };
    if (!m.dependencies.empty()) {
        j["dependencies"] = m.dependencies;
    }
}

inline void from_json(const nlohmann::json& j, RawAssetManifest& m) {
    m.assetId = j.value("assetId", "");
    m.displayName = j.value("displayName", "Unnamed Asset");
    m.author = j.value("author", "LocalAuthor");
    m.schemaVersion = j.value("schemaVersion", 1u);
    m.revision = j.value("revision", static_cast<RawAssetRevision>(1));
    m.stage = static_cast<RawAssetStage>(j.value("stage", static_cast<uint8_t>(0)));
    m.creationTimestamp = j.value("creationTimestamp", static_cast<uint64_t>(0));
    m.lastModifiedTimestamp = j.value("lastModifiedTimestamp", static_cast<uint64_t>(0));
    m.targetRuntimeVoxelId = j.value("targetRuntimeVoxelId", static_cast<uint8_t>(0));
    if (j.contains("dependencies") && j["dependencies"].is_array()) {
        m.dependencies = j["dependencies"].get<std::vector<RawAssetDependency>>();
    }
}

inline void to_json(nlohmann::json& j, const RawAssetDraft& d) {
    j = nlohmann::json{
        {"manifest", d.manifest},
        {"transform", {
            {"position", {d.localPosition.x, d.localPosition.y, d.localPosition.z}},
            {"rotation", {d.localRotation.x, d.localRotation.y, d.localRotation.z}},
            {"scale", {d.localScale.x, d.localScale.y, d.localScale.z}},
            {"pivot", {d.pivotOffset.x, d.pivotOffset.y, d.pivotOffset.z}}
        }},
        {"graphics", {
            {"baseColor", {d.baseColorFallback.x, d.baseColorFallback.y, d.baseColorFallback.z}},
            {"roughness", d.roughnessFallback},
            {"metallic", d.metallicFallback},
            {"emissive", d.emissiveStrength},
            {"alpha", d.alphaFallback},
            {"albedoPath", d.albedoPath},
            {"normalPath", d.normalPath},
            {"ormPath", d.ormPath},
            {"behaviors", d.behaviors}
        }},
        {"physics", {
            {"mass", d.physics.mass},
            {"density", d.physics.density},
            {"friction", d.physics.friction},
            {"restitution", d.physics.restitution},
            {"collisionEnabled", d.physics.collisionEnabled},
            {"motionType", static_cast<int>(d.physics.motionType)},
            {"massMode", static_cast<int>(d.physics.massMode)},
            {"thermalResistance", d.thermalResistance},
            {"thermalCapacity", d.thermalCapacity},
            {"lightEmission", d.lightEmission}
        }}
    };
    if (!d.baseUVTransform.IsIdentity()) {
        nlohmann::json jUv;
        to_json(jUv, d.baseUVTransform);
        j["uvTransform"] = jUv;
    }
    if (d.baseProjection != UVProjectionMode::Default) {
        j["baseProjection"] = static_cast<uint8_t>(d.baseProjection);
    }
    if (!d.surfaces.empty()) {
        nlohmann::json jSurfaces = nlohmann::json::array();
        for (const auto& s : d.surfaces) {
            nlohmann::json js;
            to_json(js, s);
            jSurfaces.push_back(js);
        }
        j["surfaces"] = jSurfaces;
    }
}

inline void from_json(const nlohmann::json& j, RawAssetDraft& d) {
    if (j.contains("manifest")) {
        j["manifest"].get_to(d.manifest);
    }
    if (j.contains("transform")) {
        const auto& jt = j["transform"];
        if (jt.contains("position") && jt["position"].size() >= 3) {
            d.localPosition = {jt["position"][0], jt["position"][1], jt["position"][2]};
        }
        if (jt.contains("rotation") && jt["rotation"].size() >= 3) {
            d.localRotation = {jt["rotation"][0], jt["rotation"][1], jt["rotation"][2]};
        }
        if (jt.contains("scale") && jt["scale"].size() >= 3) {
            d.localScale = {jt["scale"][0], jt["scale"][1], jt["scale"][2]};
        }
        if (jt.contains("pivot") && jt["pivot"].size() >= 3) {
            d.pivotOffset = {jt["pivot"][0], jt["pivot"][1], jt["pivot"][2]};
        }
    }
    if (j.contains("graphics")) {
        const auto& jg = j["graphics"];
        if (jg.contains("baseColor") && jg["baseColor"].size() >= 3) {
            d.baseColorFallback = {jg["baseColor"][0], jg["baseColor"][1], jg["baseColor"][2]};
        }
        d.roughnessFallback = jg.value("roughness", 1.0f);
        d.metallicFallback = jg.value("metallic", 0.0f);
        d.emissiveStrength = jg.value("emissive", 0.0f);
        d.alphaFallback = jg.value("alpha", 1.0f);
        d.albedoPath = jg.value("albedoPath", "");
        d.normalPath = jg.value("normalPath", "");
        d.ormPath = jg.value("ormPath", "");
        d.behaviors = jg.value("behaviors", 0u);
    }
    if (j.contains("physics")) {
        const auto& jp = j["physics"];
        d.physics.mass = jp.value("mass", 1.0f);
        d.physics.density = jp.value("density", 1000.0f);
        d.physics.friction = jp.value("friction", 0.6f);
        d.physics.restitution = jp.value("restitution", 0.0f);
        d.physics.collisionEnabled = jp.value("collisionEnabled", true);
        d.physics.motionType = static_cast<BlockMotionType>(jp.value("motionType", 0));
        d.physics.massMode = static_cast<BlockMassMode>(jp.value("massMode", 0));
        d.thermalResistance = jp.value("thermalResistance", 1.0f);
        d.thermalCapacity = jp.value("thermalCapacity", 1.0f);
        d.lightEmission = jp.value("lightEmission", 0.0f);
    }
    if (j.contains("uvTransform")) {
        from_json(j["uvTransform"], d.baseUVTransform);
    }
    d.baseProjection = static_cast<UVProjectionMode>(j.value("baseProjection", static_cast<uint8_t>(0)));
    if (j.contains("surfaces") && j["surfaces"].is_array()) {
        d.surfaces.clear();
        for (const auto& js : j["surfaces"]) {
            SurfaceOverride s;
            from_json(js, s);
            d.surfaces.push_back(s);
        }
    }
}

} // namespace fw
