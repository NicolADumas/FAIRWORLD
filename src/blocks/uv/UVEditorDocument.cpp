#include "pch.h"
#include "UVEditorDocument.h"
#include <cmath>
#include <algorithm>
#include <iostream>

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

static inline bool PointInTriangle2D(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
    float d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
    float d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
    float d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
    bool has_neg = (d1 < -1e-5f) || (d2 < -1e-5f) || (d3 < -1e-5f);
    bool has_pos = (d1 > 1e-5f) || (d2 > 1e-5f) || (d3 > 1e-5f);
    return !(has_neg && has_pos);
}

// =============================================================================
// BM-PAINTER-PERF-02: GENERAZIONE E LOOKUP MASCHERA RASTER CACHED
// =============================================================================

std::shared_ptr<const UVIslandRasterMask> GetOrCreateIslandRasterMask(
    const EditableMesh& mesh,
    const UVLayout& layout,
    UVIslandID islandId,
    uint32_t width,
    uint32_t height,
    bool flipV
) {
    if (islandId >= layout.islands.size() || width == 0 || height == 0) {
        return nullptr;
    }

    // Fail-closed se la topologia non e sincronizzata con la sorgente topologica autorevole
    if (layout.meshTopologyRevision != mesh.GetTopologyRevision()) {
        return nullptr;
    }

    const UVIsland& island = layout.islands[islandId];

    // Verifica validita maschera precedentemente memorizzata (BM-PAINTER-PERF-02)
    if (island.cachedRasterMask &&
        island.cachedRasterMask->islandId == islandId &&
        island.cachedRasterMask->width == width &&
        island.cachedRasterMask->height == height &&
        island.cachedRasterMask->meshTopologyRevision == mesh.GetTopologyRevision() &&
        island.cachedRasterMask->meshAttributeRevision == mesh.GetAttributeRevision() &&
        island.cachedRasterMask->atlasRevision == layout.atlasRevision &&
        island.cachedRasterMask->flipV == flipV) 
    {
        return island.cachedRasterMask;
    }

    auto mask = std::make_shared<UVIslandRasterMask>();
    mask->islandId = islandId;
    mask->width = width;
    mask->height = height;
    mask->meshTopologyRevision = layout.meshTopologyRevision;
    mask->meshAttributeRevision = mesh.GetAttributeRevision();
    mask->atlasRevision = layout.atlasRevision;
    mask->flipV = flipV;
    mask->pixels.assign(static_cast<size_t>(width) * height, 0);

    for (FaceID fId : island.faces) {
        if (!mesh.IsValid(fId)) continue;
        const auto* f = mesh.GetFace(fId);
        if (!f || !IsValid(f->halfEdge)) continue;

        HalfEdgeID h0 = f->halfEdge;
        const auto* he0 = mesh.GetHalfEdge(h0);
        if (!he0) continue;
        glm::vec2 uv0 = he0->corner.uv;

        HalfEdgeID hPrev = he0->next;
        while (IsValid(hPrev)) {
            const auto* hePrev = mesh.GetHalfEdge(hPrev);
            if (!hePrev) break;
            HalfEdgeID hCurr = hePrev->next;
            if (!IsValid(hCurr) || hCurr == h0) break;
            const auto* heCurr = mesh.GetHalfEdge(hCurr);
            if (!heCurr) break;

            glm::vec2 uv1 = hePrev->corner.uv;
            glm::vec2 uv2 = heCurr->corner.uv;

            float minU = std::min({uv0.x, uv1.x, uv2.x});
            float maxU = std::max({uv0.x, uv1.x, uv2.x});
            float minV = std::min({uv0.y, uv1.y, uv2.y});
            float maxV = std::max({uv0.y, uv1.y, uv2.y});

            int32_t minPx = std::clamp(static_cast<int32_t>(std::floor(minU * static_cast<float>(width) - 1.0f)), 0, static_cast<int32_t>(width - 1));
            int32_t maxPx = std::clamp(static_cast<int32_t>(std::ceil(maxU * static_cast<float>(width) + 1.0f)), 0, static_cast<int32_t>(width - 1));

            int32_t minPy = 0;
            int32_t maxPy = static_cast<int32_t>(height - 1);
            if (!flipV) {
                minPy = std::clamp(static_cast<int32_t>(std::floor(minV * static_cast<float>(height) - 1.0f)), 0, static_cast<int32_t>(height - 1));
                maxPy = std::clamp(static_cast<int32_t>(std::ceil(maxV * static_cast<float>(height) + 1.0f)), 0, static_cast<int32_t>(height - 1));
            } else {
                float pyA = (1.0f - maxV) * static_cast<float>(height) - 1.0f;
                float pyB = (1.0f - minV) * static_cast<float>(height) + 1.0f;
                minPy = std::clamp(static_cast<int32_t>(std::floor(std::min(pyA, pyB))), 0, static_cast<int32_t>(height - 1));
                maxPy = std::clamp(static_cast<int32_t>(std::ceil(std::max(pyA, pyB))), 0, static_cast<int32_t>(height - 1));
            }

            for (int32_t py = minPy; py <= maxPy; ++py) {
                float v = (static_cast<float>(py) + 0.5f) / static_cast<float>(height);
                if (flipV) v = 1.0f - v;
                size_t rowOffset = static_cast<size_t>(py) * width;

                for (int32_t px = minPx; px <= maxPx; ++px) {
                    if (mask->pixels[rowOffset + static_cast<size_t>(px)] != 0) continue;

                    float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(width);
                    if (PointInTriangle2D(glm::vec2(u, v), uv0, uv1, uv2)) {
                        mask->pixels[rowOffset + static_cast<size_t>(px)] = 1;
                    }
                }
            }

            hPrev = hCurr;
        }
    }

    island.cachedRasterMask = mask;
    return mask;
}

