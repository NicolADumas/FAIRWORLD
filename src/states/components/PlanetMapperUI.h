#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "SharedContext.h"
#include "WorldProjectManager.h"
#include "RaycastSystem.h"

namespace fw {
    class SphericalLODSystem;
}

struct PlanetMapperUIResult {
    bool requestSave = false;
    bool requestRebuildRoots = false;
    bool documentChanged = false; // Trigger compilation
    bool goToPlayState = false;
    bool goToHubState = false;
    bool recenterCamera = false;
};

class PlanetMapperUI {
public:
    PlanetMapperUI();
    ~PlanetMapperUI() = default;

    PlanetMapperUIResult Draw(SharedContext* context, 
                              int& activePlanetIndex, 
                              int& activeTemplateIndex, 
                              float& orbitDistance, 
                              float orbitYaw, 
                              float orbitPitch,
                              const fw::RaycastHit& lastRayHit,
                              fw::SphericalLODSystem& lodSystem);

    void Update(float dt);

private:
    bool m_showPlacementTable = false;
    bool m_showSaveConfirmPopup = false;
    float m_saveFlashTimer = 0.0f;
    std::string m_saveFlashMsg;

    void DoSave(fw::WorldProjectManager* pm);
};
