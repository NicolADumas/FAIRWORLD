#include "pch.h"
#include "ChunkEditorState.h"
#include "SharedContext.h"
#include "StateManager.h"
#include "HubState.h"
#include "WorldProjectManager.h"
#include "BlockRegistry.h"
#include "FAIRWORLD.h"
#include "DeviceManager.h"
#include "MapWorldGenerator.h"
#include "BiomeSystems.h"
#include "JobSystem.h"
#include "RenderManager.h"
#include "CacheManager.h"
#include "imgui.h"
#include <iostream>
#include <algorithm>
#include <cmath>

ChunkEditorState::ChunkEditorState(SharedContext* context) : AppBaseState(context) {
    std::cout << "[ChunkEditorState] Costruito come estensione di AppBaseState.\n";
}

ChunkEditorState::~ChunkEditorState() {
    if (m_context) {
        if (m_previewWorld && m_context->forgeWorld == m_previewWorld.get()) {
            m_context->forgeWorld = nullptr;
        }
        if (m_previewWorld && m_context->activeRegistry == &m_previewWorld->GetRegistry()) {
            m_context->activeRegistry = nullptr;
        }
    }
    m_previewWorld.reset();
    std::cout << "[ChunkEditorState] Distrutto.\n";
}

bool ChunkEditorState::InitApp() {
    if (m_context) {
        m_context->isMapBuilderMode = true;
        m_context->isForgeMode = false;
        if (m_context->blockRegistry) {
            m_paintSurfaceBlock = m_context->blockRegistry->GetBlock("fairworld:grass").id;
            m_paintSubsurfaceBlock = m_context->blockRegistry->GetBlock("fairworld:dirt").id;
            if (m_paintSurfaceBlock == 0) m_paintSurfaceBlock = 1;
            if (m_paintSubsurfaceBlock == 0) m_paintSubsurfaceBlock = 2;
        }
    }

    if (m_context && m_context->projectManager) {
        m_context->projectManager->EnsureDefaultPlanetExists();
        m_context->projectManager->ValidateBlocks(m_context->blockRegistry);
    }

    m_previewWorld = std::make_unique<fw::GameWorld>();
    m_previewWorld->Initialize(m_context);

    if (m_context && m_context->engine) {
        m_context->engine->SetGameMode(GameMode::ChunkEditor);
        m_context->activeRegistry = &m_previewWorld->GetRegistry();
        m_context->forgeWorld = m_previewWorld.get();
    }

    RebuildChunkPreview();
    return true;
}

