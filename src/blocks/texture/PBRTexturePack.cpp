#include "pch.h"
#include "PBRTexturePack.h"
#include <algorithm>

namespace fw {

// =============================================================================
// BM-TX2.5 & BM-TX2.6: PBR TEXTURE PACK IMPLEMENTATION
// =============================================================================

PBRTexturePack::PBRTexturePack()
    : m_revision(1), m_fingerprintDirty(true) {
}

void PBRTexturePack::SetBaseColorMap(const ImageBitmap& img) {
    m_baseColor = img;
    // BaseColor deve essere nello spazio sRGB
    if (m_baseColor->GetColorSpace() != TextureColorSpace::sRGB) {
        m_baseColor->ConvertColorSpace(TextureColorSpace::sRGB);
    }
    IncrementRevision();
}

void PBRTexturePack::SetNormalMap(const ImageBitmap& img) {
    m_normal = img;
    // Normal map deve essere lineare
    if (m_normal->GetColorSpace() != TextureColorSpace::Linear) {
        m_normal->ConvertColorSpace(TextureColorSpace::Linear);
    }
    IncrementRevision();
}

void PBRTexturePack::SetAmbientOcclusionMap(const ImageBitmap& img) {
    m_ao = img;
    if (m_ao->GetColorSpace() != TextureColorSpace::Linear) {
        m_ao->ConvertColorSpace(TextureColorSpace::Linear);
    }
    IncrementRevision();
}

void PBRTexturePack::SetRoughnessMap(const ImageBitmap& img) {
    m_roughness = img;
    if (m_roughness->GetColorSpace() != TextureColorSpace::Linear) {
        m_roughness->ConvertColorSpace(TextureColorSpace::Linear);
    }
    IncrementRevision();
}

void PBRTexturePack::SetMetallicMap(const ImageBitmap& img) {
    m_metallic = img;
    if (m_metallic->GetColorSpace() != TextureColorSpace::Linear) {
        m_metallic->ConvertColorSpace(TextureColorSpace::Linear);
    }
    IncrementRevision();
}

void PBRTexturePack::SetEmissiveMap(const ImageBitmap& img) {
    m_emissive = img;
    IncrementRevision();
}

void PBRTexturePack::SetORMMap(const ImageBitmap& img) {
    m_orm = img;
    if (m_orm->GetColorSpace() != TextureColorSpace::Linear) {
        m_orm->ConvertColorSpace(TextureColorSpace::Linear);
    }
    IncrementRevision();
}

void PBRTexturePack::ClearBaseColorMap() { m_baseColor.reset(); IncrementRevision(); }
void PBRTexturePack::ClearNormalMap() { m_normal.reset(); IncrementRevision(); }
void PBRTexturePack::ClearAmbientOcclusionMap() { m_ao.reset(); IncrementRevision(); }
void PBRTexturePack::ClearRoughnessMap() { m_roughness.reset(); IncrementRevision(); }
void PBRTexturePack::ClearMetallicMap() { m_metallic.reset(); IncrementRevision(); }
void PBRTexturePack::ClearEmissiveMap() { m_emissive.reset(); IncrementRevision(); }
void PBRTexturePack::ClearORMMap() { m_orm.reset(); IncrementRevision(); }

// =============================================================================
// BM-TX2.6: PACKING ORM COMPOSITO (R=AO, G=Roughness, B=Metallic, A=255)
// =============================================================================

bool PBRTexturePack::GenerateORM(uint32_t targetWidth, uint32_t targetHeight) {
    // 1. Determina la risoluzione di destinazione
    uint32_t w = targetWidth;
    uint32_t h = targetHeight;

    if (w == 0 || h == 0) {
        if (HasRoughness()) {
            w = std::max(w, m_roughness->GetWidth());
            h = std::max(h, m_roughness->GetHeight());
        }
        if (HasMetallic()) {
            w = std::max(w, m_metallic->GetWidth());
            h = std::max(h, m_metallic->GetHeight());
        }
        if (HasAmbientOcclusion()) {
            w = std::max(w, m_ao->GetWidth());
            h = std::max(h, m_ao->GetHeight());
        }
        if (HasBaseColor()) {
            w = std::max(w, m_baseColor->GetWidth());
            h = std::max(h, m_baseColor->GetHeight());
        }
        if (w == 0 || h == 0) {
            w = 512;
            h = 512;
        }
    }

    if (!ImageBitmap::ValidateDimensions(w, h)) {
        return false;
    }

    // 2. Prepara le tre mappe sorgente, ricampionandole se le dimensioni differiscono
    ImageBitmap aoResampled;
    if (HasAmbientOcclusion()) {
        aoResampled = (m_ao->GetWidth() == w && m_ao->GetHeight() == h) 
                      ? *m_ao 
                      : m_ao->Resample(w, h, TextureFilterMode::Bilinear);
    }

    ImageBitmap roughResampled;
    if (HasRoughness()) {
        roughResampled = (m_roughness->GetWidth() == w && m_roughness->GetHeight() == h) 
                         ? *m_roughness 
                         : m_roughness->Resample(w, h, TextureFilterMode::Bilinear);
    }

    ImageBitmap metalResampled;
    if (HasMetallic()) {
        metalResampled = (m_metallic->GetWidth() == w && m_metallic->GetHeight() == h) 
                         ? *m_metallic 
                         : m_metallic->Resample(w, h, TextureFilterMode::Bilinear);
    }

    // 3. Alloca l'immagine ORM composita (RGBA8, Rigorosamente Linear)
    ImageBitmap ormBitmap(w, h, TexturePixelFormat::RGBA8, TextureColorSpace::Linear);

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            float aoVal = aoResampled.IsValid() 
                          ? aoResampled.GetPixel(x, y).r 
                          : fallbackAO;

            float roughVal = roughResampled.IsValid() 
                             ? roughResampled.GetPixel(x, y).r 
                             : fallbackRoughness;

            float metalVal = metalResampled.IsValid() 
                             ? metalResampled.GetPixel(x, y).r 
                             : fallbackMetallic;

            // R = AO, G = Roughness, B = Metallic, A = 1.0 (255)
            glm::vec4 ormPixel(
                std::clamp(aoVal, 0.0f, 1.0f),
                std::clamp(roughVal, 0.0f, 1.0f),
                std::clamp(metalVal, 0.0f, 1.0f),
                1.0f
            );
            ormBitmap.SetPixel(x, y, ormPixel);
        }
    }

    m_orm = std::move(ormBitmap);
    IncrementRevision();
    return true;
}

