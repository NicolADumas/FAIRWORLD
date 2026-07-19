#pragma once
#include "State.h"
#include "entt/entt.hpp"
#include <vector>
#include <memory>
#include "Systems.h"
#include "../app/GlobalAssetBrowser.h"

struct SharedContext;
namespace fw {
    class ForgeWorld;
}

#include <map>
#include <string>

class ForgeState : public State {
public:
    explicit ForgeState(SharedContext* context);
    ~ForgeState() override;

    bool Init() override;
    void Update(float dt) override;
    void Render() override;
    
    void UpdatePreviewMesh(int colorIndex);

    entt::registry* GetRegistry() override { return &m_registry; }

private:
    SharedContext* m_context;
    entt::registry m_registry;
    std::vector<std::unique_ptr<System>> m_systems;

    // --- Orbital Camera ---
    float m_orbitDistance = 24.0f;
    float m_orbitYaw = -90.0f;
    float m_orbitPitch = 30.0f;
    glm::vec3 m_orbitTarget = glm::vec3(8.0f, 8.0f, 8.0f);

    // --- Editor Tools ---
    int m_selectedTool = 1; // 0: Select, 1: Place, 2: Erase
    int m_selectedColorIndex = 1; // Default to Palette Index 1
    int m_lastPreviewIndex = -1;
    entt::entity m_previewEntity = entt::null;
    int m_activeMaterialsCount = 1; // Number of currently active materials (starts with 1)
    char m_structureNameBuffer[64] = "NuovaStruttura"; // Buffer per il nome del salvataggio
    int m_exportPlacementMode = 0; // 0 = Prefab (Struttura PBR), 1 = Minivoxel (Iniezione)
    bool m_isMouseOverUI = false;

    // --- Camera & POV ---
    bool m_isFirstPerson = false;
    float m_cameraFov = 75.0f;
    float m_fpYaw = -90.0f;
    float m_fpPitch = 0.0f;
    glm::vec3 m_fpPosition = glm::vec3(8.0f, 8.0f, 25.0f);
    bool m_fpCursorLocked = false;
    // --- Keyboard Cursor ---
    int m_controlMode = 0; // 0: Auto, 1: Mouse, 2: Keyboard
    bool m_useKeyboardCursor = false;
    glm::vec3 m_keyboardCursorPos = glm::vec3(8.0f, 0.0f, 8.0f);

    // --- Global UI ---
    fw::GlobalAssetBrowser m_assetBrowser;
    bool m_showAssetBrowser = false;
    
    // --- Biome Designer UI ---
    bool m_showBiomeDesigner = false;
    int m_selectedBiomeIndex = 0;
    
    std::map<int, std::string> m_blockNames;
};
