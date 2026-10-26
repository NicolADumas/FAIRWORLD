#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <glm/glm.hpp>
#include "PaintLayer.h"

namespace fw {

struct UVIslandRasterMask;

using PixelClipCallback = std::function<bool(int32_t x, int32_t y)>;

// =============================================================================
// BM-TX3.5 — BRUSH DEFINITION & FALLOFF
// =============================================================================

enum class BrushFalloff : uint8_t {
    SmoothHermite = 0,
    Linear = 1,
    Sphere = 2,
    Sharp = 3,
    Constant = 4
};

struct PaintBrush {
    float radiusPixels = 16.0f;
    float radiusMetric = 0.0f; // Raggio metrico in unita 3D (per BrushSpaceMode::SurfaceSpace; se <= 0 usa fallback derivato)
    float strength = 1.0f;     // [0.0, 1.0]
    float opacity = 1.0f;      // [0.0, 1.0]
    float hardness = 0.5f;     // [0.0, 1.0]
    float spacing = 0.1f;      // Frazione del raggio [0.01, 1.0]
    BrushFalloff falloff = BrushFalloff::SmoothHermite;
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f}; // RGBA lineare
    bool eraseMode = false;    // Se true, riduce alpha/cancella

    float EvaluateFalloff(float distNormalized) const;
};

// =============================================================================
// BM-PAINT-STAMP-02 — GEOMETRIC STAMP DEFINITION
// =============================================================================

enum class StampShape : uint8_t {
    Circle = 0,
    Square = 1
};

struct StampDefinition {
    StampShape shape = StampShape::Circle;
    float sizePixels = 16.0f;                // Raggio (Circle) o semilato (Square) in pixel
    float rotationRadians = 0.0f;            // Rotazione in radianti
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f}; // RGBA lineare
    float opacity = 1.0f;                    // [0.0, 1.0]
};

// =============================================================================
// BM-TX3.6 — STROKE TRANSACTION & PAINT HISTORY
// =============================================================================

struct PaintStrokeTransaction {
    uint32_t layerId = 0;
    bool isMaskStroke = false;
    PaintDirtyRect dirtyRect;
    ImageBitmap preStrokeBackup;
    bool isActive = false;
};

struct PaintUndoRecord {
    uint32_t layerId = 0;
    bool isMask = false;
    PaintDirtyRect dirtyRect;
    ImageBitmap beforeSnapshot;
    ImageBitmap afterSnapshot;
};

// =============================================================================
// BM-TX3.5 — TEXTURE PAINTING SERVICE (HEADLESS CPU ENGINE)
// =============================================================================

class TexturePaintingService {
public:
    TexturePaintingService();
    TexturePaintingService(uint32_t width, uint32_t height);
    ~TexturePaintingService() = default;

