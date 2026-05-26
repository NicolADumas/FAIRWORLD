#include "pch.h"
#include "FAIRWORLD.h"
#include "XrManager.h"
#include "RenderManager.h"
#include "WindowManager.h"
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

FairWorldEngine::FairWorldEngine() 
    : m_isRunning(false), m_isVrMode(false),
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
    return true;
}

void FairWorldEngine::Run() {
    auto lastTime = std::chrono::high_resolution_clock::now();

    while (m_isRunning) {
        if (m_isVrMode) {
            // Gestisci gli eventi del visore
            m_xrManager->PollEvents(m_isRunning);
        } else {
            // Gestisci gli eventi della finestra desktop (es. la crocetta per chiudere)
            if (!m_windowManager->PollEvents()) {
                m_isRunning = false;
                break;
            }
        }

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
    
    // Toggle Inventory con 'E' o 'Tab'
    static bool eWasDown = false;
    bool eDown = (GetAsyncKeyState('E') & 0x8000) != 0 || (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
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
        if (m_gameMode == GameMode::Dev) {
            m_editor.isOpen = true;
        } else {
            m_editor.isOpen = false;
        }
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

        // Parametri AABB Player
        float playerRadius = 0.3f;
        float playerHeight = 1.8f;
        float eyeHeight = 1.6f;

        glm::vec3 pos = m_camera.Position;
        glm::vec3 feetPos = pos; feetPos.y -= eyeHeight;

        // Controlla se siamo nei liquidi
        BlockType centerBlock = m_world.GetBlock((int)floor(pos.x), (int)floor(pos.y - 0.5f), (int)floor(pos.z));
        BlockDef* centerDef = m_assets.GetBlock((int)centerBlock);
        m_camera.IsSwimming = (centerDef && centerDef->isLiquid);

        if (m_camera.IsSwimming) {
            // Logica Nuoto
            if (centerDef->damagePerSecond > 0) {
                static float damageTimer = 0.0f;
                damageTimer += deltaTime;
                if (damageTimer >= 1.0f) {
                    m_player.stats.currentHP -= centerDef->damagePerSecond;
                    damageTimer = 0.0f;
                }
            }

            m_camera.VelocityY *= 0.8f; // Attrito acqua
            if (GetAsyncKeyState(VK_SPACE) & 0x8000) m_camera.VelocityY = 4.0f;
            else if (GetAsyncKeyState(VK_SHIFT) & 0x8000) m_camera.VelocityY = -4.0f;
        } else {
            // Gravità
            if (m_gameMode == GameMode::Dev) {
                // Volo libero in Dev Mode
                m_camera.VelocityY = 0.0f;
                if (GetAsyncKeyState(VK_SPACE) & 0x8000) m_camera.VelocityY = m_camera.MovementSpeed;
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000) m_camera.VelocityY = -m_camera.MovementSpeed;
            } else {
                m_camera.VelocityY -= 20.0f * deltaTime; // Accelerazione gravitazionale
                if (m_camera.IsGrounded && (GetAsyncKeyState(VK_SPACE) & 0x8000)) {
                    m_camera.VelocityY = 7.0f; // Salto
                }
            }
        }

        // Calcola nuovo spostamento
        glm::vec3 targetVelocity = moveDir * m_camera.MovementSpeed;
        if (m_camera.IsSwimming) targetVelocity *= 0.5f; // Più lenti in acqua
        
        glm::vec3 nextPos = pos;
        nextPos.x += targetVelocity.x * deltaTime;
        nextPos.z += targetVelocity.z * deltaTime;
        nextPos.y += m_camera.VelocityY * deltaTime;

        // Funzione helper per collisioni AABB
        auto CheckCollision = [&](glm::vec3 testPos) {
            if (m_gameMode == GameMode::Dev && !m_camera.IsSwimming) return false; // Niente collisioni in volo dev
            float minX = testPos.x - playerRadius, maxX = testPos.x + playerRadius;
            float minY = testPos.y - eyeHeight, maxY = testPos.y + (playerHeight - eyeHeight);
            float minZ = testPos.z - playerRadius, maxZ = testPos.z + playerRadius;

            for (int x = (int)floor(minX); x <= (int)floor(maxX); x++) {
                for (int y = (int)floor(minY); y <= (int)floor(maxY); y++) {
                    for (int z = (int)floor(minZ); z <= (int)floor(maxZ); z++) {
                        BlockType b = m_world.GetBlock(x, y, z);
                        BlockDef* def = m_assets.GetBlock((int)b);
                        if (def && def->isSolid) return true;
                    }
                }
            }
            return false;
        };

        // Movimento asse per asse per scivolare sui muri
        // X-axis
        if (!CheckCollision(glm::vec3(nextPos.x, pos.y, pos.z))) {
            pos.x = nextPos.x;
        }
        // Z-axis
        if (!CheckCollision(glm::vec3(pos.x, pos.y, nextPos.z))) {
            pos.z = nextPos.z;
        }
        // Y-axis
        m_camera.IsGrounded = false;
        if (!CheckCollision(glm::vec3(pos.x, nextPos.y, pos.z))) {
            pos.y = nextPos.y;
        } else {
            if (m_camera.VelocityY < 0.0f) m_camera.IsGrounded = true;
            m_camera.VelocityY = 0.0f;
        }

        m_camera.Position = pos;
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

    // Se God Mode o il Diario sono aperti, il cursore è sempre libero
    if (m_editor.isOpen || m_isDiaryOpen) {
        m_cursorLocked = false;
    }

    // Click sinistro fuori dall'editor/diario: blocca il cursore per il gioco FPS
    if (!m_cursorLocked && !m_editor.isOpen && !m_isDiaryOpen && !io.WantCaptureMouse) {
        bool lDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (lDown && !m_lButtonWasDown) {
            m_cursorLocked = true;
            m_firstMouse = true; // Resetta per evitare salto al primo frame
        }
    }

    // Applica visibilità cursore (ShowCursor ha un contatore interno, chiamiamo solo se cambia)
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
                    m_world.SetBlock(hitBlock.x, hitBlock.y, hitBlock.z, BlockType::Air);
                    worldChanged = true;
                    std::cout << "[WORLD] Blocco rimosso in ("
                              << hitBlock.x << "," << hitBlock.y << "," << hitBlock.z << ")" << std::endl;
                } else if (placeBlock && hitBlock.x >= 0 && m_world.IsInBounds(prevBlock.x, prevBlock.y, prevBlock.z) && !hitGhost) {
                    const InventoryItem& activeItem = m_player.inventory.slots[m_selectedSlot];
                    if (!activeItem.IsEmpty() && activeItem.type == ItemType::Block) {
                        m_world.SetBlock(prevBlock.x, prevBlock.y, prevBlock.z, (BlockType)activeItem.blockType);
                        worldChanged = true;
                        
                        // Consuma l'oggetto
                        m_player.inventory.RemoveItem(m_selectedSlot, 1);
                        
                        std::cout << "[WORLD] Blocco '" << GetSlotName(m_selectedSlot) << "' piazzato in ("
                                  << prevBlock.x << "," << prevBlock.y << "," << prevBlock.z << ")" << std::endl;
                    }
                }

                if (worldChanged) {
                    // L'aggiornamento dei chunk sporchi avverrà all'inizio del prossimo frame
                }
            }
        }
    } // Chiude if (!io.WantCaptureMouse)

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

                ImGui::BeginChild((std::string("HB") + std::to_string(i)).c_str(), ImVec2(38, 38), true, ImGuiWindowFlags_NoScrollbar);
                if (!item.IsEmpty()) {
                    std::string itemName = GetSlotName(i);
                    std::string initial = itemName.empty() ? "?" : itemName.substr(0, 3);
                    ImGui::SetCursorPos(ImVec2(2, 2));
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", initial.c_str());

                    std::string countStr = std::to_string(item.count);
                    ImVec2 textSize = ImGui::CalcTextSize(countStr.c_str());
                    ImGui::SetCursorPos(ImVec2(38 - textSize.x - 4, 38 - textSize.y - 4));
                    ImGui::Text("%s", countStr.c_str());
                }
                ImGui::EndChild();
            }
            ImGui::End();
        } else {
            // --- INVENTARIO COMPLETO (Aperto) ---
            ImGui::SetNextWindowPos(center, ImGuiCond_Once, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(500, 350), ImGuiCond_Once);
            if (ImGui::Begin("Inventario Personale", &m_isInventoryOpen, ImGuiWindowFlags_NoCollapse)) {
                
                auto drawSlot = [&](int i, const char* labelPrefix) {
                    const auto& item = m_player.inventory.slots[i];
                    
                    ImGui::BeginChild((std::string(labelPrefix) + std::to_string(i)).c_str(), ImVec2(40, 40), true, ImGuiWindowFlags_NoScrollbar);
                    if (!item.IsEmpty()) {
                        std::string itemName = GetSlotName(i);
                        std::string initial = itemName.empty() ? "?" : itemName.substr(0, 3);
                        ImGui::SetCursorPos(ImVec2(2, 2));
                        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", initial.c_str());

                        std::string countStr = std::to_string(item.count);
                        ImVec2 textSize = ImGui::CalcTextSize(countStr.c_str());
                        ImGui::SetCursorPos(ImVec2(40 - textSize.x - 4, 40 - textSize.y - 4));
                        ImGui::Text("%s", countStr.c_str());
                    }
                    ImGui::EndChild();

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
                            m_player.inventory.SwapSlots(sourceIdx, i);
                        }
                        ImGui::EndDragDropTarget();
                    }
                    
                    if (ImGui::IsItemHovered() && !item.IsEmpty()) {
                        ImGui::SetTooltip("%s (x%d)", GetSlotName(i).c_str(), item.count);
                    }
                };

                ImGui::Text("Zaino:");
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

        // --- RENDER DEI MOB IN OVERLAY 2D (Finché Vulkan non supporta i modelli 3D) ---
        {
            ImVec2 screenSize = ImGui::GetMainViewport()->Size;
            glm::mat4 view = m_camera.GetViewMatrix();
            glm::mat4 proj = glm::perspective(glm::radians(m_renderManager->GetFov()), screenSize.x / screenSize.y, 0.1f, 1000.0f);
            
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

        if (m_gameMode == GameMode::Dev) {
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

        ImGui::Render();

        m_renderManager->RenderDesktop(m_camera.GetViewMatrix(), &m_assets, &m_mobManager, &m_player);
    }
}

void FairWorldEngine::Shutdown() {
    if (m_isVrMode) {
        m_xrManager->Shutdown();
    }
    m_windowManager->Shutdown();
    m_renderManager->Shutdown();
    m_isRunning = false;
}
