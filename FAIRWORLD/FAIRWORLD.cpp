#include "pch.h"
#include "FAIRWORLD.h"
#include "BlockMaterial.h"
#include "XrManager.h"
#include "RenderManager.h"
#include "WindowManager.h"
#include "EventManager.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <algorithm>
#include <windows.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_win32.h>
#include <filesystem>
#include <fstream>
#include "json.hpp"

using json = nlohmann::json;

std::string FairWorldEngine::GetSlotName(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= Inventory::HOTBAR_SIZE) return "Unknown";
    
    const InventoryItem& item = m_player.inventory.slots[slotIndex];
    if (item.IsEmpty()) return "Empty";

    if (item.type == ItemType::Block) {
        int id = item.blockType;
        BlockDef* def = m_assets.GetBlock(id);
        if (def) {
            return def->name;
        }
        return "Block " + std::to_string(id);
    } else if (item.type == ItemType::Weapon) {
        return "Weapon";
    } else if (item.type == ItemType::Consumable) {
        return "Consumable";
    }
    
    return "Unknown";
}

// ============================================================
//  Tabelle statiche dei TAB  (ordine = ordine visivo nella barra)
// ============================================================
static constexpr GameState kTabStates[] = {
    GameState::TAB_BLOCCHI,
    GameState::TAB_MOB,
    GameState::TAB_PLAYER,
    GameState::TAB_TEXTURE_PAINTER,
    GameState::TAB_MODEL_SCULPTOR,
    GameState::TAB_MODEL_EDITOR,
    GameState::TAB_MONDO,
    GameState::TAB_ENGINE,
};
static constexpr int kTabCount = static_cast<int>(sizeof(kTabStates) / sizeof(kTabStates[0]));

static constexpr const char* kTabLabels[] = {
    "Blocchi",
    "Mob",
    "Player",
    "Texture Painter",
    "Model Sculptor",
    "Model Editor",
    "Mondo",
    "Engine",
};

static constexpr const char* kStateNames[] = {
    "LOADING",
    "MAIN_MENU",
    "PLAYING",
    "PAUSE_MENU",
    "TAB_BLOCCHI",
    "TAB_MOB",
    "TAB_PLAYER",
    "TAB_TEXTURE_PAINTER",
    "TAB_MODEL_SCULPTOR",
    "TAB_MODEL_EDITOR",
    "TAB_MONDO",
    "TAB_ENGINE",
    "WEB_BROWSER",
    "_COUNT",
};

// ============================================================
//  Helpers statici
// ============================================================
bool FairWorldEngine::isTabState(GameState s) const {
    for (int i = 0; i < kTabCount; ++i)
        if (kTabStates[i] == s) return true;
    return false;
}

int FairWorldEngine::tabIndex(GameState s) const {
    for (int i = 0; i < kTabCount; ++i)
        if (kTabStates[i] == s) return i;
    return 0;
}

GameState FairWorldEngine::tabFromIndex(int i) const {
    if (i < 0 || i >= kTabCount) return kTabStates[0];
    return kTabStates[i];
}

const char* FairWorldEngine::tabLabel(GameState s) const {
    return kTabLabels[tabIndex(s)];
}

const char* FairWorldEngine::getStateName() const {
    int idx = static_cast<int>(m_current);
    return kStateNames[idx];
}

void FairWorldEngine::transitionTo(GameState next) {
    if (next == m_current) return;
    
    // Logica di uscita dallo stato
    if (m_current == GameState::WEB_BROWSER) {
        m_webView.SetVisible(false);
        m_cursorLocked = true; // Riblocca il cursore uscendo dal browser
    }
    
    m_current = next;
    
    // Logica di entrata nello stato
    if (m_current == GameState::WEB_BROWSER) {
        m_webView.SetVisible(true);
        m_cursorLocked = false;
        
        // Alla prima apertura del browser, assicuriamoci di mostrare una pagina (es. Google se è la prima volta)
        if (!m_webView.IsVisible()) {
            m_webView.Navigate(L"https://www.google.com");
        }
    }
}

bool FairWorldEngine::isWorldRunning() const {
    switch (m_current) {
        case GameState::LOADING:
        case GameState::MAIN_MENU:
        case GameState::PAUSE_MENU:
            return false;
        default:
            return true;
    }
}

bool FairWorldEngine::isEditorOpen() const {
    return isTabState(m_current);
}

FairWorldEngine::FairWorldEngine() 
    : m_isRunning(false), m_isVrMode(false),
      m_current(GameState::MAIN_MENU), m_previousTab(GameState::TAB_BLOCCHI),
      m_xrManager(std::make_unique<XrManager>()), 
      m_renderManager(std::make_unique<RenderManager>()),
      m_windowManager(std::make_unique<WindowManager>()) {}

FairWorldEngine::~FairWorldEngine() {
    Shutdown();
}

bool FairWorldEngine::Init() {
    std::cout << "[SYSTEM] Inizializzazione Finestra Principale..." << std::endl;
    if (!m_windowManager->Init(1280, 720, "FAIRWORLD")) {
        return false;
    }

    HWND hwnd = m_windowManager->GetWindowHandle();
    HINSTANCE hinstance = GetModuleHandle(nullptr);

    std::cout << "[SYSTEM] Tentativo di connessione al visore VR..." << std::endl;
    // 1. Inizializza OpenXR
    if (m_xrManager->Init()) {
        m_isVrMode = true;
        std::cout << "[SYSTEM] Visore VR trovato! Avvio in MODALITA' VR." << std::endl;
        // Inizializza Vulkan per la VR
        if (!m_renderManager->Init(m_isVrMode, m_xrManager.get(), hwnd, hinstance)) return false;
        // m_xrManager->CreateSession(m_renderManager->GetVulkanInstance(), m_renderManager->GetVulkanDevice()); // COMMENTATO FINCHE' IL DEVICE NON E' PRONTO
    } else {
        std::cout << "[SYSTEM] Visore VR non trovato. Fallback in MODALITA' DESKTOP (Schermo piatto)." << std::endl;
        m_isVrMode = false;
        
        // Inizializza Vulkan per lo schermo piatto
        if (!m_renderManager->Init(m_isVrMode, nullptr, hwnd, hinstance)) return false;
    }

    // Carica asset definitions (blocchi e mob da JSON)
    m_assets.LoadAll("assets/");
    m_renderManager->LoadBlockTextures("assets/", m_assets.GetBlocks());
    m_renderManager->LoadAllMobMeshes(m_assets);
    // Pre-carica il preset 1 per il blocco custom (Layer 4) se presente su disco
    m_renderManager->LoadTextureFromFile("assets/textures/custom1.png", 4);

    // Posiziona la telecamera sopra il pavimento al centro del mondo
    m_camera.Position = glm::vec3(0.0f, 30.0f, 0.0f); // Spostato a y=30 per non incastrarsi nelle colline
    m_camera.Yaw   = -90.0f;
    m_camera.Pitch = -20.0f;
    
    m_playerBody.position = m_camera.Position;

    // Carica la mesh iniziale del mondo sulla GPU (Chunks)
    m_world.InitWorld();
    auto dirtyChunks = m_world.BuildDirtyChunks();
    for (auto& coord : dirtyChunks) {
        auto* chunk = m_world.GetChunk(coord.x, coord.z);
        if (chunk && !chunk->isMeshEmpty) {
            m_renderManager->UploadChunkMesh(coord, chunk->vertices, chunk->indices);
        }
    }

    m_isRunning = true;
    m_player.LoadFromJson("assets/player.json");
    
    // Inizializza WebView2 per GUI DESKARM
    std::cout << "[SYSTEM] Inizializzazione WebView2 in corso..." << std::endl;
    m_webView.Init(hwnd);
    
    // --- Sottoscrizione Eventi (Event-Driven Input System) ---
    EventManager::Get().Subscribe<Event_BlockMined>([this](const Event_BlockMined& e) {
        // Logica eseguita UNA SOLA VOLTA quando l'evento viene triggerato
        BlockType brokenType = m_world.GetBlock(e.position.x, e.position.y, e.position.z);
        
        if (brokenType != BlockType::Air) {
            m_world.SetBlock(e.position.x, e.position.y, e.position.z, BlockType::Air);
            
            // Gestione Drop (Loot)
            InventoryItem drop;
            drop.type = ItemType::Block;
            drop.blockType = (int)brokenType;
            drop.count = 1;
            
            if (!m_player.inventory.AddItem(drop)) {
                // L'inventario è pieno, spawna l'oggetto fisico a terra
                DroppedItem di;
                di.item = drop;
                // Spawna al centro esatto del blocco
                di.position = glm::vec3(e.position.x + 0.5f, e.position.y + 0.5f, e.position.z + 0.5f);
                // Dai un piccolo "pop" casuale verso l'alto
                float rx = ((rand() % 100) / 100.0f) * 2.0f - 1.0f;
                float rz = ((rand() % 100) / 100.0f) * 2.0f - 1.0f;
                di.velocity = glm::vec3(rx * 1.5f, 3.0f, rz * 1.5f);
                
                m_droppedItems.push_back(di);
            }
            std::cout << "[EVENT] Blocco distrutto in (" << e.position.x << "," << e.position.y << "," << e.position.z << ")\n";
        }
    });

    EventManager::Get().Subscribe<Event_BlockPlaced>([this](const Event_BlockPlaced& e) {
        m_world.SetBlock(e.position.x, e.position.y, e.position.z, e.type);
        std::cout << "[EVENT] Blocco piazzato in (" << e.position.x << "," << e.position.y << "," << e.position.z << ")\n";
    });

    return true;
}

void FairWorldEngine::PollHardwareEvents() {
    if (m_isVrMode) {
        // Gestisci gli eventi del visore
        m_xrManager->PollEvents(m_isRunning);
    } else {
        // Gestisci gli eventi della finestra desktop
        if (!m_windowManager->PollEvents()) {
            m_isRunning = false;
        }
    }
}

