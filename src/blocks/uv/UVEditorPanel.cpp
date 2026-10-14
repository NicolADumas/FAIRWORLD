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
            outline.color = glm::vec4(1.0f, 0.8f, 0.2f, 1.0f); // Giallo evidenziato
        } else if (outline.isPinned) {
            outline.color = glm::vec4(0.3f, 0.8f, 1.0f, 1.0f); // Ciano pin
        } else {
            outline.color = glm::vec4(0.8f, 0.8f, 0.8f, 0.8f); // Grigio neutro
        }

        // Costruisci poligoni dai boundary half-edges dell'isola
        std::vector<glm::vec2> loop;
        for (HalfEdgeID heId : isl.boundaryHalfEdges) {
            const auto* he = mesh.GetHalfEdge(heId);
            if (he) {
                loop.push_back(he->corner.uv);
            }
        }
        if (!loop.empty()) {
            outline.boundaryVerticesAtlasUV = loop;
            outline.loops.push_back(std::move(loop));
        }

        // Seam segments per visualizzazione cuciture
        for (EdgeID eId : isl.seamEdges) {
            const auto* edge = mesh.GetEdge(eId);
            if (edge && edge->halfEdge != INVALID_HALFEDGE_ID) {
                const auto* he1 = mesh.GetHalfEdge(edge->halfEdge);
                if (he1) {
                    glm::vec2 uv1 = he1->corner.uv;
                    glm::vec2 uv2 = uv1;
                    if (he1->next != INVALID_HALFEDGE_ID) {
                        const auto* heNext = mesh.GetHalfEdge(he1->next);
                        if (heNext) uv2 = heNext->corner.uv;
                    }
                    outline.seamSegmentsAtlasUV.emplace_back(uv1, uv2);
                }
            }
        }

        outlines.push_back(std::move(outline));
    }

    return outlines;
}

} // namespace fw