bool UVEditorDocument::IsPointInIsland(const glm::vec2& atlasUV, UVIslandID islandId) const {
    if (islandId >= m_layout.islands.size()) return false;
    const auto& isl = m_layout.islands[islandId];
    if (isl.atlasPage != m_activeAtlasPage) return false;

    // Fail-closed se la revisione topologica della mesh non corrisponde alla revisione registrata nel layout UV
    if (m_layout.meshTopologyRevision != m_mesh.GetTopologyRevision()) {
        return false;
    }

    // Invariante Bounding Box: [scaleU, scaleV, offsetU, offsetV]
    // x = scaleU, y = scaleV, z = offsetU, w = offsetV
    float uMin = isl.atlasTransform.z - 1e-4f;
    float vMin = isl.atlasTransform.w - 1e-4f;
    float uMax = isl.atlasTransform.z + isl.atlasTransform.x + 1e-4f;
    float vMax = isl.atlasTransform.w + isl.atlasTransform.y + 1e-4f;
    if (atlasUV.x < uMin || atlasUV.x > uMax || atlasUV.y < vMin || atlasUV.y > vMax) {
        return false;
    }

    auto testFace = [this, &atlasUV](FaceID fId) -> bool {
        if (!m_mesh.IsValid(fId)) return false;
        const auto* f = m_mesh.GetFace(fId);
        if (!f || !IsValid(f->halfEdge)) return false;

        HalfEdgeID h0 = f->halfEdge;
        const auto* he0 = m_mesh.GetHalfEdge(h0);
        if (!he0) return false;
        glm::vec2 uv0 = he0->corner.uv;

        HalfEdgeID hPrev = he0->next;
        while (IsValid(hPrev)) {
            const auto* hePrev = m_mesh.GetHalfEdge(hPrev);
            if (!hePrev) break;
            HalfEdgeID hCurr = hePrev->next;
            if (!IsValid(hCurr) || hCurr == h0) break;
            const auto* heCurr = m_mesh.GetHalfEdge(hCurr);
            if (!heCurr) break;

            glm::vec2 uv1 = hePrev->corner.uv;
            glm::vec2 uv2 = heCurr->corner.uv;

            if (PointInTriangle2D(atlasUV, uv0, uv1, uv2)) {
                return true;
            }

            hPrev = hCurr;
        }
        return false;
    };

    // Testa esclusivamente le facce registrate nell'isola verificando che siano valide
    for (FaceID fId : isl.faces) {
        if (m_mesh.IsValid(fId)) {
            if (testFace(fId)) return true;
        }
    }

    return false;
}

