#pragma once
#include "AppBaseState.h"
#include <memory>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <unordered_set>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include "../core/utils/PlanetMath.h"

struct SharedContext;

class ChunkEditorState : public AppBaseState {
public:
    explicit ChunkEditorState(SharedContext* context);
    ~ChunkEditorState() override;

protected:
    bool InitApp() override;
    void UpdateApp(float dt) override;
    void RenderApp() override;

private:
    void DrawUI();
    void RebuildChunkPreview();
    void ClampBrushToMacroChunk(glm::ivec2& targetMin, glm::ivec2& targetMax) {
        int extents = fw::PlanetMath::GetEditorCanvasExtents(m_previewPlanetSize);
        targetMin.x = std::max(targetMin.x, -extents);
        targetMin.y = std::max(targetMin.y, -extents);
        targetMax.x = std::min(targetMax.x, extents);
        targetMax.y = std::min(targetMax.y, extents);
    }
    
    // Editor State (Specific Architecture)
    int m_activeTemplateIndex = 0;
    int m_selectedSubRegionIndex = -1;
    
    // UI state & Canvas 2D
    glm::vec2 m_canvasPan = glm::vec2(0.0f);
    float m_canvasZoom = 1.0f;
    int m_brushSize = 1;
    int m_paintSurfaceBlock = 1;
    int m_paintSubsurfaceBlock = 2;
    int m_paintCoreBlock = 2;
    int m_paintSubsurfaceDepth = 3;
    bool m_paintEnableCaves = false;
    int m_paintRegionType = 0;
    int m_paintBrushShape = 0;
    bool m_isBrushModeActive = false; // Nuovo toggle per uscire dal pennello continuo
    bool m_autoRebuildPreview = false;
    bool m_showSaveConfirmPopup = false;
    
    // Orbital camera for right-hand 3D Voxel preview viewport
    float m_orbitDistance = 65.0f;
    float m_orbitYaw = 45.0f;
    float m_orbitPitch = 35.0f;
    glm::vec3 m_orbitTarget = glm::vec3(0.0f, 15.0f, 0.0f);
    
    bool m_needsRebuild = false;
    float m_rebuildTimer = 0.0f;

    // Stroke tracking
    std::unordered_set<glm::ivec2> m_strokeProcessedCells;
    bool m_isStrokeActive = false;
    fw::PlanetSize m_previewPlanetSize = fw::PlanetSize::Medium;
};
