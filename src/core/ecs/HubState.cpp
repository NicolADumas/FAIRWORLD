#include "pch.h"
#include "HubState.h"
#include "SharedContext.h"
#include "DeviceManager.h"
#include "StateManager.h"
#include "PlayState.h"
#include "ForgeState.h"
#include "FAIRWORLD.h"
#include "PhysicsLabState.h"
#include "ChunkEditorState.h"
#include "PlanetMapperState.h"
#include "BlockMakerState.h"
#include <iostream>
#include "imgui.h"

HubState::HubState(SharedContext* context) : m_context(context), m_simulatedTimeAccumulator(0.0f) {
    std::cout << "[HubState] Costruito.\n";
}

HubState::~HubState() {
    std::cout << "[HubState] Distrutto. Isolamento memoria garantito.\n";
}

bool HubState::Init() {
    std::cout << "[HubState] Inizializzazione completata. Mostro il Menu Principale ImGui.\n";
    m_context->deviceManager->InitDefaultBindings();
    return true;
}

void HubState::Update(float dt) {
    // Fai aggiornare l'engine (che ignorerà la fisica essendo in MAIN_MENU)
    m_context->engine->Update(dt);
}

void HubState::Render() {
    static bool showDeviceManager = false;
    // Il nostro OS prende il controllo totale dello schermo
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    
    // Background in stile Wii (Bianco/Grigio chiaro)
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.9f, 0.9f, 0.95f, 1.0f));

    if (ImGui::Begin("FAIRWORLD OS - WII DASHBOARD", nullptr, 
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) 
    {
        // Titolo in alto
        ImGui::SetCursorPos(ImVec2(viewport->Size.x / 2.0f - 150.0f, 40.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::SetWindowFontScale(2.5f);
        ImGui::Text("FAIRWORLD HUB");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        // --- GRIGLIA CANALI (3 colonne x 3 righe) ---
        float totalW = viewport->Size.x;
        float totalH = viewport->Size.y;
        int cols = 3;
        int rows = 3;
        float padding = 15.0f;
        float channelWidth  = (totalW - padding * (cols + 1)) / cols;
        float channelHeight = std::min(150.0f, (totalH - 160.0f - padding * (rows + 1)) / rows);
        float gridStartX = padding;
        float gridStartY = 130.0f;

        struct TileInfo {
            const char* label;
            ImVec4 color;
            ImVec4 hoverColor;
            ImVec4 textColor;
        };
        TileInfo tiles[7] = {
            { "FAIRWORLD\n[ Avvia Progetto JSON ]",         {0.3f,0.55f,0.9f,1.f}, {0.45f,0.7f,1.f,1.f},  {1.f,1.f,1.f,1.f} },
            { "LA FORGE\n[ Officina 3D ]",                  {1.0f,0.55f,0.1f,1.f}, {1.f,0.7f,0.3f,1.f},   {0.1f,0.1f,0.1f,1.f} },
            { "PHYSICS LAB\n[ Calibrazione Materiali ]",     {0.2f,0.75f,0.3f,1.f}, {0.3f,0.9f,0.4f,1.f},  {0.05f,0.05f,0.05f,1.f} },
            { "CHUNK EDITOR\n[ Modellazione Terreni 2D/3D ]",{0.8f,0.4f,0.2f,1.f}, {0.9f,0.5f,0.3f,1.f},  {1.f,1.f,1.f,1.f} },
            { "PLANET MAPPER\n[ Configura Globo & Sistema ]",{0.55f,0.3f,0.85f,1.f},{0.7f,0.45f,1.f,1.f},  {1.f,1.f,1.f,1.f} },
            { "BLOCK MAKER\n[ PBR & Lookdev ]",              {0.1f,0.7f,0.75f,1.f}, {0.2f,0.85f,0.9f,1.f}, {0.05f,0.05f,0.05f,1.f} },
            { "CONNESSIONE DISPOSITIVI\n[ Hardware ]",       {0.7f,0.7f,0.7f,1.f}, {0.85f,0.85f,0.85f,1.f},{0.1f,0.1f,0.1f,1.f} }
        };

        for (int i = 0; i < 7; i++) {
            int col = i % cols;
            int row = i / cols;
            float px = gridStartX + col * (channelWidth + padding);
            float py = gridStartY + row * (channelHeight + padding);
            ImGui::SetCursorPos(ImVec2(px, py));
            ImGui::PushStyleColor(ImGuiCol_Button,        tiles[i].color);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tiles[i].hoverColor);
            ImGui::PushStyleColor(ImGuiCol_Text,          tiles[i].textColor);
            if (ImGui::Button(tiles[i].label, ImVec2(channelWidth, channelHeight))) {
                switch(i) {
                    case 0: // FAIRWORLD Play
                        m_context->targetGameJsonPath = "saves/map/world_map.json";
                        m_context->engine->SetGameMode(GameMode::Play);
                        m_context->engine->ForceGameState(GameState::PLAYING);
                        m_context->stateManager->ChangeState(std::make_unique<PlayState>(m_context));
                        break;
                    case 1: // LA FORGE
                        m_context->engine->SetGameMode(GameMode::Dev);
                        m_context->engine->ForceGameState(GameState::PLAYING);
                        m_context->stateManager->ChangeState(std::make_unique<ForgeState>(m_context));
                        break;
                    case 2: // PHYSICS LAB
                        m_context->engine->SetGameMode(GameMode::PhysicsLab);
                        m_context->engine->ForceGameState(GameState::PLAYING);
                        m_context->stateManager->ChangeState(std::make_unique<PhysicsLabState>(m_context));
                        break;
                    case 3: // CHUNK EDITOR
                        m_context->engine->SetGameMode(GameMode::ChunkEditor);
                        m_context->engine->ForceGameState(GameState::PLAYING);
                        m_context->stateManager->ChangeState(std::make_unique<ChunkEditorState>(m_context));
                        break;
                    case 4: // PLANET MAPPER
                        m_context->engine->SetGameMode(GameMode::PlanetMapper);
                        m_context->engine->ForceGameState(GameState::PLAYING);
                        m_context->stateManager->ChangeState(std::make_unique<PlanetMapperState>(m_context));
                        break;
                    case 5: // BLOCK MAKER
                        m_context->engine->SetGameMode(GameMode::BlockMaker);
                        m_context->engine->ForceGameState(GameState::PLAYING);
                        m_context->stateManager->ChangeState(std::make_unique<BlockMakerState>(m_context));
                        break;
                    case 6: // CONNESSIONE DISPOSITIVI
                        showDeviceManager = true;
                        break;
                }
            }
            ImGui::PopStyleColor(3);
        }

    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // --- POPUP IMPOSTAZIONI DISPOSITIVI ---
    if (showDeviceManager) {
            ImGui::SetNextWindowPos(ImVec2(viewport->Size.x / 2.0f - 300, viewport->Size.y / 2.0f - 250));
            ImGui::SetNextWindowSize(ImVec2(600, 500));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.15f, 0.95f)); // Sfondo scuro per il popup
            
            if (ImGui::Begin("GESTIONE DISPOSITIVI", &showDeviceManager, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
                
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "--- CONNESSIONE AL GAMEPAD ---");
                ImGui::Text("Connesso: %s", m_context->deviceManager->GetGamepadData().isConnected ? "SI" : "NO");
                ImGui::Text("Indice: %d", m_context->deviceManager->GetGamepadData().index);
                ImGui::Separator();
                
                ImGui::Text("Stick Sinistro: (%.2f, %.2f)", 
                    m_context->deviceManager->GetGamepadData().leftThumbX, 
                    m_context->deviceManager->GetGamepadData().leftThumbY);
                    
                ImGui::Text("Grilletti: L=%.2f R=%.2f", 
                    m_context->deviceManager->GetGamepadData().leftTrigger, 
                    m_context->deviceManager->GetGamepadData().rightTrigger);
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "--- MAPPATURA DEI COMANDI ---");
                
                bool isPad = m_context->deviceManager->GetGamepadData().isConnected;
                ImGui::Text("Modalita' Input: %s", isPad ? "CONTROLLER" : "COMPUTER");
                ImGui::Spacing();

                std::vector<const char*> mappableActions = {
                    "MOVE_FORWARD", "MOVE_BACKWARD", "MOVE_LEFT", "MOVE_RIGHT",
                    "JUMP", "DESTROY_BLOCK", "PLACE_BLOCK", "PAUSE", "TOGGLE_INVENTORY",
                    "TOGGLE_BROWSER", "TOGGLE_DIARY", "TOGGLE_SETTINGS"
                };

                if (ImGui::BeginTable("Mappings", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
                    for (const char* actName : mappableActions) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", actName);
                        ImGui::TableNextColumn();
                        
                        entt::id_type hash = entt::hashed_string(actName);
                        fw::InputID currentKey = fw::InputID::NONE;
                        static bool waitingForGamepad = false;
                        
                        auto& bindings = m_context->deviceManager->GetActionMap().bindings;
                        auto it = bindings.find(hash);
                        if (it != bindings.end()) {
                            for (const auto& b : it->second) {
                                bool bIsPad = ((int)b.primaryKey >= (int)fw::InputID::PAD_FACE_DOWN);
                                if (bIsPad == isPad) {
                                    currentKey = b.primaryKey;
                                    break;
                                }
                            }
                        }

                        ImGui::PushID(actName);
                        const char* btnLabel = m_context->deviceManager->InputIDToString(currentKey);
                        if (ImGui::Button(btnLabel, ImVec2(150, 0))) {
                            waitingForGamepad = isPad;
                            m_context->deviceManager->GetActionMap().isListening = true;
                            ImGui::OpenPopup("Premi un tasto...");
                        }

                        if (ImGui::BeginPopupModal("Premi un tasto...", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                            ImGui::Text("Premi il nuovo tasto per:\n\n %s\n\n", actName);
                            ImGui::Separator();
                            
                            fw::InputID newKey = m_context->deviceManager->GetFirstPressedKey(waitingForGamepad);
                            if (newKey != fw::InputID::NONE) {
                                // Conflict resolution: rimuovi 'newKey' da tutte le altre azioni (della stessa periferica)
                                for (auto& pair : bindings) {
                                    if (pair.first != hash) {
                                        auto& otherBindings = pair.second;
                                        otherBindings.erase(std::remove_if(otherBindings.begin(), otherBindings.end(), 
                                            [&](const fw::ActionBinding& b) {
                                                bool bIsPad = ((int)b.primaryKey >= (int)fw::InputID::PAD_FACE_DOWN);
                                                return bIsPad == isPad && b.primaryKey == newKey;
                                            }), otherBindings.end());
                                    }
                                }

                                if (it != bindings.end()) {
                                    bool found = false;
                                    for (auto& b : it->second) {
                                        bool bIsPad = ((int)b.primaryKey >= (int)fw::InputID::PAD_FACE_DOWN);
                                        if (bIsPad == isPad) {
                                            b.primaryKey = newKey;
                                            found = true;
                                            break;
                                        }
                                    }
                                    // Se non c'era un binding per questa mod, lo aggiungiamo
                                    if (!found) {
                                        it->second.push_back({newKey, fw::InputID::NONE});
                                    }
                                } else {
                                    bindings[hash].push_back({newKey, fw::InputID::NONE});
                                }
                                m_context->deviceManager->GetActionMap().isListening = false;
                                ImGui::CloseCurrentPopup();
                            }

                            if (ImGui::Button("Annulla", ImVec2(120, 0))) {
                                m_context->deviceManager->GetActionMap().isListening = false;
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::EndPopup();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "--- VR / BCI / SERIALE ---");
                ImGui::Text("Integrazione futura...");
            }
            ImGui::End();
            ImGui::PopStyleColor();
        }
}