UVIslandID UVEditorDocument::FindIslandAtUV(const glm::vec2& atlasUV) const {
    for (UVIslandID selId : m_selectedIslands) {
        if (IsPointInIsland(atlasUV, selId)) return selId;
    }
    for (size_t i = 0; i < m_layout.islands.size(); ++i) {
        if (IsPointInIsland(atlasUV, static_cast<UVIslandID>(i))) {
            return static_cast<UVIslandID>(i);
        }
    }
    return INVALID_UVISLAND_ID;
}

bool UVEditorDocument::PaintAtUV2D(const glm::vec2& atlasUV, 
                                   const PaintBrush& brush, 
                                   TexturePaintingService* paintingService, 
                                   uint32_t targetLayerId) {
    if (!paintingService) return false;
    m_lastPaintBlockedReason.clear();

    // Fail-closed immediato: verifica contro la sorgente topologica autorevole prima di qualsiasi mutazione
    if (m_layout.meshTopologyRevision != m_mesh.GetTopologyRevision()) {
        m_lastPaintBlockedReason = "Surface Paint: Revisione topologica incompatibile. Richiesto aggiornamento del layout UV.";
        return false;
    }

    UVIslandID targetIsland = FindIslandAtUV(atlasUV);
    if (targetIsland == INVALID_UVISLAND_ID) {
        m_lastPaintBlockedReason = "Surface Paint: Nessuna superficie/faccia sotto il cursore. Seleziona una faccia o usa Atlas Paint.";
        return false;
    }
    if (!m_selectedIslands.empty() && m_selectedIslands.find(targetIsland) == m_selectedIslands.end()) {
        m_lastPaintBlockedReason = "Surface Paint: Il click e' fuori dalla superficie selezionata. Pittura bloccata.";
        return false;
    }

    // BM-P02: Utilizza autoritativamente le dimensioni reali del canvas del paintingService
    uint32_t targetW = paintingService->GetCanvasWidth();
    uint32_t targetH = paintingService->GetCanvasHeight();
    if (targetW == 0) targetW = (m_layout.atlasTargetWidth > 0 ? m_layout.atlasTargetWidth : 512);
    if (targetH == 0) targetH = (m_layout.atlasTargetHeight > 0 ? m_layout.atlasTargetHeight : 512);

    glm::vec2 pixelCoords = UVAtlasPacker::AtlasToPixel(atlasUV, targetW, targetH);
    pixelCoords.x = std::clamp(pixelCoords.x, 0.0f, static_cast<float>(targetW - 1));
    pixelCoords.y = std::clamp(pixelCoords.y, 0.0f, static_cast<float>(targetH - 1));

    // Risolve il layer ID effettivo esclusivamente post-validazione (nessuna mutazione di layer se respinto)
    uint32_t layerId = targetLayerId;
    if (layerId == 0 && paintingService->GetLayerCount() > 0) {
        layerId = paintingService->GetLayers()[0].layerId;
    } else if (paintingService->GetLayerCount() == 0) {
        layerId = paintingService->AddLayer("Paint Layer 1");
    }

    // BM-PAINTER-PERF-02: Fast O(1) mask lookup replaces per-pixel geometric face scans
    auto mask = GetOrCreateIslandRasterMask(m_mesh, m_layout, targetIsland, targetW, targetH, false);
    if (!mask) {
        return false;
    }
    paintingService->SetStrokeClipMask(mask);

    if (!paintingService->BeginStroke(layerId, false)) {
        paintingService->SetStrokeClipMask(nullptr);
        return false;
    }

    paintingService->ApplyDab(pixelCoords, brush);
    paintingService->EndStroke();
    paintingService->SetStrokeClipMask(nullptr);
    return true;
}

