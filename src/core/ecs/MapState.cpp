#include "pch.h"
#include "MapState.h"
#include "SharedContext.h"
#include "StateManager.h"
#include "HubState.h"
#include "FAIRWORLD.h"
#include "MapWorldGenerator.h"
#include "ForgeWorld.h"
#include "PlayState.h"
#include <imgui.h>
#include <iostream>
#include <algorithm>

MapState::MapState(SharedContext* context) : m_context(context) {
    std::cout << "[MapState] Costruito.\n";
}

MapState::~MapState() {
    std::cout << "[MapState] Distrutto. Memoria isolata rilasciata.\n";
}

bool MapState::Init() {
    // Prova a caricare la mappa esistente
    if (!m_document.LoadJSON("saves/map/world_map.json") || m_document.planets.empty()) {
        // Fallback: Setup iniziale del Sistema Solare
        fw::PlanetMap earth = { ::PlanetType::EarthPrime, "Terra Prime", {} };
        fw::PlanetMap mars = { ::PlanetType::MarsDesolation, "Marte Desolato", {} };
        fw::PlanetMap glacies = { ::PlanetType::Glacies, "Glacies", {} };
        m_document.planets = { earth, mars, glacies };
        std::cout << "[MapState] Nessun salvataggio valido trovato. Creato nuovo Sistema Solare.\n";
    }
    return true;
}

void MapState::Update(float dt) {
    if (!m_isBuilderMode) {
        // Logica Telecamera Orbitale per l'anteprima 3D
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureMouse) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                m_orbitYaw -= io.MouseDelta.x * 0.5f;
                m_orbitPitch += io.MouseDelta.y * 0.5f;
                m_orbitPitch = std::clamp(m_orbitPitch, -89.0f, 89.0f);
            }
            m_orbitDistance -= io.MouseWheel * 10.0f;
            m_orbitDistance = std::max(m_orbitDistance, 10.0f);
        }

        // Calcolo coordinate sferiche per la telecamera
        float pitchRad = glm::radians(m_orbitPitch);
        float yawRad = glm::radians(m_orbitYaw);
        
        glm::vec3 camPos;
        camPos.x = m_orbitTarget.x + m_orbitDistance * cos(pitchRad) * sin(yawRad);
        camPos.y = m_orbitTarget.y + m_orbitDistance * sin(pitchRad);
        camPos.z = m_orbitTarget.z + m_orbitDistance * cos(pitchRad) * cos(yawRad);

        // Aggiorna la telecamera del contesto condiviso
        m_context->activeCameraView.cameraPosition = camPos;
        m_context->activeCameraView.cameraFront = glm::normalize(m_orbitTarget - camPos);
        m_context->activeCameraView.viewMatrix = glm::lookAt(camPos, m_orbitTarget, glm::vec3(0, 1, 0));
        
        // Passiamo un aspect ratio approssimativo (ideale prendere le dimensioni della finestra)
        float aspect = 16.0f / 9.0f; 
        if (m_context->engine) {
            // Se abbiamo accesso all'engine, usiamo la risoluzione corrente
            // Ma per ora un default funziona bene
        }
        m_context->activeCameraView.projectionMatrix = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);
        m_context->activeCameraView.projectionMatrix[1][1] *= -1; // Correzione Y per Vulkan

        if (m_context && m_context->jobSystem && m_context->assetManager) {
            const fw::PlanetMap* pMap = nullptr;
            if (m_activePlanetIndex >= 0 && m_activePlanetIndex < m_document.planets.size()) {
                pMap = &m_document.planets[m_activePlanetIndex];
            }
            for (auto& root : m_planetRootNodes) {
                m_lodSystem.UpdateLODTree(root, camPos, m_previewWorld.get(), m_context->jobSystem, m_context->assetManager, pMap);
            }
        }
        
        if (m_previewWorld) m_previewWorld->Update(dt);
    }
}

void MapState::Render() {
    if (m_isBuilderMode) {
        DrawBuilderUI();
    } else {
        DrawRuntimeUI();
    }
}

