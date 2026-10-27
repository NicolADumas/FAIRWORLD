#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <optional>
#include "json.hpp"
#include "ShapeRegistry.h"
#include "SurfaceDefinition.h"
#include "BlockRegistry.h"
#include "ForgeComponents.h"
#include "blocks/sculpt/EditableMesh.h"
#include "blocks/texture/PBRTexturePack.h"
#include "blocks/texture/PaintLayer.h"
#include "blocks/uv/UVLayout.h"
#include "Sha256.h"
#include "AssetFilesystemAdapter.h"

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

// =============================================================================
// BM-F4.4: DEPENDENCY CONTRACT & REFERENTIAL INTEGRITY
// =============================================================================

enum class AssetDependencyType : uint8_t {
    Custom = 0,
    Shape = 1,
    Material = 2,
    Texture = 3,
    ParentAsset = 4
};

inline const char* AssetDependencyTypeToString(AssetDependencyType type) {
    switch (type) {
        case AssetDependencyType::Shape: return "shape";
        case AssetDependencyType::Material: return "material";
        case AssetDependencyType::Texture: return "texture";
        case AssetDependencyType::ParentAsset: return "parent_asset";
        default: return "custom";
    }
}

inline AssetDependencyType StringToAssetDependencyType(const std::string& str) {
    if (str == "shape") return AssetDependencyType::Shape;
    if (str == "material") return AssetDependencyType::Material;
    if (str == "texture") return AssetDependencyType::Texture;
    if (str == "parent_asset") return AssetDependencyType::ParentAsset;
    return AssetDependencyType::Custom;
}

enum class DependencyRevisionPolicy : uint8_t {
    Exact = 0,  // Richiede rigorosamente la targetRevision specificata
    Active = 1  // Si aggancia alla revisione correntemente active dell'asset dipendente
};

inline const char* DependencyRevisionPolicyToString(DependencyRevisionPolicy pol) {
    switch (pol) {
        case DependencyRevisionPolicy::Exact: return "Exact";
        case DependencyRevisionPolicy::Active: return "Active";
        default: return "Unknown";
    }
}

// Dependency declaration between assets or derived artifacts
struct RawAssetDependency {
    std::string dependencyType = "custom"; // "shape", "texture", "material", "parent_asset", "custom"
    std::string targetId;                  // ID o URI della risorsa dipendente (es. "fairworld:brick_mat")
    uint64_t requiredRevision = 0;         // Revisione richiesta quando revisionPolicy == Exact
    bool isOptional = false;               // True se la dipendenza non e' bloccante per la pubblicazione
    DependencyRevisionPolicy revisionPolicy = DependencyRevisionPolicy::Exact;
    std::string expectedDigestSha256;     // SHA-256 atteso per dipendenze immutabili (opzionale)

    std::string GetAssetId() const { return targetId; }
    void SetAssetId(const std::string& id) { targetId = id; }
    AssetDependencyType GetTypeEnum() const { return StringToAssetDependencyType(dependencyType); }
    void SetTypeEnum(AssetDependencyType t) { dependencyType = AssetDependencyTypeToString(t); }

    bool operator==(const RawAssetDependency& other) const {
        return dependencyType == other.dependencyType &&
               targetId == other.targetId &&
               requiredRevision == other.requiredRevision &&
               isOptional == other.isOptional &&
               revisionPolicy == other.revisionPolicy &&
               expectedDigestSha256 == other.expectedDigestSha256;
    }
};

using AssetDependency = RawAssetDependency;

// =============================================================================
// BM-F4.5: SCHEMA VERSION CONTRACTS & MIGRATION STATES
// =============================================================================

constexpr uint32_t CURRENT_RAW_ASSET_SCHEMA_VERSION = 2;
constexpr uint32_t LEGACY_RAW_ASSET_SCHEMA_VERSION = 1;

enum class SchemaVersionState : uint8_t {
    CurrentSchema = 0,     // Schema V2 (corrente, autoritativo e conforme a Strict)
    LegacySchema = 1,      // Schema V1 (supportato retrocompatibilmente o migrabile)
    MigrationRequired = 2, // Schema V1 in contesto che impone migrazione obbligatoria
    UnsupportedSchema = 3, // Schema sconosciuto o non supportato (es. V0, V99)
    MigrationFailed = 4    // Migrazione tentata e non andata a buon fine
};

inline const char* SchemaVersionStateToString(SchemaVersionState state) {
    switch (state) {
        case SchemaVersionState::CurrentSchema: return "CurrentSchema";
        case SchemaVersionState::LegacySchema: return "LegacySchema";
        case SchemaVersionState::MigrationRequired: return "MigrationRequired";
        case SchemaVersionState::UnsupportedSchema: return "UnsupportedSchema";
        case SchemaVersionState::MigrationFailed: return "MigrationFailed";
        default: return "Unknown";
    }
}

// =============================================================================
// BM-F4.2: TRUST POLICY & INTEGRITY STATUS CONTRACTS
// =============================================================================

enum class AssetTrustPolicy : uint8_t {
    Strict = 0,
    AllowLegacy = 1
};

enum class AssetIntegrityStatus : uint8_t {
    Verified = 0,
    LegacyUnverified = 1,
    Rejected = 2
};

