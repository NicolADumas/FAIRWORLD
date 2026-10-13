#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include "ShapeRegistry.h"
#include "json.hpp"

namespace fw {

enum class UVProjectionMode : uint8_t {
    Default = 0,
    Planar,
    Box,
    Cylindrical,
    Spherical,
    Triplanar
};

// BM-C4: UV Transform with unambiguous canonical order:
// 1. Tiling -> 2. Mirror -> 3. Rotation (around UV center 0.5, 0.5) -> 4. Offset
struct UVTransform {
    glm::vec2 tiling{1.0f, 1.0f};
    glm::vec2 offset{0.0f, 0.0f};
    float rotationDegrees = 0.0f;
    bool mirrorU = false;
    bool mirrorV = false;

    bool IsIdentity() const {
        return tiling.x == 1.0f && tiling.y == 1.0f &&
               offset.x == 0.0f && offset.y == 0.0f &&
               rotationDegrees == 0.0f &&
               !mirrorU && !mirrorV;
    }

    glm::vec2 Transform(glm::vec2 inUv) const {
        // 1. Tiling
        glm::vec2 uv = inUv * tiling;
        // 2. Mirroring
        if (mirrorU) uv.x = 1.0f - uv.x;
        if (mirrorV) uv.y = 1.0f - uv.y;
        // 3. Rotation around (0.5, 0.5)
        if (std::abs(rotationDegrees) > 1e-4f) {
            float rad = glm::radians(rotationDegrees);
            float c = std::cos(rad);
            float s = std::sin(rad);
            glm::vec2 center(0.5f, 0.5f);
            glm::vec2 p = uv - center;
            uv = glm::vec2(p.x * c - p.y * s, p.x * s + p.y * c) + center;
        }
        // 4. Offset
        uv += offset;
        return uv;
    }
};

// BM-TX2.7: Surface Texture Binding Contract (per-surface texture maps)
struct SurfaceTextureBinding {
    std::string albedoMapPath;
    std::string normalMapPath;
    std::string ormMapPath;
    std::string emissiveMapPath;
    bool useExplicitTextures = false;

    bool HasTextures() const {
        return useExplicitTextures && (!albedoMapPath.empty() || !normalMapPath.empty() || !ormMapPath.empty() || !emissiveMapPath.empty());
    }
};

// BM-C2 & BM-C5: Surface Material & Mapping Contract
struct SurfaceOverride {
    uint32_t surfaceId = 0;              // Semantic surface identifier (0..N-1)
    uint32_t materialId = 0;             // Target material/layer ID (0 = inherit from parent block)
    bool useExplicitMaterial = false;

    UVProjectionMode projection = UVProjectionMode::Default;
    UVTransform uvTransform;

    bool overridePbr = false;
    glm::vec3 baseColorFallback{1.0f};
    float roughness = 1.0f;
    float metallic = 0.0f;
    float emissive = 0.0f;