void MapState::DrawBuilderUI() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | 
                                   ImGuiWindowFlags_NoMove | 
                                   ImGuiWindowFlags_NoSavedSettings | 
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("MAP BUILDER - SEZIONE AUREA", nullptr, windowFlags);
    
    float width = viewport->Size.x;
    float height = viewport->Size.y;
    
    // SAFEGUARD: Assicuriamoci che esista almeno un pianeta
    if (m_document.planets.empty() || m_activePlanetIndex < 0 || m_activePlanetIndex >= m_document.planets.size()) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "[ERRORE CRITICO] Nessun pianeta caricato nel documento!");
        if (ImGui::Button("Forza Ripristino Pianeta Default")) {
            m_document.planets.push_back({ ::PlanetType::EarthPrime, "Terra Prime (Ripristinata)", {} });
            m_activePlanetIndex = 0;
        }
        ImGui::End();
        return;
    }
    
    // Proporzioni auree (Fibonacci: 38.2% e 61.8%)
    float sidebarWidth = width * 0.382f;
    float canvasWidth = width * 0.618f;
    
    // ==========================================
    // SIDEBAR (38.2%) - SYSTEM INSPECTOR
    // ==========================================
    ImGui::BeginChild("Sidebar", ImVec2(sidebarWidth, 0), true);
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "SYSTEM INSPECTOR (38.2%)");
    ImGui::Separator();
    
    // Tabs per i Pianeti
    if (ImGui::BeginTabBar("PianetiTabBar")) {
        for (int i = 0; i < (int)m_document.planets.size(); i++) {
            if (ImGui::BeginTabItem(m_document.planets[i].name.c_str())) {
                if (m_activePlanetIndex != i) {
                    m_activePlanetIndex = i;
                    m_selectedRegionIndex = -1; // Resetta la selezione cambiando pianeta
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    
    ImGui::Spacing();
    
    auto& currentPlanet = m_document.planets[m_activePlanetIndex];
    ImGui::Text("Regioni in %s: %d", currentPlanet.name.c_str(), (int)currentPlanet.regions.size());
    
    if (ImGui::Button("Aggiungi Regione", ImVec2(-1, 0))) {
        fw::MapRegion r = { glm::vec2(0.5f, 0.5f), 0.1f, fw::MapRegionType::Forest, "Nuova Regione", 12345 };
        currentPlanet.regions.push_back(r);
        m_selectedRegionIndex = (int)currentPlanet.regions.size() - 1;
    }
    
    ImGui::Separator();
    
    // Inspector della Regione Selezionata
    if (m_selectedRegionIndex >= 0 && m_selectedRegionIndex < (int)currentPlanet.regions.size()) {
        auto& r = currentPlanet.regions[m_selectedRegionIndex];
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Configura: %s", r.label.c_str());
        
        char nameBuf[64];
        #ifdef _WIN32
            strcpy_s(nameBuf, r.label.c_str());
        #else
            strncpy(nameBuf, r.label.c_str(), sizeof(nameBuf));
            nameBuf[sizeof(nameBuf) - 1] = '\0';
        #endif
        
        if (ImGui::InputText("Nome", nameBuf, sizeof(nameBuf))) {
            r.label = nameBuf;
        }
        
        // Tipo regione
        const char* regionTypes[] = { "Foresta", "Deserto", "Tundra", "Oceano", "Vulcano", "Citta", "Dungeon", "Portale" };
        int typeInt = static_cast<int>(r.type);
        if (ImGui::Combo("Tipo", &typeInt, regionTypes, IM_ARRAYSIZE(regionTypes))) {
            r.type = static_cast<fw::MapRegionType>(typeInt);
        }
        
        // Controlli spaziali
        ImGui::SliderFloat2("Coordinate", &r.center.x, 0.0f, 1.0f);
        ImGui::SliderFloat("Raggio", &r.radius, 0.01f, 0.5f);
        
        // Controlli ambientali
        ImGui::SliderFloat("Frequenza Perlin", &r.perlinFrequency, 0.005f, 0.2f);
        ImGui::SliderFloat("Densita' Alberi", &r.treeDensity, 0.0f, 1.0f);
        ImGui::SliderFloat("Modificatore Gravita'", &r.gravityModifier, 0.1f, 5.0f);
        
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Rimuovi Regione", ImVec2(-1, 0))) {
            currentPlanet.regions.erase(currentPlanet.regions.begin() + m_selectedRegionIndex);
            m_selectedRegionIndex = -1;
        }
        ImGui::PopStyleColor();
    }
    
    // Bottom Buttons
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 150.0f); // Spingi in basso
    ImGui::Separator();
    if (ImGui::Button("SALVA PROGETTO", ImVec2(-1, 40))) {
        m_document.SaveJSON("saves/map/world_map.json");
    }
    if (ImGui::Button("COMPILA E GENERA VOXEL (FASE 2)", ImVec2(-1, 40))) {
        CompileAndGenerate();
    }
    if (ImGui::Button("TORNA ALL'HUB", ImVec2(-1, 30))) {
        m_context->engine->SetGameMode(GameMode::Hub);
        m_context->engine->ForceGameState(GameState::MAIN_MENU);
        m_context->stateManager->ChangeState(std::make_unique<HubState>(m_context));
    }
    
    ImGui::EndChild();
    
    ImGui::SameLine();
    
    // ==========================================
    // CANVAS VETTORIALE (61.8%)
    // ==========================================
    ImGuiWindowFlags canvasFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("Canvas", ImVec2(canvasWidth, 0), true, canvasFlags);
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "CANVAS MAPPA PLANISFERO 2D (61.8%)");
    ImGui::Separator();
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    
    // 1. Sfondo del canvas (Spazio profondo)
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(20, 20, 25, 255));
    
    // 2. Disegno del Planisfero (Semplificato per ora, proietta su [0..1])
    for (int i = 0; i < (int)currentPlanet.regions.size(); ++i) {
        const auto& r = currentPlanet.regions[i];
        
        // Mappa le coordinate [0..1] alla dimensione in pixel del canvas
        ImVec2 screenPos = ImVec2(
            canvasPos.x + (r.center.x * canvasSize.x),
            canvasPos.y + (r.center.y * canvasSize.y)
        );
        float screenRadius = r.radius * std::min(canvasSize.x, canvasSize.y);
        
        // Colore dinamico se selezionato
        ImU32 color;
        if (i == m_selectedRegionIndex) {
            color = IM_COL32(255, 255, 100, 200);
        } else {
            // Colore a seconda del tipo
            switch (r.type) {
                case fw::MapRegionType::Forest:  color = IM_COL32(40, 180, 40, 150); break;
                case fw::MapRegionType::Desert:  color = IM_COL32(220, 200, 100, 150); break;
                case fw::MapRegionType::Tundra:  color = IM_COL32(180, 220, 220, 150); break;
                case fw::MapRegionType::Ocean:   color = IM_COL32(40, 80, 220, 150); break;
                case fw::MapRegionType::Volcano: color = IM_COL32(220, 40, 40, 150); break;
                case fw::MapRegionType::City:    color = IM_COL32(150, 150, 150, 150); break;
                case fw::MapRegionType::Dungeon: color = IM_COL32(120, 60, 160, 150); break;
                case fw::MapRegionType::Portal:  color = IM_COL32(220, 100, 220, 150); break;
                default:                         color = IM_COL32(100, 200, 100, 150); break;
            }
        }
        
        drawList->AddCircleFilled(screenPos, screenRadius, color);
        drawList->AddCircle(screenPos, screenRadius, IM_COL32(255, 255, 255, 255), 0, 2.0f); // Bordo
        drawList->AddText(ImVec2(screenPos.x - 15, screenPos.y - 8), IM_COL32(255, 255, 255, 255), r.label.c_str());
    }
    
    // Gestione basilare del click sul canvas per selezionare regioni o trascinarle
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 mousePos = ImGui::GetMousePos();
        m_selectedRegionIndex = -1; // Deseleziona di default
        
        for (int i = 0; i < (int)currentPlanet.regions.size(); ++i) {
            const auto& r = currentPlanet.regions[i];
            ImVec2 centerScreen = ImVec2(
                canvasPos.x + (r.center.x * canvasSize.x),
                canvasPos.y + (r.center.y * canvasSize.y)
            );
            float screenRadius = r.radius * std::min(canvasSize.x, canvasSize.y);
            
            // Distanza euclidea
            float dx = mousePos.x - centerScreen.x;
            float dy = mousePos.y - centerScreen.y;
            if ((dx*dx + dy*dy) <= (screenRadius * screenRadius)) {
                m_selectedRegionIndex = i;
                break; // Seleziona il primo che tocca
            }
        }
    }
    
    // Supporto per il trascinamento della regione selezionata
    if (m_selectedRegionIndex >= 0 && m_selectedRegionIndex < (int)currentPlanet.regions.size() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
        auto& r = currentPlanet.regions[m_selectedRegionIndex];
        
        r.center.x += mouseDelta.x / canvasSize.x;
        r.center.y += mouseDelta.y / canvasSize.y;
        
        // Clampa tra 0 e 1
        r.center.x = std::clamp(r.center.x, 0.0f, 1.0f);
        r.center.y = std::clamp(r.center.y, 0.0f, 1.0f);
    }
    
    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void MapState::DrawRuntimeUI() {
    if (m_document.planets.empty() || m_activePlanetIndex < 0 || m_activePlanetIndex >= m_document.planets.size()) return;

    // Finestra fissa in alto a destra o centrata in basso
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 400.0f, 20.0f), ImGuiCond_Always);
    ImGui::Begin("Collaudo Mondo", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Pianeta Attivo: %s", m_document.planets[m_activePlanetIndex].name.c_str());
    ImGui::TextDisabled("Tasto Destro: Orbita | Rotellina: Zoom");
    ImGui::Separator();
    
    if (ImGui::Button("TORNA AL BUILDER 2D", ImVec2(-1, 30))) {
        m_previewWorld.reset(); // Distrugge l'universo di prova per liberare RAM/VRAM
        m_isBuilderMode = true;
    }
    
    ImGui::Spacing(); ImGui::Spacing();
    
    // IL PONTE VERSO FAIRWORLD
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.6f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));
    
    if (ImGui::Button("CARICA LA MAPPA COME PRINCIPALE DI FAIRWORLD", ImVec2(-1, 50))) {
        // 1. Salva la "Cartuccia"
        m_document.SaveJSON("saves/map/world_map.json");
        
        // 2. Inserisci la cartuccia nel contesto globale (mock)
        // m_context->targetGameJsonPath = "saves/map/world_map.json"; // Richiede modifica a SharedContext
        
        // 3. Lancia il gioco
        m_context->engine->SetGameMode(GameMode::Play);
        m_context->engine->ForceGameState(GameState::PLAYING);
        m_context->stateManager->ChangeState(std::make_unique<PlayState>(m_context));
    }
    
    ImGui::PopStyleColor(3);
    ImGui::End();
}