enum class AssetIntegrityErrorCode : uint8_t {
    None = 0,
    MissingDigest,
    InvalidDigestFormat,
    DigestMismatch,
    UnsupportedSchema,
    MissingDependency,
    TextureDecodeFailure,
    FileReadFailure,
    LegacyUnverified
};

inline const char* AssetTrustPolicyToString(AssetTrustPolicy policy) {
    switch (policy) {
        case AssetTrustPolicy::Strict: return "Strict";
        case AssetTrustPolicy::AllowLegacy: return "AllowLegacy";
        default: return "Unknown";
    }
}

inline const char* AssetIntegrityStatusToString(AssetIntegrityStatus status) {
    switch (status) {
        case AssetIntegrityStatus::Verified: return "Verified";
        case AssetIntegrityStatus::LegacyUnverified: return "LegacyUnverified";
        case AssetIntegrityStatus::Rejected: return "Rejected";
        default: return "Unknown";
    }
}

inline const char* AssetIntegrityErrorCodeToString(AssetIntegrityErrorCode code) {
    switch (code) {
        case AssetIntegrityErrorCode::None: return "None";
        case AssetIntegrityErrorCode::MissingDigest: return "MissingDigest";
        case AssetIntegrityErrorCode::InvalidDigestFormat: return "InvalidDigestFormat";
        case AssetIntegrityErrorCode::DigestMismatch: return "DigestMismatch";
        case AssetIntegrityErrorCode::UnsupportedSchema: return "UnsupportedSchema";
        case AssetIntegrityErrorCode::MissingDependency: return "MissingDependency";
        case AssetIntegrityErrorCode::TextureDecodeFailure: return "TextureDecodeFailure";
        case AssetIntegrityErrorCode::FileReadFailure: return "FileReadFailure";
        case AssetIntegrityErrorCode::LegacyUnverified: return "LegacyUnverified";
        default: return "Unknown";
    }
}