void FairWorldEngine::Run() {
    auto lastTime = std::chrono::high_resolution_clock::now();

    while (m_isRunning) {
        PollHardwareEvents();
        if (!m_isRunning) break;

        // Calcola il Delta Time
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        // Logica di gioco
        Update(deltaTime);

        // Rendering
        Render();
    }
}

bool FairWorldEngine::Update(float deltaTime) {
    ImGuiIO& io = ImGui::GetIO();

    // Aggiornamento Chunks
    auto dirtyChunks = m_world.BuildDirtyChunks();
    for (auto& coord : dirtyChunks) {
        auto* chunk = m_world.GetChunk(coord.x, coord.z);
        if (chunk) {
            if (chunk->isMeshEmpty) {
                m_renderManager->DestroyChunkBuffer(coord);
            } else {
                m_renderManager->UploadChunkMesh(coord, chunk->vertices, chunk->indices);
            }
        }
    }

    // Integrazione Polling DESKARM Editor (Python 2D)
    try {
        std::string exportPath = "assets/deskarm_export.json";
        if (std::filesystem::exists(exportPath)) {
            auto lastWrite = std::filesystem::last_write_time(exportPath);
            if (lastWrite != m_lastDeskarmExportTime) {
                m_lastDeskarmExportTime = lastWrite;
                std::ifstream f(exportPath);
                if (f.is_open()) {
                    json data = json::parse(f);
                    bool worldChanged = false;
                    for (const auto& pt : data) {
                        int x = pt["x"];
                        int z = pt["z"];
                        // Piazziamo una colonna alta 3 blocchi al suolo
                        for (int y = 30; y < 33; y++) {
                            m_world.SetBlock(x, y, z, BlockType::Stone);
                        }
                        worldChanged = true;
                    }
                    if (worldChanged) {
                        std::cout << "[DESKARM] Costruzione dall'editor ricevuta e applicata!" << std::endl;
                        // Il prossimo Update(deltaTime) chiamerà BuildDirtyChunks e uploaderà i chunk giusti.
                    }
                }
            }
        }
    } catch (...) {}

    // Toggle Diario AI con 'J'
    static bool jWasDown = false;
    bool jDown = (GetAsyncKeyState('J') & 0x8000) != 0;
    if (jDown && !jWasDown && !m_isDiaryOpen) {
        m_isDiaryOpen = true;
        m_diaryFocusRequested = true;
        m_cursorLocked = false; // Sblocca il cursore per cliccare sul diario se serve
    }
    jWasDown = jDown;
    
    // Apri Browser Web Integrato con 'H'
    static bool hWasDown = false;
    bool hDown = (GetAsyncKeyState('H') & 0x8000) != 0;
    if (hDown && !hWasDown) {
        if (m_current == GameState::WEB_BROWSER) {
            transitionTo(GameState::PLAYING);
        } else if (m_current == GameState::PLAYING) {
            transitionTo(GameState::WEB_BROWSER);
        }
    }
    hWasDown = hDown;
    
    
    // --- ESC: apre/chiude il menu di pausa ---
    static bool escWasDownEngine = false;
    bool escDownEngine = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (escDownEngine && !escWasDownEngine) {
        if (m_current == GameState::PLAYING) {
            transitionTo(GameState::PAUSE_MENU);
        } else if (m_current == GameState::PAUSE_MENU || isEditorOpen()) {
            transitionTo(GameState::PLAYING);
        }
    }
    escWasDownEngine = escDownEngine;

    if (!isWorldRunning()) return true;

    // Avanza il tempo nel ciclo Giorno/Notte (Fase 2)
    m_world.AdvanceTime(deltaTime);
    
    // Avanza la simulazione Orbitale e Kepleriana (Fase 8)
    m_world.SimulateOrbits(deltaTime);

    // Fisica dell'acqua (Fase 4)
    m_world.m_waterTickAccum += deltaTime;
    if (m_world.m_waterTickAccum >= 0.25f) {
        m_world.SimulateWaterTick();
        m_world.m_waterTickAccum = 0.0f;
    }

    // Termodinamica SI (Fase 6)
    m_world.m_thermoTickAccum += deltaTime;
    if (m_world.m_thermoTickAccum >= 1.0f) {
        m_world.SimulateThermodynamicsTick();
        m_world.m_thermoTickAccum = 0.0f;
    }

    // Toggle Inventory con 'E' (TAB ora è menu principale)
    static bool eWasDown = false;
    bool eDown = (GetAsyncKeyState('E') & 0x8000) != 0;
    if (eDown && !eWasDown && !m_isDiaryOpen && !io.WantCaptureKeyboard) {
        m_isInventoryOpen = !m_isInventoryOpen;
        if (m_isInventoryOpen) {
            m_cursorLocked = false;
        }
    }
    eWasDown = eDown;

    // --- INVENTARIO E MOVIMENTO ---
    if (!io.WantCaptureKeyboard && !m_isDiaryOpen && !m_isInventoryOpen) {
        // I tasti '1'-'9' mappano agli slot 0-8, '0' mappa allo slot 9
        const int keyMap[10] = { '1','2','3','4','5','6','7','8','9','0' };
        for (int i = 0; i < Inventory::HOTBAR_SIZE; i++) {
            if (GetAsyncKeyState(keyMap[i]) & 0x8000) {
                if (m_selectedSlot != i) {
                    m_selectedSlot  = i;
                    std::cout << "[INVENTORY] Slot " << (i + 1 == 10 ? 0 : i + 1)
                              << " selezionato: " << GetSlotName(i) << std::endl;
                }
            }
        }
    }

    // Tasto F1: Cambia modalità
    static bool f1WasDown = false;
    bool f1Down = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
    if (f1Down && !f1WasDown) {
        m_gameMode = (m_gameMode == GameMode::Dev) ? GameMode::Play : GameMode::Dev;
        m_player.SaveToJson("assets/player.json");
    }
    f1WasDown = f1Down;

    if (!io.WantCaptureKeyboard && !m_isDiaryOpen) {
        // --- FISICA E MOVIMENTO (Fase 2) ---
        float baseSpeed = m_camera.MovementSpeed;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) m_camera.MovementSpeed = baseSpeed * 1.5f;

        glm::vec3 flatFront = glm::normalize(glm::vec3(m_camera.Front.x, 0.0f, m_camera.Front.z));
        glm::vec3 flatRight = glm::normalize(glm::vec3(m_camera.Right.x, 0.0f, m_camera.Right.z));
        glm::vec3 moveDir(0.0f);

        if (GetAsyncKeyState('W') & 0x8000) moveDir += flatFront;
        if (GetAsyncKeyState('S') & 0x8000) moveDir -= flatFront;
        if (GetAsyncKeyState('A') & 0x8000) moveDir -= flatRight;
        if (GetAsyncKeyState('D') & 0x8000) moveDir += flatRight;

        float hLen = glm::length(moveDir);
        if (hLen > 0.0f) moveDir = (moveDir / hLen);

        glm::vec3 targetVelocity = moveDir * m_camera.MovementSpeed;
        
        // Modalita' Dev (Volo) vs Play (Fisica Reale)
        if (m_gameMode == GameMode::Dev) {
            m_playerBody.velocity = targetVelocity;
            if (GetAsyncKeyState(VK_SPACE) & 0x8000) m_playerBody.velocity.y = m_camera.MovementSpeed;
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) m_playerBody.velocity.y = -m_camera.MovementSpeed;
            m_playerBody.position += m_playerBody.velocity * deltaTime;
            m_playerBody.isGrounded = false;
        } else {
            // Nuoto: danno (es. Lava)
            BlockType centerBlock = m_world.GetBlock((int)floor(m_playerBody.position.x), (int)floor(m_playerBody.position.y - m_playerBody.radius), (int)floor(m_playerBody.position.z));
            BlockDef* centerDef = m_assets.GetBlock((int)centerBlock);
            if (centerDef && centerDef->isLiquid && centerDef->damagePerSecond > 0) {
                static float damageTimer = 0.0f;
                damageTimer += deltaTime;
                if (damageTimer >= 1.0f) {
                    m_player.stats.currentHP -= centerDef->damagePerSecond;
                    damageTimer = 0.0f;
                }
            }
            
            // Imposta la velocità orizzontale desiderata dal player (WASD)
            m_playerBody.velocity.x = targetVelocity.x;
            m_playerBody.velocity.z = targetVelocity.z;
            
            if (m_playerBody.isInWater) {
                // In acqua il movimento orizzontale è rallentato
                m_playerBody.velocity.x *= 0.5f;
                m_playerBody.velocity.z *= 0.5f;
                if (GetAsyncKeyState(VK_SPACE) & 0x8000) m_playerBody.velocity.y = 4.0f;
                else if (GetAsyncKeyState(VK_SHIFT) & 0x8000) m_playerBody.velocity.y = -4.0f;
            } else {
                // Salto (impulso istantaneo di velocità verticale)
                if (m_playerBody.isGrounded && (GetAsyncKeyState(VK_SPACE) & 0x8000)) {
                    m_playerBody.velocity.y = 7.0f; 
                }
            }
            
            // Vecchia velocità Y per calcolare l'impatto del danno da caduta
            float oldVelY = m_playerBody.velocity.y;
            
            // Esegui lo step fisico: gravita', attrito, integrazione, collisioni continue AABB
            m_physics.StepSimulation(m_playerBody, deltaTime, m_world);
            
            // --- STARGATE TELEPORT LOGIC ---
            if (m_playerBody.touchedStargate) {
                m_playerBody.touchedStargate = false;
                
                // Passa al pianeta successivo: Earth -> Mars -> Glacies -> Earth
                PlanetType current = m_world.GetCurrentPlanet()->type;
                PlanetType next = PlanetType::EarthPrime;
                if (current == PlanetType::EarthPrime) next = PlanetType::MarsDesolation;
                else if (current == PlanetType::MarsDesolation) next = PlanetType::Glacies;
                
                m_world.ChangePlanet(next);
                
                // Riposiziona il giocatore in alto al centro
                m_playerBody.position = glm::vec3(0.0f, 40.0f, 0.0f);
                m_playerBody.velocity = glm::vec3(0.0f);
                std::cout << "[STARGATE] Attraversamento wormhole completato! Gravita e Termodinamica ricalcolati." << std::endl;
            }
            
            // Danno da caduta automatico (Cap. 12/13 - perdita di energia cinetica)
            if (m_playerBody.isGrounded && oldVelY < -10.0f) {
                float deltaV = abs(oldVelY - m_playerBody.velocity.y);
                float damage = m_physics.ComputeFallDamage(deltaV, m_playerBody.mass);
                if (damage > 0.0f) {
                    m_player.stats.currentHP -= (int)damage;
                }
            }
        }
        
        // Sincronizza la camera con il corpo fisico
        m_camera.Position = m_playerBody.position;
        m_camera.IsGrounded = m_playerBody.isGrounded;
        m_camera.IsSwimming = m_playerBody.isInWater;
        m_camera.MovementSpeed = baseSpeed;
    } // Chiude if (!io.WantCaptureKeyboard)

    // =======================================================
    // --- SISTEMA FPS: CURSORE LOCK + ROTAZIONE MOUSE ---
    // =======================================================

    // Escape: sblocca il cursore (torna al menu / libera il mouse)
    {
        bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        if (escDown && !m_escWasDown) {
            m_cursorLocked = false;
        }
        m_escWasDown = escDown;
    }

    // Se God Mode, Diario, Inventario o Web Browser sono aperti, il cursore è sempre libero
    if (m_editor.isOpen || m_isDiaryOpen || m_isInventoryOpen || m_current == GameState::WEB_BROWSER) {
        m_cursorLocked = false;
    }

    // Click sinistro fuori dall'editor/diario/inventario/browser: blocca il cursore per il gioco FPS
    if (!m_cursorLocked && !m_editor.isOpen && !m_isDiaryOpen && !m_isInventoryOpen && m_current != GameState::WEB_BROWSER && !io.WantCaptureMouse) {
        bool lDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (lDown && !m_lButtonWasDown) {
            m_cursorLocked = true;
            m_firstMouse = true; // Resetta per evitare salto al primo frame
        }
    }

    // Applica visibilità cursore (usa il cursore software di ImGui per aggirare bug di visibilità Win32)
    ImGui::GetIO().MouseDrawCursor = !m_cursorLocked;
    
    // Per sicurezza, se il sistema Win32 nasconde il cursore, forziamolo a zero
    if (m_cursorLocked && m_cursorVisible) {
        ShowCursor(FALSE);
        m_cursorVisible = false;
    } else if (!m_cursorLocked && !m_cursorVisible) {
        ShowCursor(TRUE);
        m_cursorVisible = true;
    }

    // --- ROTAZIONE MOUSE FPS ---
    if (!io.WantCaptureMouse) {
        bool lDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        bool rDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;

        if (m_cursorLocked) {
            // Calcola il centro della finestra in coordinate schermo
            HWND hwnd = m_windowManager->GetWindowHandle();
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            POINT center = {
                (clientRect.right  - clientRect.left) / 2,
                (clientRect.bottom - clientRect.top)  / 2
            };
            ClientToScreen(hwnd, &center);

            // Delta dal centro
            POINT cursorPos;
            GetCursorPos(&cursorPos);
            float xoffset = (float)(cursorPos.x - center.x);
            float yoffset = (float)(center.y    - cursorPos.y);

            // Ricentra il cursore ogni frame
            SetCursorPos(center.x, center.y);

            // Primo frame dopo lock: ignora il delta (potrebbe essere grande)
            if (!m_firstMouse) {
                m_camera.ProcessMouseMovement(xoffset, yoffset);
            }
            m_firstMouse = false;
        } else {
            // Cursore libero: reset firstMouse per quando si ribloccherà
            m_firstMouse = true;
        }

        // Freccette: rotazione alternativa (sempre attive)
        float rs = 100.0f * deltaTime;
        if (GetAsyncKeyState(VK_UP)    & 0x8000) m_camera.ProcessMouseMovement(0.0f,  rs);
        if (GetAsyncKeyState(VK_DOWN)  & 0x8000) m_camera.ProcessMouseMovement(0.0f, -rs);
        if (GetAsyncKeyState(VK_LEFT)  & 0x8000) m_camera.ProcessMouseMovement(-rs,  0.0f);
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) m_camera.ProcessMouseMovement( rs,  0.0f);

        // --- PIAZZAMENTO / RIMOZIONE BLOCCHI (solo con cursore bloccato) ---
        // Ottieni lo stato dei tasti prima che la logica lo consumi
        bool placeBlock  = m_cursorLocked && (!m_lButtonWasDown && lDown);
        bool breakBlock  = m_cursorLocked && (!m_rButtonWasDown && rDown);
        bool holdingBreak = m_cursorLocked && rDown;  // Tenuto premuto (per mining progressivo)
        
        // --- 1. SIMULAZIONE DESKTOP VIEW-MODEL (Prima Persona) ---
        // Se non siamo in VR, calcoliamo la posizione della mano ancorandola alla telecamera.
        if (!m_isVrMode) {
            // Riduciamo il timer dell'animazione
            if (m_player.attackAnimTimer > 0.0f) {
                m_player.attackAnimTimer -= deltaTime;
            }

            // A. Prendi la matrice di rotazione della telecamera
            glm::mat4 camRotation = glm::mat4(glm::mat3(m_camera.GetViewMatrix()));
            camRotation = glm::inverse(camRotation); 

            // B. Crea la matrice base della mano (ancorata alla camera)
            glm::mat4 handMatrix = glm::translate(glm::mat4(1.0f), m_camera.Position);
            handMatrix *= camRotation;

            // C. Sposta l'arma in "basso, a destra, in avanti" (coordinate locali View-Space)
            handMatrix = glm::translate(handMatrix, glm::vec3(0.5f, -0.4f, -0.8f)); 
            
            // D. Applica animazione di attacco se attiva (Rotazione fendente verso il basso)
            if (m_player.attackAnimTimer > 0.0f) {
                float animProgress = m_player.attackAnimTimer / 0.3f; // 0.3f è la durata totale
                // Una parabola semplice (sin) che va da 0 a 1 e torna a 0
                float swingAngle = glm::sin(animProgress * glm::pi<float>()) * glm::radians(60.0f);
                handMatrix = glm::rotate(handMatrix, swingAngle, glm::vec3(1.0f, 0.0f, 0.0f)); // Ruota in avanti
            }

            m_player.rightHandTransform = handMatrix;
        }

        // --- 2. LOGICA COMBATTIMENTO E MOB (Sempre attiva) ---
        m_mobManager.UpdateMobs(deltaTime, m_camera.Position, m_player, m_assets, m_world);
        if (placeBlock) { // Usiamo lo stesso trigger 'placeBlock' (Click Sinistro) per attaccare
            m_player.attackAnimTimer = 0.3f; // Avvia l'animazione di attacco

            glm::vec3 rayPos = m_camera.Position;
            glm::vec3 rayDir = glm::normalize(m_camera.Front);
            
            int hitIndex = m_mobManager.Raycast(rayPos, rayDir, 3.5f);
            if (hitIndex >= 0) {
                // Abbiamo colpito un mob! Consumiamo l'input per non piazzare/spaccare blocchi sotto al mob
                placeBlock = false; 
                auto& mob = m_mobManager.instances[hitIndex];
                int damage = m_player.stats.GetTotalPhysicalDamage(m_player.equippedWeaponDamage);
                int actualDmg = mob.stats.TakePhysicalDamage(damage);
                std::cout << "[COMBAT] Colpito " << mob.displayName << "! Danni: " << actualDmg 
                          << " (HP Mob: " << mob.stats.currentHP << "/" << mob.stats.GetMaxHP() << ")" << std::endl;
                          
                if (!mob.stats.IsAlive()) {
                    mob.isAlive = false;
                    auto* tmpl = m_assets.GetMobByID(mob.templateID);
                    int expGained = tmpl ? tmpl->stats.expYield : 45;
                    bool leveledUp = m_player.GainExp(expGained);
                    std::cout << "[COMBAT] " << mob.displayName << " è stato sconfitto! Guadagni " << expGained << " EXP." << std::endl;
                    
                    if (leveledUp) {
                        m_levelUpTimer = 4.0f;
                        m_levelUpNewLevel = m_player.stats.level;
                        m_levelUpPoints = m_player.freeStatPoints;
                        m_player.SaveToJson("assets/player.json");
                    }
                }
            }
        }

        // Ora aggiorniamo lo stato precedente
        m_lButtonWasDown = lDown;
        m_rButtonWasDown = rDown;

        // Raycast SEMPRE attivo ogni frame (aggiorna il mirino HUD)
        {
            glm::vec3 rayPos = m_camera.Position;
            glm::vec3 rayDir = glm::normalize(m_camera.Front);
            const float STEP     = 0.05f;
            const float MAX_DIST = 8.0f;

            glm::ivec3 hitBlock(-1, -1, -1);
            glm::ivec3 prevBlock(-1, -1, -1);
            bool hitGhost = false;

            for (float t = 0.0f; t < MAX_DIST; t += STEP) {
                glm::vec3  p    = rayPos + rayDir * t;
                glm::ivec3 bPos = { (int)floor(p.x), (int)floor(p.y), (int)floor(p.z) };
                
                // 1. Controlla collisione con ologrammi se in attesa di approvazione
                if (m_aiAssistant.GetState() == AIState::WaitingForApproval) {
                    for (const auto& ghost : m_aiAssistant.GetPreviewBlocks()) {
                        if (ghost.pos == bPos) {
                            hitBlock = bPos;
                            hitGhost = true;
                            break;
                        }
                    }
                    if (hitGhost) break;
                }

                // 2. Controlla collisione col mondo
                if (m_world.IsInBounds(bPos.x, bPos.y, bPos.z) &&
                    m_world.GetBlock(bPos.x, bPos.y, bPos.z) != BlockType::Air) {
                    hitBlock = bPos;
                    break;
                }
                prevBlock = bPos;
            }

            m_hasTarget     = (hitBlock.x >= 0);
            m_targetedBlock = hitBlock;

            if (m_gameMode == GameMode::Dev) {
                bool worldChanged = false;
                
                if (breakBlock && hitGhost) {
                    // Rimuovi blocco dall'ologramma!
                    auto& preview = m_aiAssistant.GetPreviewBlocks();
                    preview.erase(std::remove_if(preview.begin(), preview.end(), [&](const GhostBlock& gb) {
                        return gb.pos == hitBlock;
                    }), preview.end());
                    m_aiAssistant.SetPreviewDirty();
                    std::cout << "[AI] Blocco fantasma rimosso in ("
                              << hitBlock.x << "," << hitBlock.y << "," << hitBlock.z << ")" << std::endl;
                } else if (placeBlock && hitGhost && prevBlock.x >= 0) {
                    const InventoryItem& activeItem = m_player.inventory.slots[m_selectedSlot];
                    if (!activeItem.IsEmpty() && activeItem.type == ItemType::Block) {
                        // Aggiungi un blocco fantasma! (NON consuma l'inventario per la preview)
                        m_aiAssistant.GetPreviewBlocks().push_back({ prevBlock, (BlockType)activeItem.blockType });
                        m_aiAssistant.SetPreviewDirty();
                        std::cout << "[AI] Blocco fantasma aggiunto in ("
                                  << prevBlock.x << "," << prevBlock.y << "," << prevBlock.z << ")" << std::endl;
                    }
                } else if (breakBlock && hitBlock.x >= 0 && !hitGhost) {
                    // DevMode: scavo ISTANTANEO (nessun timer)
                    BlockType brokenType = m_world.GetBlock(hitBlock.x, hitBlock.y, hitBlock.z);
                    if (brokenType != BlockType::Air) {
                        EventManager::Get().Dispatch(Event_BlockMined(hitBlock, brokenType));
                        worldChanged = true;
                    }
                } else if (placeBlock && hitBlock.x >= 0 && m_world.IsInBounds(prevBlock.x, prevBlock.y, prevBlock.z) && !hitGhost) {
                    const InventoryItem& activeItem = m_player.inventory.slots[m_selectedSlot];
                    if (!activeItem.IsEmpty() && activeItem.type == ItemType::Block) {
                        EventManager::Get().Dispatch(Event_BlockPlaced(prevBlock, (BlockType)activeItem.blockType));
                        worldChanged = true;
                        
                        // Consuma l'oggetto
                        m_player.inventory.RemoveItem(m_selectedSlot, 1);
                    }
                }

                if (worldChanged) {
                    // L'aggiornamento dei chunk sporchi avverra all'inizio del prossimo frame
                }
            } else {
                // ============================================================
                // PlayMode: SCAVO PROGRESSIVO basato sullo sforzo di taglio
                // τ = F / A — il tempo dipende da τ_yield del materiale
                // ============================================================
                
                if (holdingBreak && hitBlock.x >= 0 && !hitGhost) {
                    BlockType targetType = m_world.GetBlock(hitBlock.x, hitBlock.y, hitBlock.z);
                    const auto& mat = GetBlockMaterial(targetType);
                    
                    // Blocco indistruttibile? (miningTime < 0)
                    if (mat.miningTime < 0.0f) {
                        m_miningProgress = 0.0f;
                    } else {
                        // Se cambiamo bersaglio, resettiamo il progresso
                        if (m_miningTarget != hitBlock) {
                            m_miningTarget = hitBlock;
                            m_miningTimeRequired = mat.miningTime;
                            m_miningProgress = 0.0f;
                        }
                        
                        // Accumula progresso (dt / tempo_totale)
                        if (m_miningTimeRequired > 0.0f) {
                            m_miningProgress += deltaTime / m_miningTimeRequired;
                        } else {
                            m_miningProgress = 1.0f; // Scavo istantaneo (aria, acqua)
                        }
                        
                        // Blocco rotto!
                        if (m_miningProgress >= 1.0f) {
                            BlockType brokenType = m_world.GetBlock(hitBlock.x, hitBlock.y, hitBlock.z);
                            if (brokenType != BlockType::Air) {
                                EventManager::Get().Dispatch(Event_BlockMined(hitBlock, brokenType));
                            }
                            
                            // Reset mining state
                            m_miningProgress = 0.0f;
                            m_miningTarget = glm::ivec3(-1, -1, -1);
                        }
                    }
                } else {
                    // Rilasciato il tasto o nessun bersaglio: reset progresso
                    if (m_miningProgress > 0.0f) {
                        m_miningProgress = 0.0f;
                        m_miningTarget = glm::ivec3(-1, -1, -1);
                    }
                }
                
                // Piazzamento blocco (PlayMode — click sinistro singolo)
                if (placeBlock && hitBlock.x >= 0 && m_world.IsInBounds(prevBlock.x, prevBlock.y, prevBlock.z) && !hitGhost) {
                    const InventoryItem& activeItem = m_player.inventory.slots[m_selectedSlot];
                    if (!activeItem.IsEmpty() && activeItem.type == ItemType::Block) {
                        EventManager::Get().Dispatch(Event_BlockPlaced(prevBlock, (BlockType)activeItem.blockType));
                        m_player.inventory.RemoveItem(m_selectedSlot, 1);
                    }
                }
            }
        }
    } // Chiude if (!io.WantCaptureMouse)

    // ================================================================
    // --- UPDATE DROPPED ITEMS (Fisica di Newton + Archimede) ---
    // ================================================================
    {
        float g = m_world.GetCurrentPlanet()->gravity;
        
        for (auto& di : m_droppedItems) {
            if (!di.isAlive) continue;
            
            di.lifetime -= deltaTime;
            di.bobTimer += deltaTime;
            if (di.lifetime <= 0.0f) { di.isAlive = false; continue; }
            
            // 1. Gravita: F_g = m * g
            di.velocity.y -= g * deltaTime;
            
            // 2. Archimede: se in acqua e il materiale galleggia
            BlockType blockAtDrop = m_world.GetBlock((int)floor(di.position.x), (int)floor(di.position.y), (int)floor(di.position.z));
            if (blockAtDrop == BlockType::Water) {
                if (di.ShouldFloat()) {
                    // F_A = rho_water * g * V > F_g => spinta netta verso l'alto
                    di.velocity.y += g * 2.0f * deltaTime; // Galleggiamento forte
                } else {
                    // Affonda piu lentamente (attrito viscoso acqua)
                    di.velocity *= 0.95f;
                }
            }
            
            // 3. Attrito aria
            di.velocity.x *= (1.0f - 2.0f * deltaTime);
            di.velocity.z *= (1.0f - 2.0f * deltaTime);
            
            // 4. Collisione semplice col terreno
            glm::vec3 nextPos = di.position + di.velocity * deltaTime;
            BlockType below = m_world.GetBlock((int)floor(nextPos.x), (int)floor(nextPos.y - 0.2f), (int)floor(nextPos.z));
            if (below != BlockType::Air && below != BlockType::Water && below != BlockType::Lava) {
                di.velocity.y = 0.0f;
                nextPos.y = floor(nextPos.y - 0.2f) + 1.2f; // Appoggia sopra il blocco
            }
            di.position = nextPos;
            
            // 5. Pickup automatico: distanza < 1.5m dal giocatore
            float dist = glm::length(di.position - m_camera.Position);
            if (dist < 1.5f) {
                bool added = m_player.inventory.AddItem(di.item);
                if (added) {
                    di.isAlive = false;
                    std::cout << "[PICKUP] Raccolto " << di.item.count << "x blocco (ID " << di.item.blockType << ")" << std::endl;
                }
            } else if (dist < 3.0f) {
                // Magnetismo: attira verso il giocatore quando e vicino
                glm::vec3 toPlayer = glm::normalize(m_camera.Position - di.position);
                di.velocity += toPlayer * 5.0f * deltaTime;
            }
        }
        
        // Rimuovi items morti (garbage collection)
        m_droppedItems.erase(
            std::remove_if(m_droppedItems.begin(), m_droppedItems.end(),
                [](const DroppedItem& di) { return !di.isAlive; }),
            m_droppedItems.end()
        );
    }

    if (m_gameMode == GameMode::Play) {
        if (!m_player.stats.IsAlive()) {
            if (!m_justDied) {
                m_justDied = true;
                m_deathOverlayTimer = 3.0f; // Schermata rossa di morte per 3 secondi
                // Penalità di exp del 10%
                int expPenalty = m_player.stats.nextLevelExp * 0.1f;
                m_player.stats.currentExp = (std::max)(0, m_player.stats.currentExp - expPenalty);
                m_player.SaveToJson("assets/player.json");
            }
            
            m_deathOverlayTimer -= deltaTime;
            if (m_deathOverlayTimer <= 0.0f) {
                // Respawn al centro
                m_camera.Position = glm::vec3(0.0f, 30.0f, 0.0f);
                m_player.stats.currentHP = m_player.stats.GetMaxHP();
                m_player.stats.currentMP = m_player.stats.GetMaxMP();
                m_player.stats.currentStamina = m_player.stats.GetMaxStamina();
                m_justDied = false;
                std::cout << "[SYSTEM] Player rigenerato al punto di spawn." << std::endl;
            }
        } else {
            // (La logica di aggiornamento Mobs e Combattimento è stata spostata sopra, prima del consumo input)
        }
    }

    if (m_aiAssistant.IsPreviewDirty()) {
        m_world.BuildGhostMesh(m_aiAssistant.GetPreviewBlocks());
        m_renderManager->UploadGhostMesh(m_world.GetGhostVertices(), m_world.GetGhostIndices());
        m_aiAssistant.ClearPreviewDirty();
    }

    return true;
}

