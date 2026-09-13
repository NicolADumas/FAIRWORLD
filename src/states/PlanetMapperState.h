#pragma once
#include "AppBaseState.h"
#include <memory>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "SphericalLOD.h"

// Componenti
#include "components/PlanetMapperUI.h"
#include "components/PlanetMapperCamera.h"
#include "components/PlanetMapperCompiler.h"

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
    
    PlanetMapperUI m_ui;
    PlanetMapperCamera m_camera;
    PlanetMapperCompiler m_compiler;

    int m_activePlanetIndex = 0;
    int m_activeTemplateIndex = 0;

    std::vector<fw::ChunkNode> m_planetRootNodes;
    std::vector<entt::entity> m_spawnPointMarkers;
    entt::entity m_cursorMarker = entt::null;
    fw::SphericalLODSystem m_lodSystem;

    // Risultato UI dell'ultimo RenderApp(), consumato in UpdateApp() del frame successivo
    PlanetMapperUIResult m_lastUIResult{};
};
