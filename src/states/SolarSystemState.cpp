#include "pch.h"
#include "SolarSystemState.h"
#include "SharedContext.h"
#include "DeviceManager.h"
#include "FAIRWORLD.h"
#include "StateManager.h"
#include "HubState.h"
#include "PlayState.h"
#include "TimeManager.h"
#include "PlanetSystems.h"
#include "GameWorld.h"
#include <iostream>
#include <filesystem>
#include <imgui.h>
#include "GameWorld.h"
#include <iostream>
#include <imgui.h>

SolarSystemState::SolarSystemState(SharedContext* context) : m_context(context) {
    std::cout << "[SolarSystemState] Costruito.\n";
}

SolarSystemState::~SolarSystemState() {
    std::cout << "[SolarSystemState] Distrutto.\n";
}

bool SolarSystemState::Init() {
    std::cout << "[SolarSystemState] Inizializzazione...\n";
    RefreshPlanetList();
    
    // Setup iniziale camera per mappa
    if (m_context && m_context->engine) {
        m_context->activeCameraView.cameraPosition = glm::vec3(0.0f, 50000.0f, 100000.0f); // Lontano per vedere il sistema
        m_context->activeCameraView.cameraFront = glm::normalize(glm::vec3(0.0f) - m_context->activeCameraView.cameraPosition);
        m_context->deviceManager->requireFreeCursor = true; // Liberiamo il cursore per l'UI
    }
    return true;
}

void SolarSystemState::Update(float dt) {
    using namespace entt::literals;

    // Ritorna all'HUB
    if (m_context->deviceManager->IsActionActive("PAUSE"_hs) || (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
        m_context->engine->SetGameMode(GameMode::Hub);
        m_context->engine->ForceGameState(GameState::MAIN_MENU);
        m_context->stateManager->ChangeState(std::make_unique<HubState>(m_context));
        return;
    }

    if (m_isSimulating) {
        // Acceleriamo il tempo per vedere i corpi in movimento
        m_context->engine->GetTimeManager().Update(dt * m_simulationSpeed);

        // TODO: Aggiornare la visualizzazione del sistema solare basata sul nuovo AstronomySystem
    }
}

void SolarSystemState::RefreshPlanetList() {
    m_availablePlanets.clear();
    m_selectedPlanetIndex = -1;
    std::string searchPath = "saves/map";
    if (std::filesystem::exists(searchPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(searchPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                m_availablePlanets.push_back(entry.path().filename().string());
            }
        }
    }
    if (!m_availablePlanets.empty()) {
        m_selectedPlanetIndex = 0;
    }
}

void SolarSystemState::Render() {
    // Interfaccia Utente Mappa
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    if (ImGui::Begin("Navigazione Sistema Solare", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 1.0f, 1.0f), "Mappa Stellare (Time-Lapse)");
        ImGui::Separator();
        ImGui::Text("Premi [ESC] per tornare all'Hub.");
        ImGui::Spacing();
        ImGui::SliderFloat("Velocita'", &m_simulationSpeed, 1.0f, 50000.0f, "%.1f x");
        ImGui::Checkbox("Play/Pause", &m_isSimulating);
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Pianeti Disponibili (Planet Mapper):");
        
        if (m_availablePlanets.empty()) {
            ImGui::Text("Nessun pianeta (.json) trovato in saves/map/");
        } else {
            std::string comboPreview = m_selectedPlanetIndex >= 0 ? m_availablePlanets[m_selectedPlanetIndex] : "";
            if (ImGui::BeginCombo("##Pianeti", comboPreview.c_str())) {
                for (int i = 0; i < (int)m_availablePlanets.size(); ++i) {
                    bool isSelected = (m_selectedPlanetIndex == i);
                    if (ImGui::Selectable(m_availablePlanets[i].c_str(), isSelected)) {
                        m_selectedPlanetIndex = i;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Spacing();
            if (ImGui::Button("Esplora in PlayState", ImVec2(-1, 35))) {
                if (m_selectedPlanetIndex >= 0) {
                    m_context->targetGameJsonPath = "saves/map/" + m_availablePlanets[m_selectedPlanetIndex];
                    m_context->engine->SetGameMode(GameMode::Play);
                    m_context->engine->ForceGameState(GameState::PLAYING);
                    m_context->stateManager->ChangeState(std::make_unique<PlayState>(m_context));
                    
                    ImGui::End();
                    return; // Evita crash per cambio di stato in corso d'opera
                }
            }
        }
    }
    ImGui::End();
}
