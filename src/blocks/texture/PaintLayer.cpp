#include "PaintLayer.h"
#include <sstream>
#include <iomanip>

namespace fw {

// =============================================================================
// HELPER BASE64 HEADLESS
// =============================================================================

namespace {
    static const char s_base64Chars[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string Base64Encode(const uint8_t* data, size_t len) {
        std::string ret;
        int i = 0;
        int j = 0;
        uint8_t charArray3[3];
        uint8_t charArray4[4];

        while (len--) {
            charArray3[i++] = *(data++);
            if (i == 3) {
                charArray4[0] = (charArray3[0] & 0xfc) >> 2;
                charArray4[1] = ((charArray3[0] & 0x03) << 4) + ((charArray3[1] & 0xf0) >> 4);
                charArray4[2] = ((charArray3[1] & 0x0f) << 2) + ((charArray3[2] & 0xc0) >> 6);
                charArray4[3] = charArray3[2] & 0x3f;

                for (i = 0; i < 4; i++) ret += s_base64Chars[charArray4[i]];
                i = 0;
            }
        }

        if (i) {
            for (j = i; j < 3; j++) charArray3[j] = '\0';
            charArray4[0] = (charArray3[0] & 0xfc) >> 2;
            charArray4[1] = ((charArray3[0] & 0x03) << 4) + ((charArray3[1] & 0xf0) >> 4);
            charArray4[2] = ((charArray3[1] & 0x0f) << 2) + ((charArray3[2] & 0xc0) >> 6);
            for (j = 0; j < (i + 1); j++) ret += s_base64Chars[charArray4[j]];
            while ((i++ < 3)) ret += '=';
        }
        return ret;
    }

    std::vector<uint8_t> Base64Decode(const std::string& encoded) {
        size_t inLen = encoded.size();
        int i = 0;
        int j = 0;
        int in = 0;
        uint8_t charArray4[4], charArray3[3];
        std::vector<uint8_t> ret;

        auto isBase64 = [](uint8_t c) -> bool {
            return (isalnum(c) || (c == '+') || (c == '/'));
        };

        while (inLen-- && (encoded[in] != '=') && isBase64(encoded[in])) {
            charArray4[i++] = encoded[in]; in++;
            if (i == 4) {
                for (i = 0; i < 4; i++) {
                    const char* p = strchr(s_base64Chars, charArray4[i]);
                    charArray4[i] = p ? static_cast<uint8_t>(p - s_base64Chars) : 0;
                }
                charArray3[0] = (charArray4[0] << 2) + ((charArray4[1] & 0x30) >> 4);
                charArray3[1] = ((charArray4[1] & 0xf) << 4) + ((charArray4[2] & 0x3c) >> 2);
                charArray3[2] = ((charArray4[2] & 0x3) << 6) + charArray4[3];
                for (i = 0; i < 3; i++) ret.push_back(charArray3[i]);
                i = 0;
            }
        }

        if (i) {
            for (j = 0; j < i; j++) {
                const char* p = strchr(s_base64Chars, charArray4[j]);
                charArray4[j] = p ? static_cast<uint8_t>(p - s_base64Chars) : 0;
            }
            charArray3[0] = (charArray4[0] << 2) + ((charArray4[1] & 0x30) >> 4);
            charArray3[1] = ((charArray4[1] & 0xf) << 4) + ((charArray4[2] & 0x3c) >> 2);
            for (j = 0; j < i - 1; j++) ret.push_back(charArray3[j]);
        }
        return ret;
    }
}

// =============================================================================
// BM-TX3.4 — BLEND MODES STRING CONVERSION
// =============================================================================

const char* LayerBlendModeToString(LayerBlendMode mode) {
    switch (mode) {
        case LayerBlendMode::Normal:   return "Normal";
        case LayerBlendMode::Multiply: return "Multiply";
        case LayerBlendMode::Additive: return "Additive";
        case LayerBlendMode::Screen:   return "Screen";
        case LayerBlendMode::Overlay:  return "Overlay";
        default: return "Normal";
    }
}

LayerBlendMode StringToLayerBlendMode(const std::string& str) {
    if (str == "Multiply") return LayerBlendMode::Multiply;
    if (str == "Additive" || str == "Add") return LayerBlendMode::Additive;
    if (str == "Screen")   return LayerBlendMode::Screen;
    if (str == "Overlay")  return LayerBlendMode::Overlay;
    return LayerBlendMode::Normal;
}

// =============================================================================
// BM-TX3.3 — PAINT MASK
// =============================================================================

PaintMask::PaintMask() : m_bitmap() {}

PaintMask::PaintMask(uint32_t width, uint32_t height, float initialValue) 
    : m_bitmap(width, height, TexturePixelFormat::R8, TextureColorSpace::Linear, 
               glm::vec4(std::clamp(initialValue, 0.0f, 1.0f))) {}

float PaintMask::GetNormalizedValue(uint32_t x, uint32_t y) const {
    if (!m_bitmap.IsValid() || x >= m_bitmap.GetWidth() || y >= m_bitmap.GetHeight()) {
        return 1.0f; // Default trasparente/invariato
    }
    return m_bitmap.GetPixel(x, y).r;
}

void PaintMask::SetNormalizedValue(uint32_t x, uint32_t y, float val) {
    if (!m_bitmap.IsValid()) return;
    float clamped = std::clamp(val, 0.0f, 1.0f);
    m_bitmap.SetPixel(x, y, glm::vec4(clamped, clamped, clamped, 1.0f));
}

void PaintMask::Fill(float val) {
    if (!m_bitmap.IsValid()) return;
    float clamped = std::clamp(val, 0.0f, 1.0f);
    m_bitmap.Fill(glm::vec4(clamped, clamped, clamped, 1.0f));
}

void PaintMask::Invert() {
    if (!m_bitmap.IsValid()) return;
    uint32_t w = m_bitmap.GetWidth();
    uint32_t h = m_bitmap.GetHeight();
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            float v = GetNormalizedValue(x, y);
            SetNormalizedValue(x, y, 1.0f - v);
        }
    }
}