bool UVEditorDocument::BeginStroke2D(const glm::vec2& atlasUV, 
                                     const PaintBrush& brush, 
                                     TexturePaintingService* paintingService, 
                                     uint32_t targetLayerId,
                                     CanvasPaintMode paintMode) {
    if (!paintingService) return false;
    m_lastPaintBlockedReason.clear();

    UVIslandID targetIsland = INVALID_UVISLAND_ID;
    if (paintMode == CanvasPaintMode::SurfacePaint) {
        if (m_layout.meshTopologyRevision != m_mesh.GetTopologyRevision()) {
            m_lastPaintBlockedReason = "Surface Paint: Revisione topologica incompatibile. Richiesto aggiornamento del layout UV.";
            return false;
        }
        ValidateSelection();

        // GATE D: SURFACE PAINT AUTO-BINDING & LOCK
        if (m_selectedIslands.empty() && m_selectedFaces.empty()) {
            if (m_autoSelectFaceOnPaint) {
                UVIslandID autoHit = FindIslandAtUV(atlasUV);
                if (autoHit != INVALID_UVISLAND_ID) {
                    SelectIsland(autoHit, false);
                    targetIsland = autoHit;
                } else {
                    m_lastPaintBlockedReason = "Surface Paint: Nessuna superficie/faccia sotto il cursore. Seleziona una faccia o usa Atlas Paint.";
                    return false;
                }
            } else {
                m_lastPaintBlockedReason = "Surface Paint: Nessuna superficie/faccia selezionata. Seleziona una faccia o usa Atlas Paint.";
                return false;
            }
        } else {
            // 2. Verificare che lo stroke inizi nella regione UV autorizzata (tra le isole selezionate)
            for (UVIslandID selId : m_selectedIslands) {
                if (IsPointInIsland(atlasUV, selId)) {
                    targetIsland = selId;
                    break;
                }
            }

            if (targetIsland == INVALID_UVISLAND_ID) {
                if (m_autoSelectFaceOnPaint) {
                    UVIslandID hitOther = FindIslandAtUV(atlasUV);
                    if (hitOther != INVALID_UVISLAND_ID) {
                        SelectIsland(hitOther, false);
                        targetIsland = hitOther;
                    } else {
                        m_lastPaintBlockedReason = "Surface Paint: Il click e' fuori dalla superficie selezionata. Pittura bloccata.";
                        return false;
                    }
                } else {
                    m_lastPaintBlockedReason = "Surface Paint: Il click e' fuori dalla superficie selezionata. Pittura bloccata.";
                    return false;
                }
            }
        }

        // 4. Memorizzare il target dello stroke (targetIsland e' fissato, NESSUN cambio automatico durante drag o click)
    } else {
        // Modalita Atlas Paint esplicita: pittura libera sull'intero canvas senza alcun ritaglio
        targetIsland = INVALID_UVISLAND_ID;
    }

    // BM-P02: Utilizza autoritativamente le dimensioni reali del canvas del paintingService (512x512)
    // invece del default 1024 dell'UVLayout che creava un disallineamento 2x!
    uint32_t targetW = paintingService->GetCanvasWidth();
    uint32_t targetH = paintingService->GetCanvasHeight();
    if (targetW == 0) targetW = (m_layout.atlasTargetWidth > 0 ? m_layout.atlasTargetWidth : 512);
    if (targetH == 0) targetH = (m_layout.atlasTargetHeight > 0 ? m_layout.atlasTargetHeight : 512);

    glm::vec2 pixelCoords = UVAtlasPacker::AtlasToPixel(atlasUV, targetW, targetH);
    pixelCoords.x = std::clamp(pixelCoords.x, 0.0f, static_cast<float>(targetW - 1));
    pixelCoords.y = std::clamp(pixelCoords.y, 0.0f, static_cast<float>(targetH - 1));

    uint32_t layerId = targetLayerId;
    if (layerId == 0 && paintingService->GetLayerCount() > 0) {
        layerId = paintingService->GetLayers()[0].layerId;
    } else if (paintingService->GetLayerCount() == 0) {
        layerId = paintingService->AddLayer("Paint Layer 1");
    }

    if (!paintingService->BeginStroke(layerId, false)) {
        return false;
    }

    m_isStroking2D = true;
    m_strokeIslandId = targetIsland;
    m_prevStrokeUV = atlasUV;

    if (targetIsland != INVALID_UVISLAND_ID) {
        // BM-PAINTER-PERF-02: Fast O(1) mask lookup replaces per-pixel geometric face scans
        auto mask = GetOrCreateIslandRasterMask(m_mesh, m_layout, targetIsland, targetW, targetH, false);
        paintingService->SetStrokeClipMask(mask);
        paintingService->ApplyDab(pixelCoords, brush);
    } else {
        paintingService->SetStrokeClipMask(nullptr);
        paintingService->ApplyDab(pixelCoords, brush);
    }

    std::cout << "[PAINTER DIAG] StrokeBegin: UV=(" << atlasUV.x << "," << atlasUV.y << ") TargetIsland=" 
              << (targetIsland != INVALID_UVISLAND_ID ? std::to_string(targetIsland) : "UNCLIPPED") 
              << " LayerID=" << layerId 
              << " Mode=" << (paintMode == CanvasPaintMode::SurfacePaint ? "SurfacePaint" : "AtlasPaint") << "\n";
    std::cout << "[PAINTER DIAG] PaintLayer Initial Point: (" << pixelCoords.x << "," << pixelCoords.y 
              << ") on LayerID=" << layerId << "\n";
    return true;
}