uint64_t PBRTexturePack::ComputeFingerprint() const {
    if (!m_fingerprintDirty) {
        return m_cachedFingerprint;
    }

    uint64_t hash = 14695981039346656037ULL;
    auto addU64 = [&hash](uint64_t v) {
        hash ^= v;
        hash *= 1099511628211ULL;
    };

    auto addMapHash = [&](const std::optional<ImageBitmap>& map) {
        if (map.has_value() && map->IsValid()) {
            addU64(map->ComputeContentHash());
        } else {
            addU64(0ULL);
        }
    };

    addMapHash(m_baseColor);
    addMapHash(m_normal);
    addMapHash(m_ao);
    addMapHash(m_roughness);
    addMapHash(m_metallic);
    addMapHash(m_emissive);
    addMapHash(m_orm);

    // Fallback scalars hash
    uint32_t fallbacks[6] = {
        *reinterpret_cast<const uint32_t*>(&fallbackAO),
        *reinterpret_cast<const uint32_t*>(&fallbackRoughness),
        *reinterpret_cast<const uint32_t*>(&fallbackMetallic),
        *reinterpret_cast<const uint32_t*>(&fallbackBaseColor.r),
        *reinterpret_cast<const uint32_t*>(&fallbackNormal.x),
        *reinterpret_cast<const uint32_t*>(&fallbackEmissive.x)
    };
    for (uint32_t fb : fallbacks) {
        addU64(static_cast<uint64_t>(fb));
    }

    m_cachedFingerprint = hash;
    m_fingerprintDirty = false;
    return m_cachedFingerprint;
}

TextureValidationReport PBRTexturePack::Validate(uint32_t maxDimension) const {
    TextureValidationReport report;
    report.Clear();

    auto checkMap = [&](const std::optional<ImageBitmap>& map, const char* name) {
        if (map.has_value()) {
            if (!map->IsValid()) {
                report.AddError(std::string("Mappa ") + name + " presente ma non valida (pixel vuoti o buffer corrotto).");
            } else if (!ImageBitmap::ValidateDimensions(map->GetWidth(), map->GetHeight(), maxDimension)) {
                report.AddError(std::string("Mappa ") + name + " eccede le dimensioni massime ammesse (" + 
                                std::to_string(maxDimension) + ").");
            }
        }
    };

    checkMap(m_baseColor, "BaseColor");
    checkMap(m_normal, "Normal");
    checkMap(m_ao, "AmbientOcclusion");
    checkMap(m_roughness, "Roughness");
    checkMap(m_metallic, "Metallic");
    checkMap(m_emissive, "Emissive");
    checkMap(m_orm, "ORM");

    return report;
}

} // namespace fw
