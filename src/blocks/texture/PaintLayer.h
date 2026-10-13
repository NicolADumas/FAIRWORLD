#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include "ImageBitmap.h"

namespace fw {

// =============================================================================
// BM-TX3.4 — BLEND MODES
// =============================================================================

enum class LayerBlendMode : uint8_t {
    Normal = 0,    // Alpha Over: C_out = C_src * a_src + C_dst * (1 - a_src)
    Multiply = 1,  // C_out = C_src * C_dst
    Additive = 2,  // C_out = min(1.0, C_src + C_dst)
    Screen = 3,    // C_out = 1.0 - (1.0 - C_src) * (1.0 - C_dst)
    Overlay = 4    // C_dst < 0.5 ? 2*C_src*C_dst : 1 - 2*(1-C_src)*(1-C_dst)
};

const char* LayerBlendModeToString(LayerBlendMode mode);
LayerBlendMode StringToLayerBlendMode(const std::string& str);

// =============================================================================
// BM-TX3.7 — DIRTY RECT TRACKING
// =============================================================================

struct PaintDirtyRect {
    uint32_t xMin = 0;
    uint32_t yMin = 0;
    uint32_t xMax = 0;
    uint32_t yMax = 0;
    bool isEmpty = true;

    void Clear() {
        xMin = yMin = xMax = yMax = 0;
        isEmpty = true;
    }

    void ExpandToInclude(uint32_t x, uint32_t y) {
        if (isEmpty) {
            xMin = xMax = x;
            yMin = yMax = y;
            isEmpty = false;
        } else {
            xMin = std::min(xMin, x);
            yMin = std::min(yMin, y);
            xMax = std::max(xMax, x);
            yMax = std::max(yMax, y);
        }
    }

    void UnionWith(const PaintDirtyRect& other) {
        if (other.isEmpty) return;
        if (isEmpty) {
            *this = other;
            return;
        }
        xMin = std::min(xMin, other.xMin);
        yMin = std::min(yMin, other.yMin);
        xMax = std::max(xMax, other.xMax);
        yMax = std::max(yMax, other.yMax);
    }

    void ClipTo(uint32_t width, uint32_t height) {
        if (isEmpty || width == 0 || height == 0) {
            Clear();
            return;
        }
        xMin = std::min(xMin, width - 1);
        yMin = std::min(yMin, height - 1);
        xMax = std::min(xMax, width - 1);
        yMax = std::min(yMax, height - 1);
        if (xMin > xMax || yMin > yMax) {
            Clear();
        }
    }

    uint32_t GetWidth() const { return isEmpty ? 0 : (xMax - xMin + 1); }
    uint32_t GetHeight() const { return isEmpty ? 0 : (yMax - yMin + 1); }
};

// =============================================================================
// BM-TX3.3 — PAINT MASK (CANALE R8 LINEARE [0.0, 1.0])
// =============================================================================

class PaintMask {
public:
    PaintMask();
    PaintMask(uint32_t width, uint32_t height, float initialValue = 1.0f);
    ~PaintMask() = default;

    PaintMask(const PaintMask& other) = default;
    PaintMask(PaintMask&& other) noexcept = default;
    PaintMask& operator=(const PaintMask& other) = default;
    PaintMask& operator=(PaintMask&& other) noexcept = default;

    bool IsValid() const { return m_bitmap.IsValid(); }
    uint32_t GetWidth() const { return m_bitmap.GetWidth(); }
    uint32_t GetHeight() const { return m_bitmap.GetHeight(); }

    float GetNormalizedValue(uint32_t x, uint32_t y) const;
    void SetNormalizedValue(uint32_t x, uint32_t y, float val);

    void Fill(float val);
    void Invert();

    const ImageBitmap& GetBitmap() const { return m_bitmap; }
    ImageBitmap& GetBitmapMutable() { return m_bitmap; }

    uint64_t ComputeFingerprint() const;

private:
    ImageBitmap m_bitmap;
};

// =============================================================================
// BM-TX3.2 — PAINT LAYER (LIVELLO PITTORICO NON DISTRUTTIVO)
// =============================================================================

struct PaintLayer {
    uint32_t layerId = 0;
    std::string name = "Layer";
    bool isVisible = true;
    bool isLocked = false;
    float opacity = 1.0f; // Intervallo rigoroso [0.0, 1.0]
    LayerBlendMode blendMode = LayerBlendMode::Normal;

    ImageBitmap pixelBuffer;             // Buffer colore RGBA8 del livello
    std::optional<PaintMask> mask;       // Maschera alfanumerica R8 opzionale
    uint64_t revision = 1;

    PaintLayer() = default;
    PaintLayer(uint32_t id, const std::string& layerName, uint32_t width, uint32_t height, 
               const glm::vec4& fillColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));

    bool HasMask() const { return mask.has_value() && mask->IsValid(); }
    void EnsureMask(float defaultVal = 1.0f);
    void RemoveMask();

    uint64_t ComputeFingerprint() const;

    // Serializzazione JSON headless
    nlohmann::json ToJson() const;
    static PaintLayer FromJson(const nlohmann::json& j);
};

// Funzioni di conversione e serializzazione JSON per nlohmann::json
void to_json(nlohmann::json& j, const PaintDirtyRect& r);
void from_json(const nlohmann::json& j, PaintDirtyRect& r);

void to_json(nlohmann::json& j, const PaintLayer& l);
void from_json(const nlohmann::json& j, PaintLayer& l);

} // namespace fw