void FairWorldEngine::Render() {
    if (m_isVrMode) {
        // Logica VR con OpenXR (Fase 4)
        if (!m_xrManager->BeginFrame()) return;
        // m_renderManager->RenderStereo(m_xrManager.get()); // Lo faremo in seguito
        m_xrManager->EndFrame();
    } else {
        // FINALMENTE DISEGNAMO SU DESKTOP!
        
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // --- RENDER MENU & TAB ---
        switch (m_current) {
            case GameState::MAIN_MENU:
                renderMainMenu();
                break;
            case GameState::PAUSE_MENU:
                renderPauseMenu();
                break;
            default:
                // Gli stati TAB_* sono sub-stati del menu di pausa
                if (isEditorOpen()) renderPauseMenu();
                break;
        }

        if (m_current == GameState::PLAYING) {
            // 1. Disegna il Mirino 2D al centro dello schermo (con outline nero per visibilità)
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const float CH = 14.0f;  // metà lunghezza braccio
        const float GAP = 3.0f;  // gap centrale (stile Minecraft)
        // Outline nero (spessore +2)
        dl->AddLine(ImVec2(center.x - CH, center.y), ImVec2(center.x - GAP, center.y), IM_COL32(0,0,0,200), 4.0f);
        dl->AddLine(ImVec2(center.x + GAP, center.y), ImVec2(center.x + CH, center.y), IM_COL32(0,0,0,200), 4.0f);
        dl->AddLine(ImVec2(center.x, center.y - CH), ImVec2(center.x, center.y - GAP), IM_COL32(0,0,0,200), 4.0f);
        dl->AddLine(ImVec2(center.x, center.y + GAP), ImVec2(center.x, center.y + CH), IM_COL32(0,0,0,200), 4.0f);
        // Bianco interno
        dl->AddLine(ImVec2(center.x - CH, center.y), ImVec2(center.x - GAP, center.y), IM_COL32(255,255,255,255), 2.0f);
        dl->AddLine(ImVec2(center.x + GAP, center.y), ImVec2(center.x + CH, center.y), IM_COL32(255,255,255,255), 2.0f);
        dl->AddLine(ImVec2(center.x, center.y - CH), ImVec2(center.x, center.y - GAP), IM_COL32(255,255,255,255), 2.0f);
        dl->AddLine(ImVec2(center.x, center.y + GAP), ImVec2(center.x, center.y + CH), IM_COL32(255,255,255,255), 2.0f);

        // Punto centrale
        dl->AddCircleFilled(center, 2.0f, IM_COL32(0,0,0,200));
        dl->AddCircleFilled(center, 1.0f, IM_COL32(255,255,255,255));

        // Info blocco puntato (sopra il mirino)
        if (m_hasTarget && m_gameMode == GameMode::Dev) {
            char blockInfo[64];
            snprintf(blockInfo, sizeof(blockInfo), "[%d, %d, %d]", m_targetedBlock.x, m_targetedBlock.y, m_targetedBlock.z);
            ImVec2 textSize = ImGui::CalcTextSize(blockInfo);
            ImVec2 textPos = ImVec2(center.x - textSize.x * 0.5f, center.y - 40.0f);
            dl->AddRectFilled(ImVec2(textPos.x - 4, textPos.y - 2), ImVec2(textPos.x + textSize.x + 4, textPos.y + textSize.y + 2), IM_COL32(0,0,0,150), 3.0f);
            dl->AddText(textPos, IM_COL32(255,255,100,255), blockInfo);
        }

        // --- BARRA DI PROGRESSO MINING (PlayMode) ---
        if (m_miningProgress > 0.0f && m_miningProgress < 1.0f && m_gameMode == GameMode::Play) {
            float barW = 120.0f;
            float barH = 8.0f;
            ImVec2 barPos = ImVec2(center.x - barW * 0.5f, center.y + 25.0f);
            
            // Sfondo barra
            dl->AddRectFilled(barPos, ImVec2(barPos.x + barW, barPos.y + barH), IM_COL32(0, 0, 0, 180), 3.0f);
            // Progresso (arancione → verde)
            float r = 1.0f - m_miningProgress;
            float g = m_miningProgress;
            dl->AddRectFilled(barPos, ImVec2(barPos.x + barW * m_miningProgress, barPos.y + barH), 
                IM_COL32((int)(r*255), (int)(g*255), 50, 255), 3.0f);
            // Bordo
            dl->AddRect(barPos, ImVec2(barPos.x + barW, barPos.y + barH), IM_COL32(200, 200, 200, 200), 3.0f);
            
            // Percentuale
            char pctText[16];
            snprintf(pctText, sizeof(pctText), "%d%%", (int)(m_miningProgress * 100.0f));
            ImVec2 pctSize = ImGui::CalcTextSize(pctText);
            dl->AddText(ImVec2(center.x - pctSize.x * 0.5f, barPos.y + barH + 2.0f), IM_COL32(255, 255, 255, 255), pctText);
        }

        // --- INFO MATERIALE blocco puntato (PlayMode) ---
        if (m_hasTarget && m_gameMode == GameMode::Play) {
            BlockType targetType = m_world.GetBlock(m_targetedBlock.x, m_targetedBlock.y, m_targetedBlock.z);
            if (targetType != BlockType::Air) {
                const auto& mat = GetBlockMaterial(targetType);
                char matInfo[128];
                snprintf(matInfo, sizeof(matInfo), "%s | %.0f kg/m3 | %.0f J/kg*K | t=%.1fs", 
                    mat.name, mat.density, mat.heatCapacitySp, mat.miningTime);
                ImVec2 matSize = ImGui::CalcTextSize(matInfo);
                ImVec2 matPos = ImVec2(center.x - matSize.x * 0.5f, center.y - 40.0f);
                dl->AddRectFilled(ImVec2(matPos.x - 4, matPos.y - 2), 
                    ImVec2(matPos.x + matSize.x + 4, matPos.y + matSize.y + 2), IM_COL32(0,0,0,150), 3.0f);
                dl->AddText(matPos, IM_COL32(180, 220, 255, 255), matInfo);
            }
        }

        // --- RENDER DROPPED ITEMS (Tags 2D Proiettati) ---
        if (!m_droppedItems.empty()) {
            glm::mat4 view = m_camera.GetViewMatrix();
            float aspect = ImGui::GetMainViewport()->Size.x / ImGui::GetMainViewport()->Size.y;
            glm::mat4 proj = glm::perspective(glm::radians(m_renderManager->GetFov()), aspect, 0.1f, 100.0f);
            
            // Per Vulkan, l'asse Y di proiezione è invertito rispetto a OpenGL, 
            // ma ImGui ha (0,0) in alto a sinistra. Dobbiamo stare attenti.
            for (const auto& di : m_droppedItems) {
                if (!di.isAlive) continue;
                
                // Posizione con animazione di fluttuazione (sin)
                glm::vec3 renderPos = di.position + glm::vec3(0.0f, sin(di.bobTimer * 2.0f) * 0.1f, 0.0f);
                glm::vec4 clipPos = proj * view * glm::vec4(renderPos, 1.0f);
                
                if (clipPos.w > 0.1f) {
                    glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
                    if (ndc.z >= 0.0f && ndc.z <= 1.0f && ndc.x >= -1.0f && ndc.x <= 1.0f && ndc.y >= -1.0f && ndc.y <= 1.0f) {
                        float screenX = (ndc.x * 0.5f + 0.5f) * ImGui::GetMainViewport()->Size.x;
                        float screenY = (ndc.y * 0.5f + 0.5f) * ImGui::GetMainViewport()->Size.y;
                        
                        const auto& mat = GetBlockMaterial((BlockType)di.item.blockType);
                        char dropText[64];
                        snprintf(dropText, sizeof(dropText), "%s (x%d)", mat.name, di.item.count);
                        ImVec2 textSize = ImGui::CalcTextSize(dropText);
                        ImVec2 textPos(screenX - textSize.x * 0.5f, screenY - textSize.y * 0.5f);
                        
                        dl->AddRectFilled(ImVec2(textPos.x - 4, textPos.y - 2), 
                                          ImVec2(textPos.x + textSize.x + 4, textPos.y + textSize.y + 2), IM_COL32(0, 0, 0, 180), 3.0f);
                        dl->AddText(textPos, IM_COL32(150, 255, 150, 255), dropText);
                    }
                }
            }
        }

        // 2. Disegna la Hotbar in basso se l'inventario non è aperto
        if (!m_isInventoryOpen) {
            float hotbarWidth = 450.0f;
            
            // --- Nome del blocco selezionato ---
            std::string activeName = GetSlotName(m_selectedSlot);
            ImVec2 nameSize = ImGui::CalcTextSize(activeName.c_str());
            ImVec2 textPos = ImVec2(center.x - nameSize.x / 2.0f, ImGui::GetMainViewport()->Size.y - 100);
            dl->AddRectFilled(ImVec2(textPos.x - 6, textPos.y - 2), ImVec2(textPos.x + nameSize.x + 6, textPos.y + nameSize.y + 2), IM_COL32(0,0,0,150), 4.0f);
            dl->AddText(textPos, IM_COL32(255,255,255,255), activeName.c_str());

            ImGui::SetNextWindowPos(ImVec2(center.x - hotbarWidth/2, ImGui::GetMainViewport()->Size.y - 70), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(hotbarWidth, 60), ImGuiCond_Always);
            ImGui::Begin("Hotbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav);
            
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(center.x - hotbarWidth/2, ImGui::GetMainViewport()->Size.y - 70),
                ImVec2(center.x + hotbarWidth/2, ImGui::GetMainViewport()->Size.y - 10),
                IM_COL32(0, 0, 0, 150), 5.0f
            );

            ImGui::SetCursorPos(ImVec2(10, 15));
            for (int i = 0; i < Inventory::HOTBAR_SIZE; i++) {
                if (i > 0) ImGui::SameLine(0, 5);
                const auto& item = m_player.inventory.slots[i];
                
                // Highlight selected
                if (i == m_selectedSlot) {
                    ImGui::GetWindowDrawList()->AddRect(
                        ImGui::GetCursorScreenPos(), 
                        ImVec2(ImGui::GetCursorScreenPos().x + 38, ImGui::GetCursorScreenPos().y + 38), 
                        IM_COL32(255, 255, 0, 255), 2.0f, 0, 2.0f);
                }

                ImGui::PushID(i);
                ImVec2 curPos = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##hbslot", ImVec2(38, 38));
                
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRect(curPos, ImVec2(curPos.x + 38, curPos.y + 38), IM_COL32(100, 100, 100, 255));
                
                if (!item.IsEmpty()) {
                    std::string itemName = GetSlotName(i);
                    std::string initial = itemName.empty() ? "?" : itemName.substr(0, 3);
                    dl->AddText(ImVec2(curPos.x + 2, curPos.y + 2), IM_COL32(100, 200, 255, 255), initial.c_str());

                    std::string countStr = std::to_string(item.count);
                    ImVec2 textSize = ImGui::CalcTextSize(countStr.c_str());
                    dl->AddText(ImVec2(curPos.x + 38 - textSize.x - 4, curPos.y + 38 - textSize.y - 4), IM_COL32(255, 255, 255, 255), countStr.c_str());
                }
                ImGui::PopID();
                
                if (ImGui::IsItemHovered() && !item.IsEmpty()) {
                    ImGui::SetTooltip("%s (x%d)", GetSlotName(i).c_str(), item.count);
                }
            }
            ImGui::End();
        } else {
            // --- INVENTARIO COMPLETO (Aperto) ---
            ImGui::SetNextWindowPos(center, ImGuiCond_Once, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(500, 350), ImGuiCond_Once);
            if (ImGui::Begin("Inventario Personale", &m_isInventoryOpen, ImGuiWindowFlags_NoCollapse)) {
                
                auto drawSlot = [&](int i, const char* labelPrefix) {
                    const auto& item = m_player.inventory.slots[i];
                    
                    ImGui::PushID(i);
                    ImVec2 curPos = ImGui::GetCursorScreenPos();
                    ImGui::InvisibleButton("##slot", ImVec2(40, 40));
                    
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    // Bordo dello slot
                    dl->AddRect(curPos, ImVec2(curPos.x + 40, curPos.y + 40), IM_COL32(100, 100, 100, 255));
                    
                    if (!item.IsEmpty()) {
                        std::string itemName = GetSlotName(i);
                        std::string initial = itemName.empty() ? "?" : itemName.substr(0, 3);
                        dl->AddText(ImVec2(curPos.x + 2, curPos.y + 2), IM_COL32(100, 200, 255, 255), initial.c_str());

                        std::string countStr = std::to_string(item.count);
                        ImVec2 textSize = ImGui::CalcTextSize(countStr.c_str());
                        dl->AddText(ImVec2(curPos.x + 40 - textSize.x - 4, curPos.y + 40 - textSize.y - 4), IM_COL32(255, 255, 255, 255), countStr.c_str());
                    }

                    // Drag source
                    if (!item.IsEmpty() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        ImGui::SetDragDropPayload("INVENTORY_SLOT", &i, sizeof(int));
                        ImGui::Text("Sposta oggetto");
                        ImGui::EndDragDropSource();
                    }

                    // Drop target
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INVENTORY_SLOT")) {
                            int sourceIdx = *(const int*)payload->Data;
                            
                            if (ImGui::GetIO().KeyShift) {
                                // Sposta 1 solo oggetto
                                m_player.inventory.MovePartial(sourceIdx, i, 1);
                            } else if (ImGui::GetIO().KeyCtrl) {
                                // Sposta metà stack
                                int half = m_player.inventory.slots[sourceIdx].count / 2;
                                if (half < 1) half = 1;
                                m_player.inventory.MovePartial(sourceIdx, i, half);
                            } else {
                                // Spostamento normale (o swap / merge completo)
                                m_player.inventory.SwapSlots(sourceIdx, i);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    
                    if (ImGui::IsItemHovered() && !item.IsEmpty()) {
                        ImGui::SetTooltip("%s (x%d)\nDrag: Sposta/Unisci\nShift+Drag: Sposta 1\nCtrl+Drag: Sposta Metà\nClick Destro: Dividi a metà in nuovo slot\nClick Centrale: Prendi 1 in nuovo slot", GetSlotName(i).c_str(), item.count);
                        
                        // Click destro per splittare a metà al volo (senza drag&drop)
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                            int half = item.count / 2;
                            if (half > 0) {
                                for (int empty = 0; empty < Inventory::INVENTORY_SIZE; empty++) {
                                    if (m_player.inventory.slots[empty].IsEmpty()) {
                                        m_player.inventory.MovePartial(i, empty, half);
                                        break;
                                    }
                                }
                            }
                        }
                        // Click centrale (rotellina) per prendere 1 singolo pezzo
                        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
                            for (int empty = 0; empty < Inventory::INVENTORY_SIZE; empty++) {
                                if (m_player.inventory.slots[empty].IsEmpty()) {
                                    m_player.inventory.MovePartial(i, empty, 1);
                                    break;
                                }
                            }
                        }
                    }
                    ImGui::PopID();
                };

                ImGui::Text("Zaino:");
                ImGui::SameLine(ImGui::GetWindowWidth() - 170);
                if (ImGui::Button("Ordina", ImVec2(70, 25))) {
                    m_player.inventory.Sort();
                }
                ImGui::SameLine();
                ImGui::BeginChild("TrashBin", ImVec2(70, 25), true, ImGuiWindowFlags_NoScrollbar);
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Cestino");
                ImGui::EndChild();
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INVENTORY_SLOT")) {
                        int sourceIdx = *(const int*)payload->Data;
                        m_player.inventory.ClearSlot(sourceIdx);
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::Separator();
                // Disegna storage (10 a 39)
                for (int r = 0; r < 3; r++) {
                    for (int c = 0; c < 10; c++) {
                        int slotIdx = 10 + r * 10 + c;
                        drawSlot(slotIdx, "INV");
                        if (c < 9) ImGui::SameLine();
                    }
                }
                
                ImGui::Spacing(); ImGui::Spacing();
                ImGui::Text("Hotbar:");
                ImGui::Separator();
                // Disegna hotbar (0 a 9)
                for (int c = 0; c < 10; c++) {
                    drawSlot(c, "HOT");
                    if (c < 9) ImGui::SameLine();
                }
            }
            ImGui::End();
        }

        // 3. Menu grafico di selezione per il Blocco Custom (Slot 4 - tasto 4)
        if (m_selectedSlot == 3 && m_showCustomBlockMenu && m_gameMode == GameMode::Dev) {
            ImGui::SetNextWindowPos(ImVec2(center.x - 160, center.y - 120), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(320, 150), ImGuiCond_Always);
            
            // Stile premium
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.25f, 0.30f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.15f, 0.15f, 0.18f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.20f, 0.45f, 0.85f, 1.00f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));

            if (ImGui::Begin("Blocco Custom (Slot 4)", &m_showCustomBlockMenu, 
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
                
                ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "Scegli una texture personalizzata:");
                ImGui::Separator();
                ImGui::Spacing();
                
                static int activePreset = 1;
                
                // Disegniamo 5 pulsanti affiancati
                for (int i = 1; i <= 5; i++) {
                    std::string path = "assets/textures/custom" + std::to_string(i) + ".png";
                    
                    bool exists = false;
                    std::ifstream f(path);
                    if (f.good()) exists = true;
                    f.close();
                    
                    char btnLabel[16];
                    snprintf(btnLabel, sizeof(btnLabel), "Slot %d", i);
                    
                    bool isSelected = (activePreset == i);
                    
                    // Colori pulsante dinamici
                    if (isSelected) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f)); // Attivo
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.50f, 0.90f, 1.0f));
                    } else if (!exists) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 0.5f)); // Vuoto
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.22f, 0.22f, 0.8f));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.22f, 0.25f, 0.8f)); // Esistente non selezionato
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.30f, 0.35f, 1.0f));
                    }
                    
                    if (ImGui::Button(btnLabel, ImVec2(52, 40))) {
                        activePreset = i;
                        m_renderManager->LoadTextureFromFile(path, 4);
                    }
                    ImGui::PopStyleColor(2);
                    
                    // Aggiunge un tooltip per mostrare se lo slot è occupato o vuoto
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        if (exists) {
                            ImGui::Text("File: custom%d.png", i);
                            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Pronta all'uso");
                        } else {
                            ImGui::Text("File: custom%d.png", i);
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Nessun disegno salvato");
                        }
                        ImGui::EndTooltip();
                    }

                    if (i < 5) ImGui::SameLine();
                }
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Premi ESC per sbloccare mouse");
            }
            ImGui::End();
            
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(4);
        }

        // --- HUD E OVERLAYS DI GIOCO ---
        
        if (m_gameMode == GameMode::Play) {
            // Disegna l'HUD del Player (HP, MP, Stamina, EXP) in basso a sinistra
            ImGui::SetNextWindowPos(ImVec2(10, ImGui::GetMainViewport()->Size.y - 120), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(350, 110), ImGuiCond_Always);
            ImGui::Begin("Player HUD", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);
        
        // Barre colorate
        float hpPercent = m_player.stats.GetHPPercent();
        float mpPercent = m_player.stats.GetMPPercent();
        float staminaPercent = m_player.stats.GetMaxStamina() > 0 ? (float)m_player.stats.currentStamina / m_player.stats.GetMaxStamina() : 0.0f;
        float expPercent = m_player.stats.GetExpPercent();

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.8f, 0.15f, 0.15f, 0.9f));
        ImGui::ProgressBar(hpPercent, ImVec2(220, 16), "");
        ImGui::SameLine(); ImGui::Text("HP %d/%d", m_player.stats.currentHP, m_player.stats.GetMaxHP());
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.15f, 0.35f, 0.8f, 0.9f));
        ImGui::ProgressBar(mpPercent, ImVec2(220, 16), "");
        ImGui::SameLine(); ImGui::Text("MP %d/%d", m_player.stats.currentMP, m_player.stats.GetMaxMP());
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.15f, 0.7f, 0.2f, 0.9f));
        ImGui::ProgressBar(staminaPercent, ImVec2(220, 16), "");
        ImGui::SameLine(); ImGui::Text("STA %d/%d", m_player.stats.currentStamina, m_player.stats.GetMaxStamina());
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.6f, 0.2f, 0.8f, 0.9f));
        ImGui::ProgressBar(expPercent, ImVec2(220, 12), "");
        ImGui::SameLine(); ImGui::Text("EXP %d%% (Lv.%d)", (int)(expPercent*100), m_player.stats.level);
        ImGui::PopStyleColor();
        ImGui::End();
        }

        // Disegna l'overlay del Level Up
        if (m_levelUpTimer > 0.0f) {
            ImVec2 size = ImGui::GetMainViewport()->Size;
            ImGui::SetNextWindowPos(ImVec2(size.x * 0.5f - 150, size.y * 0.3f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(300, 100), ImGuiCond_Always);
            ImGui::Begin("LevelUpOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
            
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetWindowPos();
            dl->AddRectFilled(pos, ImVec2(pos.x + 300, pos.y + 100), IM_COL32(20, 20, 20, 230), 10.0f);
            dl->AddRect(pos, ImVec2(pos.x + 300, pos.y + 100), IM_COL32(255, 215, 0, 255), 10.0f, 0, 3.0f); // Bordo oro
            
            ImGui::SetCursorPos(ImVec2(10, 15));
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), "      LEVEL UP!");
            ImGui::Text("    Sei ora al livello %d!", m_levelUpNewLevel);
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "   Hai %d punti da spendere!", m_levelUpPoints);
            
            ImGui::End();
        }

        // Disegna l'overlay di Morte (schermata rossa)
        if (m_justDied && m_deathOverlayTimer > 0.0f) {
            ImVec2 size = ImGui::GetMainViewport()->Size;
            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(size, ImGuiCond_Always);
            ImGui::Begin("DeathOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
            
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(ImVec2(0, 0), size, IM_COL32(180, 0, 0, 100)); // Flash rosso trasparente
            
            ImGui::SetCursorPos(ImVec2(size.x * 0.5f - 100, size.y * 0.5f - 20));
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "       SEI MORTO!");
            ImGui::Text(" Rigenerazione tra %.1f secondi...", m_deathOverlayTimer);
            
            ImGui::End();
        }

        // --- VISUAL THERMAL CUES ---
        if (m_gameMode == GameMode::Play && !m_justDied) {
            glm::vec3 pos = m_camera.Position;
            float temp = m_world.GetTemperatureAt((int)std::floor(pos.x), (int)std::floor(pos.z));
            
            if (temp < 273.15f || temp > 323.15f) {
                ImVec2 size = ImGui::GetMainViewport()->Size;
                ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
                ImGui::SetNextWindowSize(size, ImGuiCond_Always);
                ImGui::Begin("ThermalOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                
                ImU32 outerCol;
                if (temp < 273.15f) {
                    float intensity = std::clamp((273.15f - temp) / 20.0f, 0.0f, 0.8f);
                    outerCol = IM_COL32(100, 200, 255, (int)(intensity * 150));
                } else {
                    float intensity = std::clamp((temp - 323.15f) / 100.0f, 0.0f, 0.8f);
                    outerCol = IM_COL32(255, 80, 0, (int)(intensity * 150));
                }
                ImU32 innerCol = IM_COL32(0,0,0,0);
                
                float b = 150.0f; // Border size
                // Top
                dl->AddRectFilledMultiColor(ImVec2(0,0), ImVec2(size.x, b), outerCol, outerCol, innerCol, innerCol);
                // Bottom
                dl->AddRectFilledMultiColor(ImVec2(0,size.y-b), ImVec2(size.x, size.y), innerCol, innerCol, outerCol, outerCol);
                // Left
                dl->AddRectFilledMultiColor(ImVec2(0,b), ImVec2(b, size.y-b), outerCol, innerCol, innerCol, outerCol);
                // Right
                dl->AddRectFilledMultiColor(ImVec2(size.x-b,b), ImVec2(size.x, size.y-b), innerCol, outerCol, outerCol, innerCol);
                
                ImGui::End();
            }
        }

        } // Fine blocco PLAYING

        // --- RENDER DEI MOB IN OVERLAY 2D (Finché Vulkan non supporta i modelli 3D) ---
        {
            ImVec2 screenSize = ImGui::GetMainViewport()->Size;
            glm::mat4 view = m_camera.GetViewMatrix();
            float aspect = 1.0f;
            if (screenSize.y > 0.001f && screenSize.x > 0.001f) {
                aspect = screenSize.x / screenSize.y;
            }
            glm::mat4 proj = glm::perspective(glm::radians(m_renderManager->GetFov()), aspect, 0.1f, 1000.0f);
            
            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(screenSize, ImGuiCond_Always);
            ImGui::Begin("MobOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            
            for (const auto& mob : m_mobManager.instances) {
                if (!mob.isAlive) continue;
                
                // Posizione base (piedi) e altezza approssimativa
                glm::vec3 headPos = mob.position + glm::vec3(0.0f, 1.5f, 0.0f);
                glm::vec3 feetPos = mob.position;
                
                auto WorldToScreen = [&](glm::vec3 pos, ImVec2& out) -> bool {
                    glm::vec4 clip = proj * view * glm::vec4(pos, 1.0f);
                    if (clip.w < 0.1f) return false;
                    glm::vec3 ndc = glm::vec3(clip) / clip.w;
                    out.x = (ndc.x + 1.0f) * 0.5f * screenSize.x;
                    out.y = (1.0f - ndc.y) * 0.5f * screenSize.y;
                    return true;
                };
                
                ImVec2 sHead, sFeet;
                if (WorldToScreen(headPos, sHead) && WorldToScreen(feetPos, sFeet)) {
                    // Disegna bounding box rosso attorno al mob
                    float height = sFeet.y - sHead.y;
                    float width = height * 0.6f;
                    ImVec2 pMin(sHead.x - width * 0.5f, sHead.y);
                    ImVec2 pMax(sHead.x + width * 0.5f, sFeet.y);
                    
                    // Colore box base
                    dl->AddRect(pMin, pMax, IM_COL32(255, 50, 50, 200), 0.0f, 0, 2.0f);
                    dl->AddRectFilled(pMin, pMax, IM_COL32(255, 50, 50, 50));
                    
                    // Nome mob sopra la testa
                    std::string label = mob.displayName + " (Lv." + std::to_string(mob.stats.level) + ")";
                    ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
                    dl->AddText(ImVec2(sHead.x - textSize.x * 0.5f, sHead.y - 25.0f), IM_COL32(255, 255, 255, 255), label.c_str());
                    
                    // Barra degli HP
                    float hpPercent = (float)mob.stats.currentHP / mob.stats.GetMaxHP();
                    ImVec2 hpBgMin(sHead.x - width * 0.5f, sHead.y - 8.0f);
                    ImVec2 hpBgMax(sHead.x + width * 0.5f, sHead.y - 3.0f);
                    ImVec2 hpFgMax(sHead.x - width * 0.5f + width * hpPercent, sHead.y - 3.0f);
                    
                    dl->AddRectFilled(hpBgMin, hpBgMax, IM_COL32(0, 0, 0, 255));
                    dl->AddRectFilled(hpBgMin, hpFgMax, IM_COL32(50, 255, 50, 255));
                }
            }
            ImGui::End();
        }

        if (m_current == GameState::PLAYING && m_gameMode == GameMode::Dev) {
            m_editor.Draw(m_assets, m_world, m_renderManager.get(), &m_mobManager, &m_player, m_camera);
        }
        
        // --- DIARIO MAGICO (AI ASSISTANT) ---
        if (m_isDiaryOpen) {
            ImVec2 screenSize = ImGui::GetMainViewport()->Size;
            
            // Centriamo il diario sullo schermo, stile libro largo
            ImVec2 windowSize(800, 600);
            ImGui::SetNextWindowPos(ImVec2((screenSize.x - windowSize.x) * 0.5f, (screenSize.y - windowSize.y) * 0.5f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
            
            // Colore di sfondo "Pelle/Pergamena"
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.85f, 0.76f, 0.6f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f, 0.1f, 0.1f, 1.0f)); // Testo scuro
            ImGui::Begin("Il Diario del Creatore", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);

            ImGui::Columns(2, "book_columns", false);
            
            // Pagin Sinistra: Istruzioni e Log
            ImGui::Text("--- COME FUNZIONA ---");
            ImGui::TextWrapped("1. Scrivi le tue istruzioni nella pagina a destra.");
            ImGui::TextWrapped("2. Usa parole chiave come 'crea casa', 'crea torre', 'spawn zombie'.");
            ImGui::TextWrapped("3. Clicca su 'Chiedi allo Spirito'.");
            ImGui::TextWrapped("4. L'AI genererà un ologramma. Controllalo e clicca 'si' per confermare.");
            
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            
            ImGui::Text("--- MEMORIE DELLO SPIRITO ---");
            ImGui::BeginChild("DiaryHistory", ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            for (const auto& msg : m_diaryHistory) {
                if (msg.find("[Sistema]") != std::string::npos) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.5f, 1.0f));
                    ImGui::TextWrapped("%s", msg.c_str());
                    ImGui::PopStyleColor();
                } else {
                    ImGui::TextWrapped("%s", msg.c_str());
                }
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();

            ImGui::NextColumn();

            // Pagina Destra: Input testo multi-linea
            ImGui::Text("--- I TUOI APPUNTI ---");
            if (m_diaryFocusRequested) {
                ImGui::SetKeyboardFocusHere();
                m_diaryFocusRequested = false;
            }

            ImGui::InputTextMultiline("##DiaryInput", m_diaryInput, sizeof(m_diaryInput), ImVec2(-FLT_MIN, -50), ImGuiInputTextFlags_AllowTabInput);
            
            if (ImGui::Button("Chiedi allo Spirito", ImVec2(-FLT_MIN, 40))) {
                std::string inputStr = m_diaryInput;
                if (!inputStr.empty()) {
                    m_diaryHistory.push_back("[Tu]: (Evocazione)");
                    
                    // L'input può essere multilinea, lo passiamo all'AIAssistant
                    std::string response = m_aiAssistant.ProcessPlayerMessage(inputStr, m_world, m_mobManager, m_player, m_camera.Position, m_camera.Front);
                    m_diaryHistory.push_back(response);
                    
                    // NB: Non cancelliamo l'input del diario, così funge da veri appunti che restano scritti!
                }
            }

            ImGui::Columns(1);
            
            ImGui::SetCursorPosY(windowSize.y - 30);
            ImGui::Text("Premi ESC per chiudere il diario.");
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                m_isDiaryOpen = false;
                m_cursorLocked = true; // Ri-blocca il cursore
            }

            ImGui::End();
            ImGui::PopStyleColor(2); // Ripristina colori
        }

        // --- BROWSER WEB INTEGRATO (WebView2) ---
        if (m_current == GameState::WEB_BROWSER) {
            ImVec2 size = ImGui::GetMainViewport()->Size;
            float winW = size.x * 0.90f;
            float winH = size.y * 0.90f;
            ImGui::SetNextWindowPos(ImVec2(size.x * 0.5f, size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);

            bool keepOpen = true;
            if (ImGui::Begin("Browser Web Integrato - Premi 'H' per chiudere", &keepOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                
                // --- BARRA DEGLI INDIRIZZI ---
                static char urlBuffer[512] = "https://www.google.com";
                ImGui::SetNextItemWidth(winW - 100.0f);
                if (ImGui::InputText("##URLBar", urlBuffer, sizeof(urlBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                    std::string urlStr(urlBuffer);
                    std::wstring wUrl(urlStr.begin(), urlStr.end());
                    m_webView.Navigate(wUrl);
                }
                ImGui::SameLine();
                if (ImGui::Button("Vai", ImVec2(70, 0))) {
                    std::string urlStr(urlBuffer);
                    std::wstring wUrl(urlStr.begin(), urlStr.end());
                    m_webView.Navigate(wUrl);
                }
                ImGui::Separator();
                
                // Calcola le coordinate assolute sullo schermo dello spazio per i contenuti HTML
                ImVec2 contentPos = ImGui::GetCursorScreenPos();
                ImVec2 contentSize = ImGui::GetContentRegionAvail();
                
                // Comunica al wrapper C++ di Edge di sovrapporsi esattamente qui
                m_webView.Resize((int)contentPos.x, (int)contentPos.y, (int)contentSize.x, (int)contentSize.y);
            }
            ImGui::End();
            
            // Se l'utente chiude dalla "X" della finestra
            if (!keepOpen) {
                transitionTo(GameState::PLAYING);
            }
        }

        ImGui::Render();

        m_renderManager->RenderDesktop(m_camera.GetViewMatrix(), m_world.GetSkyColor(), &m_assets, &m_mobManager, &m_player);
    }
}

void FairWorldEngine::renderMainMenu() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_Always);
    ImGui::Begin("FAIRWORLD", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::SetCursorPosX((300 - ImGui::CalcTextSize("FAIRWORLD").x) * 0.5f);
    ImGui::Text("FAIRWORLD");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Nuova partita", ImVec2(-1, 40)))
        transitionTo(GameState::PLAYING);

    if (ImGui::Button("Continua", ImVec2(-1, 40)))
        transitionTo(GameState::PLAYING);

    ImGui::Spacing();
    if (ImGui::Button("Esci", ImVec2(-1, 30))) {
        m_isRunning = false;
    }
    ImGui::End();
}

void FairWorldEngine::renderPauseMenu() {
    ImGuiIO& io = ImGui::GetIO();

    // Finestra grande centrata che occupa la maggior parte dello schermo
    float winW = io.DisplaySize.x * 0.80f;
    float winH = io.DisplaySize.y * 0.85f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);

    ImGui::Begin("FAIRWORLD  -  Menu", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    // ---- Riga superiore: pulsanti azione ----
    float btnW = 160.0f;
    float btnH = 36.0f;
    float totalBtns = btnW * 3 + ImGui::GetStyle().ItemSpacing.x * 2;
    ImGui::SetCursorPosX((winW - totalBtns) * 0.5f);

    if (ImGui::Button("  Riprendi  (ESC)", ImVec2(btnW, btnH)))
        transitionTo(GameState::PLAYING);

    ImGui::SameLine();
    if (ImGui::Button("  Menu Principale", ImVec2(btnW, btnH)))
        transitionTo(GameState::MAIN_MENU);

    ImGui::SameLine();
    if (ImGui::Button("  Esci dal gioco", ImVec2(btnW, btnH)))
        m_isRunning = false;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---- Parte inferiore: le vere tab dell'editor ----
    m_editor.Draw(m_assets, m_world, m_renderManager.get(), &m_mobManager, &m_player, m_camera);

    ImGui::End();
}


void FairWorldEngine::renderEditorTabs() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - 20, io.DisplaySize.y - 20), ImGuiCond_Always);

    ImGui::Begin("Dev Tools  -  [TAB] per chiudere  |  [ESC] per tornare al gioco", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    if (ImGui::BeginTabBar("EditorTabBar")) {
        for (int i = 0; i < kTabCount; ++i) {
            GameState tabState = kTabStates[i];
            ImGuiTabItemFlags flags = (m_current == tabState) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;

            if (ImGui::BeginTabItem(kTabLabels[i], nullptr, flags)) {
                if (m_current != tabState) transitionTo(tabState);
                
                switch (tabState) {
                    case GameState::TAB_BLOCCHI:          renderTab_Blocchi();         break;
                    case GameState::TAB_MOB:              renderTab_Mob();             break;
                    case GameState::TAB_PLAYER:           renderTab_Player();          break;
                    case GameState::TAB_TEXTURE_PAINTER:  renderTab_TexturePainter();  break;
                    case GameState::TAB_MODEL_SCULPTOR:   renderTab_ModelSculptor();   break;
                    case GameState::TAB_MODEL_EDITOR:     renderTab_ModelEditor();     break;
                    case GameState::TAB_MONDO:            renderTab_Mondo();           break;
                    case GameState::TAB_ENGINE:           renderTab_Engine();          break;
                    default: break;
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void FairWorldEngine::renderTab_Blocchi() {
    ImGui::Text("Palette blocchi");
    ImGui::Separator();
    ImGui::TextDisabled("(nessun blocco caricato)");
}

void FairWorldEngine::renderTab_Mob() {
    ImGui::Text("Gestione Mob");
    ImGui::Separator();
    ImGui::TextDisabled("(nessun mob caricato)");
}

void FairWorldEngine::renderTab_Player() {
    ImGui::Text("Dati Player");
    ImGui::Separator();
    ImGui::TextDisabled("(player non inizializzato)");
}

void FairWorldEngine::renderTab_TexturePainter() {
    ImGui::Text("Texture Painter");
    ImGui::Separator();
    if (ImGui::Button("Ricarica Texture (Refresh)")) {}
}

void FairWorldEngine::renderTab_ModelSculptor() {
    ImGui::Text("Model Sculptor");
    ImGui::Separator();
    ImGui::TextDisabled("(nessun modello attivo)");
}

void FairWorldEngine::renderTab_ModelEditor() {
    ImGui::Text("Model Editor");
    ImGui::Separator();
    ImGui::TextDisabled("(nessun modello attivo)");
}

void FairWorldEngine::renderTab_Mondo() {
    ImGui::Text("World Editor");
    ImGui::Separator();
    static int seedValue = 12345;
    ImGui::InputInt("Seed", &seedValue);
    if (ImGui::Button("Rigenera Chunk attorno al player")) {}
}

void FairWorldEngine::renderTab_Engine() {
    ImGui::Text("Engine Debug");
    ImGui::Separator();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("FPS: %.1f (%.3f ms/frame)", io.Framerate, 1000.0f / io.Framerate);
    ImGui::Text("Stato corrente: %s", getStateName());
    
    ImGui::Spacing();
    ImGui::Text("Transizioni manuali:");
    if (ImGui::Button("-> PLAYING"))    transitionTo(GameState::PLAYING);
    ImGui::SameLine();
    if (ImGui::Button("-> MAIN MENU"))  transitionTo(GameState::MAIN_MENU);
    ImGui::SameLine();
    if (ImGui::Button("-> PAUSA"))      transitionTo(GameState::PAUSE_MENU);
}

void FairWorldEngine::Shutdown() {
    if (m_isVrMode) {
        m_xrManager->Shutdown();
    }
    m_windowManager->Shutdown();
    m_renderManager->Shutdown();
    m_isRunning = false;
}
