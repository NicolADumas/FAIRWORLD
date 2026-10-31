#pragma once

#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <memory>
#include <glm/glm.hpp>
#include "blocks/sculpt/EditableMesh.h"
#include "blocks/uv/UVLayout.h"
#include "blocks/uv/UVAtlasPacker.h"
#include "blocks/texture/TexturePaintingService.h"

namespace fw {

// =============================================================================
// MODALITA STRUMENTO UV EDITOR (BM-UV5.2)
// =============================================================================

enum class UVToolMode : uint8_t {
    Select = 0,
    Translate,
    Rotate,
    Scale,
    Pin,
    SeamCut,
    Paint2D
};

inline const char* GetUVToolModeName(UVToolMode m) {
    switch (m) {
        case UVToolMode::Select:    return "Select";
        case UVToolMode::Translate: return "Translate";
        case UVToolMode::Rotate:    return "Rotate";
        case UVToolMode::Scale:     return "Scale";
        case UVToolMode::Pin:       return "Pin";
        case UVToolMode::SeamCut:   return "SeamCut";
        case UVToolMode::Paint2D:   return "Paint2D";
        default:                    return "Unknown";
    }
}

// =============================================================================
// MODALITA PITTURA CANVAS 2D (BM-UV-PAINTER MG02)
// =============================================================================

enum class CanvasPaintMode : uint8_t {
    SurfacePaint = 0, // Pennellata vincolata rigorosamente alla superficie/isola sotto cursore
    AtlasPaint = 1    // Pennellata libera sull'intero atlante UV (modalita esplicita)
};

// =============================================================================
// COMANDO UNDO/REDO UV EDITOR (BM-UV5.2)
// =============================================================================


struct UVEditSnapshot {
    UVLayout layout;
    std::unordered_set<UVIslandID> selectedIslands;
    std::unordered_set<UVIslandID> pinnedIslands;
    uint32_t activeAtlasPage = 0;
};

// =============================================================================
// UV EDITOR DOCUMENT MODEL (BM-UV5.2)
// Modello headless autoritativo per manipolazione e ispezione delle UV in 2D
// =============================================================================

class UVEditorDocument {
public:
    UVEditorDocument(EditableMesh& mesh, UVLayout& layout);
    ~UVEditorDocument() = default;

    // --- Riferimenti Autoritativi ---
    EditableMesh& GetMesh() { return m_mesh; }
    const EditableMesh& GetMesh() const { return m_mesh; }
    UVLayout& GetLayout() { return m_layout; }
    const UVLayout& GetLayout() const { return m_layout; }

    // --- Gestione Strumenti & Modalita ---
    void SetToolMode(UVToolMode mode) { m_toolMode = mode; }
    UVToolMode GetToolMode() const { return m_toolMode; }
    void SetActiveAtlasPage(uint32_t page) { m_activeAtlasPage = page; }
    uint32_t GetActiveAtlasPage() const { return m_activeAtlasPage; }

    // --- Selezione Sincronizzata Bidirezionale 2D <-> 3D (BM-UV5.4) ---
    void SelectIsland(UVIslandID id, bool addToSelection = false);
    void DeselectIsland(UVIslandID id);
    void SelectFace3D(FaceID faceId, bool addToSelection = false);
    void SelectSurface(uint32_t surfaceId, bool addToSelection = false);
    void ClearSelection();

    bool IsIslandSelected(UVIslandID id) const;
    bool IsFaceSelected(FaceID faceId) const;
    const std::unordered_set<UVIslandID>& GetSelectedIslands() const { return m_selectedIslands; }
    const std::unordered_set<FaceID>& GetSelectedFaces() const { return m_selectedFaces; }
    uint64_t GetSelectionRevision() const { return m_selectionRevision; }

    // Validazione generazionale contro modifiche topologiche della mesh
    void ValidateSelection();

    // --- Manipolazione UV Island (BM-UV5.3) ---
    bool TranslateSelectedIslands(const glm::vec2& deltaAtlasUV);
    bool RotateSelectedIslands(float angleDegrees, const glm::vec2& pivotUV = glm::vec2(0.5f));
    bool ScaleSelectedIslands(const glm::vec2& scaleFactor, const glm::vec2& pivotUV = glm::vec2(0.5f));
    