    // BM-TX2.7: Texture maps per-superficie opzionali
    SurfaceTextureBinding textures;
};

// Semantic surface definitions for standard primitives (BM-C: Asset-specific cube faces, distinctly typed from PlanetMath CubeFace)
enum class AssetCubeFace : uint32_t {
    Back = 0,    // -Z
    Front = 1,   // +Z
    Left = 2,    // -X
    Right = 3,   // +X
    Bottom = 4,  // -Y
    Top = 5      // +Y
};

enum class CylinderPart : uint32_t {
    Body = 0,
    TopCap = 1,
    BottomCap = 2
};

enum class ConePart : uint32_t {
    Body = 0,
    BaseCap = 1
};

enum class CapsulePart : uint32_t {
    Body = 0,
    TopHemisphere = 1,
    BottomHemisphere = 2
};

inline uint32_t GetSemanticSurfaceCount(ShapeType type) {
    switch (type) {
        case ShapeType::Cube:
        case ShapeType::Cuboid:
        case ShapeType::Parallelepiped:
            return 6;
        case ShapeType::Cylinder:
        case ShapeType::HollowCylinder:
        case ShapeType::SlicedCylinder:
        case ShapeType::Ungula:
        case ShapeType::Barrel:
            return 3;
        case ShapeType::Cone:
        case ShapeType::ConeFrustum:
            return 2;
        case ShapeType::Capsule:
            return 3;
        case ShapeType::Pyramid:
            return 5;
        case ShapeType::PyramidFrustum:
            return 6;
        default:
            return 1;
    }
}

inline const char* GetSemanticSurfaceName(ShapeType type, uint32_t surfaceId) {
    if (type == ShapeType::Cube || type == ShapeType::Cuboid || type == ShapeType::Parallelepiped) {
        switch (surfaceId) {
            case 0: return "Back (-Z)";
            case 1: return "Front (+Z)";
            case 2: return "Left (-X)";
            case 3: return "Right (+X)";
            case 4: return "Bottom (-Y)";
            case 5: return "Top (+Y)";
            default: return "Face";
        }
    } else if (type == ShapeType::Cylinder || type == ShapeType::HollowCylinder || type == ShapeType::Barrel) {
        switch (surfaceId) {
            case 0: return "Body (Lateral)";
            case 1: return "Top Cap (+Y)";
            case 2: return "Bottom Cap (-Y)";
            default: return "Part";
        }
    } else if (type == ShapeType::Cone || type == ShapeType::ConeFrustum) {
        switch (surfaceId) {
            case 0: return "Body (Lateral)";
            case 1: return "Base Cap";
            default: return "Part";
        }
    } else if (type == ShapeType::Capsule) {
        switch (surfaceId) {
            case 0: return "Body (Lateral)";
            case 1: return "Top Hemisphere (+Y)";
            case 2: return "Bottom Hemisphere (-Y)";
            default: return "Part";
        }
    }
    return "Main Surface";
}

// BM-C3: Analytical UV projection generators
inline glm::vec2 CalculatePlanarUV(const glm::vec3& pos, const glm::vec3& normal) {
    glm::vec3 absN = glm::abs(normal);
    if (absN.y >= absN.x && absN.y >= absN.z) {
        return glm::vec2(pos.x + 0.5f, pos.z + 0.5f);
    } else if (absN.x >= absN.y && absN.x >= absN.z) {
        return glm::vec2(pos.z + 0.5f, pos.y + 0.5f);
    } else {
        return glm::vec2(pos.x + 0.5f, pos.y + 0.5f);
    }
}

inline glm::vec2 CalculateCylindricalUV(const glm::vec3& pos, float height, uint32_t surfaceId) {
    if (surfaceId == 1) { // Top Cap
        return glm::vec2(pos.x + 0.5f, pos.z + 0.5f);
    } else if (surfaceId == 2) { // Bottom Cap
        return glm::vec2(pos.x + 0.5f, -pos.z + 0.5f);
    }
    // Body
    float angle = std::atan2(pos.z, pos.x); // [-pi, pi]
    float u = (angle / (2.0f * glm::pi<float>())) + 0.5f;
    float safeH = (height > 1e-4f) ? height : 1.0f;
    float v = (pos.y / safeH) + 0.5f;
    return glm::vec2(u, v);
}

inline glm::vec2 CalculateSphericalUV(const glm::vec3& norm) {
    glm::vec3 n = glm::normalize(norm);
    float u = 0.5f + (std::atan2(n.z, n.x) / (2.0f * glm::pi<float>()));
    float v = 0.5f - (std::asin(glm::clamp(n.y, -1.0f, 1.0f)) / glm::pi<float>());
    return glm::vec2(u, v);
}

// JSON Serialization helpers
inline void to_json(nlohmann::json& j, const UVTransform& t) {
    j = nlohmann::json{
        {"tiling", {t.tiling.x, t.tiling.y}},
        {"offset", {t.offset.x, t.offset.y}},
        {"rotation", t.rotationDegrees},
        {"mirrorU", t.mirrorU},
        {"mirrorV", t.mirrorV}
    };
}

inline void from_json(const nlohmann::json& j, UVTransform& t) {
    if (j.contains("tiling") && j["tiling"].is_array() && j["tiling"].size() >= 2) {
        t.tiling = glm::vec2(j["tiling"][0].get<float>(), j["tiling"][1].get<float>());
    }
    if (j.contains("offset") && j["offset"].is_array() && j["offset"].size() >= 2) {
        t.offset = glm::vec2(j["offset"][0].get<float>(), j["offset"][1].get<float>());
    }
    t.rotationDegrees = j.value("rotation", 0.0f);
    t.mirrorU = j.value("mirrorU", false);
    t.mirrorV = j.value("mirrorV", false);
}

inline void to_json(nlohmann::json& j, const SurfaceOverride& s) {
    j = nlohmann::json{
        {"surfaceId", s.surfaceId},
        {"materialId", s.materialId},
        {"useExplicitMaterial", s.useExplicitMaterial},
        {"projection", static_cast<uint8_t>(s.projection)},
        {"uvTransform", s.uvTransform},
        {"overridePbr", s.overridePbr},
        {"baseColor", {s.baseColorFallback.x, s.baseColorFallback.y, s.baseColorFallback.z}},
        {"roughness", s.roughness},
        {"metallic", s.metallic},
        {"emissive", s.emissive}
    };
    if (s.textures.HasTextures()) {
        j["textures"] = {
            {"albedo", s.textures.albedoMapPath},
            {"normal", s.textures.normalMapPath},
            {"orm", s.textures.ormMapPath},
            {"emissive", s.textures.emissiveMapPath},
            {"useExplicit", s.textures.useExplicitTextures}
        };
    }
}

inline void from_json(const nlohmann::json& j, SurfaceOverride& s) {
    s.surfaceId = j.value("surfaceId", (uint32_t)0);
    s.materialId = j.value("materialId", (uint32_t)0);
    s.useExplicitMaterial = j.value("useExplicitMaterial", false);
    s.projection = static_cast<UVProjectionMode>(j.value("projection", (uint8_t)0));
    if (j.contains("uvTransform")) {
        j["uvTransform"].get_to(s.uvTransform);
    }
    s.overridePbr = j.value("overridePbr", false);
    if (j.contains("baseColor") && j["baseColor"].is_array() && j["baseColor"].size() >= 3) {
        s.baseColorFallback = glm::vec3(j["baseColor"][0].get<float>(), j["baseColor"][1].get<float>(), j["baseColor"][2].get<float>());
    }
    s.roughness = j.value("roughness", 1.0f);
    s.metallic = j.value("metallic", 0.0f);
    s.emissive = j.value("emissive", 0.0f);
    if (j.contains("textures")) {
        const auto& jt = j["textures"];
        s.textures.albedoMapPath = jt.value("albedo", "");
        s.textures.normalMapPath = jt.value("normal", "");
        s.textures.ormMapPath = jt.value("orm", "");
        s.textures.emissiveMapPath = jt.value("emissive", "");
        s.textures.useExplicitTextures = jt.value("useExplicit", false);
    }
}

} // namespace fw