uint64_t PaintMask::ComputeFingerprint() const {
    return m_bitmap.ComputeContentHash();
}

// =============================================================================
// BM-TX3.2 — PAINT LAYER
// =============================================================================

PaintLayer::PaintLayer(uint32_t id, const std::string& layerName, uint32_t width, uint32_t height, 
                       const glm::vec4& fillColor)
    : layerId(id)
    , name(layerName)
    , isVisible(true)
    , isLocked(false)
    , opacity(1.0f)
    , blendMode(LayerBlendMode::Normal)
    , pixelBuffer(width, height, TexturePixelFormat::RGBA8, TextureColorSpace::Linear, fillColor)
    , mask(std::nullopt)
    , revision(1) {}

void PaintLayer::EnsureMask(float defaultVal) {
    if (!mask.has_value() || !mask->IsValid()) {
        mask = PaintMask(pixelBuffer.GetWidth(), pixelBuffer.GetHeight(), defaultVal);
        revision++;
    }
}

void PaintLayer::RemoveMask() {
    if (mask.has_value()) {
        mask.reset();
        revision++;
    }
}

uint64_t PaintLayer::ComputeFingerprint() const {
    const uint64_t kFnvOffsetBasis = 14695981039346656037ull;
    const uint64_t kFnvPrime = 1099511628211ull;

    uint64_t hash = kFnvOffsetBasis;
    auto hashBytes = [&](const void* ptr, size_t len) {
        const uint8_t* p = static_cast<const uint8_t*>(ptr);
        for (size_t i = 0; i < len; ++i) {
            hash ^= p[i];
            hash *= kFnvPrime;
        }
    };

    hashBytes(&layerId, sizeof(layerId));
    hashBytes(name.data(), name.size());
    uint8_t vis = isVisible ? 1 : 0;
    uint8_t lck = isLocked ? 1 : 0;
    hashBytes(&vis, sizeof(vis));
    hashBytes(&lck, sizeof(lck));
    hashBytes(&opacity, sizeof(opacity));
    uint8_t bm = static_cast<uint8_t>(blendMode);
    hashBytes(&bm, sizeof(bm));
    hashBytes(&revision, sizeof(revision));

    uint64_t pixelHash = pixelBuffer.ComputeContentHash();
    hashBytes(&pixelHash, sizeof(pixelHash));

    if (mask.has_value() && mask->IsValid()) {
        uint64_t maskHash = mask->ComputeFingerprint();
        hashBytes(&maskHash, sizeof(maskHash));
    }

    return hash;
}

