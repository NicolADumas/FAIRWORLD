#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "blocks/uv/UVEditorDocument.h"

namespace fw {

// =============================================================================
// STATO VISUALIZZAZIONE PANNELLO UV EDITOR 2D (BM-UV5.10)
// =============================================================================

struct UVPanelViewState {
    float zoom = 1.0f;
    glm::vec2 panOffset{0.0f, 0.0f};
    bool showCheckerboard = true;
    bool showDistortionHeatmap = false;
    bool showSeams = true;
    bool showWireframe = true;
    bool snapToGrid = false;
    float snapGridSize = 0.05f; // 5% intervallo griglia
    uint32_t activeAtlasPage = 0;
};

// =============================================================================
// UV EDITOR PANEL (BM-UV5.10)
// Helper headless e bridge UI per l'ispezione e manipolazione 2D delle UV
// =============================================================================

class UVEditorPanel {
public:
    UVEditorPanel() = default;
    ~UVEditorPanel() = default;

    UVPanelViewState& GetViewState() { return m_viewState; }
    const UVPanelViewState& GetViewState() const { return m_viewState; }

    // Converte coordinate da spazio canvas UI a spazio Atlas UV [0, 1]
    glm::vec2 ScreenToAtlasUV(const glm::vec2& screenPos, 
                              const glm::vec2& canvasOrigin, 
                              const glm::vec2& canvasSize) const;

    // Converte da spazio Atlas UV [0, 1] a coordinate screen pixel del canvas
    glm::vec2 AtlasUVToScreen(const glm::vec2& atlasUV, 
                              const glm::vec2& canvasOrigin, 
                              const glm::vec2& canvasSize) const;

    // Genera dati vettoriali di outline per il disegno delle isole sulla pagina corrente
    struct IslandDrawOutline {
        UVIslandID islandId = 0;
        uint32_t atlasPage = 0;
        bool isSelected = false;
        bool isPinned = false;
        glm::vec4 color{1.0f};
        std::vector<std::vector<glm::vec2>> loops; // Cicli perimetrali di vertici in spazio UV atlas
        std::vector<glm::vec2> boundaryVerticesAtlasUV; // Loop perimetrale primario
        std::vector<std::pair<glm::vec2, glm::vec2>> seamSegmentsAtlasUV; // Segmenti di seam 2D
    };

    std::vector<IslandDrawOutline> BuildDrawOutlines(const UVEditorDocument& doc) const;

private:
    UVPanelViewState m_viewState;
};

} // namespace fw
