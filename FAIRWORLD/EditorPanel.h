#pragma once
#include "AssetManager.h"
#include <string>
#include "Editor/ModelEditor.h"

// Forward declarations
class World;
class RenderManager;
class Player;
class MobManager;
class Camera;

class EditorPanel {
public:
    bool isOpen = false;

    void Toggle() { isOpen = !isOpen; }

    // Chiamata ogni frame dentro il render loop, PRIMA di ImGui::Render()
    // Ora riceve anche il RenderManager per aggiornare le texture in real-time
    void Draw(AssetManager& assets, World& world, RenderManager* renderer, MobManager* mobManager, Player* player, const Camera& camera);

private:
    void DrawBlocksTab(AssetManager& assets, RenderManager* renderer);
    void DrawMobsTab(AssetManager& assets, MobManager* mobManager, const Camera& camera);
    void DrawPlayerTab(Player& player, MobManager& mobManager);
    void DrawWorldTab(World& world);
    void DrawEngineTab(RenderManager* renderer);

    void DrawTexturePainterTab(AssetManager& assets, RenderManager* renderer);
    void DrawRadialMenu(World& world, RenderManager* renderer);
    
    // Apre la Windows File Dialog nativa e restituisce il path scelto
    std::string BrowseForFile(const char* filter);

    int  m_selectedBlock = 0;
    int  m_selectedMob   = 0;
    int  m_activeTab     = -1;
    
    ModelEditor m_modelEditor;
};
