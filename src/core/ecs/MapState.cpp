#include "pch.h"
#include "MapState.h"
#include "SharedContext.h"
#include "StateManager.h"
#include "HubState.h"
#include "FAIRWORLD.h"
#include "MapWorldGenerator.h"
#include "GameWorld.h"
#include "PlayState.h"
#include "BlockRegistry.h"
#include "ForgeComponents.h"
#include "SimulationManager.h"
#include "SimDataLayer.h"
#include "DeviceManager.h"
#include "JobSystem.h"
#include "RenderManager.h"
#include <imgui.h>
#include <iostream>
#include <algorithm>

MapState::MapState(SharedContext* context) : m_context(context) {
    std::cout << "[MapState] Costruito.\n";
}

MapState::~MapState() {
    std::cout << "[MapState] Attendiamo completamento job asincroni pendenti prima di distruggere...\n";
    if (m_context) {
        if (m_previewWorld && m_context->forgeWorld == m_previewWorld.get()) {
            m_context->forgeWorld = nullptr;
        }
        if (m_previewWorld && m_context->activeRegistry == &m_previewWorld->GetRegistry()) {
            m_context->activeRegistry = nullptr;
        }
        if (m_context->jobSystem) {
            m_context->jobSystem->Shutdown(); // Attende che tutti i job finiscano, evitando uso-dopo-rilascio
            m_context->jobSystem->Initialize(); // Riaccende i thread per gli altri stati
        }
    }
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
    if (m_context && m_context->deviceManager) {
        m_context->deviceManager->requireFreeCursor = true; // Impedisce al mouse di bloccarsi al centro
    }

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
        
        float aspect = 16.0f / 9.0f; 
        if (m_context && m_context->engine && m_context->engine->GetRenderManager()) {
            uint32_t w = m_context->engine->GetRenderManager()->GetWindowWidth();
            uint32_t h = m_context->engine->GetRenderManager()->GetWindowHeight();
            if (h > 0) aspect = (float)w / (float)h;
        }
        m_context->activeCameraView.projectionMatrix = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 2000.0f);
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
    
    // Configurazione Dimensioni Globali della Mappa
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Map Dimensions (Chunks)");
    ImGui::DragInt("Min X", &currentPlanet.minX);
    ImGui::DragInt("Max X", &currentPlanet.maxX);
    ImGui::DragInt("Min Y (Altezza)", &currentPlanet.minY);
    ImGui::DragInt("Max Y (Altezza)", &currentPlanet.maxY);
    ImGui::DragInt("Min Z", &currentPlanet.minZ);
    ImGui::DragInt("Max Z", &currentPlanet.maxZ);
    ImGui::Separator();
    
    ImGui::Text("Regioni in %s: %d", currentPlanet.name.c_str(), (int)currentPlanet.regions.size());
    
    // Strumenti di Disegno (Pennello Biomi)
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "STRUMENTI BIOMI");
    ImGui::Separator();
    
    const char* biomeNames[] = { "Forest", "Desert", "Tundra", "Ocean", "Volcano", "City", "Dungeon", "Portal" };
    ImGui::Combo("Tipo Bioma", &m_paintRegionType, biomeNames, IM_ARRAYSIZE(biomeNames));
    
    ImGui::SliderInt("Dim. Pennello (Chunk)", &m_brushSize, 1, 10);

    ImGui::Spacing();
    if (ImGui::Button("RIEMPI TUTTO CON OCEANO", ImVec2(-1, 30))) {
        currentPlanet.regions.clear();
        fw::MapRegion oceanRegion;
        oceanRegion.rectMin = glm::ivec2(currentPlanet.minX, currentPlanet.minZ);
        oceanRegion.rectMax = glm::ivec2(currentPlanet.maxX, currentPlanet.maxZ);
        oceanRegion.type = fw::MapRegionType::Ocean;
        oceanRegion.label = "Oceano Globale";
        oceanRegion.surfaceBlockId = 4; // Assuming 4 is Water or Sand
        oceanRegion.subsurfaceBlockId = 2; // Stone
        currentPlanet.regions.push_back(oceanRegion);
    }
    
    // Bottom Buttons
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 150.0f); // Spingi in basso
    ImGui::Separator();
    static float saveNotificationTimer = 0.0f;
    if (saveNotificationTimer > 0.0f) {
        saveNotificationTimer -= ImGui::GetIO().DeltaTime;
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[OK] PROGETTO MAPPA SALVATO!");
    }
    if (ImGui::Button("SALVA PROGETTO", ImVec2(-1, 40))) {
        if (m_document.SaveJSON("saves/map/world_map.json")) {
            saveNotificationTimer = 3.0f;
        }
    }
    if (ImGui::Button("COMPILA E GENERA VOXEL (FASE 2)", ImVec2(-1, 40))) {
        CompileAndGenerate();
    }
    if (ImGui::Button("TORNA ALL'HUB", ImVec2(-1, 30))) {
        if (m_previewWorld && m_context->forgeWorld == m_previewWorld.get()) {
            m_context->forgeWorld = nullptr;
        }
        m_context->activeRegistry = nullptr;
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
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "EDITOR MAPPA");
    ImGui::SameLine();
    ImGui::TextDisabled("[Sin: Dipingi] [Des: Cancella] [Rotella: Dim.Pennello] [CTRL+Rotella: Zoom] [CTRL+Sin: Sposta]");
    ImGui::Separator();
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    
    // Usa un InvisibleButton per catturare TUTTI gli input del canvas
    ImGui::InvisibleButton("CanvasHitArea", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool canvasHovered = ImGui::IsItemHovered();
    bool canvasActive = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mousePos = ImGui::GetMousePos();
    
    // Gestione Input
    if (canvasHovered || canvasActive) {
        // Pan con CTRL + Tasto Sinistro
        if (io.KeyCtrl && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            m_canvasPan.x += io.MouseDelta.x;
            m_canvasPan.y += io.MouseDelta.y;
        }
        if (io.MouseWheel != 0.0f) {
            if (io.KeyCtrl) {
                // CTRL + Scroll = Zoom centrato sul mouse
                float zoomFactor = (io.MouseWheel > 0) ? 1.15f : (1.0f / 1.15f);
                float mxRel = mousePos.x - canvasOrigin.x - m_canvasPan.x;
                float myRel = mousePos.y - canvasOrigin.y - m_canvasPan.y;
                m_canvasPan.x -= mxRel * (zoomFactor - 1.0f);
                m_canvasPan.y -= myRel * (zoomFactor - 1.0f);
                m_canvasZoom *= zoomFactor;
                m_canvasZoom = std::clamp(m_canvasZoom, 0.1f, 20.0f);
            } else {
                // Scroll senza CTRL = dimensione pennello
                m_brushSize += (io.MouseWheel > 0) ? 1 : -1;
                m_brushSize = std::clamp(m_brushSize, 1, 50);
            }
        }
    }
    
    // Ripristina il cursore di disegno sopra l'area (sovrascriviamo l'InvisibleButton)
    drawList->AddRectFilled(canvasOrigin,
        ImVec2(canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y),
        IM_COL32(20, 20, 25, 255));
    
    // Dimensione base in pixel di un singolo chunk al livello di zoom 1.0
    const float BASE_CHUNK_SIZE = 20.0f;
    
    // Proietta coordinate Chunk -> Screen (con pan e zoom)
    auto ChunkToScreen = [&](float cx, float cz) -> ImVec2 {
        // Calcola l'offset dal bordo in base alle coordinate chunk
        float offsetX = (cx - currentPlanet.minX) * BASE_CHUNK_SIZE * m_canvasZoom;
        float offsetZ = (cz - currentPlanet.minZ) * BASE_CHUNK_SIZE * m_canvasZoom;
        
        return ImVec2(
            canvasOrigin.x + m_canvasPan.x + offsetX,
            canvasOrigin.y + m_canvasPan.y + offsetZ
        );
    };
    
    // Proietta coordinate Screen -> Chunk
    auto ScreenToChunk = [&](ImVec2 sp) -> glm::ivec2 {
        float sx = sp.x - canvasOrigin.x - m_canvasPan.x;
        float sy = sp.y - canvasOrigin.y - m_canvasPan.y;
        
        float chunkX = (sx / (BASE_CHUNK_SIZE * m_canvasZoom)) + currentPlanet.minX;
        float chunkZ = (sy / (BASE_CHUNK_SIZE * m_canvasZoom)) + currentPlanet.minZ;
        
        return glm::ivec2(
            (int)std::floor(chunkX),
            (int)std::floor(chunkZ)
        );
    };
    
    // ==========================================
    // LAYER 1: LA MAPPA (Sfondo e Regioni con bordi chiari)
    // ==========================================
    ImVec2 mapMin = ChunkToScreen(currentPlanet.minX, currentPlanet.minZ);
    ImVec2 mapMax = ChunkToScreen(currentPlanet.maxX + 1.0f, currentPlanet.maxZ + 1.0f);
    
    // Disegna lo sfondo base della mappa (colore di fallback, es. oceano o void)
    drawList->AddRectFilled(mapMin, mapMax, IM_COL32(30, 30, 40, 255));
    
    // Forza il clipping alle coordinate della mappa in modo che le regioni non escano dai bordi
    drawList->PushClipRect(mapMin, mapMax, true);

    for (int i = 0; i < (int)currentPlanet.regions.size(); ++i) {
        const auto& r = currentPlanet.regions[i];
        ImVec2 pMin = ChunkToScreen(r.rectMin.x, r.rectMin.y);
        ImVec2 pMax = ChunkToScreen(r.rectMax.x, r.rectMax.y);
        if (pMin.x > pMax.x) std::swap(pMin.x, pMax.x);
        if (pMin.y > pMax.y) std::swap(pMin.y, pMax.y);
        
        ImU32 fillColor;
        if (i == m_selectedRegionIndex) {
            fillColor  = IM_COL32(255, 255, 100, 210);
        } else {
            switch (r.type) {
                case fw::MapRegionType::Forest:  fillColor = IM_COL32(40,  180, 40,  255); break;
                case fw::MapRegionType::Desert:  fillColor = IM_COL32(220, 200, 100, 255); break;
                case fw::MapRegionType::Tundra:  fillColor = IM_COL32(180, 220, 220, 255); break;
                case fw::MapRegionType::Ocean:   fillColor = IM_COL32(40,  80,  220, 255); break;
                case fw::MapRegionType::Volcano: fillColor = IM_COL32(220, 40,  40,  255); break;
                case fw::MapRegionType::City:    fillColor = IM_COL32(150, 150, 150, 255); break;
                case fw::MapRegionType::Dungeon: fillColor = IM_COL32(120, 60,  160, 255); break;
                case fw::MapRegionType::Portal:  fillColor = IM_COL32(220, 100, 220, 255); break;
                default:                         fillColor = IM_COL32(100, 200, 100, 255); break;
            }
        }
        
        drawList->AddRectFilled(pMin, pMax, fillColor);
        
        // Bordo regione
        if (i == m_selectedRegionIndex) {
            drawList->AddRect(pMin, pMax, IM_COL32(255, 220, 0, 255), 0, 0, 2.0f);
        }
        
        // Etichetta (se c'è spazio)
        if ((pMax.x - pMin.x) > 20.0f * m_canvasZoom) {
            drawList->AddText(ImVec2(pMin.x + 3, pMin.y + 3), IM_COL32(255,255,255,255), r.label.c_str());
        }
    }
    drawList->PopClipRect();
    
    // Disegna il Bordo Esterno della mappa
    drawList->AddRect(mapMin, mapMax, IM_COL32(255, 255, 255, 255), 0.0f, 0, 3.0f);
    
    // ==========================================
    // LAYER 2: GRIGLIA 1x1 e SELEZIONE CHUNK
    // ==========================================
    // Disegna griglia chunk 1x1 della mappa
    if (m_canvasZoom > 1.5f) {
        for (int gx = currentPlanet.minX; gx <= currentPlanet.maxX + 1; gx++) {
            ImVec2 a = ChunkToScreen(gx, currentPlanet.minZ);
            ImVec2 b = ChunkToScreen(gx, currentPlanet.maxZ + 1.0f);
            drawList->AddLine(a, b, IM_COL32(255, 255, 255, 60));
        }
        for (int gz = currentPlanet.minZ; gz <= currentPlanet.maxZ + 1; gz++) {
            ImVec2 a = ChunkToScreen(currentPlanet.minX, gz);
            ImVec2 b = ChunkToScreen(currentPlanet.maxX + 1.0f, gz);
            drawList->AddLine(a, b, IM_COL32(255, 255, 255, 60));
        }
    }
    
    // Brush Preview e Input (clamped to map)
    if (canvasHovered) {
        glm::ivec2 chunkCoord = ScreenToChunk(mousePos);
        int halfBrush = m_brushSize / 2;
        int bMinX = chunkCoord.x - halfBrush;
        int bMinZ = chunkCoord.y - halfBrush;
        int bMaxX = bMinX + m_brushSize;
        int bMaxZ = bMinZ + m_brushSize;
        
        // Clamping ai confini della mappa (evita di dipingere fuori dai bordi)
        bMinX = std::max((int)currentPlanet.minX, bMinX);
        bMinZ = std::max((int)currentPlanet.minZ, bMinZ);
        bMaxX = std::min((int)currentPlanet.maxX + 1, bMaxX);
        bMaxZ = std::min((int)currentPlanet.maxZ + 1, bMaxZ);
        
        // Se il cursore è valido, disegna la selezione
        if (bMinX < bMaxX && bMinZ < bMaxZ) {
            ImVec2 bScreenMin = ChunkToScreen(bMinX, bMinZ);
            ImVec2 bScreenMax = ChunkToScreen(bMaxX, bMaxZ);
            
            // Bordo azzurro brillante per il pennello (il chunk selezionato)
            drawList->AddRect(bScreenMin, bScreenMax, IM_COL32(50, 255, 255, 255), 0.0f, 0, 3.0f);
            
            // Indicatore dimensione
            char brushLabel[32];
            snprintf(brushLabel, sizeof(brushLabel), "%dx%d", (bMaxX - bMinX), (bMaxZ - bMinZ));
            drawList->AddText(ImVec2(mousePos.x + 12, mousePos.y + 4), IM_COL32(50, 255, 255, 255), brushLabel);
            
            // Tasto Sinistro (senza CTRL): Dipingi (supporta anche il trascinamento)
            if (!io.KeyCtrl && (ImGui::IsItemClicked(ImGuiMouseButton_Left) || (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)))) {
                // Aggiungi una nuova regione quando dipingiamo (con rate limiting o su click per evitare troppe regioni)
                static float paintTimer = 0.0f;
                paintTimer += io.DeltaTime;
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || paintTimer > 0.1f) {
                    fw::MapRegion newRegion;
                    newRegion.rectMin = glm::ivec2(bMinX, bMinZ);
                    newRegion.rectMax = glm::ivec2(bMaxX - 1, bMaxZ - 1);
                    newRegion.type = static_cast<fw::MapRegionType>(m_paintRegionType);
                    
                    if (newRegion.type == fw::MapRegionType::Forest) { newRegion.label = "Foresta"; newRegion.surfaceBlockId = 1; newRegion.subsurfaceBlockId = 3; }
                    else if (newRegion.type == fw::MapRegionType::Desert) { newRegion.label = "Deserto"; newRegion.surfaceBlockId = 6; newRegion.subsurfaceBlockId = 6; } // Sabbia (es. 6)
                    else if (newRegion.type == fw::MapRegionType::Ocean) { newRegion.label = "Oceano"; newRegion.surfaceBlockId = 4; newRegion.subsurfaceBlockId = 2; } // Acqua (4)
                    else if (newRegion.type == fw::MapRegionType::Volcano) { newRegion.label = "Vulcano"; newRegion.surfaceBlockId = 2; newRegion.subsurfaceBlockId = 2; } // Pietra
                    else if (newRegion.type == fw::MapRegionType::Tundra) { newRegion.label = "Tundra"; newRegion.surfaceBlockId = 5; newRegion.subsurfaceBlockId = 3; } // Neve (5)
                    else { newRegion.label = "Regione"; newRegion.surfaceBlockId = 1; }
                    
                    currentPlanet.regions.push_back(newRegion);
                    paintTimer = 0.0f;
                }
            }
            
            // Tasto Destro: Cancella l'ultima regione o svuota
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                if (!currentPlanet.regions.empty()) {
                    currentPlanet.regions.pop_back();
                }
            }
        }
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
        // IMPORTANTE: prima di distruggere il mondo, azzera i puntatori nel context
        // per evitare dangling pointer crash al prossimo frame di rendering
        m_context->forgeWorld = nullptr;
        m_context->activeRegistry = nullptr;
        m_context->isForgeMode = false;
        if (m_context->engine) {
            m_context->engine->SetGameMode(GameMode::Hub); // Evita RenderFairworld col mondo distrutto
        }
        m_previewWorld.reset(); // Ora possiamo distruggere il mondo in sicurezza
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
        
        // 2. Inserisci la cartuccia nel contesto globale
        m_context->targetGameJsonPath = "saves/map/world_map.json"; 
        
        // 3. Lancia il gioco
        m_context->engine->SetGameMode(GameMode::Play);
        m_context->engine->ForceGameState(GameState::PLAYING);
        m_context->stateManager->ChangeState(std::make_unique<PlayState>(m_context));
    }
    
    ImGui::PopStyleColor(3);
    ImGui::End();
}

