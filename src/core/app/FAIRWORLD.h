#pragma once

#define FAIRWORLD_API

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
#include "DroppedItem.h"
#include "DeskarmWebView.h"


enum class GameMode { Dev, Play, Hub, PhysicsLab, Map, BlockMaker };

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
    WEB_BROWSER,
    _COUNT
};

// Forward declarations
class XrManager;
class RenderManager;
class WindowManager;

namespace fw {
    class ForgeWorld;
    class TimeManager;
}
struct SharedContext;

class FAIRWORLD_API FairWorldEngine {
public:
    FairWorldEngine();
    ~FairWorldEngine();

    bool Init();
    void Run();
    void Shutdown();

    // Nuovi metodi per la State Machine Data-Driven (Fase 1.3)
    GameState GetCurrentState() const { return m_current; }
    bool IsRunning() const { return m_isRunning; }
    void PollHardwareEvents();

    bool isWorldRunning() const;
    bool isEditorOpen() const;

    bool Update(float deltaTime);
    
    // Gestione ImGui disaccoppiata per il Sistema Operativo (HubState)
    void BeginUIFrame();
    void EndUIFrame();
    
    void Render();

    // Collega il Bus Dati dell'OS al motore (chiamato dal main prima del loop)
    void SetSharedContext(SharedContext* ctx);

    // Getters esposti per i sistemi ECS
    PhysicsEngine& GetPhysicsEngine() { return m_physics; }
    Player& GetPlayer() { return m_player; }
    World& GetWorld() { return m_world; }
    fw::TimeManager& GetTimeManager() { return *m_timeManager; }
    RenderManager* GetRenderManager() { return m_renderManager.get(); }
    AssetManager* GetAssetManager() { return &m_assets; }
    GameMode GetGameMode() const { return m_gameMode; }
    void SetGameMode(GameMode mode) { m_gameMode = mode; }
    void ForceGameState(GameState state) { transitionTo(state); }
    bool IsInventoryOpen() const { return m_isInventoryOpen; }
    void SetInventoryOpen(bool open) { m_isInventoryOpen = open; }

private:
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
    bool m_hasShutdown = false;
    bool m_isVrMode;
    World  m_world; // Deprecated
    std::unique_ptr<fw::ForgeWorld> m_forgeWorld;
    std::unique_ptr<fw::TimeManager> m_timeManager;

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
    GameMode    m_gameMode = GameMode::Play;

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
    DeskarmWebView m_webView;

    // MIRINO: blocco attualmente puntato dalla camera (aggiornato ogni frame)
    glm::ivec3 m_targetedBlock = glm::ivec3(-1, -1, -1);
    bool       m_hasTarget     = false;

    // --- Mining Progressivo (Sforzo di Taglio) ---
    float      m_miningProgress     = 0.0f;             // Progresso corrente [0..1]
    glm::ivec3 m_miningTarget       = {-1, -1, -1};     // Blocco che stiamo scavando
    float      m_miningTimeRequired = 0.0f;              // Tempo totale necessario [s]

    // --- Dropped Items (Entità fisiche nel mondo) ---
    std::vector<DroppedItem> m_droppedItems;

    std::unique_ptr<XrManager>     m_xrManager;
    std::unique_ptr<RenderManager> m_renderManager;
    std::unique_ptr<WindowManager> m_windowManager;

    // Puntatore al Bus Dati dell'OS (non owning, vita gestita da main)
    SharedContext* m_sharedContext = nullptr;
};

