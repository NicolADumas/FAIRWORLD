#pragma once
#include "State.h"
#include "MapDocument.h"
#include <memory>
#include <glm/glm.hpp>
#include "SphericalLOD.h"
#include <vector>

// Forward declaration per evitare inclusioni circolari
struct SharedContext;
namespace fw { class ForgeWorld; }

class MapState : public State {
public:
    explicit MapState(SharedContext* context);
    ~MapState() override;

    bool Init() override;
    void Update(float dt) override;
    void Render() override;

private:
    void DrawBuilderUI();
    void DrawRuntimeUI();
    void CompileAndGenerate();

    SharedContext* m_context;
    fw::MapDocument m_document;
    
    // Stato dell'editor
    int m_activePlanetIndex = 0;
    int m_selectedRegionIndex = -1;
    bool m_isBuilderMode = true;
    
    // Zoom/Pan per il Canvas 2D
    glm::vec2 m_canvasPan = glm::vec2(0.0f);
    float m_canvasZoom = 1.0f;

    // Paint Canvas State
    int m_brushSize = 1; // 1x1 chunk, 2x2 chunk...
    int m_paintSurfaceBlock = 1;
    int m_paintSubsurfaceBlock = 3;
    int m_paintRegionType = 0;

    // --- MONDO DI PREVIEW (Fase 2) ---
    std::unique_ptr<fw::ForgeWorld> m_previewWorld;

    // --- TELECAMERA ORBITALE ---
    float m_orbitDistance = 150.0f;
    float m_orbitYaw = 0.0f;
    float m_orbitPitch = 30.0f;
    glm::vec3 m_orbitTarget = glm::vec3(0.0f);
    
    // --- SPHERICAL LOD SYSTEM ---
    std::vector<fw::ChunkNode> m_planetRootNodes;
    fw::SphericalLODSystem m_lodSystem;
};