void MapState::CompileAndGenerate() {
    std::cout << "[MapState] Inizio compilazione e generazione voxel per mappa piatta...\n";
    
    // Auto-salva prima di generare l'anteprima in modo che il file non venga perso!
    m_document.SaveJSON("saves/map/world_map.json");

    // 0. Se esiste già un mondo precedente, prima pulisci i puntatori nel context
    if (m_previewWorld) {
        m_context->forgeWorld = nullptr;
        m_context->activeRegistry = nullptr;
    }

    // 1. Alloca il mondo di collaudo e resettalo se esisteva gia'
    m_previewWorld = std::make_unique<fw::GameWorld>();
    m_previewWorld->Initialize(m_context);
    
    // Pulisci il vecchio LOD sferico (non serve per la mappa voxel piatta)
    m_lodSystem.SetPlanetRadius(0.0f);
    m_planetRootNodes.clear();
    
    // Agganciamo il nuovo ForgeWorld al motore di rendering ORA, prima di generare!
    m_context->activeRegistry = &m_previewWorld->GetRegistry();
    m_context->forgeWorld = m_previewWorld.get(); // FONDAMENTALE PER RENDERFAIRWORLD
    m_context->isForgeMode = false;

    if (m_context->engine && m_context->engine->GetRenderManager()) {
        m_context->engine->GetRenderManager()->InvalidateForgeCache();
    }
    
    // Imposta il GameMode a Map cosi' RenderFairworld viene chiamato correttamente
    if (m_context->engine) {
        m_context->engine->SetGameMode(GameMode::Map);
    }
    
    // 2. Compila i dati 2D in Voxel 3D usando il MapWorldGenerator
    fw::MapWorldGenerator::Generate(m_document, m_activePlanetIndex, *m_previewWorld, m_context->jobSystem);

    // 3. Passa alla visuale 3D
    m_isBuilderMode = false; 

    const auto& currentPlanet = m_document.planets[m_activePlanetIndex];

    // Posiziona la telecamera al centro della mappa generata (superficie del terreno ~ Y=20)
    float midX = ((float)(currentPlanet.maxX + currentPlanet.minX) * 0.5f) * 16.0f;
    float midZ = ((float)(currentPlanet.maxZ + currentPlanet.minZ) * 0.5f) * 16.0f;
    float midY = 20.0f; // Superficie media del terreno
    m_orbitTarget = glm::vec3(midX, midY, midZ); // FONDAMENTALE: punta la camera al centro!
    float mapSpan = std::max((float)(currentPlanet.maxX - currentPlanet.minX), (float)(currentPlanet.maxZ - currentPlanet.minZ)) * 16.0f;
    m_orbitDistance = std::max(mapSpan * 1.5f, 60.0f);
    m_orbitPitch = 40.0f;
    m_orbitYaw = 45.0f;

    std::cout << "[MapState] Anteprima generata! Camera al centro: (" << midX << ", " << midY << ", " << midZ << ")\n";
}
