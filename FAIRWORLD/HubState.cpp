#include "pch.h"
#include "HubState.h"
#include "SharedContext.h"
#include "StateManager.h"
#include "PlayState.h"
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
        ImGui::Text("FAIRWORLD OS");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        // --- GRIGLIA CANALI WII ---
        float channelWidth = 300.0f;
        float channelHeight = 200.0f;
        float padding = 30.0f;
        float startX = (viewport->Size.x - (channelWidth * 2 + padding)) / 2.0f;
        float startY = 150.0f;

        // Canale 1: Esecuzione Progetti (Il "Disco" del Gioco)
        ImGui::SetCursorPos(ImVec2(startX, startY));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.9f, 1.0f, 1.0f)); // Azzurrino
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("Canale Disco\n[ Avvia Progetto JSON ]", ImVec2(channelWidth, channelHeight))) {
            std::cout << "[HubState] Avvio Cartuccia JSON richiesto.\n";
            m_context->targetGameJsonPath = "projects/game_config.json";
            m_context->stateManager->ChangeState(std::make_unique<PlayState>(m_context));
        }
        ImGui::PopStyleColor(3);

        // Canale 2: Gestione Dispositivi (Device Manager)
        ImGui::SetCursorPos(ImVec2(startX + channelWidth + padding, startY));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.9f, 0.9f, 1.0f)); // Grigio
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
        
        static bool showDeviceManager = false;
        if (ImGui::Button("Canale Dispositivi\n[ Impostazioni Hardware ]", ImVec2(channelWidth, channelHeight))) {
            showDeviceManager = true;
        }
        ImGui::PopStyleColor(3);

        // --- POPUP IMPOSTAZIONI DISPOSITIVI ---
        if (showDeviceManager) {
            ImGui::SetNextWindowPos(ImVec2(viewport->Size.x / 2.0f - 200, viewport->Size.y / 2.0f - 150));
            ImGui::SetNextWindowSize(ImVec2(400, 300));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.15f, 0.95f)); // Sfondo scuro per il popup
            
            if (ImGui::Begin("Gestore Dispositivi Hardware", &showDeviceManager, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
                
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "--- CONNESSIONE GAMEPAD ---");
                if (m_context->isGamepadConnected) {
                    ImGui::Text("Stato: CONNESSO (Player %d)", m_context->gamepadIndex + 1);
                    ImGui::Separator();
                    
                    // Mostriamo i dati in tempo reale dal Bus Dati (SharedContext)
                    ImGui::Text("Analogico Sinistro:");
                    ImGui::ProgressBar((m_context->gamepadInput.leftThumbX + 1.0f) / 2.0f, ImVec2(-1, 0), "X Axis");
                    ImGui::ProgressBar((m_context->gamepadInput.leftThumbY + 1.0f) / 2.0f, ImVec2(-1, 0), "Y Axis");
                    
                    ImGui::Text("Analogico Destro:");
                    ImGui::ProgressBar((m_context->gamepadInput.rightThumbX + 1.0f) / 2.0f, ImVec2(-1, 0), "X Axis");
                    ImGui::ProgressBar((m_context->gamepadInput.rightThumbY + 1.0f) / 2.0f, ImVec2(-1, 0), "Y Axis");
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Stato: DISCONNESSO");
                    ImGui::Text("In attesa di controller XInput...");
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
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}