void ChunkEditorState::RebuildChunkPreview() {
    if (!m_context || !m_context->projectManager || !m_previewWorld) return;
    auto& doc = m_context->projectManager->GetDocument();
    if (m_context && m_context->jobSystem) {
        m_context->jobSystem->WaitAll();
    }
    if (doc.terrainLibrary.empty() || m_activeTemplateIndex < 0 || m_activeTemplateIndex >= (int)doc.terrainLibrary.size()) {
        if (m_context && m_context->engine && m_context->engine->GetRenderManager()) {
            vkDeviceWaitIdle(m_context->engine->GetRenderManager()->GetDevice());
        }
        if (m_context) {
            if (m_previewWorld && m_context->forgeWorld == m_previewWorld.get()) m_context->forgeWorld = nullptr;
            if (m_previewWorld && m_context->activeRegistry == &m_previewWorld->GetRegistry()) m_context->activeRegistry = nullptr;
        }
        m_previewWorld = std::make_unique<fw::GameWorld>();
        m_previewWorld->Initialize(m_context);
        m_context->forgeWorld = m_previewWorld.get();
        m_context->activeRegistry = &m_previewWorld->GetRegistry();
        if (m_context->cacheManager) {
            m_context->cacheManager->FlushGpuRenderCaches(m_context);
            m_context->cacheManager->FlushCpuTransientCaches(m_context);
        } else if (m_context->engine && m_context->engine->GetRenderManager()) {
            m_context->engine->GetRenderManager()->InvalidateForgeCache();
        }
        return;
    }

    std::cout << "[ChunkEditorState] Rigenerazione anteprima 3D Voxel per chunk corrente...\n";

    if (m_previewWorld) {
        m_previewWorld->CancelJobs();
    }

    if (m_context && m_context->engine && m_context->engine->GetRenderManager()) {
        vkDeviceWaitIdle(m_context->engine->GetRenderManager()->GetDevice());
    }

    if (m_context) {
        if (m_previewWorld && m_context->forgeWorld == m_previewWorld.get()) m_context->forgeWorld = nullptr;
        if (m_previewWorld && m_context->activeRegistry == &m_previewWorld->GetRegistry()) m_context->activeRegistry = nullptr;
    }
    m_previewWorld = std::make_unique<fw::GameWorld>();
    m_previewWorld->Initialize(m_context);
    m_context->forgeWorld = m_previewWorld.get();
    m_context->activeRegistry = &m_previewWorld->GetRegistry();
    if (m_context->cacheManager) {
        m_context->cacheManager->FlushGpuRenderCaches(m_context);
        m_context->cacheManager->FlushCpuTransientCaches(m_context);
    } else if (m_context->engine && m_context->engine->GetRenderManager()) {
        m_context->engine->GetRenderManager()->InvalidateForgeCache();
    }

    const auto& tmpl = doc.terrainLibrary[m_activeTemplateIndex];

    fw::MapDocument tempDoc;
    fw::PlanetMap tempPlanet;
    tempPlanet.name = "PreviewChunk";
    
    // Calcola i limiti (Bounding Box) in base a ciò che hai disegnato nel Canvas 2D
    int minX = 0, maxX = 0, minZ = 0, maxZ = 0;
    if (!tmpl.subRegions.empty()) {
        minX = tmpl.subRegions[0].rectMin.x;
        maxX = tmpl.subRegions[0].rectMax.x;
        minZ = tmpl.subRegions[0].rectMin.y;
        maxZ = tmpl.subRegions[0].rectMax.y;
        for (const auto& sub : tmpl.subRegions) {
            minX = std::min(minX, sub.rectMin.x);
            maxX = std::max(maxX, sub.rectMax.x);
            minZ = std::min(minZ, sub.rectMin.y);
            maxZ = std::max(maxZ, sub.rectMax.y);
        }
    } else {
        minX = -2; maxX = 2; minZ = -2; maxZ = 2; // Default se vuoto
    }
    
    // Aggiungi un margine di 1 chunk per vedere i bordi
    minX -= 1; maxX += 1; minZ -= 1; maxZ += 1;
    
    // Clamp estremo per evitare che se disegni a -16, +16 freezi l'intero PC (max 21x21 chunks)
    minX = std::max(minX, -10);
    maxX = std::min(maxX, 10);
    minZ = std::max(minZ, -10);
    maxZ = std::min(maxZ, 10);

    tempPlanet.planetRadius = 0.0f; // DEVE essere 0.0f per forzare la generazione piana dell'anteprima
    tempPlanet.minX = minX;
    tempPlanet.maxX = maxX;
    tempPlanet.minZ = minZ;
    tempPlanet.maxZ = maxZ;

    fw::MapRegion baseRegion;
    baseRegion.eulerAngles = glm::vec3(0.0f);
    baseRegion.angularRadius = tmpl.baseAngularRadius;
    baseRegion.type = tmpl.baseType;
    baseRegion.perlinFrequency = tmpl.basePerlinFrequency;
    baseRegion.gravityModifier = tmpl.baseGravityModifier;
    baseRegion.seed = tmpl.seed;
    tempPlanet.regions.push_back(baseRegion);

    for (const auto& sub : tmpl.subRegions) {
        fw::MapRegion projectedSub = sub;
        projectedSub.eulerAngles = glm::vec3(0.0f);
        tempPlanet.regions.push_back(projectedSub);
    }
    tempDoc.planets.push_back(tempPlanet);

    fw::MapWorldGenerator::Generate(tempDoc, 0, *m_previewWorld, m_context->jobSystem);
    m_orbitTarget = glm::vec3(0.0f, 18.0f, 0.0f);
}