inline bool IsValidSha256HexFormat(const std::string& hex) {
    if (hex.size() != 64) return false;
    for (char c : hex) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

// Validation report produced by Quality Control and station checks
struct RawAssetValidation {
    bool isValid = false;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    AssetIntegrityStatus integrityStatus = AssetIntegrityStatus::Rejected;
    AssetIntegrityErrorCode primaryErrorCode = AssetIntegrityErrorCode::None;

    void AddError(const std::string& err, AssetIntegrityErrorCode code = AssetIntegrityErrorCode::None) {
        errors.push_back(err);
        isValid = false;
        integrityStatus = AssetIntegrityStatus::Rejected;
        if (primaryErrorCode == AssetIntegrityErrorCode::None && code != AssetIntegrityErrorCode::None) {
            primaryErrorCode = code;
        }
    }

    void AddWarning(const std::string& warn) {
        warnings.push_back(warn);
    }

    void Reset() {
        isValid = true;
        errors.clear();
        warnings.clear();
        integrityStatus = AssetIntegrityStatus::Rejected;
        primaryErrorCode = AssetIntegrityErrorCode::None;
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

    // BM-F4.5: Tracciamento della provenienza per migrazioni di schema
    struct MigrationProvenance {
        bool wasMigrated = false;
        uint32_t sourceSchemaVersion = 0;
        uint64_t migrationTimestamp = 0;
        uint64_t originalDistributionChecksum = 0;
        std::string migrationNotes;
    } migrationProvenance;
};

// =============================================================================
// GEOMETRY SOURCE SELECTION (BM-F3)
// =============================================================================
enum class GeometrySource : uint8_t {
    Primitive = 0,
    SculptedMesh = 1
};

// =============================================================================
// RAW ASSET DRAFT — Rappresentazione Modificabile dell'Asset in Fabbrica
// =============================================================================
struct RawAssetDraft {
    RawAssetManifest manifest;

    // Sorgente geometrica esplicita (BM-F3)
    GeometrySource geometrySource = GeometrySource::Primitive;

    // 1. Stazione Shape: Geometria parametrica o definita
    ShapeDefinition shape;

    // 1b. Stazione Sculpt: Mesh poligonale avanzata CPU-authoritative (BM-S)
    std::optional<EditableMesh> sculptMesh;

    // 2. Trasformazioni di base e Pivot nello spazio modello dell'asset
    glm::vec3 localPosition{0.0f, 0.0f, 0.0f};
    glm::vec3 localRotation{0.0f, 0.0f, 0.0f}; // Gradi Euler (Pitch, Yaw, Roll)
    glm::vec3 localScale{1.0f, 1.0f, 1.0f};
    glm::vec3 pivotOffset{0.0f, 0.0f, 0.0f};

    // 3. Stazione SurfaceUV: Mappatura, proiezioni e superfici semantiche (BM-UV2)
    UVTransform baseUVTransform;
    UVProjectionMode baseProjection = UVProjectionMode::Default;
    std::vector<SurfaceOverride> surfaces;
    uint64_t uvRevision = 1;
    std::optional<UVLayout> uvLayout;

    // 4. Stazione Material & Texture: Parametri visivi PBR e mappe
    uint32_t scientificMaterialId = 0; // MA-02.2: Scientific Material Core reference (0 = none/custom)
    glm::vec3 baseColorFallback{1.0f, 1.0f, 1.0f};
    float roughnessFallback = 1.0f;
    float metallicFallback = 0.0f;
    float emissiveStrength = 0.0f;
    float alphaFallback = 1.0f;

    std::string albedoPath;
    std::string normalPath;
    std::string ormPath;
    std::string emissivePath;
    uint32_t behaviors = 0;

    // BM-TX2: Texture Pack CPU Headless
    uint64_t textureRevision = 1;
    std::optional<PBRTexturePack> textures;

    // BM-TX3: Painting & Layer Engine CPU Headless
    uint64_t paintRevision = 1;
    std::vector<PaintLayer> paintLayers;

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
    std::string emissiveMapUri;
    uint64_t textureFingerprint = 0;

    // Trasformazione standard congelata
    glm::vec3 packagePosition{0.0f};
    glm::vec3 packageRotation{0.0f};
    glm::vec3 packageScale{1.0f};
    glm::vec3 packagePivot{0.0f};

    // Impronta digitale univoca (hash di validazione Zero-Work, FNV-1a non crittografico)
    uint64_t packageFingerprint = 0;
    uint64_t paintFingerprint = 0;

    // Checksum legacy non crittografico (conservato per backward-compatibility schema v1)
    uint64_t distributionChecksum = 0;

    // Digest crittografico SHA-256 (BM-F4.1) per integrità immutabile e distribuzione online
    std::string contentDigestSha256;

    // Sorgente geometrica esplicita (BM-F3)
    GeometrySource geometrySource = GeometrySource::Primitive;

    // Rappresentazione topologica persistente isolata (senza riferimenti a GPU/Vulkan o puntatori UI)
    std::string sculptTopologyJson;

    // Definizione di forma compilata per ShapeRegistry (BM-F2C)
    ShapeDefinition shape;

    // Parametri visivi compilati (BM-F2C)
    uint32_t scientificMaterialId = 0; // MA-02.2: Scientific Material Core reference
    glm::vec3 baseColorFallback{1.0f};
    float roughnessFallback = 1.0f;
    float metallicFallback = 0.0f;
    float emissiveStrength = 0.0f;
    float alphaFallback = 1.0f;
    uint32_t behaviors = 0;

    bool SaveToFile(const std::string& filepath) const;
    bool LoadFromFile(const std::string& filepath);

    uint64_t ComputeDistributionChecksum() const {
        uint64_t h = 14695981039346656037ULL;
        auto hashStr = [&](const std::string& str) {
            for (char c : str) {
                h ^= static_cast<uint64_t>(c);
                h *= 1099511628211ULL;
            }
        };
        auto hashU64 = [&](uint64_t val) {
            h ^= val;
            h *= 1099511628211ULL;
        };

        hashStr(manifest.assetId);
        hashU64(manifest.revision);
        hashU64(static_cast<uint64_t>(geometrySource));
        hashStr(albedoMapUri);
        hashStr(normalMapUri);
        hashStr(ormMapUri);
        hashU64(textureFingerprint);
        hashU64(paintFingerprint);
        hashU64(geometry.vertexCount);
        hashU64(geometry.triangleCount);
        for (const auto& v : geometry.vertices) {
            hashU64(*reinterpret_cast<const uint32_t*>(&v.position.x));
            hashU64(*reinterpret_cast<const uint32_t*>(&v.position.y));
            hashU64(*reinterpret_cast<const uint32_t*>(&v.position.z));
            hashU64(*reinterpret_cast<const uint32_t*>(&v.roughMetal.x));
            hashU64(*reinterpret_cast<const uint32_t*>(&v.roughMetal.y));
            hashU64(v.materialID);
        }
        for (const auto& f : geometry.faces) {
            hashU64(f.surfaceId);
            hashU64(static_cast<uint64_t>(f.matIndex));
            for (int idx : f.indices) {
                hashU64(static_cast<uint64_t>(idx));
            }
        }
        hashStr(sculptTopologyJson);
        return h;
    }

    std::string ComputeContentDigestSha256(const std::string& assetRootDir = "") const {
        Sha256 ctx;
        // 1. Header canonico deterministico
        const char header[] = "FAIRWORLD_RAW_ASSET_CANONICAL_V2\n";
        ctx.Update(header, sizeof(header) - 1);

        auto hashU32 = [&](uint32_t val) {
            uint8_t bytes[4] = {
                static_cast<uint8_t>(val & 0xFF),
                static_cast<uint8_t>((val >> 8) & 0xFF),
                static_cast<uint8_t>((val >> 16) & 0xFF),
                static_cast<uint8_t>((val >> 24) & 0xFF)
            };
            ctx.Update(bytes, 4);
        };
        auto hashU64 = [&](uint64_t val) {
            uint8_t bytes[8] = {
                static_cast<uint8_t>(val & 0xFF),
                static_cast<uint8_t>((val >> 8) & 0xFF),
                static_cast<uint8_t>((val >> 16) & 0xFF),
                static_cast<uint8_t>((val >> 24) & 0xFF),
                static_cast<uint8_t>((val >> 32) & 0xFF),
                static_cast<uint8_t>((val >> 40) & 0xFF),
                static_cast<uint8_t>((val >> 48) & 0xFF),
                static_cast<uint8_t>((val >> 56) & 0xFF)
            };
            ctx.Update(bytes, 8);
        };
        auto hashFloat = [&](float f) {
            uint32_t u;
            std::memcpy(&u, &f, sizeof(float));
            hashU32(u);
        };
        auto hashStr = [&](const std::string& s) {
            hashU32(static_cast<uint32_t>(s.size()));
            if (!s.empty()) ctx.Update(s.data(), s.size());
        };

        // 2. Manifest (identità logica, revisione, targetVoxelId, stage)
        hashU32(manifest.schemaVersion);
        hashStr(manifest.assetId);
        hashU64(manifest.revision);
        hashU32(static_cast<uint32_t>(manifest.stage));
        hashU32(static_cast<uint32_t>(manifest.targetRuntimeVoxelId));

        // 3. GeometrySource & Shape
        hashU32(static_cast<uint32_t>(geometrySource));
        hashU32(shape.id);
        hashU32(shape.revision);
        hashU32(static_cast<uint32_t>(shape.type));
        hashU32(shape.supportsVisualCompilation ? 1 : 0);
        hashU32(shape.supportsCollisionCompilation ? 1 : 0);

        if (shape.type == ShapeType::Cuboid && std::holds_alternative<ShapeParamsCuboid>(shape.parameters)) {
            const auto& p = std::get<ShapeParamsCuboid>(shape.parameters);
            hashFloat(p.size.x); hashFloat(p.size.y); hashFloat(p.size.z);
        } else if (shape.type == ShapeType::Cylinder && std::holds_alternative<ShapeParamsCylinder>(shape.parameters)) {
            const auto& p = std::get<ShapeParamsCylinder>(shape.parameters);
            hashFloat(p.radius); hashFloat(p.height);
        } else if (shape.type == ShapeType::Sphere && std::holds_alternative<ShapeParamsSphere>(shape.parameters)) {
            const auto& p = std::get<ShapeParamsSphere>(shape.parameters);
            hashFloat(p.radius);
        } else if (shape.type == ShapeType::Cone && std::holds_alternative<ShapeParamsCone>(shape.parameters)) {
            const auto& p = std::get<ShapeParamsCone>(shape.parameters);
            hashFloat(p.radius); hashFloat(p.height);
        } else if (shape.type == ShapeType::TriangleMesh && std::holds_alternative<ShapeParamsTriangleMesh>(shape.parameters)) {
            const auto& p = std::get<ShapeParamsTriangleMesh>(shape.parameters);
            hashU32(static_cast<uint32_t>(p.vertices.size()));
            for (const auto& v : p.vertices) {
                hashFloat(v.x); hashFloat(v.y); hashFloat(v.z);
            }
            hashU32(static_cast<uint32_t>(p.indices.size()));
            for (uint32_t idx : p.indices) {
                hashU32(idx);
            }
        }

        // 4. Transform
        hashFloat(packagePosition.x); hashFloat(packagePosition.y); hashFloat(packagePosition.z);
        hashFloat(packageRotation.x); hashFloat(packageRotation.y); hashFloat(packageRotation.z);
        hashFloat(packageScale.x); hashFloat(packageScale.y); hashFloat(packageScale.z);
        hashFloat(packagePivot.x); hashFloat(packagePivot.y); hashFloat(packagePivot.z);

        // 5. Visual Fallback
        hashFloat(baseColorFallback.x); hashFloat(baseColorFallback.y); hashFloat(baseColorFallback.z);
        hashFloat(roughnessFallback);
        hashFloat(metallicFallback);
        hashFloat(emissiveStrength);
        hashFloat(alphaFallback);
        hashU32(behaviors);

        // 6. Physics
        hashFloat(compiledPhysics.mass);
        hashFloat(compiledPhysics.density);
        hashFloat(compiledPhysics.friction);
        hashFloat(compiledPhysics.restitution);
        hashU32(compiledPhysics.collisionEnabled ? 1 : 0);
        hashU32(static_cast<uint32_t>(compiledPhysics.motionType));
        hashU32(static_cast<uint32_t>(compiledPhysics.massMode));

        // 7. Geometry Vertices & Faces
        hashU64(geometry.vertexCount);
        hashU64(geometry.triangleCount);
        hashFloat(geometry.bounds.min.x); hashFloat(geometry.bounds.min.y); hashFloat(geometry.bounds.min.z);
        hashFloat(geometry.bounds.max.x); hashFloat(geometry.bounds.max.y); hashFloat(geometry.bounds.max.z);

        for (const auto& v : geometry.vertices) {
            hashFloat(v.position.x); hashFloat(v.position.y); hashFloat(v.position.z);
            hashFloat(v.color.x); hashFloat(v.color.y); hashFloat(v.color.z); hashFloat(v.color.w);
            hashFloat(v.roughMetal.x); hashFloat(v.roughMetal.y);
            hashU32(v.materialID);
            hashFloat(v.normal.x); hashFloat(v.normal.y); hashFloat(v.normal.z);
            hashFloat(v.ao);
            hashFloat(v.light);
            hashFloat(v.emissive);
        }

        for (const auto& f : geometry.faces) {
            hashU32(f.surfaceId);
            hashU32(static_cast<uint32_t>(f.matIndex));
            hashU32(static_cast<uint32_t>(f.indices.size()));
            for (int idx : f.indices) {
                hashU32(static_cast<uint32_t>(idx));
            }
        }

        // 8. Sculpt Topology Json
        hashStr(sculptTopologyJson);

        // 9. Texture Artifacts (Canonical Relative Portable URI + RAW BYTES)
        auto hashTextureArtifact = [&](const std::string& uri) {
            std::string normUri = uri;
            for (char& c : normUri) {
                if (c == '\\') c = '/';
            }
            hashStr(normUri);

            if (!normUri.empty()) {
                std::filesystem::path fullPath = normUri;
                if (!assetRootDir.empty()) {
                    fullPath = std::filesystem::path(assetRootDir) / normUri;
                }
                std::error_code ec;
                if (std::filesystem::exists(fullPath, ec) && std::filesystem::is_regular_file(fullPath, ec)) {
                    uintmax_t sz = std::filesystem::file_size(fullPath, ec);
                    hashU64(sz);
                    std::ifstream f(fullPath, std::ios::binary);
                    if (f.is_open()) {
                        char buf[8192];
                        while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
                            ctx.Update(buf, static_cast<size_t>(f.gcount()));
                        }
                    }
                } else {
                    hashU64(0xFFFFFFFFFFFFFFFFULL);
                }
            }
        };

        hashTextureArtifact(albedoMapUri);
        hashTextureArtifact(normalMapUri);
        hashTextureArtifact(ormMapUri);
        hashTextureArtifact(emissiveMapUri);

        return ctx.FinalizeHex();
    }

    bool VerifyContentDigest(
        const std::string& assetRootDir = "",
        std::string* outExpected = nullptr,
        std::string* outComputed = nullptr,
        AssetTrustPolicy policy = AssetTrustPolicy::Strict,
        AssetIntegrityErrorCode* outError = nullptr
    ) const {
        if (outError) *outError = AssetIntegrityErrorCode::None;

        if (manifest.schemaVersion == 0 || manifest.schemaVersion > 2) {
            if (outError) *outError = AssetIntegrityErrorCode::UnsupportedSchema;
            return false;
        }

        if (manifest.schemaVersion == 1) {
            if (policy == AssetTrustPolicy::Strict) {
                if (outError) *outError = AssetIntegrityErrorCode::UnsupportedSchema;
                return false;
            }
            if (contentDigestSha256.empty()) {
                if (outError) *outError = AssetIntegrityErrorCode::LegacyUnverified;
                return true;
            }
        }

        if (contentDigestSha256.empty()) {
            if (outError) *outError = AssetIntegrityErrorCode::MissingDigest;
            return false;
        }

        if (!IsValidSha256HexFormat(contentDigestSha256)) {
            if (outError) *outError = AssetIntegrityErrorCode::InvalidDigestFormat;
            return false;
        }

        std::string computed = ComputeContentDigestSha256(assetRootDir);
        if (outExpected) *outExpected = contentDigestSha256;
        if (outComputed) *outComputed = computed;

        if (contentDigestSha256 != computed) {
            if (outError) *outError = AssetIntegrityErrorCode::DigestMismatch;
            return false;
        }

        return true;
    }

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
        {"isOptional", d.isOptional},
        {"revisionPolicy", static_cast<uint8_t>(d.revisionPolicy)},
        {"expectedDigestSha256", d.expectedDigestSha256}
    };
}