bool UVEditorDocument::UpdateStroke2D(const glm::vec2& atlasUV, 
                                      const PaintBrush& brush, 
                                      TexturePaintingService* paintingService) {
    if (!m_isStroking2D || !paintingService) {
        return false;
    }

    uint32_t targetW = paintingService->GetCanvasWidth();
    uint32_t targetH = paintingService->GetCanvasHeight();
    if (targetW == 0) targetW = (m_layout.atlasTargetWidth > 0 ? m_layout.atlasTargetWidth : 512);
    if (targetH == 0) targetH = (m_layout.atlasTargetHeight > 0 ? m_layout.atlasTargetHeight : 512);

    glm::vec2 curPixel = UVAtlasPacker::AtlasToPixel(atlasUV, targetW, targetH);
    glm::vec2 prevPixel = UVAtlasPacker::AtlasToPixel(m_prevStrokeUV, targetW, targetH);
    curPixel.x = std::clamp(curPixel.x, 0.0f, static_cast<float>(targetW - 1));
    curPixel.y = std::clamp(curPixel.y, 0.0f, static_cast<float>(targetH - 1));
    prevPixel.x = std::clamp(prevPixel.x, 0.0f, static_cast<float>(targetW - 1));
    prevPixel.y = std::clamp(prevPixel.y, 0.0f, static_cast<float>(targetH - 1));

    // BM-PAINTER-PERF-02: Fast O(1) mask lookup già attivo sul TexturePaintingService
    paintingService->UpdateStroke(prevPixel, curPixel, brush);

    m_prevStrokeUV = atlasUV;
    return true;
}

PaintDirtyRect UVEditorDocument::EndStroke2D(TexturePaintingService* paintingService) {
    PaintDirtyRect dirty;
    if (m_isStroking2D && paintingService) {
        dirty = paintingService->EndStroke();
        std::cout << "[PAINTER DIAG] StrokeEnd: DirtyRect=[" << dirty.xMin << "," << dirty.yMin 
                  << " -> " << dirty.xMax << "," << dirty.yMax << "] isEmpty=" << (dirty.isEmpty ? "true" : "false") << "\n";
    }
    m_isStroking2D = false;
    m_strokeIslandId = INVALID_UVISLAND_ID;
    return dirty;
}