    // --- Gestione Dimensioni Canvas ---
    void InitializeCanvas(uint32_t width, uint32_t height, const glm::vec4& baseColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    void SetCanvasDimensions(uint32_t width, uint32_t height) {
        m_canvasWidth = std::clamp(width, 16u, 4096u);
        m_canvasHeight = std::clamp(height, 16u, 4096u);
    }
    uint32_t GetCanvasWidth() const { return m_canvasWidth; }
    uint32_t GetCanvasHeight() const { return m_canvasHeight; }

    // --- Gestione Layer (BM-TX3.2) ---
    uint32_t AddLayer(const std::string& name, const glm::vec4& fillColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    bool RemoveLayer(uint32_t layerId);
    bool MoveLayerUp(uint32_t layerId);
    bool MoveLayerDown(uint32_t layerId);
    bool SetLayerOrder(uint32_t layerId, size_t newIndex);
    uint32_t DuplicateLayer(uint32_t layerId);
    bool MergeDown(uint32_t layerId);

    PaintLayer* GetLayer(uint32_t layerId);
    const PaintLayer* GetLayer(uint32_t layerId) const;
    PaintLayer* GetLayerMutable(size_t index) {
        return (index < m_layers.size()) ? &m_layers[index] : nullptr;
    }
    const PaintLayer* GetLayerMutable(size_t index) const {
        return (index < m_layers.size()) ? &m_layers[index] : nullptr;
    }
    std::vector<PaintLayer>& GetLayers() { return m_layers; }
    const std::vector<PaintLayer>& GetLayers() const { return m_layers; }
    size_t GetLayerCount() const { return m_layers.size(); }
    void ClearAllLayers();

    // BM-PAINT-UI-03C: Integrita e sincronizzazione ID dei PaintLayer
    uint32_t GetNextLayerId() const { return m_nextLayerId; }
    void SetNextLayerId(uint32_t id) { m_nextLayerId = id; }
    bool SynchronizeNextLayerId() {
        if (m_layers.empty()) {
            m_nextLayerId = 1;
            return true;
        }

        uint32_t maxId = 0;
        bool hasDuplicates = false;
        for (size_t i = 0; i < m_layers.size(); ++i) {
            uint32_t id = m_layers[i].layerId;
            if (id > maxId) {
                maxId = id;
            }
            for (size_t j = i + 1; j < m_layers.size(); ++j) {
                if (m_layers[j].layerId == id) {
                    hasDuplicates = true;
                    std::cerr << "[TexturePaintingService] ATTENZIONE: Rilevato layerId duplicato (" 
                              << id << ", nome: '" << m_layers[i].name << "'). Nessuna rinumerazione eseguita.\n";
                }
            }
        }

        if (maxId < (std::numeric_limits<uint32_t>::max)()) {
            m_nextLayerId = maxId + 1;
        } else {
            std::cerr << "[TexturePaintingService] ATTENZIONE: layerId massimo ha raggiunto UINT32_MAX. Overflow evitato.\n";
            m_nextLayerId = (std::numeric_limits<uint32_t>::max)();
        }

        return !hasDuplicates;
    }

    // --- Gestione Maschere (BM-TX3.3) ---
    bool AddMaskToLayer(uint32_t layerId, float defaultFill = 1.0f);
    bool RemoveMaskFromLayer(uint32_t layerId);
    bool InvertLayerMask(uint32_t layerId);

    // --- Pennellate Transazionali (BM-TX3.5 & BM-TX3.6) ---
    bool BeginStroke(uint32_t layerId, bool targetMask = false);
    bool ApplyDab(glm::vec2 pixelCoords, const PaintBrush& brush);
    bool ApplyDabClipped(glm::vec2 pixelCoords, const PaintBrush& brush, const PixelClipCallback& clipCb);
    bool AddDab(glm::vec2 pixelCoords, const PaintBrush& brush) { return ApplyDab(pixelCoords, brush); }
    bool UpdateStroke(glm::vec2 prevCoords, glm::vec2 curCoords, const PaintBrush& brush);
    bool UpdateStrokeClipped(glm::vec2 prevCoords, glm::vec2 curCoords, const PaintBrush& brush, const PixelClipCallback& clipCb);
    PaintDirtyRect EndStroke();
    bool CancelStroke();
    bool IsStroking() const { return m_activeTransaction.isActive; }
    const PaintDirtyRect& GetActiveStrokeDirtyRect() const { return m_activeTransaction.dirtyRect; }

    // --- BM-PAINTER-PERF-02: Maschera raster cached per isola UV (Fast O(1) Dab Clipping) ---
    void SetStrokeClipMask(std::shared_ptr<const UVIslandRasterMask> mask) {
        m_activeClipMask = std::move(mask);
    }
    std::shared_ptr<const UVIslandRasterMask> GetStrokeClipMask() const {
        return m_activeClipMask;
    }

    // --- Geometric Stamp Tool (BM-PAINT-STAMP-02) ---
    bool ApplyStampClipped(uint32_t layerId, glm::vec2 centerCoords, const StampDefinition& stamp, const PixelClipCallback& clipCb = nullptr);
    bool ApplyStamp(uint32_t layerId, glm::vec2 centerCoords, const StampDefinition& stamp) {
        return ApplyStampClipped(layerId, centerCoords, stamp, nullptr);
    }

    // --- Blending & Compositing Incrementale (BM-TX3.4 & BM-TX3.8) ---
    static glm::vec4 BlendPixelsLinear(const glm::vec4& srcColor, float srcAlpha, 
                                       const glm::vec4& dstColor, float dstAlpha, 
                                       PaintLayerBlendMode mode);

    bool CompositeLayers(ImageBitmap& outComposite, const PaintDirtyRect* subRect = nullptr) const;

    // --- Zero-Work & Revision Tracking ---
    uint64_t GetCompositionRevision() const { return m_compositionRevision; }
    uint64_t GetRevision() const { return m_compositionRevision; }
    void IncrementRevision() { m_compositionRevision++; }
    uint64_t ComputeCombinedFingerprint() const;

    // --- Paint History (Undo/Redo Separato da MeshHistory) ---
    bool CanUndo() const { return !m_undoStack.empty(); }
    bool CanRedo() const { return !m_redoStack.empty(); }
    bool Undo();
    bool Redo();
    void ClearHistory();
    size_t GetUndoCount() const { return m_undoStack.size(); }
    size_t GetRedoCount() const { return m_redoStack.size(); }

    // --- Serializzazione & Deserializzazione (BM-TX3.9) ---
    nlohmann::json ToJson() const;
    bool FromJson(const nlohmann::json& j);

private:
    uint32_t m_canvasWidth = 512;
    uint32_t m_canvasHeight = 512;
    uint32_t m_nextLayerId = 1;

    std::vector<PaintLayer> m_layers;
    PaintStrokeTransaction m_activeTransaction;
    uint64_t m_compositionRevision = 1;

    // Undo/Redo stack con cap massimo a 32 livelli
    static constexpr size_t kMaxHistoryRecords = 32;
    std::deque<PaintUndoRecord> m_undoStack;
    std::deque<PaintUndoRecord> m_redoStack;

    void ApplyDabInternal(PaintLayer& layer, glm::vec2 center, const PaintBrush& brush, bool isMask, const PixelClipCallback& clipCb = nullptr);
    size_t FindLayerIndex(uint32_t layerId) const;

    // Maschera raster attiva per la transazione di pittura corrente (BM-PAINTER-PERF-02)
    std::shared_ptr<const UVIslandRasterMask> m_activeClipMask = nullptr;
};

} // namespace fw
