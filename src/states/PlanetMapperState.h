#pragma once
#include "AppBaseState.h"
#include <memory>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "SphericalLOD.h"

struct SharedContext;

class PlanetMapperState : public AppBaseState {
public:
    explicit PlanetMapperState(SharedContext* context);
    ~PlanetMapperState() override;

protected:
    bool InitApp() override;
    void UpdateApp(float dt) override;
    void RenderApp() override;

private:
    void RebuildPlanetRoots();
    void DrawBuilderUI();
    void DrawRuntimeUI();
    void CompileAndGenerate();
    
    // Architettura Specifica (Planet Mapper State)
    int m_activePlanetIndex = 0;
    bool m_isBuilderMode = true;
    
    bool m_showPlacementTable = false;
    bool m_showSaveConfirmPopup = false;
    
    int m_activeTemplateIndex = 0; // Scelto tra la terrainLibrary di WorldProjectManager
    int m_selectedChunkInstanceIndex = -1;

    // Telecamera Orbitale e Navigazione Sfera
    float m_orbitDistance = 250.0f;
    float m_orbitYaw = 45.0f;
    float m_orbitPitch = 30.0f;
    glm::vec3 m_orbitTarget = glm::vec3(0.0f);
    
    // Sistema LOD Sferico
    std::vector<fw::ChunkNode> m_planetRootNodes;
    std::vector<entt::entity> m_spawnPointMarkers;
    entt::entity m_cursorMarker = entt::null;
    fw::SphericalLODSystem m_lodSystem;
    
    // Feedback visivo salvataggio
    float m_saveFlashTimer = 0.0f; // secondi rimasti per mostrare il messaggio
    std::string m_saveFlashMsg;
};