void ChunkEditorState::UpdateApp(float dt) {
    if (m_needsRebuild) {
        m_rebuildTimer -= dt;
        if (m_rebuildTimer <= 0.0f) {
            m_needsRebuild = false;
            RebuildChunkPreview();
        }
    }

    if (m_context) {
        m_context->isMapBuilderMode = true;
        m_context->isForgeMode = false;
        if (m_previewWorld) {
            m_context->forgeWorld = m_previewWorld.get();
            m_context->activeRegistry = &m_previewWorld->GetRegistry();
        }
        if (m_context->engine) {
            m_context->engine->SetGameMode(GameMode::ChunkEditor);
        }
    }

    ImGuiIO& io = ImGui::GetIO();
    uint32_t w = 1920;
    uint32_t h = 1080;
    if (m_context && m_context->engine && m_context->engine->GetRenderManager()) {
        w = m_context->engine->GetRenderManager()->GetWindowWidth();
        h = m_context->engine->GetRenderManager()->GetWindowHeight();
    }

    if (!io.WantCaptureMouse && io.MousePos.x >= w * 0.45f) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            m_orbitYaw -= io.MouseDelta.x * 0.5f;
            m_orbitPitch += io.MouseDelta.y * 0.5f;
            m_orbitPitch = std::clamp(m_orbitPitch, -89.0f, 89.0f);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            float pitchRad = glm::radians(m_orbitPitch);
            float yawRad = glm::radians(m_orbitYaw);
            glm::vec3 forward = glm::vec3(cos(pitchRad) * sin(yawRad), sin(pitchRad), cos(pitchRad) * cos(yawRad));
            glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
            glm::vec3 up = glm::normalize(glm::cross(right, forward));
            m_orbitTarget += right * io.MouseDelta.x * 0.1f;
            m_orbitTarget -= up * io.MouseDelta.y * 0.1f;
        }
        if (io.MouseWheel != 0.0f) {
            m_orbitDistance -= io.MouseWheel * 5.0f;
            m_orbitDistance = std::max(m_orbitDistance, 5.0f);
        }
    }

    float pitchRad = glm::radians(m_orbitPitch);
    float yawRad = glm::radians(m_orbitYaw);
    
    glm::vec3 camPos;
    camPos.x = m_orbitTarget.x + m_orbitDistance * cos(pitchRad) * sin(yawRad);
    camPos.y = m_orbitTarget.y + m_orbitDistance * sin(pitchRad);
    camPos.z = m_orbitTarget.z + m_orbitDistance * cos(pitchRad) * cos(yawRad);

    if (m_context) {
        m_context->activeCameraView.cameraPosition = camPos;
        m_context->activeCameraView.cameraFront = glm::normalize(m_orbitTarget - camPos);
        m_context->activeCameraView.viewMatrix = glm::lookAt(camPos, m_orbitTarget, glm::vec3(0, 1, 0));
        
        float aspect = (w * 0.55f) / (float)(std::max((uint32_t)1, h));
        m_context->activeCameraView.projectionMatrix = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);
        m_context->activeCameraView.projectionMatrix[1][1] *= -1;
    }

    if (m_previewWorld) {
        if (m_context && m_context->blockRegistry) {
            fw::BiomeTerrainSystem::Update(m_previewWorld->GetRegistry(), 15, m_context->blockRegistry);
            fw::BiomeDecoratorSystem::Update(m_previewWorld->GetRegistry(), 15, m_context->blockRegistry);
        }
        m_previewWorld->Update(dt);
    }
}

void ChunkEditorState::RenderApp() {
    DrawUI();
}