void MapState::CompileAndGenerate() {
    std::cout << "[MapState] Inizio compilazione e generazione asincrona...\n";
    
    // 1. Alloca il mondo di collaudo e resettalo se esisteva gia'
    m_previewWorld = std::make_unique<fw::ForgeWorld>();
    m_previewWorld->Initialize(m_context);
    
    // 2. Setup Spherical LOD Base (Cube-Sphere faces)
    float radius = 50.0f;
    m_lodSystem.SetPlanetRadius(radius);
    m_planetRootNodes.clear();
    
    int baseLod = 5;
    glm::vec3 c(0.0f);
    
    // Face +Z
    m_planetRootNodes.emplace_back(glm::vec3(0,0,radius), radius, baseLod, glm::vec3(-radius, -radius, radius), glm::vec3(radius, -radius, radius), glm::vec3(-radius, radius, radius), glm::vec3(radius, radius, radius));
    // Face -Z
    m_planetRootNodes.emplace_back(glm::vec3(0,0,-radius), radius, baseLod, glm::vec3(radius, -radius, -radius), glm::vec3(-radius, -radius, -radius), glm::vec3(radius, radius, -radius), glm::vec3(-radius, radius, -radius));
    // Face +X
    m_planetRootNodes.emplace_back(glm::vec3(radius,0,0), radius, baseLod, glm::vec3(radius, -radius, radius), glm::vec3(radius, -radius, -radius), glm::vec3(radius, radius, radius), glm::vec3(radius, radius, -radius));
    // Face -X
    m_planetRootNodes.emplace_back(glm::vec3(-radius,0,0), radius, baseLod, glm::vec3(-radius, -radius, -radius), glm::vec3(-radius, -radius, radius), glm::vec3(-radius, radius, -radius), glm::vec3(-radius, radius, radius));
    // Face +Y
    m_planetRootNodes.emplace_back(glm::vec3(0,radius,0), radius, baseLod, glm::vec3(-radius, radius, radius), glm::vec3(radius, radius, radius), glm::vec3(-radius, radius, -radius), glm::vec3(radius, radius, -radius));
    // Face -Y
    m_planetRootNodes.emplace_back(glm::vec3(0,-radius,0), radius, baseLod, glm::vec3(-radius, -radius, -radius), glm::vec3(radius, -radius, -radius), glm::vec3(-radius, -radius, radius), glm::vec3(radius, -radius, radius));
    
    // 3. Passa alla visuale 3D
    m_isBuilderMode = false; 

    // BUG FIX: Agganciamo il nuovo ForgeWorld al motore di rendering!
    m_context->activeRegistry = &m_previewWorld->GetRegistry();
    m_context->isForgeMode = true; // Necessario per usare il renderer Forge (Deferred meshes)

    // Forziamo anche la visuale su un punto di partenza
    m_orbitDistance = 150.0f;
    m_orbitPitch = 30.0f;
    m_orbitYaw = 45.0f;
}
