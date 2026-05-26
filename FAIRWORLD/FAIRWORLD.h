#pragma once

#ifdef FAIRWORLD_EXPORTS
#define FAIRWORLD_API __declspec(dllexport)
#else
#define FAIRWORLD_API __declspec(dllimport)
#endif

#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include <glm/glm.hpp>
#include "Camera.h"
#include "World.h"
#include "AssetManager.h"
#include "EditorPanel.h"
#include "Player.h"
#include "MobManager.h"
#include "AIAssistant.h"
#include "PhysicsEngine.h"


enum class GameMode { Dev, Play };

// ------------------------------------------------------------------
// GameState: ogni stato è ESCLUSIVO. Mai due stati attivi assieme.
// ------------------------------------------------------------------
enum class GameState {
    LOADING,
    MAIN_MENU,
    PLAYING,
    PAUSE_MENU,
    TAB_BLOCCHI,
    TAB_MOB,
    TAB_PLAYER,
    TAB_TEXTURE_PAINTER,
    TAB_MODEL_SCULPTOR,
    TAB_MODEL_EDITOR,
    TAB_MONDO,
    TAB_ENGINE,
    _COUNT
};

// Forward declarations
class XrManager;
class RenderManager;
class WindowManager;

class FAIRWORLD_API FairWorldEngine {
public:
    FairWorldEngine();
    ~FairWorldEngine();

    bool Init();
    void Run();
    void Shutdown();

    bool isWorldRunning() const;
    bool isEditorOpen() const;

private:
    bool Update(float deltaTime);
    void Render();
    std::string GetSlotName(int slotIndex);
    void ProcessAIMessage(const std::string& input);

    // --- UI State Machine ---
    GameState m_current;
    GameState m_previousTab;
    const char* getStateName() const;
    void transitionTo(GameState next);
    void renderMainMenu();
    void renderPauseMenu();
    void renderEditorTabs();
    void renderTab_Blocchi();
    void renderTab_Mob();
    void renderTab_Player();
    void renderTab_TexturePainter();
    void renderTab_ModelSculptor();
    void renderTab_ModelEditor();
    void renderTab_Mondo();
    void renderTab_Engine();
    
    bool isTabState(GameState s) const;
    int tabIndex(GameState s) const;
    GameState tabFromIndex(int i) const;
    const char* tabLabel(GameState s) const;

    bool m_isRunning;
    bool m_isVrMode;

    Camera m_camera;
    World  m_world;

    // Input mouse
    bool m_firstMouse = true;
    float m_lastX = 400.0f;
    float m_lastY = 300.0f;
    bool m_lButtonWasDown = false;
    bool m_rButtonWasDown = false;

    // Inventario
    int       m_selectedSlot  = 0;                    // 0–9
    bool      m_showCustomBlockMenu = true;
    bool      m_isInventoryOpen = false;

    // God Mode Editor
    bool         m_tabWasDown = false;
    bool         m_escWasDown = false;
    // Cursore FPS: bloccato al centro per visuale libera
    bool         m_cursorLocked  = false; // true = cursore nascosto e centrato
    bool         m_cursorVisible = true;  // stato corrente ShowCursor
    AssetManager m_assets;
    EditorPanel  m_editor;

    Player      m_player;
    MobManager  m_mobManager;
    AIAssistant m_aiAssistant;
    GameMode    m_gameMode = GameMode::Dev;

    PhysicsEngine m_physics;
    RigidBody     m_playerBody;

    // Diario AI
    bool         m_isDiaryOpen = false;
    char         m_diaryInput[4096] = "";
    std::vector<std::string> m_diaryHistory;
    bool         m_diaryFocusRequested = false;

    // Overlay Morte e Level Up
    float       m_levelUpTimer     = 0.0f;
    int         m_levelUpNewLevel  = 0;
    int         m_levelUpPoints    = 0;

    float       m_deathOverlayTimer = 0.0f;
    bool        m_justDied          = false;

    // DESKARM Integration
    std::filesystem::file_time_type m_lastDeskarmExportTime;

    // MIRINO: blocco attualmente puntato dalla camera (aggiornato ogni frame)
    glm::ivec3 m_targetedBlock = glm::ivec3(-1, -1, -1);
    bool       m_hasTarget     = false;

    std::unique_ptr<XrManager>     m_xrManager;
    std::unique_ptr<RenderManager> m_renderManager;
    std::unique_ptr<WindowManager> m_windowManager;
};