inline void from_json(const nlohmann::json& j, RawAssetDependency& d) {
    d.dependencyType = j.value("type", "custom");
    d.targetId = j.value("targetId", "");
    d.requiredRevision = j.value("requiredRevision", static_cast<uint64_t>(0));
    d.isOptional = j.value("isOptional", false);
    d.revisionPolicy = static_cast<DependencyRevisionPolicy>(j.value("revisionPolicy", static_cast<uint8_t>(DependencyRevisionPolicy::Exact)));
    d.expectedDigestSha256 = j.value("expectedDigestSha256", "");
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
    if (m.migrationProvenance.wasMigrated) {
        j["migrationProvenance"] = {
            {"wasMigrated", m.migrationProvenance.wasMigrated},
            {"sourceSchemaVersion", m.migrationProvenance.sourceSchemaVersion},
            {"migrationTimestamp", m.migrationProvenance.migrationTimestamp},
            {"originalDistributionChecksum", m.migrationProvenance.originalDistributionChecksum},
            {"migrationNotes", m.migrationProvenance.migrationNotes}
        };
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
    if (j.contains("migrationProvenance")) {
        const auto& mp = j["migrationProvenance"];
        m.migrationProvenance.wasMigrated = mp.value("wasMigrated", false);
        m.migrationProvenance.sourceSchemaVersion = mp.value("sourceSchemaVersion", 0u);
        m.migrationProvenance.migrationTimestamp = mp.value("migrationTimestamp", static_cast<uint64_t>(0));
        m.migrationProvenance.originalDistributionChecksum = mp.value("originalDistributionChecksum", static_cast<uint64_t>(0));
        m.migrationProvenance.migrationNotes = mp.value("migrationNotes", "");
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
            {"scientificMaterialId", d.scientificMaterialId},
            {"baseColor", {d.baseColorFallback.x, d.baseColorFallback.y, d.baseColorFallback.z}},
            {"roughness", d.roughnessFallback},
            {"metallic", d.metallicFallback},
            {"emissive", d.emissiveStrength},
            {"alpha", d.alphaFallback},
            {"albedoPath", d.albedoPath},
            {"normalPath", d.normalPath},
            {"ormPath", d.ormPath},
            {"emissivePath", d.emissivePath},
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
    if (d.sculptMesh.has_value()) {
        j["sculptMesh"] = d.sculptMesh->ToJson();
    }
    j["geometrySource"] = static_cast<int>(d.geometrySource);
    if (!d.paintLayers.empty()) {
        nlohmann::json jLayers = nlohmann::json::array();
        for (const auto& l : d.paintLayers) {
            jLayers.push_back(l.ToJson());
        }
        j["paintLayers"] = jLayers;
        j["paintRevision"] = d.paintRevision;
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
        d.scientificMaterialId = jg.value("scientificMaterialId", 0u);
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
        d.emissivePath = jg.value("emissivePath", "");
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
    if (j.contains("sculptMesh") && !j["sculptMesh"].is_null()) {
        EditableMesh em;
        if (em.FromJson(j["sculptMesh"])) {
            d.sculptMesh = std::move(em);
            d.geometrySource = GeometrySource::SculptedMesh;
        }
    }
    if (j.contains("geometrySource")) {
        d.geometrySource = static_cast<GeometrySource>(j.value("geometrySource", 0));
    }
    d.paintRevision = j.value("paintRevision", static_cast<uint64_t>(1));
    if (j.contains("paintLayers") && j["paintLayers"].is_array()) {
        d.paintLayers.clear();
        for (const auto& lj : j["paintLayers"]) {
            d.paintLayers.push_back(PaintLayer::FromJson(lj));
        }
    }
}

// --- SERIALIZZAZIONE RAW ASSET PACKAGE (BM-F2C) ---

inline void to_json(nlohmann::json& j, const Face& f) {
    j = nlohmann::json{
        {"indices", f.indices},
        {"faceNormal", {f.faceNormal.x, f.faceNormal.y, f.faceNormal.z}},
        {"matIndex", f.matIndex},
        {"surfaceId", f.surfaceId}
    };
}

inline void from_json(const nlohmann::json& j, Face& f) {
    if (j.contains("indices") && j["indices"].is_array()) {
        f.indices = j["indices"].get<std::vector<int>>();
    }
    if (j.contains("faceNormal") && j["faceNormal"].size() >= 3) {
        f.faceNormal = Vec3(j["faceNormal"][0], j["faceNormal"][1], j["faceNormal"][2]);
    }
    f.matIndex = j.value("matIndex", 0);
    f.surfaceId = j.value("surfaceId", 0u);
}

inline void to_json(nlohmann::json& j, const Vertex& v) {
    j = nlohmann::json{
        {"pos", {v.position.x, v.position.y, v.position.z}},
        {"color", {v.color.x, v.color.y, v.color.z, v.color.w}},
        {"roughMetal", {v.roughMetal.x, v.roughMetal.y}},
        {"materialID", v.materialID},
        {"normal", {v.normal.x, v.normal.y, v.normal.z}},
        {"ao", v.ao},
        {"light", v.light},
        {"emissive", v.emissive}
    };
}

inline void from_json(const nlohmann::json& j, Vertex& v) {
    if (j.contains("pos") && j["pos"].size() >= 3) {
        v.position = Vec3(j["pos"][0], j["pos"][1], j["pos"][2]);
    }
    if (j.contains("color") && j["color"].size() >= 4) {
        v.color = Vec4{j["color"][0], j["color"][1], j["color"][2], j["color"][3]};
    }
    if (j.contains("roughMetal") && j["roughMetal"].size() >= 2) {
        v.roughMetal = Vec2{j["roughMetal"][0], j["roughMetal"][1]};
    }
    v.materialID = j.value("materialID", 0u);
    if (j.contains("normal") && j["normal"].size() >= 3) {
        v.normal = Vec3(j["normal"][0], j["normal"][1], j["normal"][2]);
    }
    v.ao = j.value("ao", 1.0f);
    v.light = j.value("light", 1.0f);
    v.emissive = j.value("emissive", 0.0f);
}

inline void to_json(nlohmann::json& j, const ShapeDefinition& s) {
    j = nlohmann::json{
        {"id", s.id},
        {"revision", s.revision},
        {"type", static_cast<uint8_t>(s.type)},
        {"supportsVisualCompilation", s.supportsVisualCompilation},
        {"supportsCollisionCompilation", s.supportsCollisionCompilation}
    };
    if (s.type == ShapeType::Cuboid && std::holds_alternative<ShapeParamsCuboid>(s.parameters)) {
        const auto& p = std::get<ShapeParamsCuboid>(s.parameters);
        j["params"] = {{"size", {p.size.x, p.size.y, p.size.z}}};
    } else if (s.type == ShapeType::Cylinder && std::holds_alternative<ShapeParamsCylinder>(s.parameters)) {
        const auto& p = std::get<ShapeParamsCylinder>(s.parameters);
        j["params"] = {{"radius", p.radius}, {"height", p.height}};
    } else if (s.type == ShapeType::Sphere && std::holds_alternative<ShapeParamsSphere>(s.parameters)) {
        const auto& p = std::get<ShapeParamsSphere>(s.parameters);
        j["params"] = {{"radius", p.radius}};
    } else if (s.type == ShapeType::Cone && std::holds_alternative<ShapeParamsCone>(s.parameters)) {
        const auto& p = std::get<ShapeParamsCone>(s.parameters);
        j["params"] = {{"radius", p.radius}, {"height", p.height}};
    } else if (s.type == ShapeType::TriangleMesh && std::holds_alternative<ShapeParamsTriangleMesh>(s.parameters)) {
        const auto& p = std::get<ShapeParamsTriangleMesh>(s.parameters);
        nlohmann::json jv = nlohmann::json::array();
        for (const auto& v : p.vertices) {
            jv.push_back({v.x, v.y, v.z});
        }
        j["params"] = {{"vertices", jv}, {"indices", p.indices}};
    }
}

inline void from_json(const nlohmann::json& j, ShapeDefinition& s) {
    s.id = j.value("id", 0u);
    s.revision = j.value("revision", 1u);
    s.type = static_cast<ShapeType>(j.value("type", static_cast<uint8_t>(ShapeType::Cube)));
    s.supportsVisualCompilation = j.value("supportsVisualCompilation", true);
    s.supportsCollisionCompilation = j.value("supportsCollisionCompilation", true);
    if (j.contains("params")) {
        const auto& jp = j["params"];
        if (s.type == ShapeType::Cuboid && jp.contains("size") && jp["size"].size() >= 3) {
            s.parameters = ShapeParamsCuboid{glm::vec3(jp["size"][0], jp["size"][1], jp["size"][2])};
        } else if (s.type == ShapeType::Cylinder) {
            s.parameters = ShapeParamsCylinder{jp.value("radius", 0.5f), jp.value("height", 1.0f)};
        } else if (s.type == ShapeType::Sphere) {
            s.parameters = ShapeParamsSphere{jp.value("radius", 0.5f)};
        } else if (s.type == ShapeType::Cone) {
            s.parameters = ShapeParamsCone{jp.value("radius", 0.5f), jp.value("height", 1.0f)};
        } else if (s.type == ShapeType::TriangleMesh && jp.contains("vertices") && jp.contains("indices")) {
            ShapeParamsTriangleMesh p;
            for (const auto& v : jp["vertices"]) {
                if (v.size() >= 3) {
                    p.vertices.push_back(glm::vec3(v[0], v[1], v[2]));
                }
            }
            p.indices = jp["indices"].get<std::vector<uint32_t>>();
            s.parameters = p;
        }
    }
}

inline void to_json(nlohmann::json& j, const RawAssetPackage& p) {
    j = nlohmann::json{
        {"manifest", p.manifest},
        {"shape", p.shape},
        {"transform", {
            {"position", {p.packagePosition.x, p.packagePosition.y, p.packagePosition.z}},
            {"rotation", {p.packageRotation.x, p.packageRotation.y, p.packageRotation.z}},
            {"scale", {p.packageScale.x, p.packageScale.y, p.packageScale.z}},
            {"pivot", {p.packagePivot.x, p.packagePivot.y, p.packagePivot.z}}
        }},
        {"graphics", {
            {"scientificMaterialId", p.scientificMaterialId},
            {"baseColor", {p.baseColorFallback.x, p.baseColorFallback.y, p.baseColorFallback.z}},
            {"roughness", p.roughnessFallback},
            {"metallic", p.metallicFallback},
            {"emissive", p.emissiveStrength},
            {"alpha", p.alphaFallback},
            {"behaviors", p.behaviors},
            {"albedoMapUri", p.albedoMapUri},
            {"normalMapUri", p.normalMapUri},
            {"ormMapUri", p.ormMapUri},
            {"emissiveMapUri", p.emissiveMapUri},
            {"textureFingerprint", p.textureFingerprint}
        }},
        {"physics", {
            {"mass", p.compiledPhysics.mass},
            {"density", p.compiledPhysics.density},
            {"friction", p.compiledPhysics.friction},
            {"restitution", p.compiledPhysics.restitution},
            {"collisionEnabled", p.compiledPhysics.collisionEnabled},
            {"motionType", static_cast<int>(p.compiledPhysics.motionType)},
            {"massMode", static_cast<int>(p.compiledPhysics.massMode)}
        }},
        {"fingerprints", {
            {"packageFingerprint", p.packageFingerprint},
            {"paintFingerprint", p.paintFingerprint},
            {"distributionChecksum", p.distributionChecksum},
            {"contentDigestSha256", p.contentDigestSha256}
        }},
        {"geometrySource", static_cast<int>(p.geometrySource)},
        {"sculptTopologyJson", p.sculptTopologyJson}
    };

    nlohmann::json jGeom;
    jGeom["vertexCount"] = p.geometry.vertexCount;
    jGeom["triangleCount"] = p.geometry.triangleCount;
    jGeom["bounds"] = {
        {"min", {p.geometry.bounds.min.x, p.geometry.bounds.min.y, p.geometry.bounds.min.z}},
        {"max", {p.geometry.bounds.max.x, p.geometry.bounds.max.y, p.geometry.bounds.max.z}}
    };
    if (!p.geometry.vertices.empty()) {
        nlohmann::json jVerts = nlohmann::json::array();
        for (const auto& v : p.geometry.vertices) {
            nlohmann::json jv;
            to_json(jv, v);
            jVerts.push_back(jv);
        }
        jGeom["vertices"] = jVerts;
    }
    if (!p.geometry.faces.empty()) {
        nlohmann::json jFaces = nlohmann::json::array();
        for (const auto& f : p.geometry.faces) {
            nlohmann::json jf;
            to_json(jf, f);
            jFaces.push_back(jf);
        }
        jGeom["faces"] = jFaces;
    }
    j["geometry"] = jGeom;
}

inline void from_json(const nlohmann::json& j, RawAssetPackage& p) {
    if (j.contains("manifest")) j["manifest"].get_to(p.manifest);
    if (j.contains("shape")) from_json(j["shape"], p.shape);
    if (j.contains("transform")) {
        const auto& jt = j["transform"];
        if (jt.contains("position") && jt["position"].size() >= 3) {
            p.packagePosition = {jt["position"][0], jt["position"][1], jt["position"][2]};
        }
        if (jt.contains("rotation") && jt["rotation"].size() >= 3) {
            p.packageRotation = {jt["rotation"][0], jt["rotation"][1], jt["rotation"][2]};
        }
        if (jt.contains("scale") && jt["scale"].size() >= 3) {
            p.packageScale = {jt["scale"][0], jt["scale"][1], jt["scale"][2]};
        }
        if (jt.contains("pivot") && jt["pivot"].size() >= 3) {
            p.packagePivot = {jt["pivot"][0], jt["pivot"][1], jt["pivot"][2]};
        }
    }
    if (j.contains("graphics")) {
        const auto& jg = j["graphics"];
        p.scientificMaterialId = jg.value("scientificMaterialId", 0u);
        if (jg.contains("baseColor") && jg["baseColor"].size() >= 3) {
            p.baseColorFallback = {jg["baseColor"][0], jg["baseColor"][1], jg["baseColor"][2]};
        }
        p.roughnessFallback = jg.value("roughness", 1.0f);
        p.metallicFallback = jg.value("metallic", 0.0f);
        p.emissiveStrength = jg.value("emissive", 0.0f);
        p.alphaFallback = jg.value("alpha", 1.0f);
        p.behaviors = jg.value("behaviors", 0u);
        p.albedoMapUri = jg.value("albedoMapUri", "");
        p.normalMapUri = jg.value("normalMapUri", "");
        p.ormMapUri = jg.value("ormMapUri", "");
        p.emissiveMapUri = jg.value("emissiveMapUri", "");
        p.textureFingerprint = jg.value("textureFingerprint", static_cast<uint64_t>(0));
    }
    if (j.contains("physics")) {
        const auto& jp = j["physics"];
        p.compiledPhysics.mass = jp.value("mass", 1.0f);
        p.compiledPhysics.density = jp.value("density", 1000.0f);
        p.compiledPhysics.friction = jp.value("friction", 0.6f);
        p.compiledPhysics.restitution = jp.value("restitution", 0.0f);
        p.compiledPhysics.collisionEnabled = jp.value("collisionEnabled", true);
        p.compiledPhysics.motionType = static_cast<BlockMotionType>(jp.value("motionType", 0));
        p.compiledPhysics.massMode = static_cast<BlockMassMode>(jp.value("massMode", 0));
    }
    if (j.contains("fingerprints")) {
        const auto& jf = j["fingerprints"];
        p.packageFingerprint = jf.value("packageFingerprint", static_cast<uint64_t>(0));
        p.paintFingerprint = jf.value("paintFingerprint", static_cast<uint64_t>(0));
        p.distributionChecksum = jf.value("distributionChecksum", static_cast<uint64_t>(0));
        p.contentDigestSha256 = jf.value("contentDigestSha256", "");
    }
    p.geometrySource = static_cast<GeometrySource>(j.value("geometrySource", 0));
    p.sculptTopologyJson = j.value("sculptTopologyJson", "");

    if (j.contains("geometry")) {
        const auto& jg = j["geometry"];
        p.geometry.vertexCount = jg.value("vertexCount", 0u);
        p.geometry.triangleCount = jg.value("triangleCount", 0u);
        if (jg.contains("bounds")) {
            const auto& jb = jg["bounds"];
            if (jb.contains("min") && jb["min"].size() >= 3) {
                p.geometry.bounds.min = {jb["min"][0], jb["min"][1], jb["min"][2]};
            }
            if (jb.contains("max") && jb["max"].size() >= 3) {
                p.geometry.bounds.max = {jb["max"][0], jb["max"][1], jb["max"][2]};
            }
        }
        if (jg.contains("vertices") && jg["vertices"].is_array()) {
            p.geometry.vertices.clear();
            for (const auto& jv : jg["vertices"]) {
                Vertex v;
                from_json(jv, v);
                p.geometry.vertices.push_back(v);
            }
        }
        if (jg.contains("faces") && jg["faces"].is_array()) {
            p.geometry.faces.clear();
            for (const auto& jf : jg["faces"]) {
                Face f;
                from_json(jf, f);
                p.geometry.faces.push_back(f);
            }
        }
    }
    p.validationReport.isValid = !p.geometry.vertices.empty() && !p.manifest.assetId.empty();
}

inline bool RawAssetPackage::SaveToFile(const std::string& filepath) const {
    try {
        nlohmann::json j;
        to_json(j, *this);
        std::string jsonStr = j.dump(4);
        std::string err;
        return AssetFilesystemAdapter::DurableWriteFile(filepath, jsonStr, false, &err);
    } catch (...) {
        return false;
    }
}

inline bool RawAssetPackage::LoadFromFile(const std::string& filepath) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;
        nlohmann::json j;
        file >> j;
        from_json(j, *this);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace fw
