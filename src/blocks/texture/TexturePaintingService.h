#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <deque>
#include <glm/glm.hpp>
#include "PaintLayer.h"

namespace fw {

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

    // --- Gestione Maschere (BM-TX3.3) ---
    bool AddMaskToLayer(uint32_t layerId, float defaultFill = 1.0f);
    bool RemoveMaskFromLayer(uint32_t layerId);
    bool InvertLayerMask(uint32_t layerId);

    // --- Pennellate Transazionali (BM-TX3.5 & BM-TX3.6) ---
    bool BeginStroke(uint32_t layerId, bool targetMask = false);
    bool ApplyDab(glm::vec2 pixelCoords, const PaintBrush& brush);
    bool AddDab(glm::vec2 pixelCoords, const PaintBrush& brush) { return ApplyDab(pixelCoords, brush); }
    bool UpdateStroke(glm::vec2 prevCoords, glm::vec2 curCoords, const PaintBrush& brush);
    PaintDirtyRect EndStroke();
    bool CancelStroke();
    bool IsStroking() const { return m_activeTransaction.isActive; }
    const PaintDirtyRect& GetActiveStrokeDirtyRect() const { return m_activeTransaction.dirtyRect; }

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

    void ApplyDabInternal(PaintLayer& layer, glm::vec2 center, const PaintBrush& brush, bool isMask);
    size_t FindLayerIndex(uint32_t layerId) const;
};

} // namespace fw
