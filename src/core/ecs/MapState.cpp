#include "pch.h"
#include "MapState.h"
#include "SharedContext.h"
#include "StateManager.h"
#include "HubState.h"
#include "FAIRWORLD.h"
#include "MapWorldGenerator.h"
#include "ForgeWorld.h"
#include "PlayState.h"
#include "BlockRegistry.h"
#include "ForgeComponents.h"
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
    
    // Strumenti di Disegno (Brush)
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Strumenti di Disegno (Brush)");
    ImGui::SliderInt("Dimensione Pennello (Chunk)", &m_brushSize, 1, 50);
    
    // Tipi e Blocchi Pennello
    const char* regionTypes[] = { "Foresta", "Deserto", "Tundra", "Oceano", "Vulcano", "Citta", "Dungeon", "Portale" };
    ImGui::Combo("Tipo Regione", &m_paintRegionType, regionTypes, IM_ARRAYSIZE(regionTypes));
    
    if (m_context->blockRegistry) {
        auto& blocks = m_context->blockRegistry->GetAllBlocks();
        std::vector<const char*> blockNames;
        std::vector<uint8_t> blockIds;
        for (const auto& def : blocks) {
            blockNames.push_back(def.displayName.c_str());
            blockIds.push_back(def.id);
        }
        
        int currentSurfIdx = -1, currentSubIdx = -1;
        for(size_t i=0; i<blockIds.size(); ++i) {
            if(blockIds[i] == m_paintSurfaceBlock) currentSurfIdx = (int)i;
            if(blockIds[i] == m_paintSubsurfaceBlock) currentSubIdx = (int)i;
        }
        if (ImGui::Combo("Blocco Superficie", &currentSurfIdx, blockNames.data(), (int)blockNames.size())) {
            if (currentSurfIdx >= 0) m_paintSurfaceBlock = blockIds[currentSurfIdx];
        }
        if (ImGui::Combo("Blocco Sottoterra", &currentSubIdx, blockNames.data(), (int)blockNames.size())) {
            if (currentSubIdx >= 0) m_paintSubsurfaceBlock = blockIds[currentSubIdx];
        }
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
        ImGui::DragInt2("Rect Min (X,Z)", &r.rectMin.x);
        ImGui::DragInt2("Rect Max (X,Z)", &r.rectMax.x);
        
        // Controlli blocchi (surface / subsurface)
        if (m_context->blockRegistry) {
            auto& blocks = m_context->blockRegistry->GetAllBlocks();
            std::vector<const char*> blockNames;
            std::vector<uint8_t> blockIds;
            for (const auto& def : blocks) {
                blockNames.push_back(def.displayName.c_str());
                blockIds.push_back(def.id);
            }
            int currentSurfIdx = -1;
            int currentSubIdx = -1;
            for(size_t i=0; i<blockIds.size(); ++i) {
                if(blockIds[i] == r.surfaceBlockId) currentSurfIdx = (int)i;
                if(blockIds[i] == r.subsurfaceBlockId) currentSubIdx = (int)i;
            }
            if (ImGui::Combo("Surface Block", &currentSurfIdx, blockNames.data(), (int)blockNames.size())) {
                if (currentSurfIdx >= 0) r.surfaceBlockId = blockIds[currentSurfIdx];
            }
            if (ImGui::Combo("Subsurface Block", &currentSubIdx, blockNames.data(), (int)blockNames.size())) {
                if (currentSubIdx >= 0) r.subsurfaceBlockId = blockIds[currentSubIdx];
            }
        }
        
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
            static glm::ivec2 lastPaintedCoord = glm::ivec2(-9999, -9999);
            if (!io.KeyCtrl && (ImGui::IsItemClicked(ImGuiMouseButton_Left) || (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)))) {
                if (lastPaintedCoord != glm::ivec2(bMinX, bMinZ)) {
                    fw::MapRegion newRegion;
                    newRegion.label      = "Regione";
                    newRegion.type       = static_cast<fw::MapRegionType>(m_paintRegionType);
                    newRegion.rectMin    = glm::ivec2(bMinX, bMinZ);
                    newRegion.rectMax    = glm::ivec2(bMaxX, bMaxZ);
                    newRegion.surfaceBlockId    = (uint8_t)m_paintSurfaceBlock;
                    newRegion.subsurfaceBlockId = (uint8_t)m_paintSubsurfaceBlock;
                    newRegion.seed       = 12345;
                    newRegion.gravityModifier  = 1.0f;
                    newRegion.perlinFrequency  = 0.03f;
                    newRegion.treeDensity      = 0.5f;
                    currentPlanet.regions.push_back(newRegion);
                    m_selectedRegionIndex = (int)currentPlanet.regions.size() - 1;
                    lastPaintedCoord = glm::ivec2(bMinX, bMinZ);
                }
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                lastPaintedCoord = glm::ivec2(-9999, -9999);
            }
            
            // Tasto Destro: Cancella la regione sotto il cursore
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                for (int i = (int)currentPlanet.regions.size() - 1; i >= 0; --i) {
                    const auto& r = currentPlanet.regions[i];
                    if (chunkCoord.x >= r.rectMin.x && chunkCoord.x < r.rectMax.x &&
                        chunkCoord.y >= r.rectMin.y && chunkCoord.y < r.rectMax.y) {
                        currentPlanet.regions.erase(currentPlanet.regions.begin() + i);
                        if (m_selectedRegionIndex == i) m_selectedRegionIndex = -1;
                        else if (m_selectedRegionIndex > i) m_selectedRegionIndex--;
                        break;
                    }
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
    
    // 1. Alloca il mondo di collaudo e resettalo se esisteva gia'
    m_previewWorld = std::make_unique<fw::ForgeWorld>();
    m_previewWorld->Initialize(m_context);
    
    // Pulisci il vecchio LOD sferico (non serve per la mappa voxel piatta)
    m_lodSystem.SetPlanetRadius(0.0f);
    m_planetRootNodes.clear();
    
    // BUG FIX: Agganciamo il nuovo ForgeWorld al motore di rendering ORA, prima di generare!
    m_context->activeRegistry = &m_previewWorld->GetRegistry();
    m_context->isForgeMode = true; 
    
    // 2. Compila i dati 2D in Voxel 3D (Fase di Voxelizzazione)
    const auto& currentPlanet = m_document.planets[m_activePlanetIndex];
    for (int cx = currentPlanet.minX; cx <= currentPlanet.maxX; ++cx) {
        for (int cz = currentPlanet.minZ; cz <= currentPlanet.maxZ; ++cz) {
            std::string chunkName = "Chunk_" + std::to_string(cx) + "_" + std::to_string(cz);
            
            // Crea l'entita' nel previewWorld
            entt::entity chunkEntity = m_previewWorld->CreateChunkEntity(chunkName, fw::Vec3{cx * 16.0f, 0.0f, cz * 16.0f});
            auto& chunk = m_previewWorld->GetRegistry().get<fw::VoxelChunkComponent>(chunkEntity);
            
            // Identifica quale regione copre questo specifico chunk 1x1
            const fw::MapRegion* activeRegion = nullptr;
            for (int i = (int)currentPlanet.regions.size() - 1; i >= 0; --i) {
                const auto& r = currentPlanet.regions[i];
                if (cx >= r.rectMin.x && cx < r.rectMax.x && cz >= r.rectMin.y && cz < r.rectMax.y) {
                    activeRegion = &r;
                    break;
                }
            }
            
            uint8_t surfaceBlock = activeRegion ? activeRegion->surfaceBlockId : 1; // 1 = Grass
            uint8_t subsurfaceBlock = activeRegion ? activeRegion->subsurfaceBlockId : 3; // 3 = Dirt
            
            // Riempi i voxel proceduralmente
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    int height = 20; // Piattaforma base
                    
                    for (int y = 0; y < height; ++y) {
                        if (y == height - 1) chunk.blocks[x][y][z] = surfaceBlock;
                        else if (y > height - 4) chunk.blocks[x][y][z] = subsurfaceBlock;
                        else chunk.blocks[x][y][z] = 2; // 2 = Stone
                        chunk.light[x][y][z] = 255;
                    }
                    for (int y = height; y < 128; ++y) {
                        chunk.blocks[x][y][z] = 0; // Aria
                        chunk.light[x][y][z] = 255;
                    }
                }
            }
            
            // Blocca il chunk in modo che ForgeWorld non sovrascriva con il suo noise generico
            chunk.isGenerated = true;
        }
    }
    
    // 3. Passa alla visuale 3D
    m_isBuilderMode = false; 

    // Posiziona la telecamera al centro della mappa, un po' in alto
    float midX = ((currentPlanet.maxX + currentPlanet.minX) / 2.0f) * 16.0f;
    float midZ = ((currentPlanet.maxZ + currentPlanet.minZ) / 2.0f) * 16.0f;
    m_orbitDistance = std::max((currentPlanet.maxX - currentPlanet.minX) * 10.0f, 150.0f);
    m_orbitPitch = 45.0f;
    m_orbitYaw = 45.0f;
}
