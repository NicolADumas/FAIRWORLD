#include "pch.h"
#include "UVEditorPanel.h"
#include <algorithm>

namespace fw {

glm::vec2 UVEditorPanel::ScreenToAtlasUV(const glm::vec2& screenPos, 
                                        const glm::vec2& canvasOrigin, 
                                        const glm::vec2& canvasSize) const {
    if (canvasSize.x <= 1e-4f || canvasSize.y <= 1e-4f) return glm::vec2(0.0f);

    glm::vec2 relPos = (screenPos - canvasOrigin - m_viewState.panOffset) / (canvasSize * m_viewState.zoom);
    relPos.x = std::clamp(relPos.x, 0.0f, 1.0f);
    relPos.y = std::clamp(relPos.y, 0.0f, 1.0f);
    return relPos;
}

glm::vec2 UVEditorPanel::AtlasUVToScreen(const glm::vec2& atlasUV, 
                                        const glm::vec2& canvasOrigin, 
                                        const glm::vec2& canvasSize) const {
    return canvasOrigin + m_viewState.panOffset + (atlasUV * canvasSize * m_viewState.zoom);
}

std::vector<UVEditorPanel::IslandDrawOutline> UVEditorPanel::BuildDrawOutlines(const UVEditorDocument& doc) const {
    std::vector<IslandDrawOutline> outlines;
    const auto& layout = doc.GetLayout();
    const auto& mesh = doc.GetMesh();

    for (const auto& isl : layout.islands) {
        if (isl.atlasPage != m_viewState.activeAtlasPage) continue;

        IslandDrawOutline outline;
        outline.islandId = isl.id;
        outline.atlasPage = isl.atlasPage;
        outline.isSelected = doc.IsIslandSelected(isl.id);
        outline.isPinned = doc.IsIslandPinned(isl.id);

        if (outline.isSelected) {
            outline.color = glm::vec4(1.0f, 0.84f, 0.0f, 1.0f); // Oro brillante
        } else if (outline.isPinned) {
            outline.color = glm::vec4(0.3f, 0.8f, 1.0f, 1.0f); // Ciano pin
        } else {
            outline.color = glm::vec4(0.35f, 0.75f, 1.0f, 0.9f); // Azzurro UV ben visibile
        }

        // 1. Costruisci poligoni dai corner UV delle facce appartenenti all'isola
        for (FaceID fId : isl.faces) {
            const auto* f = mesh.GetFace(fId);
            if (!f || !IsValid(f->halfEdge)) continue;

            std::vector<glm::vec2> faceLoop;
            HalfEdgeID startHe = f->halfEdge;
            HalfEdgeID curHe = startHe;
            size_t guard = 0;
            do {
                const auto* he = mesh.GetHalfEdge(curHe);
                if (!he) break;
                faceLoop.push_back(he->corner.uv);

                // Determina se questo half-edge e' un bordo esterno dell'isola o una cucitura (seam)
                bool isSeamEdge = false;
                if (!IsValid(he->twin)) {
                    isSeamEdge = true;
                } else {
                    const auto* twinHe = mesh.GetHalfEdge(he->twin);
                    if (!twinHe || !IsValid(twinHe->face)) {
                        isSeamEdge = true;
                    } else {
                        auto it = layout.faceToIslandMap.find(twinHe->face);
                        if (it != layout.faceToIslandMap.end() && it->second != isl.id) {
                            isSeamEdge = true;
                        }
                    }
                }

                if (isSeamEdge) {
                    const auto* nextHe = mesh.GetHalfEdge(he->next);
                    glm::vec2 nextUV = nextHe ? nextHe->corner.uv : he->corner.uv;
                    outline.seamSegmentsAtlasUV.emplace_back(he->corner.uv, nextUV);
                }

                curHe = he->next;
                if (++guard > 64) break;
            } while (curHe != startHe && IsValid(curHe));

            if (faceLoop.size() >= 3) {
                outline.loops.push_back(faceLoop);
                if (outline.boundaryVerticesAtlasUV.empty()) {
                    outline.boundaryVerticesAtlasUV = faceLoop;
                }
            }
        }

        // 2. Se loops e' vuoto a causa di disallineamento generazionale FaceID, risolvi da faceToIslandMap o surfaceId
        if (outline.loops.empty()) {
            for (FaceID activeFId : mesh.GetActiveFaceIDs()) {
                auto it = layout.faceToIslandMap.find(activeFId);
                if (it != layout.faceToIslandMap.end() && it->second == isl.id) {
                    const auto* f = mesh.GetFace(activeFId);
                    if (!f || !IsValid(f->halfEdge)) continue;
                    std::vector<glm::vec2> faceLoop;
                    HalfEdgeID startHe = f->halfEdge;
                    HalfEdgeID curHe = startHe;
                    size_t guard = 0;
                    do {
                        const auto* he = mesh.GetHalfEdge(curHe);
                        if (!he) break;
                        faceLoop.push_back(he->corner.uv);
                        curHe = he->next;
                        if (++guard > 64) break;
                    } while (curHe != startHe && IsValid(curHe));
                    if (faceLoop.size() >= 3) {
                        outline.loops.push_back(faceLoop);
                        if (outline.boundaryVerticesAtlasUV.empty()) {
                            outline.boundaryVerticesAtlasUV = faceLoop;
                        }
                    }
                }
            }
        }

        // 3. Fallback per corrispondenza di surfaceId semantico
        if (outline.loops.empty() && isl.semanticSurfaceId < 256) {
            for (FaceID activeFId : mesh.GetActiveFaceIDs()) {
                const auto* f = mesh.GetFace(activeFId);
                if (f && f->surfaceId == isl.semanticSurfaceId && IsValid(f->halfEdge)) {
                    std::vector<glm::vec2> faceLoop;
                    HalfEdgeID startHe = f->halfEdge;
                    HalfEdgeID curHe = startHe;
                    size_t guard = 0;
                    do {
                        const auto* he = mesh.GetHalfEdge(curHe);
                        if (!he) break;
                        faceLoop.push_back(he->corner.uv);
                        curHe = he->next;
                        if (++guard > 64) break;
                    } while (curHe != startHe && IsValid(curHe));
                    if (faceLoop.size() >= 3) {
                        outline.loops.push_back(faceLoop);
                        if (outline.boundaryVerticesAtlasUV.empty()) {
                            outline.boundaryVerticesAtlasUV = faceLoop;
                        }
                    }
                }
            }
        }

        // 4. Se loops e' ancora vuoto, usa boundaryHalfEdges se validi
        if (outline.loops.empty() && !isl.boundaryHalfEdges.empty()) {
            std::vector<glm::vec2> bLoop;
            for (HalfEdgeID heId : isl.boundaryHalfEdges) {
                const auto* he = mesh.GetHalfEdge(heId);
                if (he) bLoop.push_back(he->corner.uv);
            }
            if (bLoop.size() >= 3) {
                outline.boundaryVerticesAtlasUV = bLoop;
                outline.loops.push_back(std::move(bLoop));
            }
        }

        outlines.push_back(std::move(outline));
    }

    return outlines;
}

} // namespace fw
