#include "pch.h"
#include "UVEditorDocument.h"
#include <cmath>
#include <algorithm>

namespace fw {

UVEditorDocument::UVEditorDocument(EditableMesh& mesh, UVLayout& layout)
    : m_mesh(mesh), m_layout(layout) {
    m_lastValidatedTopologyRev = mesh.GetTopologyRevision();
}

void UVEditorDocument::ClearSelection() {
    m_selectedIslands.clear();
    m_selectedFaces.clear();
    m_selectionRevision++;
}

void UVEditorDocument::SelectIsland(UVIslandID id, bool addToSelection) {
    if (!addToSelection) {
        ClearSelection();
    }

    if (id < m_layout.islands.size()) {
        m_selectedIslands.insert(id);
        const auto& isl = m_layout.islands[id];
        for (FaceID fId : isl.faces) {
            m_selectedFaces.insert(fId);
        }
        m_activeAtlasPage = isl.atlasPage;
        m_selectionRevision++;
    }
}

void UVEditorDocument::DeselectIsland(UVIslandID id) {
    auto it = m_selectedIslands.find(id);
    if (it != m_selectedIslands.end()) {
        m_selectedIslands.erase(it);
        if (id < m_layout.islands.size()) {
            for (FaceID fId : m_layout.islands[id].faces) {
                m_selectedFaces.erase(fId);
            }
        }
        m_selectionRevision++;
    }
}

void UVEditorDocument::SelectFace3D(FaceID faceId, bool addToSelection) {
    if (!addToSelection) {
        ClearSelection();
    }

    if (m_mesh.IsValid(faceId)) {
        m_selectedFaces.insert(faceId);

        // Risoluzione biunivoca dell'isola associata alla faccia 3D (BM-UV5.4)
        auto it = m_layout.faceToIslandMap.find(faceId);
        if (it != m_layout.faceToIslandMap.end()) {
            UVIslandID islId = it->second;
            m_selectedIslands.insert(islId);
            if (islId < m_layout.islands.size()) {
                m_activeAtlasPage = m_layout.islands[islId].atlasPage;
            }
        }
        m_selectionRevision++;
    }
}

void UVEditorDocument::SelectSurface(uint32_t surfaceId, bool addToSelection) {
    if (!addToSelection) {
        ClearSelection();
    }

    for (const auto& isl : m_layout.islands) {
        if (isl.semanticSurfaceId == surfaceId) {
            m_selectedIslands.insert(isl.id);
            for (FaceID fId : isl.faces) {
                m_selectedFaces.insert(fId);
            }
        }
    }
    m_selectionRevision++;
}

bool UVEditorDocument::IsIslandSelected(UVIslandID id) const {
    return m_selectedIslands.find(id) != m_selectedIslands.end();
}

bool UVEditorDocument::IsFaceSelected(FaceID faceId) const {
    return m_selectedFaces.find(faceId) != m_selectedFaces.end();
}

void UVEditorDocument::ValidateSelection() {
    if (m_lastValidatedTopologyRev == m_mesh.GetTopologyRevision()) {
        return;
    }

    // Invalida elementi con identita generazionale non piu valida (BM-UV5.4)
    std::unordered_set<FaceID> validFaces;
    for (FaceID fId : m_selectedFaces) {
        if (m_mesh.IsValid(fId)) {
            validFaces.insert(fId);
        }
    }
    m_selectedFaces = validFaces;

    // Ricostruisci selezione isole su basi valide
    std::unordered_set<UVIslandID> validIslands;
    for (FaceID fId : m_selectedFaces) {
        auto it = m_layout.faceToIslandMap.find(fId);
        if (it != m_layout.faceToIslandMap.end()) {
            validIslands.insert(it->second);
        }
    }
    m_selectedIslands = validIslands;

    m_lastValidatedTopologyRev = m_mesh.GetTopologyRevision();
    m_selectionRevision++;
}

void UVEditorDocument::SetIslandPinned(UVIslandID id, bool pinned) {
    if (pinned) {
        m_pinnedIslands.insert(id);
    } else {
        m_pinnedIslands.erase(id);
    }
}

bool UVEditorDocument::IsIslandPinned(UVIslandID id) const {
    return m_pinnedIslands.find(id) != m_pinnedIslands.end();
}

glm::vec2 UVEditorDocument::SnapUV(const glm::vec2& uv, float snapGrid) const {
    if (snapGrid <= 1e-4f) return uv;
    float snappedU = std::round(uv.x / snapGrid) * snapGrid;
    float snappedV = std::round(uv.y / snapGrid) * snapGrid;
    return glm::vec2(snappedU, snappedV);
}

void UVEditorDocument::PushUndoState() {
    UVEditSnapshot snap;
    snap.layout = m_layout;
    snap.selectedIslands = m_selectedIslands;
    snap.pinnedIslands = m_pinnedIslands;
    snap.activeAtlasPage = m_activeAtlasPage;

    m_undoStack.push_back(snap);
    if (m_undoStack.size() > MAX_UNDO_LEVELS) {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_redoStack.clear();
}

bool UVEditorDocument::Undo() {
    if (m_undoStack.empty()) return false;

    UVEditSnapshot cur;
    cur.layout = m_layout;
    cur.selectedIslands = m_selectedIslands;
    cur.pinnedIslands = m_pinnedIslands;
    cur.activeAtlasPage = m_activeAtlasPage;
    m_redoStack.push_back(cur);

    UVEditSnapshot prev = m_undoStack.back();
    m_undoStack.pop_back();

    m_layout = prev.layout;
    m_selectedIslands = prev.selectedIslands;
    m_pinnedIslands = prev.pinnedIslands;
    m_activeAtlasPage = prev.activeAtlasPage;

    // Risincronizza le coordinate per-corner su EditableMesh
    for (auto& isl : m_layout.islands) {
        ApplyAtlasTransformToIslandCorners(isl);
    }

    MarkModified();
    return true;
}

bool UVEditorDocument::Redo() {
    if (m_redoStack.empty()) return false;

    UVEditSnapshot cur;
    cur.layout = m_layout;
    cur.selectedIslands = m_selectedIslands;
    cur.pinnedIslands = m_pinnedIslands;
    cur.activeAtlasPage = m_activeAtlasPage;
    m_undoStack.push_back(cur);

    UVEditSnapshot nxt = m_redoStack.back();
    m_redoStack.pop_back();

    m_layout = nxt.layout;
    m_selectedIslands = nxt.selectedIslands;
    m_pinnedIslands = nxt.pinnedIslands;
    m_activeAtlasPage = nxt.activeAtlasPage;

    for (auto& isl : m_layout.islands) {
        ApplyAtlasTransformToIslandCorners(isl);
    }

    MarkModified();
    return true;
}

void UVEditorDocument::ApplyAtlasTransformToIslandCorners(UVIsland& island) {
    for (FaceID fId : island.faces) {
        const auto* f = m_mesh.GetFace(fId);
        if (!f || !IsValid(f->halfEdge)) continue;
        HalfEdgeID h = f->halfEdge;
        HalfEdgeID startH = h;
        do {
            auto* he = m_mesh.GetHalfEdgeMutable(h);
            if (!he) break;

            // he->corner.uv e già un valore normalizzato; riallinealo alla trasformazione atlas
            glm::vec2 localUV = UVAtlasPacker::AtlasToLocal(he->corner.uv, island);
            he->corner.uv = UVAtlasPacker::LocalToAtlas(localUV, island);

            h = he->next;
        } while (h != startH && IsValid(h));
    }
}

void UVEditorDocument::MarkModified() {
    m_mesh.IncrementAttributeRevision();
    m_layout.meshAttributeRevision = m_mesh.GetAttributeRevision();
    m_layout.atlasRevision++;
    m_requiresTextureMigration = true; // Segnala necessità di migrazione/riproiezione (BM-UV5.8)
}

bool UVEditorDocument::TranslateSelectedIslands(const glm::vec2& deltaAtlasUV) {
    if (m_selectedIslands.empty()) return false;

    PushUndoState();

    for (UVIslandID islId : m_selectedIslands) {
        if (IsIslandPinned(islId) || islId >= m_layout.islands.size()) continue;

        auto& isl = m_layout.islands[islId];
        isl.atlasTransform.z += deltaAtlasUV.x;
        isl.atlasTransform.w += deltaAtlasUV.y;

        // Mantieni i limiti entro [0, 1]
        isl.atlasTransform.z = std::clamp(isl.atlasTransform.z, 0.0f, 1.0f - isl.atlasTransform.x);
        isl.atlasTransform.w = std::clamp(isl.atlasTransform.w, 0.0f, 1.0f - isl.atlasTransform.y);

        ApplyAtlasTransformToIslandCorners(isl);
    }

    MarkModified();
    return true;
}

bool UVEditorDocument::RotateSelectedIslands(float angleDegrees, const glm::vec2& pivotUV) {
    if (m_selectedIslands.empty()) return false;

    PushUndoState();

    for (UVIslandID islId : m_selectedIslands) {
        if (IsIslandPinned(islId) || islId >= m_layout.islands.size()) continue;

        auto& isl = m_layout.islands[islId];
        isl.rotationDegrees = std::fmod(isl.rotationDegrees + angleDegrees + 360.0f, 360.0f);
        ApplyAtlasTransformToIslandCorners(isl);
    }

    MarkModified();
    return true;
}

bool UVEditorDocument::ScaleSelectedIslands(const glm::vec2& scaleFactor, const glm::vec2& pivotUV) {
    if (m_selectedIslands.empty()) return false;

    PushUndoState();

    for (UVIslandID islId : m_selectedIslands) {
        if (IsIslandPinned(islId) || islId >= m_layout.islands.size()) continue;

        auto& isl = m_layout.islands[islId];
        isl.atlasTransform.x = std::clamp(isl.atlasTransform.x * scaleFactor.x, 0.001f, 1.0f);
        isl.atlasTransform.y = std::clamp(isl.atlasTransform.y * scaleFactor.y, 0.001f, 1.0f);
        ApplyAtlasTransformToIslandCorners(isl);
    }

    MarkModified();
    return true;
}

bool UVEditorDocument::ToggleManualSeam(EdgeID edgeId) {
    auto* e = m_mesh.GetEdgeMutable(edgeId);
    if (!e) return false;

    PushUndoState();

    // Toggle: se gia seam manuale (1.0) rimuovila (0.0), altrimenti impostala (1.0)
    e->seamWeight = (e->seamWeight >= 0.99f) ? 0.0f : 1.0f;
    m_mesh.IncrementTopologyRevision();
    MarkModified();
    return true;
}

bool UVEditorDocument::PaintAtUV2D(const glm::vec2& atlasUV, 
                                   const PaintBrush& brush, 
                                   TexturePaintingService* paintingService, 
                                   uint32_t targetLayerId) {
    if (!paintingService) return false;

    // Converte coordinate atlas normalizzate in coordinate raster in pixel
    uint32_t targetW = m_layout.atlasTargetWidth > 0 ? m_layout.atlasTargetWidth : paintingService->GetCanvasWidth();
    uint32_t targetH = m_layout.atlasTargetHeight > 0 ? m_layout.atlasTargetHeight : paintingService->GetCanvasHeight();
    glm::vec2 pixelCoords = UVAtlasPacker::AtlasToPixel(atlasUV, targetW, targetH);

    // Risolve il layer ID effettivo (se 0 o non specificato, adotta il primo livello disponibile)
    uint32_t layerId = targetLayerId;
    if (layerId == 0 && paintingService->GetLayerCount() > 0) {
        layerId = paintingService->GetLayers()[0].layerId;
    }

    // Esegue il tratto di pittura 2D delegando a TexturePaintingService (BM-UV5.5)
    if (!paintingService->BeginStroke(layerId, false)) {
        return false;
    }
    paintingService->ApplyDab(pixelCoords, brush);
    paintingService->EndStroke();

    return true;
}

} // namespace fw
