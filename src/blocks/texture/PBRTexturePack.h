#pragma once

#include <optional>
#include <string>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include "ImageBitmap.h"
#include "TextureAsset.h"

namespace fw {

// =============================================================================
// BM-TX2.5 & BM-TX2.6 — PBR TEXTURE PACK & ORM PACKING
// =============================================================================

class PBRTexturePack {
public:
    PBRTexturePack();

    // --- Mappe Texture PBR ---
    const std::optional<ImageBitmap>& GetBaseColorMap() const { return m_baseColor; }
    const std::optional<ImageBitmap>& GetNormalMap() const { return m_normal; }
    const std::optional<ImageBitmap>& GetAmbientOcclusionMap() const { return m_ao; }
    const std::optional<ImageBitmap>& GetRoughnessMap() const { return m_roughness; }
    const std::optional<ImageBitmap>& GetMetallicMap() const { return m_metallic; }
    const std::optional<ImageBitmap>& GetEmissiveMap() const { return m_emissive; }
    const std::optional<ImageBitmap>& GetORMMap() const { return m_orm; }

    void SetBaseColorMap(const ImageBitmap& img);
    void SetNormalMap(const ImageBitmap& img);
    void SetAmbientOcclusionMap(const ImageBitmap& img);
    void SetRoughnessMap(const ImageBitmap& img);
    void SetMetallicMap(const ImageBitmap& img);
    void SetEmissiveMap(const ImageBitmap& img);
    void SetORMMap(const ImageBitmap& img);

    void ClearBaseColorMap();
    void ClearNormalMap();
    void ClearAmbientOcclusionMap();
    void ClearRoughnessMap();
    void ClearMetallicMap();
    void ClearEmissiveMap();
    void ClearORMMap();

    // --- Presenza Canali ---
    bool HasBaseColor() const { return m_baseColor.has_value() && m_baseColor->IsValid(); }
    bool HasNormal() const { return m_normal.has_value() && m_normal->IsValid(); }
    bool HasAmbientOcclusion() const { return m_ao.has_value() && m_ao->IsValid(); }
    bool HasRoughness() const { return m_roughness.has_value() && m_roughness->IsValid(); }
    bool HasMetallic() const { return m_metallic.has_value() && m_metallic->IsValid(); }
    bool HasEmissive() const { return m_emissive.has_value() && m_emissive->IsValid(); }
    bool HasORM() const { return m_orm.has_value() && m_orm->IsValid(); }
    bool IsValid() const {
        return HasBaseColor() || HasNormal() || HasAmbientOcclusion() || 
               HasRoughness() || HasMetallic() || HasEmissive() || HasORM();
    }

    // --- Valori Scalari di Fallback (BM-TX2.5) ---
    glm::vec4 fallbackBaseColor{1.0f, 1.0f, 1.0f, 1.0f}; // RGBA sRGB
    glm::vec3 fallbackNormal{0.5f, 0.5f, 1.0f};          // Tangent (0, 0, 1) encoded
    float fallbackAO = 1.0f;                             // Piena luce diffusa
    float fallbackRoughness = 1.0f;                      // Completamente opaco
    float fallbackMetallic = 0.0f;                       // Dielettrico puro
    glm::vec3 fallbackEmissive{0.0f, 0.0f, 0.0f};        // Zero emissione

    // --- Packing ORM (BM-TX2.6) ---
    // R = AO, G = Roughness, B = Metallic, A = 255. Dati rigorosamente lineari.
    bool GenerateORM(uint32_t targetWidth = 0, uint32_t targetHeight = 0);

    // --- Revision Tracking & Deterministic Fingerprint ---
    uint64_t GetRevision() const { return m_revision; }
    void IncrementRevision() { m_revision++; m_fingerprintDirty = true; }
    uint64_t ComputeFingerprint() const;

    // --- Validazione Strutturale ---
    TextureValidationReport Validate(uint32_t maxDimension = 4096) const;

private:
    std::optional<ImageBitmap> m_baseColor;
    std::optional<ImageBitmap> m_normal;
    std::optional<ImageBitmap> m_ao;
    std::optional<ImageBitmap> m_roughness;
    std::optional<ImageBitmap> m_metallic;
    std::optional<ImageBitmap> m_emissive;
    std::optional<ImageBitmap> m_orm;

    uint64_t m_revision = 1;
    mutable uint64_t m_cachedFingerprint = 0;
    mutable bool m_fingerprintDirty = true;
};

} // namespace fw