    void SetIslandPinned(UVIslandID id, bool pinned);
    bool IsIslandPinned(UVIslandID id) const;

    // Modifica cuciture manuali (taglio / saldatura edge)
    bool ToggleManualSeam(EdgeID edgeId);

    // Snapping UV su griglia o texel
    glm::vec2 SnapUV(const glm::vec2& uv, float snapGrid = 0.05f) const;

    // --- Undo / Redo (BM-UV5.2) ---
    void PushUndoState();
    bool Undo();
    bool Redo();
    bool CanUndo() const { return !m_undoStack.empty(); }
    bool CanRedo() const { return !m_redoStack.empty(); }

    // --- Safety & Notifica Migrazione Pittura (BM-UV5.8) ---
    bool RequiresTextureMigration() const { return m_requiresTextureMigration; }
    void SetRequiresTextureMigration(bool req) { m_requiresTextureMigration = req; }
    void AcknowledgeTextureMigration() { m_requiresTextureMigration = false; }

    // --- 2D Painting Integration (BM-UV5.5 & BM-UV-PAINTER MG-01) ---
    UVIslandID FindIslandAtUV(const glm::vec2& atlasUV) const;
    bool IsPointInIsland(const glm::vec2& atlasUV, UVIslandID islandId) const;

    bool PaintAtUV2D(const glm::vec2& atlasUV, 
                     const PaintBrush& brush, 
                     TexturePaintingService* paintingService, 
                     uint32_t targetLayerId = 0);

    bool BeginStroke2D(const glm::vec2& atlasUV, 
                       const PaintBrush& brush, 
                       TexturePaintingService* paintingService, 
                       uint32_t targetLayerId = 0,
                       CanvasPaintMode paintMode = CanvasPaintMode::SurfacePaint);

    bool UpdateStroke2D(const glm::vec2& atlasUV, 
                        const PaintBrush& brush, 
                        TexturePaintingService* paintingService);

    PaintDirtyRect EndStroke2D(TexturePaintingService* paintingService);
    bool CancelStroke2D(TexturePaintingService* paintingService);
    bool IsStroking2D() const { return m_isStroking2D; }
    UVIslandID GetActiveStrokeIslandId() const { return m_strokeIslandId; }
    const std::string& GetLastPaintBlockedReason() const { return m_lastPaintBlockedReason; }

    // --- Face Selection Lock & Auto-Binding Configuration ---
    void SetAutoSelectFaceOnPaint(bool enable) { m_autoSelectFaceOnPaint = enable; }
    bool GetAutoSelectFaceOnPaint() const { return m_autoSelectFaceOnPaint; }

    // --- 2D Stamp Tool Integration (BM-PAINT-STAMP-03) ---
    bool ApplyStamp2D(const glm::vec2& atlasUV, 
                      const StampDefinition& stamp, 
                      TexturePaintingService* paintingService, 
                      uint32_t targetLayerId,
                      CanvasPaintMode paintMode = CanvasPaintMode::SurfacePaint);

private:
    void MarkModified();
    void ApplyAtlasTransformToIslandCorners(UVIsland& island);

private:
    EditableMesh& m_mesh;
    UVLayout& m_layout;

    UVToolMode m_toolMode = UVToolMode::Select;
    uint32_t m_activeAtlasPage = 0;

    std::unordered_set<UVIslandID> m_selectedIslands;
    std::unordered_set<FaceID> m_selectedFaces;
    std::unordered_set<UVIslandID> m_pinnedIslands;

    uint64_t m_selectionRevision = 1;
    uint64_t m_lastValidatedTopologyRev = 0;
    bool m_requiresTextureMigration = false;

    bool m_isStroking2D = false;
    glm::vec2 m_prevStrokeUV{0.0f};
    UVIslandID m_strokeIslandId = INVALID_UVISLAND_ID;
    std::string m_lastPaintBlockedReason;
    bool m_autoSelectFaceOnPaint = false;

    std::vector<UVEditSnapshot> m_undoStack;
    std::vector<UVEditSnapshot> m_redoStack;
    static constexpr size_t MAX_UNDO_LEVELS = 32;
};

} // namespace fw
