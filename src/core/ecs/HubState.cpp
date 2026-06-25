#include "pch.h"
#include "HubState.h"
#include "SharedContext.h"
#include "DeviceManager.h"
#include "StateManager.h"
#include "PlayState.h"
#include "ForgeState.h"
#include "FAIRWORLD.h"
#include <iostream>
#include "imgui.h"

HubState::HubState(SharedContext* context) : m_context(context), m_simulatedTimeAccumulator(0.0f) {
    std::cout << "[HubState] Costruito.\n";
}

HubState::~HubState() {
    std::cout << "[HubState] Distrutto. Isolamento memoria garantito.\n";
}

std::expected<void, std::string> HubState::Init() {
    std::cout << "[HubState] Inizializzazione completata. Mostro il Menu Principale ImGui.\n";
    m_context->deviceManager->InitDefaultBindings();
    return {};
}

void HubState::Update(float dt) {
    // Fai aggiornare l'engine (che ignorerà la fisica essendo in MAIN_MENU)
    m_context->engine->Update(dt);
    
    // Intercettiamo il cambio di stato dell'engine (es. l'utente preme "Nuova Partita" in ImGui)
    if (m_context->engine->GetCurrentState() == GameState::PLAYING) {
        std::cout << "[HubState] L'utente ha premuto 'Gioca' nell'interfaccia. Innesco transizione...\n";
        
        // 1. Iniettiamo la cartuccia Data-Driven
        m_context->targetGameJsonPath = "projects/game_config.json";
        
        // 2. Chiamiamo il ChangeState (che userà unique_ptr direttamente)
        m_context->stateManager->ChangeState(std::make_unique<PlayState>(m_context));
    }
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

        // --- GRIGLIA CANALI WII ---
        float channelWidth = 300.0f;
        float channelHeight = 200.0f;
        float padding = 30.0f;
        float startX = (viewport->Size.x - (channelWidth * 3 + padding * 2)) / 2.0f;
        float startY = 150.0f;

        // Canale 1: Esecuzione Progetti (Il "Disco" del Gioco)
        ImGui::SetCursorPos(ImVec2(startX, startY));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.9f, 1.0f, 1.0f)); // Azzurrino
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("FAIRWORLD\n[ Avvia Progetto JSON ]", ImVec2(channelWidth, channelHeight))) {
            std::cout << "[HubState] Avvio Cartuccia JSON richiesto.\n";
            m_context->targetGameJsonPath = "projects/game_config.json";
            m_context->stateManager->ChangeState(std::make_unique<PlayState>(m_context));
        }
        ImGui::PopStyleColor(3);

        // Canale 2: LA FORGE (Ambiente 3D)
        ImGui::SetCursorPos(ImVec2(startX + channelWidth + padding, startY));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.6f, 0.2f, 1.0f)); // Arancione
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.7f, 0.4f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("LA FORGE\n[ Entra in Officina 3D ]", ImVec2(channelWidth, channelHeight))) {
            std::cout << "[HubState] Transizione alla Forge 3D.\n";
            m_context->stateManager->ChangeState(std::make_unique<ForgeState>(m_context));
        }
        ImGui::PopStyleColor(3);

        // Canale 3: Gestione Dispositivi (Spostato giu' o a destra)
        ImGui::SetCursorPos(ImVec2(startX + (channelWidth + padding) * 2, startY));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.9f, 0.9f, 1.0f)); // Grigio
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
        

        if (ImGui::Button("CONNESSIONE DISPOSITIVI\n[ Impostazioni Hardware ]", ImVec2(channelWidth, channelHeight))) {
            showDeviceManager = true;
        }
        ImGui::PopStyleColor(3);

    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // --- POPUP IMPOSTAZIONI DISPOSITIVI ---
    if (showDeviceManager) {
            ImGui::SetNextWindowPos(ImVec2(viewport->Size.x / 2.0f - 300, viewport->Size.y / 2.0f - 250));
            ImGui::SetNextWindowSize(ImVec2(600, 500));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.15f, 0.95f)); // Sfondo scuro per il popup
            
            if (ImGui::Begin("Gestore Dispositivi Hardware", &showDeviceManager, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
                
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "--- CONNESSIONE GAMEPAD ---");
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
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "--- MAPPATURA COMANDI ---");
                
                bool isPad = m_context->deviceManager->GetGamepadData().isConnected;
                ImGui::Text("Modalita' Input: %s", isPad ? "CONTROLLER" : "TASTIERA / MOUSE");
                ImGui::Spacing();

                std::vector<const char*> mappableActions = {
                    "MOVE_FORWARD", "MOVE_BACKWARD", "MOVE_LEFT", "MOVE_RIGHT",
                    "JUMP", "DESTROY_BLOCK", "PLACE_BLOCK", "PAUSE"
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
                            ImGui::OpenPopup("Premi un tasto...");
                        }

                        if (ImGui::BeginPopupModal("Premi un tasto...", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                            ImGui::Text("Premi il nuovo tasto per:\n\n %s\n\n", actName);
                            ImGui::Separator();
                            
                            fw::InputID newKey = m_context->deviceManager->GetFirstPressedKey(waitingForGamepad);
                            if (newKey != fw::InputID::NONE) {
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
                ImGui::Text("Integrazione futura.");
            }
            ImGui::End();
            ImGui::PopStyleColor();
        }
}