void ChunkEditorState::DrawUI() {
    if (!m_context || !m_context->projectManager) return;
    auto& doc = m_context->projectManager->GetDocumentMutable();

    ImGuiIO& io = ImGui::GetIO();
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float leftWidth = viewport->Size.x * 0.45f;

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(ImVec2(leftWidth, viewport->Size.y));
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | 
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    ImGui::Begin("ChunkEditorLeftPanel", nullptr, windowFlags);
    
    // Devozione alla Madre: invoca l'Architettura Madre per disegnare la barra e il pulsante Torna all'Hub
    if (DrawMotherHeader("CHUNK EDITOR - MODELLAZIONE TERRENO & BIOMI")) {
        ImGui::End();
        return;
    }

    ImGui::BeginChild("EditorContent", ImVec2(0, -90.0f), true);

    float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.65f, 0.4f, 1.0f));
    if (ImGui::Button("+ AGGIUNGI MODELLO CHUNK", ImVec2(btnW, 30))) {
        fw::TerrainTemplate t;
        t.name = "Terreno " + std::to_string(doc.terrainLibrary.size() + 1);
        t.id = "terrain_" + std::to_string(doc.terrainLibrary.size() + 1);
        t.baseType = fw::MapRegionType::Forest;
        t.basePerlinFrequency = 0.03f;
        t.baseGravityModifier = 1.0f;
        t.baseAngularRadius = 0.25f;
        doc.terrainLibrary.push_back(t);
        m_activeTemplateIndex = (int)doc.terrainLibrary.size() - 1;
        m_needsRebuild = true; m_rebuildTimer = 0.2f;
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();

    bool canDelete = !doc.terrainLibrary.empty() && m_activeTemplateIndex >= 0 && m_activeTemplateIndex < (int)doc.terrainLibrary.size();
    if (!canDelete) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
    if (ImGui::Button("- ELIMINA SELEZIONATO", ImVec2(btnW, 30))) {
        std::string deletedId = doc.terrainLibrary[m_activeTemplateIndex].id;
        doc.terrainLibrary.erase(doc.terrainLibrary.begin() + m_activeTemplateIndex);
        
        // Pulizia igienica: rimuovi anche eventuali istanze in PlanetMapper che riferivano questo modello
        for (auto& planet : doc.planets) {
            planet.chunkInstances.erase(
                std::remove_if(planet.chunkInstances.begin(), planet.chunkInstances.end(),
                    [&](const auto& inst) { return inst.templateId == deletedId; }),
                planet.chunkInstances.end());
        }

        if (m_activeTemplateIndex >= (int)doc.terrainLibrary.size()) {
            m_activeTemplateIndex = (int)doc.terrainLibrary.size() - 1;
        }
        m_needsRebuild = true; m_rebuildTimer = 0.2f;
    }
    ImGui::PopStyleColor(3);
    if (!canDelete) ImGui::EndDisabled();

    ImGui::BeginChild("LibraryList", ImVec2(0, 100), true);
    for (int i = 0; i < (int)doc.terrainLibrary.size(); ++i) {
        bool isSelected = (m_activeTemplateIndex == i);
        if (ImGui::Selectable((std::to_string(i+1) + ". " + doc.terrainLibrary[i].name).c_str(), isSelected)) {
            m_activeTemplateIndex = i;
            m_needsRebuild = true; m_rebuildTimer = 0.2f;
        }
    }
    ImGui::EndChild();

    if (m_activeTemplateIndex >= 0 && m_activeTemplateIndex < (int)doc.terrainLibrary.size()) {
        auto& activeTemplate = doc.terrainLibrary[m_activeTemplateIndex];

        if (ImGui::CollapsingHeader("Tela 2D - Dipingi Sotto-Regioni (Fiumi, Zone, Strutture)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGuiWindowFlags canvasFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
            ImGui::BeginChild("Canvas2D", ImVec2(0, 280), true, canvasFlags);
            
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
                if (io.MouseWheel != 0.0f && io.KeyCtrl) {
                    m_canvasZoom *= (io.MouseWheel > 0) ? 1.15f : (1.0f / 1.15f);
                    m_canvasZoom = std::clamp(m_canvasZoom, 0.1f, 20.0f);
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
            
            ImVec2 mapMin = ChunkToScreen(-16, -16);
            ImVec2 mapMax = ChunkToScreen(16, 16);
            drawList->AddRect(mapMin, mapMax, IM_COL32(100, 100, 100, 255), 0.0f, 0, 2.0f);
            
            drawList->PushClipRect(mapMin, mapMax, true);
            
            // Gestione Interazione Istanze
            int hoveredInstance = -1;
            glm::ivec2 cMouseCoord = ScreenToChunk(io.MousePos);
            
            if (canvasHovered) {
                for (int i = (int)activeTemplate.subRegions.size() - 1; i >= 0; --i) {
                    const auto& r = activeTemplate.subRegions[i];
                    if (cMouseCoord.x >= r.rectMin.x && cMouseCoord.x <= r.rectMax.x &&
                        cMouseCoord.y >= r.rectMin.y && cMouseCoord.y <= r.rectMax.y) {
                        hoveredInstance = i;
                        break;
                    }
                }
            }
            
            if (canvasHovered) {
                if (io.KeyShift && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    // Crea nuova istanza (pennello continuo)
                    int halfB = m_brushSize / 2;
                    glm::ivec2 targetMin = glm::ivec2(cMouseCoord.x - halfB, cMouseCoord.y - halfB);
                    
                    bool canAdd = true;
                    glm::ivec2 targetMax = glm::ivec2(cMouseCoord.x - halfB + m_brushSize - 1, cMouseCoord.y - halfB + m_brushSize - 1);
                    for (const auto& existing : activeTemplate.subRegions) {
                        if (existing.rectMin == targetMin &&
                            existing.rectMax == targetMax &&
                            existing.type == static_cast<fw::MapRegionType>(m_paintRegionType) &&
                            existing.shape == static_cast<fw::RegionShape>(m_paintBrushShape) &&
                            existing.surfaceBlockId == m_paintSurfaceBlock &&
                            existing.subsurfaceBlockId == m_paintSubsurfaceBlock) {
                            canAdd = false;
                            break;
                        }
                    }

                    if (canAdd) {
                        fw::MapRegion nr;
                        nr.isGridAligned = true;
                        nr.rectMin = targetMin;
                        nr.rectMax = targetMax;
                        nr.type = static_cast<fw::MapRegionType>(m_paintRegionType);
                        nr.shape = static_cast<fw::RegionShape>(m_paintBrushShape);
                        nr.surfaceBlockId = m_paintSurfaceBlock;
                        nr.subsurfaceBlockId = m_paintSubsurfaceBlock;
                        nr.perlinFrequency = 0.005f;
                        nr.gravityModifier = 1.0f;
                        activeTemplate.subRegions.push_back(nr);
                        m_selectedSubRegionIndex = (int)activeTemplate.subRegions.size() - 1;
                        if (m_autoRebuildPreview) { m_needsRebuild = true; m_rebuildTimer = 0.2f; }
                    }
                } else if (!io.KeyShift && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    // Seleziona istanza
                    m_selectedSubRegionIndex = hoveredInstance;
                }
            }
            
            // Dragging dell'istanza selezionata
            if (m_selectedSubRegionIndex >= 0 && m_selectedSubRegionIndex < (int)activeTemplate.subRegions.size()) {
                if (canvasActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !io.KeyShift && !io.KeyCtrl) {
                    ImVec2 prevMousePos = ImVec2(io.MousePos.x - io.MouseDelta.x, io.MousePos.y - io.MouseDelta.y);
                    glm::ivec2 deltaChunk = ScreenToChunk(io.MousePos) - ScreenToChunk(prevMousePos);
                    if (deltaChunk.x != 0 || deltaChunk.y != 0) {
                        activeTemplate.subRegions[m_selectedSubRegionIndex].rectMin += deltaChunk;
                        activeTemplate.subRegions[m_selectedSubRegionIndex].rectMax += deltaChunk;
                        if (m_autoRebuildPreview) { m_needsRebuild = true; m_rebuildTimer = 0.5f; }
                    }
                }
            }
            
            // Disegno Istanze
            for (int i = 0; i < (int)activeTemplate.subRegions.size(); ++i) {
                const auto& r = activeTemplate.subRegions[i];
                ImVec2 pMin = ChunkToScreen(r.rectMin.x, r.rectMin.y);
                ImVec2 pMax = ChunkToScreen(r.rectMax.x + 1, r.rectMax.y + 1);
                if (pMin.x > pMax.x) std::swap(pMin.x, pMax.x);
                if (pMin.y > pMax.y) std::swap(pMin.y, pMax.y);
                
                ImU32 fillColor = IM_COL32(150, 150, 150, 200);
                switch (r.type) {
                    case fw::MapRegionType::Forest:  fillColor = IM_COL32(40,  180, 40,  235); break;
                    case fw::MapRegionType::Ocean:   fillColor = IM_COL32(40,  80,  220, 235); break;
                    case fw::MapRegionType::Desert:  fillColor = IM_COL32(220, 200, 60,  235); break;
                    case fw::MapRegionType::Volcano: fillColor = IM_COL32(220, 50,  20,  235); break;
                    case fw::MapRegionType::Tundra:  fillColor = IM_COL32(200, 240, 255, 235); break;
                    default: break;
                }
                
                ImU32 outlineColor = (i == m_selectedSubRegionIndex) ? IM_COL32(255, 255, 0, 255) : 
                                     (i == hoveredInstance) ? IM_COL32(200, 200, 200, 255) : IM_COL32(0,0,0,0);
                                     
                if (r.shape == fw::RegionShape::Circle) {
                    ImVec2 center((pMin.x + pMax.x) * 0.5f, (pMin.y + pMax.y) * 0.5f);
                    float radius = std::min(pMax.x - pMin.x, pMax.y - pMin.y) * 0.5f;
                    drawList->AddCircleFilled(center, radius, fillColor);
                    if (outlineColor != IM_COL32(0,0,0,0)) drawList->AddCircle(center, radius, outlineColor, 0, 2.0f);
                } else if (r.shape == fw::RegionShape::Rhombus) {
                    ImVec2 center((pMin.x + pMax.x) * 0.5f, (pMin.y + pMax.y) * 0.5f);
                    ImVec2 points[4] = {
                        ImVec2(center.x, pMin.y), ImVec2(pMax.x, center.y),
                        ImVec2(center.x, pMax.y), ImVec2(pMin.x, center.y)
                    };
                    drawList->AddConvexPolyFilled(points, 4, fillColor);
                    if (outlineColor != IM_COL32(0,0,0,0)) drawList->AddPolyline(points, 4, outlineColor, ImDrawFlags_Closed, 2.0f);
                } else {
                    drawList->AddRectFilled(pMin, pMax, fillColor);
                    if (outlineColor != IM_COL32(0,0,0,0)) drawList->AddRect(pMin, pMax, outlineColor, 0.0f, 0, 2.0f);
                }
            }
            drawList->PopClipRect();
            
            if (canvasHovered && io.KeyShift) {
                int halfB = m_brushSize / 2;
                int bMinX = cMouseCoord.x - halfB;
                int bMinZ = cMouseCoord.y - halfB;
                int bMaxX = cMouseCoord.x - halfB + m_brushSize;
                int bMaxZ = cMouseCoord.y - halfB + m_brushSize;
                drawList->AddRect(ChunkToScreen(bMinX, bMinZ), ChunkToScreen(bMaxX, bMaxZ), IM_COL32(255, 255, 0, 150), 0, 0, 2.0f);
            }
            
            ImGui::TextDisabled("Click sx: Seleziona | Shift+Click sx: Crea Istanza | Trascina: Sposta");
            ImGui::EndChild();
        }

        if (m_selectedSubRegionIndex >= 0 && m_selectedSubRegionIndex < (int)activeTemplate.subRegions.size()) {
            if (ImGui::CollapsingHeader("Proprietà Istanza Selezionata", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto& inst = activeTemplate.subRegions[m_selectedSubRegionIndex];
                
                const char* shapeNames[] = { "Rectangle", "Circle", "Rhombus", "Star" };
                int shapeIdx = static_cast<int>(inst.shape);
                if (ImGui::Combo("Forma", &shapeIdx, shapeNames, IM_ARRAYSIZE(shapeNames))) {
                    inst.shape = static_cast<fw::RegionShape>(shapeIdx);
                    if (m_autoRebuildPreview) { m_needsRebuild = true; m_rebuildTimer = 0.2f; }
                }
                
                const char* biomeNames[] = { "Forest", "Desert", "Tundra", "Ocean", "Volcano", "City", "Dungeon", "Portal" };
                int typeIdx = static_cast<int>(inst.type);
                if (ImGui::Combo("Bioma Istanza", &typeIdx, biomeNames, IM_ARRAYSIZE(biomeNames))) {
                    inst.type = static_cast<fw::MapRegionType>(typeIdx);
                    if (m_autoRebuildPreview) { m_needsRebuild = true; m_rebuildTimer = 0.2f; }
                }
                
                if (ImGui::SliderFloat("Frequenza Perlin", &inst.perlinFrequency, 0.001f, 0.1f, "%.4f")) {
                    if (m_autoRebuildPreview) { m_needsRebuild = true; m_rebuildTimer = 0.2f; }
                }
                
                if (ImGui::SliderFloat("Gravità (Altezza)", &inst.gravityModifier, 0.1f, 5.0f, "%.2f")) {
                    if (m_autoRebuildPreview) { m_needsRebuild = true; m_rebuildTimer = 0.2f; }
                }
                
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
                if (ImGui::Button("Elimina Istanza", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                    activeTemplate.subRegions.erase(activeTemplate.subRegions.begin() + m_selectedSubRegionIndex);
                    m_selectedSubRegionIndex = -1;
                    if (m_autoRebuildPreview) { m_needsRebuild = true; m_rebuildTimer = 0.2f; }
                }
                ImGui::PopStyleColor();
            }
        }

        if (ImGui::CollapsingHeader("Proprietà Geologiche e di Bioma", ImGuiTreeNodeFlags_DefaultOpen)) {
            char labelBuf[128];
            strncpy_s(labelBuf, activeTemplate.name.c_str(), sizeof(labelBuf));
            if (ImGui::InputText("Nome Modello", labelBuf, sizeof(labelBuf))) {
                activeTemplate.name = labelBuf;
            }

            const char* biomeNames[] = { "Forest", "Desert", "Tundra", "Ocean", "Volcano", "City", "Dungeon", "Portal" };
            int typeIdx = static_cast<int>(activeTemplate.baseType);
            if (ImGui::Combo("Bioma Base", &typeIdx, biomeNames, IM_ARRAYSIZE(biomeNames))) {
                activeTemplate.baseType = static_cast<fw::MapRegionType>(typeIdx);
                if (m_autoRebuildPreview) { m_needsRebuild = true; m_rebuildTimer = 0.2f; }
            }

            if (ImGui::SliderFloat("Frequenza Perlin (Rugosità)", &activeTemplate.basePerlinFrequency, 0.001f, 0.1f, "%.4f")) {
                if (m_autoRebuildPreview) { m_needsRebuild = true; m_rebuildTimer = 0.2f; }
            }
            if (ImGui::SliderFloat("Modificatore Gravità", &activeTemplate.baseGravityModifier, 0.1f, 5.0f, "%.2f")) {
                if (m_autoRebuildPreview) { m_needsRebuild = true; m_rebuildTimer = 0.2f; }
            }
            int seed = (int)activeTemplate.seed;
            if (ImGui::InputInt("Seme Geologico (Seed)", &seed)) {
                activeTemplate.seed = seed;
                if (m_autoRebuildPreview) { m_needsRebuild = true; m_rebuildTimer = 0.2f; }
            }
            if (ImGui::SliderFloat("Estensione Base (Raggio Angolare)", &activeTemplate.baseAngularRadius, 0.01f, 0.5f, "%.3f")) {
                if (m_autoRebuildPreview) { m_needsRebuild = true; m_rebuildTimer = 0.2f; }
            }

            ImGui::Spacing();
            if (ImGui::Button("Genera Livello Acqua (Oceano)", ImVec2(-1, 25))) {
                activeTemplate.subRegions.clear();
                fw::MapRegion oceanBase;
                oceanBase.type = fw::MapRegionType::Ocean;
                oceanBase.shape = fw::RegionShape::Rectangle;
                oceanBase.isGridAligned = true;
                oceanBase.rectMin = glm::ivec2(-16, -16);
                oceanBase.rectMax = glm::ivec2(16, 16);
                uint8_t idWater = 255;
                uint8_t idSand = 255;
                if (m_context && m_context->blockRegistry) {
                    idWater = m_context->blockRegistry->GetBlock("fairworld:water").id;
                    idSand = m_context->blockRegistry->GetBlock("fairworld:sand").id;
                }
                oceanBase.surfaceBlockId = idWater;
                oceanBase.subsurfaceBlockId = idSand;
                activeTemplate.subRegions.push_back(oceanBase);
                m_needsRebuild = true; m_rebuildTimer = 0.2f;
            }
        }

        if (ImGui::CollapsingHeader("Strumenti Disegno e Sincronizzazione Blocchi (PBR/Lookdev)", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* shapeNames[] = { "Rettangolo", "Cerchio", "Rombo", "Stella" };
            ImGui::Combo("Forma Strumento", &m_paintBrushShape, shapeNames, IM_ARRAYSIZE(shapeNames));
            
            const char* biomeNames[] = { "Forest", "Desert", "Tundra", "Ocean", "Volcano", "City", "Dungeon", "Portal" };
            ImGui::Combo("Tipo Sotto-Regione", &m_paintRegionType, biomeNames, IM_ARRAYSIZE(biomeNames));

            auto drawBlockCombo = [&](const char* label, int& blockId) {
                std::string comboPreview = "ID: " + std::to_string(blockId);
                if (m_context && m_context->blockRegistry) {
                    const auto& def = m_context->blockRegistry->GetBlock((uint8_t)blockId);
                    comboPreview = def.displayName + " (" + def.stringId + ")";
                }
                if (ImGui::BeginCombo(label, comboPreview.c_str())) {
                    if (m_context && m_context->blockRegistry) {
                        for (const auto& b : m_context->blockRegistry->GetAllBlocks()) {
                            bool isSelected = (blockId == b.id);
                            if (ImGui::Selectable((b.displayName + " [" + b.stringId + "]").c_str(), isSelected)) {
                                blockId = b.id;
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            };

            drawBlockCombo("Blocco Superficie", m_paintSurfaceBlock);
            drawBlockCombo("Blocco Sottosuolo", m_paintSubsurfaceBlock);
            ImGui::SliderInt("Dimensione Pennello", &m_brushSize, 1, 10);
            if (ImGui::Button("PULISCI SOTTO-REGIONI", ImVec2(-1, 25))) {
                activeTemplate.subRegions.clear();
                m_needsRebuild = true; m_rebuildTimer = 0.2f;
            }
        }
    } else {
        ImGui::TextDisabled("Nessun Modello Chunk selezionato.");
    }
    ImGui::EndChild();

    ImGui::BeginChild("EditorBottomBar", ImVec2(0, 85.0f), true);
    ImGui::Checkbox("Rigenera Voxel al volo ad ogni modifica", &m_autoRebuildPreview);
    if (!m_autoRebuildPreview) {
        if (ImGui::Button("🔄 RIGENERA ANTEPRIMA VOXEL 3D ORA", ImVec2(-1, 25))) {
            m_needsRebuild = true; m_rebuildTimer = 0.2f;
        }
    }
    if (ImGui::Button("💾 SALVA LIBRO CHUNK (WORLD PROJECT)", ImVec2(-1, 30))) {
        m_context->projectManager->SaveProject();
        m_showSaveConfirmPopup = true;
    }
    ImGui::EndChild();

    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + leftWidth + 20.0f, viewport->Pos.y + 20.0f));
    ImGui::Begin("OverlayVoxelPreview", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground);
    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), ">>> ANTEPRIMA VOXEL 3D IN TEMPO REALE (CHUNK SELEZIONATO) <<<");
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 0.8f), "Trascina col mouse per ruotare ed esaminare | Rotellina per lo Zoom");
    ImGui::End();

    if (m_showSaveConfirmPopup) {
        ImGui::OpenPopup("LibroChunkSalvato");
        m_showSaveConfirmPopup = false;
    }
    if (ImGui::BeginPopupModal("LibroChunkSalvato", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("I modelli di chunk sono stati sincronizzati e salvati con successo su world_map.json.\nIl Planet Mapper e il motore di gioco leggeranno queste impostazioni.");
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