bool UVEditorDocument::CancelStroke2D(TexturePaintingService* paintingService) {
    bool ok = false;
    if (m_isStroking2D && paintingService) {
        ok = paintingService->CancelStroke();
    }
    m_isStroking2D = false;
    m_strokeIslandId = INVALID_UVISLAND_ID;
    return ok;
}

// =============================================================================
// BM-PAINT-STAMP-03 — 2D STAMP TOOL INTEGRATION
// =============================================================================

bool UVEditorDocument::ApplyStamp2D(const glm::vec2& atlasUV, 
                                   const StampDefinition& stamp, 
                                   TexturePaintingService* paintingService, 
                                   uint32_t targetLayerId,
                                   CanvasPaintMode paintMode) {
    if (!paintingService) {
        std::cout << "[STAMP-DIAG-02] TARGET: Valid=false Path=2D Mode=" 
                  << (paintMode == CanvasPaintMode::SurfacePaint ? "SurfacePaint" : "AtlasPaint")
                  << " Rejection='paintingService is null'\n";
        return false;
    }
    m_lastPaintBlockedReason.clear();

    // 1. Validazione PaintLayer attivo (Requisito 7)
    if (targetLayerId == 0) {
        m_lastPaintBlockedReason = "Nessun PaintLayer valido selezionato per lo stamp.";
        std::cout << "[STAMP-DIAG-02] TARGET: Valid=false Path=2D Mode=" 
                  << (paintMode == CanvasPaintMode::SurfacePaint ? "SurfacePaint" : "AtlasPaint")
                  << " Rejection='" << m_lastPaintBlockedReason << "'\n";
        return false;
    }
    const PaintLayer* targetLayer = paintingService->GetLayer(targetLayerId);
    if (!targetLayer || targetLayer->isLocked) {
        m_lastPaintBlockedReason = "Il PaintLayer selezionato e' inesistente o bloccato.";
        std::cout << "[STAMP-DIAG-02] TARGET: Valid=false Path=2D Mode=" 
                  << (paintMode == CanvasPaintMode::SurfacePaint ? "SurfacePaint" : "AtlasPaint")
                  << " LayerID=" << targetLayerId
                  << " Rejection='" << m_lastPaintBlockedReason << "'\n";
        return false;
    }

    // 2. Regole di selezione e isolamento UV coerenti con il Brush Tool (Requisito 6)
    UVIslandID targetIsland = INVALID_UVISLAND_ID;
    if (paintMode == CanvasPaintMode::SurfacePaint) {
        if (m_layout.meshTopologyRevision != m_mesh.GetTopologyRevision()) {
            m_lastPaintBlockedReason = "Surface Paint: Revisione topologica incompatibile. Richiesto aggiornamento del layout UV.";
            std::cout << "[STAMP-DIAG-02] TARGET: Valid=false Path=2D Mode=SurfacePaint Rejection='" << m_lastPaintBlockedReason << "'\n";
            return false;
        }
        ValidateSelection();

        if (m_selectedIslands.empty() && m_selectedFaces.empty()) {
            if (m_autoSelectFaceOnPaint) {
                UVIslandID autoHit = FindIslandAtUV(atlasUV);
                if (autoHit != INVALID_UVISLAND_ID) {
                    SelectIsland(autoHit, false);
                    targetIsland = autoHit;
                } else {
                    m_lastPaintBlockedReason = "Surface Paint: Nessuna superficie/faccia sotto il cursore. Seleziona una faccia o usa Atlas Paint.";
                    std::cout << "[STAMP-DIAG-02] TARGET: Valid=false Path=2D Mode=SurfacePaint SelectedIslands=0 Rejection='" << m_lastPaintBlockedReason << "'\n";
                    return false;
                }
            } else {
                m_lastPaintBlockedReason = "Surface Paint: Nessuna superficie/faccia selezionata. Seleziona una faccia o usa Atlas Paint.";
                std::cout << "[STAMP-DIAG-02] TARGET: Valid=false Path=2D Mode=SurfacePaint SelectedIslands=0 Rejection='" << m_lastPaintBlockedReason << "'\n";
                return false;
            }
        } else {
            for (UVIslandID selId : m_selectedIslands) {
                if (IsPointInIsland(atlasUV, selId)) {
                    targetIsland = selId;
                    break;
                }
            }

            if (targetIsland == INVALID_UVISLAND_ID) {
                if (m_autoSelectFaceOnPaint) {
                    UVIslandID hitOther = FindIslandAtUV(atlasUV);
                    if (hitOther != INVALID_UVISLAND_ID) {
                        SelectIsland(hitOther, false);
                        targetIsland = hitOther;
                    } else {
                        m_lastPaintBlockedReason = "Surface Paint: Il click e' fuori dalla superficie selezionata. Stamp bloccato.";
                        std::cout << "[STAMP-DIAG-02] TARGET: Valid=false Path=2D Mode=SurfacePaint UV=(" << atlasUV.x << "," << atlasUV.y << ") Rejection='" << m_lastPaintBlockedReason << "'\n";
                        return false;
                    }
                } else {
                    m_lastPaintBlockedReason = "Surface Paint: Il click e' fuori dalla superficie selezionata. Stamp bloccato.";
                    std::cout << "[STAMP-DIAG-02] TARGET: Valid=false Path=2D Mode=SurfacePaint UV=(" << atlasUV.x << "," << atlasUV.y << ") Rejection='" << m_lastPaintBlockedReason << "'\n";
                    return false;
                }
            }
        }
    } else {
        targetIsland = INVALID_UVISLAND_ID;
    }

    // 3. Conversione coordinate UV normalizzate in pixel del canvas (Requisiti 1 & 2)
    uint32_t targetW = paintingService->GetCanvasWidth();
    uint32_t targetH = paintingService->GetCanvasHeight();
    if (targetW == 0) targetW = (m_layout.atlasTargetWidth > 0 ? m_layout.atlasTargetWidth : 512);
    if (targetH == 0) targetH = (m_layout.atlasTargetHeight > 0 ? m_layout.atlasTargetHeight : 512);

    glm::vec2 pixelCoords = UVAtlasPacker::AtlasToPixel(atlasUV, targetW, targetH);
    pixelCoords.x = std::clamp(pixelCoords.x, 0.0f, static_cast<float>(targetW - 1));
    pixelCoords.y = std::clamp(pixelCoords.y, 0.0f, static_cast<float>(targetH - 1));

    std::cout << "[STAMP-DIAG-02] TARGET: Valid=true Path=2D Mode=" 
              << (paintMode == CanvasPaintMode::SurfacePaint ? "SurfacePaint" : "AtlasPaint")
              << " TargetIsland=" << (targetIsland != INVALID_UVISLAND_ID ? std::to_string(targetIsland) : "NONE/ATLAS")
              << " UV=(" << atlasUV.x << "," << atlasUV.y << ")"
              << " Pixel=(" << pixelCoords.x << "," << pixelCoords.y << ")"
              << " CanvasSize=(" << targetW << "x" << targetH << ")\n";

    // 4. BM-PAINTER-PERF-02: Fast O(1) mask lookup replaces per-pixel geometric face scans
    if (targetIsland != INVALID_UVISLAND_ID) {
        auto mask = GetOrCreateIslandRasterMask(m_mesh, m_layout, targetIsland, targetW, targetH, false);
        paintingService->SetStrokeClipMask(mask);
    } else {
        paintingService->SetStrokeClipMask(nullptr);
    }

    // 5. Applicazione singola e atomica dello stampo (Requisiti 3 & 4)
    bool applied = paintingService->ApplyStampClipped(targetLayerId, pixelCoords, stamp, nullptr);
    paintingService->SetStrokeClipMask(nullptr);
    return applied;
}

} // namespace fw