nlohmann::json PaintLayer::ToJson() const {
    nlohmann::json j;
    j["layerId"] = layerId;
    j["name"] = name;
    j["isVisible"] = isVisible;
    j["isLocked"] = isLocked;
    j["opacity"] = opacity;
    j["blendMode"] = LayerBlendModeToString(blendMode);
    j["revision"] = revision;
    j["width"] = pixelBuffer.GetWidth();
    j["height"] = pixelBuffer.GetHeight();

    // Serializzazione PNG in base64 del buffer colore
    std::vector<uint8_t> pngBytes = pixelBuffer.EncodeToPngMemory();
    j["pixelPngBase64"] = Base64Encode(pngBytes.data(), pngBytes.size());

    // Maschera opzionale
    j["hasMask"] = (mask.has_value() && mask->IsValid());
    if (mask.has_value() && mask->IsValid()) {
        std::vector<uint8_t> maskPng = mask->GetBitmap().EncodeToPngMemory();
        j["maskPngBase64"] = Base64Encode(maskPng.data(), maskPng.size());
    }

    return j;
}

PaintLayer PaintLayer::FromJson(const nlohmann::json& j) {
    PaintLayer l;
    if (j.contains("layerId")) l.layerId = j["layerId"].get<uint32_t>();
    if (j.contains("name")) l.name = j["name"].get<std::string>();
    if (j.contains("isVisible")) l.isVisible = j["isVisible"].get<bool>();
    if (j.contains("isLocked")) l.isLocked = j["isLocked"].get<bool>();
    if (j.contains("opacity")) l.opacity = j["opacity"].get<float>();
    if (j.contains("blendMode")) l.blendMode = StringToLayerBlendMode(j["blendMode"].get<std::string>());
    if (j.contains("revision")) l.revision = j["revision"].get<uint64_t>();

    uint32_t w = j.value("width", 512u);
    uint32_t h = j.value("height", 512u);

    if (j.contains("pixelPngBase64")) {
        std::string b64 = j["pixelPngBase64"].get<std::string>();
        std::vector<uint8_t> pngData = Base64Decode(b64);
        if (!pngData.empty()) {
            l.pixelBuffer.DecodeFromMemory(pngData.data(), pngData.size(), TextureColorSpace::Linear);
        }
    }

    if (!l.pixelBuffer.IsValid() && w > 0 && h > 0) {
        l.pixelBuffer = ImageBitmap(w, h, TexturePixelFormat::RGBA8, TextureColorSpace::Linear, glm::vec4(0.0f));
    }

    if (j.value("hasMask", false) && j.contains("maskPngBase64")) {
        std::string b64 = j["maskPngBase64"].get<std::string>();
        std::vector<uint8_t> maskData = Base64Decode(b64);
        if (!maskData.empty()) {
            ImageBitmap maskBmp;
            if (maskBmp.DecodeFromMemory(maskData.data(), maskData.size(), TextureColorSpace::Linear)) {
                l.mask = PaintMask(maskBmp.GetWidth(), maskBmp.GetHeight(), 1.0f);
                l.mask->GetBitmapMutable() = std::move(maskBmp);
            }
        }
    }

    return l;
}

// =============================================================================
// JSON SERIALIZATION GLUE
// =============================================================================

void to_json(nlohmann::json& j, const PaintDirtyRect& r) {
    j = nlohmann::json{
        {"xMin", r.xMin},
        {"yMin", r.yMin},
        {"xMax", r.xMax},
        {"yMax", r.yMax},
        {"isEmpty", r.isEmpty}
    };
}

void from_json(const nlohmann::json& j, PaintDirtyRect& r) {
    r.xMin = j.value("xMin", 0u);
    r.yMin = j.value("yMin", 0u);
    r.xMax = j.value("xMax", 0u);
    r.yMax = j.value("yMax", 0u);
    r.isEmpty = j.value("isEmpty", true);
}

void to_json(nlohmann::json& j, const PaintLayer& l) {
    j = l.ToJson();
}

void from_json(const nlohmann::json& j, PaintLayer& l) {
    l = PaintLayer::FromJson(j);
}

} // namespace fw
