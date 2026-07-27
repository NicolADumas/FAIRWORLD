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
#include "VulkanDmaManager.h"
#include <imgui.h>
#include <iostream>
#include <algorithm>

MapState::MapState(SharedContext* context) : m_context(context) {
    std::cout << "[MapState] Costruito.\n";
}

MapState::~MapState() {
    std::cout << "[MapState] Distrutto.\n";
    if (m_context) {
        if (m_previewWorld && m_context->forgeWorld == m_previewWorld.get()) {
            m_context->activeRegistry = m_context->gameWorld ? &m_context->gameWorld->GetRegistry() : nullptr;
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
        // Fallback: Setup iniziale del Mega-Pianeta Unificato
        fw::PlanetMap megaPlanet = { ::PlanetType::EarthPrime, "Fairworld Prime", {} };
        m_document.planets = { megaPlanet };
        std::cout << "[MapState] Nessun salvataggio valido trovato. Creato nuovo Mega-Pianeta.\n";
    }

    if (!m_context->vramAllocator) {
        m_context->vramAllocator = new fw::VramSlabAllocator(2048ULL * 1024ULL * 1024ULL);
    }
    if (!m_context->dmaManager) {
        m_context->dmaManager = new fw::VulkanDmaManager();
        if (auto* rm = m_context->engine->GetRenderManager()) {
            m_context->dmaManager->Initialize(
                rm->GetDevice(), rm->GetTransferQueue(), rm->GetTransferCommandPool(),
                rm->GetStagingRingBuffer(), rm->GetStagingDeviceMemory(), rm->GetMappedStagingData(),
                rm->GetStagingBufferSize(), rm->GetGlobalVramBuffer(), rm->GetQueueMutex()
            );
            m_document.isCompiled = false;
        }
    }

    // --- CRITICAL FIX FOR GPU RENDERING BUG ---
    // Ensure JobSystem is initialized so background meshes can be generated and sent to GPU.
    if (m_context) {
        if (!m_context->jobSystem) {
            m_context->jobSystem = new fw::JobSystem();
            m_context->jobSystem->Initialize();
        }
    }

    // Inizializza immediatamente il mondo di anteprima 3D (Globo)
    m_previewWorld = std::make_unique<fw::GameWorld>();
    m_previewWorld->Initialize(m_context);

    if (m_context->engine) {
        m_context->engine->SetGameMode(GameMode::Map);
        m_context->activeRegistry = &m_previewWorld->GetRegistry();
        m_context->forgeWorld = m_previewWorld.get();
    }

    // Inizializza i 6 root nodes della Cube-Sphere (LOD Sferico)
    float R = 50.0f;
    m_lodSystem.SetPlanetRadius(R);
    m_planetRootNodes.clear();

    // 6 facce del cubo normalizzate a sfera
    // Faccia +Z (Front)
    m_planetRootNodes.emplace_back(glm::vec3(0, 0, R), R, 2, glm::vec3(-1,-1,1), glm::vec3(1,-1,1), glm::vec3(-1,1,1), glm::vec3(1,1,1));
    // Faccia -Z (Back)
    m_planetRootNodes.emplace_back(glm::vec3(0, 0, -R), R, 2, glm::vec3(1,-1,-1), glm::vec3(-1,-1,-1), glm::vec3(1,1,-1), glm::vec3(-1,1,-1));
    // Faccia +X (Right)
    m_planetRootNodes.emplace_back(glm::vec3(R, 0, 0), R, 2, glm::vec3(1,-1,1), glm::vec3(1,-1,-1), glm::vec3(1,1,1), glm::vec3(1,1,-1));
    // Faccia -X (Left)
    m_planetRootNodes.emplace_back(glm::vec3(-R, 0, 0), R, 2, glm::vec3(-1,-1,-1), glm::vec3(-1,-1,1), glm::vec3(-1,1,-1), glm::vec3(-1,1,1));
    // Faccia +Y (Top)
    m_planetRootNodes.emplace_back(glm::vec3(0, R, 0), R, 2, glm::vec3(-1,1,1), glm::vec3(1,1,1), glm::vec3(-1,1,-1), glm::vec3(1,1,-1));
    // Faccia -Y (Bottom)
    m_planetRootNodes.emplace_back(glm::vec3(0, -R, 0), R, 2, glm::vec3(-1,-1,-1), glm::vec3(1,-1,-1), glm::vec3(-1,-1,1), glm::vec3(1,-1,1));

    return true;
}

void MapState::Update(float dt) {
    if (m_statusTimer > 0.0f) {
        m_statusTimer -= dt;
    }
    
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
        if (!m_document.planets.empty()) {
            m_activePlanetIndex = 0; // Forza il Mega-Pianeta
            pMap = &m_document.planets[0];
        }
        
        glm::mat4 vpMatrix = m_context->activeCameraView.projectionMatrix * m_context->activeCameraView.viewMatrix;
        
        for (auto& root : m_planetRootNodes) {
            m_lodSystem.UpdateLODTree(root, camPos, m_previewWorld.get(), m_context->jobSystem, m_context->assetManager, pMap, vpMatrix);
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

    ImGui::Begin("MAP BUILDER - COESISTENZA", nullptr, windowFlags);
    
    float width = viewport->Size.x;
    float height = viewport->Size.y;
    
    if (m_document.planets.empty() || m_activePlanetIndex < 0 || m_activePlanetIndex >= m_document.planets.size()) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "[ERRORE CRITICO] Nessun pianeta caricato nel documento!");
        if (ImGui::Button("Forza Ripristino Pianeta Default")) {
            m_document.planets.push_back({ ::PlanetType::EarthPrime, "Terra Prime (Ripristinata)", {} });
            m_activePlanetIndex = 0;
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
    }
    
    auto& currentPlanet = m_document.planets[m_activePlanetIndex];
    float leftWidth = width * 0.45f;
    float rightWidth = width * 0.55f;
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mousePos = ImGui::GetMousePos();

    // ==========================================
    // PANNELLO SINISTRO - EDITOR TERRENI (2D)
    // ==========================================
    ImGui::BeginChild("TerrainEditor", ImVec2(leftWidth, 0), true);
    ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f), "EDITOR TERRENI (TEMPLATE 2D)");
    ImGui::Separator();
    
    // Gestione Libreria
    if (ImGui::Button("AGGIUNGI NUOVO MODELLO", ImVec2(-1, 30))) {
        fw::TerrainTemplate t;
        t.name = "Terreno " + std::to_string(m_document.terrainLibrary.size() + 1);
        t.id = "terrain_" + std::to_string(m_document.terrainLibrary.size() + 1);
        m_document.terrainLibrary.push_back(t);
        m_activeTemplateIndex = (int)m_document.terrainLibrary.size() - 1;
    }
    
    ImGui::BeginChild("LibraryList", ImVec2(0, 100), true);
    for (int i = 0; i < (int)m_document.terrainLibrary.size(); ++i) {
        bool isSelected = (m_activeTemplateIndex == i);
        if (ImGui::Selectable((std::to_string(i+1) + ". " + m_document.terrainLibrary[i].name).c_str(), isSelected)) {
            m_activeTemplateIndex = i;
        }
    }
    ImGui::EndChild();
    
    if (m_activeTemplateIndex >= 0 && m_activeTemplateIndex < m_document.terrainLibrary.size()) {
        auto& activeTemplate = m_document.terrainLibrary[m_activeTemplateIndex];
        
        if (ImGui::CollapsingHeader("Proprietà Generali Modello", ImGuiTreeNodeFlags_DefaultOpen)) {
            char labelBuf[128];
            strncpy_s(labelBuf, activeTemplate.name.c_str(), sizeof(labelBuf));
            if (ImGui::InputText("Nome Modello", labelBuf, sizeof(labelBuf))) activeTemplate.name = labelBuf;
            
            const char* biomeNames[] = { "Forest", "Desert", "Tundra", "Ocean", "Volcano", "City", "Dungeon", "Portal" };
            int typeIdx = static_cast<int>(activeTemplate.baseType);
            if (ImGui::Combo("Bioma Base", &typeIdx, biomeNames, IM_ARRAYSIZE(biomeNames))) activeTemplate.baseType = static_cast<fw::MapRegionType>(typeIdx);
            
            ImGui::SliderFloat("Frequenza Perlin Base", &activeTemplate.basePerlinFrequency, 0.001f, 0.1f, "%.4f");
            ImGui::SliderFloat("Modificatore Gravità", &activeTemplate.baseGravityModifier, 0.1f, 5.0f, "%.2f");
            int seed = (int)activeTemplate.seed;
            if (ImGui::InputInt("Seme (Seed)", &seed)) activeTemplate.seed = seed;
        }
        
        // Strumenti Canvas 2D
        if (ImGui::CollapsingHeader("Strumenti Disegno (Micro-Design)", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* shapeNames[] = { "Rettangolo", "Cerchio", "Rombo", "Stella" };
            ImGui::Combo("Forma Struttura", &m_paintBrushShape, shapeNames, IM_ARRAYSIZE(shapeNames));
            
            const char* biomeNames[] = { "Forest", "Desert", "Tundra", "Ocean", "Volcano", "City", "Dungeon", "Portal" };
            ImGui::Combo("Tipo Sotto-Regione", &m_paintRegionType, biomeNames, IM_ARRAYSIZE(biomeNames));
            ImGui::SliderInt("Dimensione Pennello", &m_brushSize, 1, 10);
            if (ImGui::Button("PULISCI MICRO-DESIGN", ImVec2(-1, 25))) {
                activeTemplate.subRegions.clear();
            }
        }
        
        // CANVAS 2D
        ImGui::Text("Dipingi Dettagli (Fiumi, Zone, ecc.)");
        ImGuiWindowFlags canvasFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::BeginChild("Canvas2D", ImVec2(0, 300), true, canvasFlags);
        
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("CanvasHitArea", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        bool canvasHovered = ImGui::IsItemHovered();
        bool canvasActive = ImGui::IsItemActive();
        
        if (canvasHovered || canvasActive) {
            if (io.KeyCtrl && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                m_canvasPan.x += io.MouseDelta.x;
                m_canvasPan.y += io.MouseDelta.y;
            }
            if (io.MouseWheel != 0.0f) {
                if (io.KeyCtrl) {
                    m_canvasZoom *= (io.MouseWheel > 0) ? 1.15f : (1.0f / 1.15f);
                    m_canvasZoom = std::clamp(m_canvasZoom, 0.1f, 20.0f);
                }
            }
        }
        
        drawList->AddRectFilled(canvasOrigin, ImVec2(canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y), IM_COL32(20, 20, 25, 255));
        
        const float BASE_CHUNK_SIZE = 10.0f;
        auto ChunkToScreen = [&](float cx, float cz) -> ImVec2 {
            return ImVec2(canvasOrigin.x + canvasSize.x * 0.5f + m_canvasPan.x + cx * BASE_CHUNK_SIZE * m_canvasZoom,
                          canvasOrigin.y + canvasSize.y * 0.5f + m_canvasPan.y + cz * BASE_CHUNK_SIZE * m_canvasZoom);
        };
        auto ScreenToChunk = [&](ImVec2 sp) -> glm::ivec2 {
            float sx = sp.x - (canvasOrigin.x + canvasSize.x * 0.5f + m_canvasPan.x);
            float sy = sp.y - (canvasOrigin.y + canvasSize.y * 0.5f + m_canvasPan.y);
            return glm::ivec2((int)std::floor(sx / (BASE_CHUNK_SIZE * m_canvasZoom)),
                              (int)std::floor(sy / (BASE_CHUNK_SIZE * m_canvasZoom)));
        };
        
        // Disegna una "Cornice" del Chunk
        ImVec2 mapMin = ChunkToScreen(-16, -16);
        ImVec2 mapMax = ChunkToScreen(16, 16);
        drawList->AddRect(mapMin, mapMax, IM_COL32(100, 100, 100, 255), 0.0f, 0, 2.0f);
        
        drawList->PushClipRect(mapMin, mapMax, true);
        for (const auto& r : activeTemplate.subRegions) {
            ImVec2 pMin = ChunkToScreen(r.rectMin.x, r.rectMin.y);
            ImVec2 pMax = ChunkToScreen(r.rectMax.x + 1, r.rectMax.y + 1);
            if (pMin.x > pMax.x) std::swap(pMin.x, pMax.x);
            if (pMin.y > pMax.y) std::swap(pMin.y, pMax.y);
            ImU32 fillColor = IM_COL32(150, 150, 150, 200);
            switch (r.type) {
                case fw::MapRegionType::Forest:  fillColor = IM_COL32(40,  180, 40,  235); break;
                case fw::MapRegionType::Ocean:   fillColor = IM_COL32(40,  80,  220, 235); break;
                // etc... (semplificato)
            }
            drawList->AddRectFilled(pMin, pMax, fillColor);
        }
        drawList->PopClipRect();
        
        // Logica di pittura
        if (canvasHovered) {
            glm::ivec2 cCoord = ScreenToChunk(mousePos);
            int halfB = m_brushSize / 2;
            int bMinX = cCoord.x - halfB;
            int bMinZ = cCoord.y - halfB;
            int bMaxX = cCoord.x - halfB + m_brushSize;
            int bMaxZ = cCoord.y - halfB + m_brushSize;
            
            drawList->AddRect(ChunkToScreen(bMinX, bMinZ), ChunkToScreen(bMaxX, bMaxZ), IM_COL32(255, 255, 0, 255));
            
            if (!io.KeyCtrl && ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                fw::MapRegion nr;
                nr.rectMin = glm::ivec2(bMinX, bMinZ);
                nr.rectMax = glm::ivec2(bMaxX - 1, bMaxZ - 1);
                nr.type = static_cast<fw::MapRegionType>(m_paintRegionType);
                nr.shape = static_cast<fw::RegionShape>(m_paintBrushShape);
                activeTemplate.subRegions.push_back(nr);
                m_hasUnsavedChanges = true;
            }
        }
        ImGui::EndChild(); // Canvas2D
    } else {
        ImGui::TextDisabled("Nessun Modello selezionato.");
    }
    
    // Bottom Buttons Sidebar
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 150.0f);
    ImGui::Separator();
    if (m_hasUnsavedChanges) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "🗺️ Modifiche non salvate!");
    if (ImGui::Button("SALVA IL PROGETTO MAPPA", ImVec2(-1, 35))) {
        if (m_document.SaveJSON("saves/map/world_map.json")) m_hasUnsavedChanges = false;
    }
    if (ImGui::Button("TORNA AD HUB STATE", ImVec2(-1, 28))) {
        m_context->engine->SetGameMode(GameMode::Hub);
        m_context->engine->ForceGameState(GameState::MAIN_MENU);
        m_context->stateManager->ChangeState(std::make_unique<HubState>(m_context));
    }
    ImGui::EndChild(); // Sidebar
    
    ImGui::SameLine();
    
    // ==========================================
    // PANNELLO DESTRO - EDITOR PIANETA 3D
    // ==========================================
    ImGui::BeginChild("Planet3D", ImVec2(rightWidth, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "EDITOR PIANETA 3D (APPLICA MODELLI)");
    ImGui::SameLine();
    ImGui::TextDisabled("[Sin: Stampa Modello] [Des: Ruota Sfera] [Rotella: Zoom]");
    ImGui::Separator();
    
    ImVec2 pOrigin = ImGui::GetCursorScreenPos();
    ImVec2 pSize = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("PlanetHitArea", pSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool pHovered = ImGui::IsItemHovered();
    bool pActive = ImGui::IsItemActive();
    
    if (pHovered || pActive) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            m_orbitYaw -= io.MouseDelta.x * 0.5f;
            m_orbitPitch += io.MouseDelta.y * 0.5f;
            m_orbitPitch = std::clamp(m_orbitPitch, -89.0f, 89.0f);
        }
        if (io.MouseWheel != 0.0f) {
            m_orbitDistance -= io.MouseWheel * 10.0f;
            m_orbitDistance = std::clamp(m_orbitDistance, 50.0f, 1000.0f);
        }
    }
        
    m_orbitTarget = glm::vec3(0.0f, 0.0f, 0.0f);
    m_context->activeCameraView.cameraPosition = m_orbitTarget + 
        glm::vec3(
            cos(glm::radians(m_orbitPitch)) * cos(glm::radians(m_orbitYaw)),
            sin(glm::radians(m_orbitPitch)),
            cos(glm::radians(m_orbitPitch)) * sin(glm::radians(m_orbitYaw))
        ) * m_orbitDistance;
        
    m_context->activeCameraView.cameraFront = glm::normalize(m_orbitTarget - m_context->activeCameraView.cameraPosition);
    glm::vec3 up = glm::vec3(0, 1, 0);
    
    m_context->activeCameraView.viewMatrix = glm::lookAt(
        m_context->activeCameraView.cameraPosition,
        m_context->activeCameraView.cameraPosition + m_context->activeCameraView.cameraFront,
        up
    );
    
    m_context->activeCameraView.projectionMatrix = glm::perspective(
        glm::radians(60.0f), pSize.x / std::max(pSize.y, 1.0f), 0.1f, 1000.0f
    );
    m_context->activeCameraView.projectionMatrix[1][1] *= -1;

    // RAYCAST PER STAMPARE SUL PIANETA
    if ((pHovered || pActive) && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyCtrl) {
            float x = (io.MousePos.x - pOrigin.x) / pSize.x * 2.0f - 1.0f;
            float y = 1.0f - (io.MousePos.y - pOrigin.y) / pSize.y * 2.0f;
            glm::vec4 clipCoords(x, y, -1.0f, 1.0f);
            glm::vec4 eyeCoords = glm::inverse(m_context->activeCameraView.projectionMatrix) * clipCoords;
            eyeCoords = glm::vec4(eyeCoords.x, eyeCoords.y, -1.0f, 0.0f);
            glm::vec3 rayDir = glm::normalize(glm::vec3(glm::inverse(m_context->activeCameraView.viewMatrix) * eyeCoords));
            glm::vec3 rayOrig = m_context->activeCameraView.cameraPosition;
            
            float radius = 50.0f; // Raggio sferico di base
            float a = glm::dot(rayDir, rayDir);
            float b = 2.0f * glm::dot(rayDir, rayOrig);
            float c = glm::dot(rayOrig, rayOrig) - radius * radius;
            float disc = b * b - 4 * a * c;
            
            if (disc >= 0 && m_activeTemplateIndex >= 0) {
                float dist = (-b - sqrt(disc)) / (2.0f * a);
                if (dist > 0) {
                    fw::PlanetChunkInstance inst;
                    inst.templateId = m_document.terrainLibrary[m_activeTemplateIndex].id;
                    inst.centerNormal = glm::normalize(rayOrig + rayDir * dist);
                    inst.angularRadius = m_document.terrainLibrary[m_activeTemplateIndex].angularRadius; // Wait, angular radius non e' in template, let's fix it later. We'll use a default 0.2f.
                    inst.angularRadius = 0.2f;
                    
                    currentPlanet.chunkInstances.push_back(inst);
                    m_hasUnsavedChanges = true;
                }
            }
        }
    }
    
    // UI Overlay per disfare
    ImGui::SetCursorPos(ImVec2(pOrigin.x - ImGui::GetWindowPos().x + 10, pOrigin.y - ImGui::GetWindowPos().y + 10));
    ImGui::Text("Istanze Applicate: %d", (int)currentPlanet.chunkInstances.size());
    if (ImGui::Button("ANNULLA ULTIMA ISTANZA")) {
        if (!currentPlanet.chunkInstances.empty()) {
            currentPlanet.chunkInstances.pop_back();
            m_hasUnsavedChanges = true;
        }
    }
    
    ImGui::EndChild(); // Planet3D
    ImGui::End(); // MAP BUILDER
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
    
    // --- UI DEBUG FEEDBACK ---
    if (m_statusTimer > 0.0f) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "[STATUS]: %s", m_lastActionStatus.c_str());
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[STATUS]: In attesa...");
    }
    ImGui::Spacing();
    
    // --- COMMAND BUTTONS ---
    if (ImGui::Button("SALVA IL PROGETTO MAPPA", ImVec2(-1, 35))) {
        if (m_document.SaveJSON("saves/map/world_map.json")) {
            m_lastActionStatus = "Progetto salvato con successo!";
            m_statusTimer = 5.0f;
            std::cout << "[DEBUG] [MapBuilder] " << m_lastActionStatus << "\n";
            m_hasUnsavedChanges = false;
        } else {
            m_lastActionStatus = "Errore durante il salvataggio!";
            m_statusTimer = 5.0f;
            std::cerr << "[DEBUG] [MapBuilder] " << m_lastActionStatus << "\n";
        }
    }
    
    if (ImGui::Button("COMPILA E GENERA ANTEPRIMA MAPPA", ImVec2(-1, 35))) {
        m_lastActionStatus = "Compilazione Anteprima Voxel avviata...";
        m_statusTimer = 5.0f;
        std::cout << "[DEBUG] [MapBuilder] " << m_lastActionStatus << "\n";
        CompileAndGenerate();
        m_previewIsUpToDate = true;
    }
    
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
    if (ImGui::Button("TORNA ALL'EDITOR MAPPA (2D)", ImVec2(-1, 30))) {
        // Ferma i thread in background per evitare crash (use-after-free)
        if (m_context && m_context->jobSystem) {
            m_context->jobSystem->Shutdown();
            m_context->jobSystem->Initialize();
        }
        // Ritorna al Builder senza uscire dallo state
        m_previewWorld.reset(); 
        m_isBuilderMode = true;
    }
    ImGui::PopStyleColor(3);

    ImGui::Spacing();
    
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("ESCI AL MENU PRINCIPALE (HUB)", ImVec2(-1, 30))) {
        m_context->forgeWorld = nullptr;
        m_context->activeRegistry = nullptr;
        m_context->isForgeMode = false;
        if (m_context->engine) {
            m_context->engine->SetGameMode(GameMode::Hub); 
        }
        // Ferma i thread in background per evitare crash (use-after-free)
        if (m_context && m_context->jobSystem) {
            m_context->jobSystem->Shutdown();
            m_context->jobSystem->Initialize();
        }
        m_previewWorld.reset(); 
        m_context->stateManager->ChangeState(std::make_unique<HubState>(m_context));
    }
    ImGui::PopStyleColor(3);
    
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
        // Ferma i thread in background per evitare che usino il mondo in distruzione
        if (m_context && m_context->jobSystem) {
            m_context->jobSystem->Shutdown();
            m_context->jobSystem->Initialize();
        }
        m_context->forgeWorld = nullptr;
        m_context->activeRegistry = nullptr;
    }

    // --- CRITICAL FIX FOR GPU RENDERING BUG ---
    // Ensure JobSystem is initialized so background meshes can be generated and sent to GPU.
    if (m_context) {
        if (!m_context->jobSystem) {
            m_context->jobSystem = new fw::JobSystem();
            m_context->jobSystem->Initialize();
        }
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
    
    // --- FLATTEN TEMPLATES IN REGIONS PER GENERATORE ---
    auto& currentPlanet = m_document.planets[m_activePlanetIndex];
    currentPlanet.regions.clear(); // Usa regions come cache flat
    
    for (const auto& inst : currentPlanet.chunkInstances) {
        // Cerca il template nella libreria
        const fw::TerrainTemplate* tmpl = nullptr;
        for (const auto& t : m_document.terrainLibrary) {
            if (t.id == inst.templateId) {
                tmpl = &t;
                break;
            }
        }
        
        if (tmpl) {
            // Aggiungi una macro-regione base per il template
            fw::MapRegion baseRegion;
            baseRegion.centerNormal = inst.centerNormal;
            baseRegion.angularRadius = inst.angularRadius;
            baseRegion.type = tmpl->baseType;
            baseRegion.perlinFrequency = tmpl->basePerlinFrequency;
            baseRegion.gravityModifier = tmpl->baseGravityModifier;
            baseRegion.seed = tmpl->seed;
            currentPlanet.regions.push_back(baseRegion);
            
            // TODO: In futuro, mappare anche le tmpl->subRegions (micro-design) usando proiezioni sferiche.
        }
    }

    // 2. Compila i dati 2D in Voxel 3D usando il MapWorldGenerator
    fw::MapWorldGenerator::Generate(m_document, m_activePlanetIndex, *m_previewWorld, m_context->jobSystem);

    // 3. Passa alla visuale 3D
    m_isBuilderMode = false; 
    m_hasUnsavedChanges = false;
    m_previewIsUpToDate = true;

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
