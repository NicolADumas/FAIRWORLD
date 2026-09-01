#include "pch.h"
#include "PlanetMapperState.h"
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
#include "PlayState.h"
#include <iostream>
#include <algorithm>
#include <cmath>

PlanetMapperState::PlanetMapperState(SharedContext* context) : AppBaseState(context) {
    std::cout << "[PlanetMapperState] Costruito come estensione di AppBaseState.\n";
}

// Helper centralizzato: salva e avvia il flash visivo di conferma
static void PMS_DoSave(fw::WorldProjectManager* pm, float& flashTimer, std::string& flashMsg) {
    if (!pm) return;
    bool ok = pm->SaveProject();
    flashTimer = 2.5f; // mostra il messaggio per 2.5 secondi
    flashMsg = ok ? "✅ SALVATO!" : "❌ ERRORE SALVATAGGIO";
}

PlanetMapperState::~PlanetMapperState() {
    if (m_context) {
        if (m_previewWorld && m_context->forgeWorld == m_previewWorld.get()) {
            m_context->forgeWorld = nullptr;
        }
        if (m_previewWorld && m_context->activeRegistry == &m_previewWorld->GetRegistry()) {
            m_context->activeRegistry = nullptr;
        }
    }
    m_previewWorld.reset();
    std::cout << "[PlanetMapperState] Distrutto.\n";
}

bool PlanetMapperState::InitApp() {
    if (m_context) {
        m_context->isMapBuilderMode = true;
        m_context->isForgeMode = false;
    }

    if (m_context && m_context->projectManager) {
        m_context->projectManager->EnsureDefaultPlanetExists();
        m_context->projectManager->ValidateBlocks(m_context->blockRegistry);
    }

    m_previewWorld = std::make_unique<fw::GameWorld>();
    m_previewWorld->Initialize(m_context);

    if (m_context && m_context->engine) {
        m_context->engine->SetGameMode(GameMode::PlanetMapper);
        m_context->activeRegistry = &m_previewWorld->GetRegistry();
        m_context->forgeWorld = m_previewWorld.get();
    }

    RebuildPlanetRoots();

    m_orbitTarget = glm::vec3(0.0f, 0.0f, 0.0f);
    m_orbitDistance = 250.0f;
    if (m_context) {
        m_context->activeCameraView.cameraPosition = glm::vec3(0.0f, 0.0f, m_orbitDistance);
        m_context->activeCameraView.cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
        m_context->activeCameraView.viewMatrix = glm::lookAt(m_context->activeCameraView.cameraPosition, m_orbitTarget, glm::vec3(0, 1, 0));
        m_context->activeCameraView.projectionMatrix = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 2000.0f);
        m_context->activeCameraView.projectionMatrix[1][1] *= -1;
    }

    return true;
}

void PlanetMapperState::RebuildPlanetRoots() {
    if (!m_context || !m_context->projectManager) return;
    const auto& doc = m_context->projectManager->GetDocument();
    float R = 50.0f;
    if (!doc.planets.empty() && m_activePlanetIndex >= 0 && m_activePlanetIndex < (int)doc.planets.size()) {
        R = doc.planets[m_activePlanetIndex].planetRadius;
    }

    m_lodSystem.SetPlanetRadius(R);

    if (m_previewWorld) {
        std::function<void(fw::ChunkNode&)> destroyTree = [&](fw::ChunkNode& n) {
            if (n.targetEntity != entt::null && m_previewWorld->GetRegistry().valid(n.targetEntity)) {
                m_previewWorld->DestroyEntity(n.targetEntity);
            }
            for (int i = 0; i < 4; ++i) {
                if (n.children[i]) destroyTree(*n.children[i]);
            }
        };
        for (auto& root : m_planetRootNodes) {
            destroyTree(root);
        }
    }

    m_planetRootNodes.clear();

    m_planetRootNodes.emplace_back(glm::vec3(0, 0, R), R, 2, glm::vec3(-1,-1,1), glm::vec3(1,-1,1), glm::vec3(-1,1,1), glm::vec3(1,1,1));
    m_planetRootNodes.emplace_back(glm::vec3(0, 0, -R), R, 2, glm::vec3(1,-1,-1), glm::vec3(-1,-1,-1), glm::vec3(1,1,-1), glm::vec3(-1,1,-1));
    m_planetRootNodes.emplace_back(glm::vec3(R, 0, 0), R, 2, glm::vec3(1,-1,1), glm::vec3(1,-1,-1), glm::vec3(1,1,1), glm::vec3(1,1,-1));
    m_planetRootNodes.emplace_back(glm::vec3(-R, 0, 0), R, 2, glm::vec3(-1,-1,-1), glm::vec3(-1,-1,1), glm::vec3(-1,1,-1), glm::vec3(-1,1,1));
    m_planetRootNodes.emplace_back(glm::vec3(0, R, 0), R, 2, glm::vec3(-1,1,1), glm::vec3(1,1,1), glm::vec3(-1,1,-1), glm::vec3(1,1,-1));
    m_planetRootNodes.emplace_back(glm::vec3(0, -R, 0), R, 2, glm::vec3(-1,-1,-1), glm::vec3(1,-1,-1), glm::vec3(-1,-1,1), glm::vec3(1,-1,1));
}

void PlanetMapperState::UpdateApp(float dt) {
    if (!m_context || !m_context->projectManager) return;
    auto& doc = m_context->projectManager->GetDocument();

    // Decrementa timer feedback salvataggio
    if (m_saveFlashTimer > 0.0f) m_saveFlashTimer -= dt;

    if (m_context) {
        m_context->isMapBuilderMode = m_isBuilderMode;
        m_context->isForgeMode = false;
        if (m_previewWorld) {
            m_context->forgeWorld = m_previewWorld.get();
            m_context->activeRegistry = &m_previewWorld->GetRegistry();
        }
        if (m_context->engine) {
            m_context->engine->SetGameMode(GameMode::PlanetMapper);
        }
    }

    ImGuiIO& io = ImGui::GetIO();
    uint32_t w = 1920, h = 1080;
    if (m_context->engine && m_context->engine->GetRenderManager()) {
        w = m_context->engine->GetRenderManager()->GetWindowWidth();
        h = m_context->engine->GetRenderManager()->GetWindowHeight();
    }

    bool allowCameraControl = false;
    if (!io.WantCaptureMouse) {
        if (m_isBuilderMode) {
            if (io.MousePos.x >= w * 0.45f) allowCameraControl = true;
        } else {
            allowCameraControl = true;
        }
    }

    if (allowCameraControl) {
        // --- Orbita con drag mouse ---
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            m_orbitYaw -= io.MouseDelta.x * 0.5f;
            m_orbitPitch += io.MouseDelta.y * 0.5f;
            m_orbitPitch = std::clamp(m_orbitPitch, -89.0f, 89.0f);
        }
        // --- Zoom con rotellina (più aggressivo vicino al pianeta) ---
        if (io.MouseWheel != 0.0f) {
            float scrollSpeed = std::max(m_orbitDistance * 0.1f, 5.0f);
            m_orbitDistance -= io.MouseWheel * scrollSpeed;
            m_orbitDistance = std::max(m_orbitDistance, 2.0f);
        }
        // --- Free-fly WASD: modifica la posizione orbitale "avanzando" ---
        // W/S = avvicina/allontana  (come zoom ma da tastiera)
        // A/D = ruota yaw orbitale  (come drag orizzontale)
        // R/F = ruota pitch orbitale (salire/scendere sull'orbita)
        float flySpeed = std::max(m_orbitDistance * 0.03f, 1.5f) * dt * 60.0f;
        if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
            m_orbitDistance -= flySpeed;
            m_orbitDistance = std::max(m_orbitDistance, 2.0f);
        }
        if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
            m_orbitDistance += flySpeed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
            m_orbitYaw -= flySpeed * 0.5f;
        }
        if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
            m_orbitYaw += flySpeed * 0.5f;
        }
        if (ImGui::IsKeyDown(ImGuiKey_R) || ImGui::IsKeyDown(ImGuiKey_PageUp)) {
            m_orbitPitch = std::min(m_orbitPitch + flySpeed * 0.4f, 89.0f);
        }
        if (ImGui::IsKeyDown(ImGuiKey_F) || ImGui::IsKeyDown(ImGuiKey_PageDown)) {
            m_orbitPitch = std::max(m_orbitPitch - flySpeed * 0.4f, -89.0f);
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
        
        float aspect = 16.0f / 9.0f; 
        if (h > 0) {
            aspect = m_isBuilderMode ? (w * 0.55f) / (float)h : (float)w / (float)h;
        }
        m_context->activeCameraView.projectionMatrix = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 3000.0f);
        m_context->activeCameraView.projectionMatrix[1][1] *= -1;
    }

    if (m_isBuilderMode && m_context->jobSystem && m_context->assetManager) {
        const fw::PlanetMap* pMap = nullptr;
        std::vector<fw::MapRegion> activeRegions;
        if (!doc.planets.empty() && m_activePlanetIndex >= 0 && m_activePlanetIndex < (int)doc.planets.size()) {
            pMap = &doc.planets[m_activePlanetIndex];
            activeRegions = pMap->regions;
            
            for (const auto& inst : pMap->chunkInstances) {
                for (const auto& tpl : doc.terrainLibrary) {
                    if (tpl.id == inst.templateId) {
                        int baseX = inst.gridX;
                        int baseZ = inst.gridY;
                        
                        // Push base region
                        fw::MapRegion baseR;
                        baseR.eulerAngles = inst.eulerAngles;
                        baseR.angularRadius = inst.angularRadius;
                        baseR.isGridAligned = inst.isGridAligned;
                        baseR.faceIndex = inst.faceIndex;
                        baseR.gridX = baseX;
                        baseR.gridY = baseZ;
                        baseR.rectMin = glm::ivec2(baseX, baseZ);
                        baseR.rectMax = glm::ivec2(baseX, baseZ);
                        baseR.type = tpl.baseType;
                        baseR.gravityModifier = tpl.baseGravityModifier;
                        baseR.perlinFrequency = tpl.basePerlinFrequency;
                        if (m_context && m_context->blockRegistry) {
                            baseR.surfaceBlockId = m_context->blockRegistry->GetBlock("fairworld:grass").id;
                            baseR.subsurfaceBlockId = m_context->blockRegistry->GetBlock("fairworld:dirt").id;
                        }
                        activeRegions.push_back(baseR);
                        
                        // Push subregions (painter's algorithm)
                        for (const auto& sub : tpl.subRegions) {
                            fw::MapRegion projected = sub;
                            projected.faceIndex = inst.faceIndex;
                            projected.isGridAligned = inst.isGridAligned;
                            projected.gridX = baseX;
                            projected.gridY = baseZ;
                            projected.rectMin += glm::ivec2(baseX, baseZ);
                            projected.rectMax += glm::ivec2(baseX, baseZ);
                            activeRegions.push_back(projected);
                        }
                        break;
                    }
                }
            }
        }

        glm::mat4 vpMatrix = m_context->activeCameraView.projectionMatrix * m_context->activeCameraView.viewMatrix;
        if (pMap) {
            m_lodSystem.SetPlanetRadius(pMap->planetRadius);
        }
        for (auto& root : m_planetRootNodes) {
            m_lodSystem.UpdateLODTree(root, m_context->activeCameraView.cameraPosition, m_previewWorld.get(), m_context->jobSystem, m_context->assetManager, activeRegions, vpMatrix, m_context->blockRegistry);
        }
    }

    if (m_previewWorld) {
        if (m_context && m_context->blockRegistry) {
            fw::BiomeTerrainSystem::Update(m_previewWorld->GetRegistry(), 15, m_context->blockRegistry);
            fw::BiomeDecoratorSystem::Update(m_previewWorld->GetRegistry(), 15, m_context->blockRegistry);
        }
        
        m_previewWorld->Update(dt);
        
        // --- Sincronizzazione Marker Spawn Point ---
        if (m_isBuilderMode && !doc.planets.empty() && m_activePlanetIndex >= 0 && m_activePlanetIndex < (int)doc.planets.size()) {
            auto& p = doc.planets[m_activePlanetIndex];
            // Se ci sono più marker che punti, elimina i marker in eccesso
            while (m_spawnPointMarkers.size() > p.spawnPoints.size()) {
                auto ent = m_spawnPointMarkers.back();
                if (m_previewWorld->GetRegistry().valid(ent)) {
                    m_previewWorld->DestroyEntity(ent);
                }
                m_spawnPointMarkers.pop_back();
            }
            // Se ci sono meno marker, creali come obelisco (pilastro + piramide)
            while (m_spawnPointMarkers.size() < p.spawnPoints.size()) {
                entt::entity newMarker = m_previewWorld->CreatePrimitive("SpawnMarker", fw::Vec3(0.0f, 0.0f, 0.0f), "obelisk");
                auto& mesh = m_previewWorld->GetRegistry().get<fw::MeshComponent>(newMarker);
                mesh.type = fw::MeshType::Standard;
                m_previewWorld->UploadMeshToVram(newMarker);
                m_spawnPointMarkers.push_back(newMarker);
            }
            
            // Aggiorna posizione, scala e colore di ogni marker
            for (size_t i = 0; i < p.spawnPoints.size(); ++i) {
                auto& sp = p.spawnPoints[i];
                auto ent = m_spawnPointMarkers[i];
                if (!m_previewWorld->GetRegistry().valid(ent)) continue;
                
                float cx = sp.localX / p.planetRadius;
                float cy = sp.localZ / p.planetRadius;
                glm::vec3 dir(0.0f);
                switch (sp.faceIndex) {
                    case 0: dir = glm::vec3(cx, cy, 1.0f); break;
                    case 1: dir = glm::vec3(-cx, cy, -1.0f); break;
                    case 2: dir = glm::vec3(1.0f, cy, -cx); break;
                    case 3: dir = glm::vec3(-1.0f, cy, cx); break;
                    case 4: dir = glm::vec3(cx, 1.0f, -cy); break;
                    case 5: dir = glm::vec3(cx, -1.0f, cy); break;
                }
                dir = glm::normalize(dir);
                glm::vec3 pos = dir * (p.planetRadius + sp.heightOffset);
                
                auto& trans = m_previewWorld->GetRegistry().get<fw::TransformComponent>(ent);
                trans.location = fw::Vec3(pos.x, pos.y, pos.z);

                // Rotazione: vogliamo che l'obelisco abbia la base sulla superficie
                // e la punta puntata verso l'esterno del pianeta (dir = outward normal)
                // Usiamo una matrice che mappa +Y locale -> dir del pianeta
                glm::vec3 worldUp = dir; // la direzione "su" per questo punto sulla sfera
                glm::vec3 forward(1.0f, 0.0f, 0.0f);
                if (glm::abs(glm::dot(worldUp, forward)) > 0.99f) forward = glm::vec3(0.0f, 0.0f, 1.0f);
                glm::vec3 right = glm::normalize(glm::cross(worldUp, forward));
                forward = glm::normalize(glm::cross(right, worldUp));
                glm::mat3 rotMat(right, worldUp, forward);
                glm::quat oq = glm::quat_cast(rotMat);
                trans.rotation = fw::Quat(oq.x, oq.y, oq.z, oq.w);
                trans.scale = fw::Vec3(1.0f, 1.0f, 1.0f); // obelisco ha già le sue dimensioni
                
                auto& meshComp = m_previewWorld->GetRegistry().get<fw::MeshComponent>(ent);
                meshComp.colorOverride[0] = sp.color.r;
                meshComp.colorOverride[1] = sp.color.g;
                meshComp.colorOverride[2] = sp.color.b;
                meshComp.colorOverride[3] = sp.color.a; // usa l'alpha reale del colore spawn
            }
            
            // --- GESTIONE CURSORE ---
            if (!m_previewWorld->GetRegistry().valid(m_cursorMarker)) {
                m_cursorMarker = m_previewWorld->CreatePrimitive("CursorMarker", fw::Vec3(0.0f, 0.0f, 0.0f), "cube");
                auto& mesh = m_previewWorld->GetRegistry().get<fw::MeshComponent>(m_cursorMarker);
                mesh.type = fw::MeshType::Standard;
                m_previewWorld->UploadMeshToVram(m_cursorMarker);
            }
            
            if (m_previewWorld->GetRegistry().valid(m_cursorMarker)) {
                glm::vec3 cdir = glm::normalize(m_context->activeCameraView.cameraPosition);
                glm::vec3 cpos = cdir * (p.planetRadius + 10.0f); // Leggermente sopra la superficie
                
                auto& ctrans = m_previewWorld->GetRegistry().get<fw::TransformComponent>(m_cursorMarker);
                ctrans.location = fw::Vec3(cpos.x, cpos.y, cpos.z);
                
                glm::vec3 cup(0.0f, 1.0f, 0.0f);
                if (glm::abs(glm::dot(cdir, cup)) > 0.999f) cup = glm::vec3(1.0f, 0.0f, 0.0f);
                glm::mat4 clookAt = glm::lookAt(glm::vec3(0.0f), cdir, cup);
                glm::quat cq = glm::conjugate(glm::quat_cast(clookAt));
                ctrans.rotation = fw::Quat(cq.x, cq.y, cq.z, cq.w);
                
                // Cursor pi piccolo e sottile degli spawn point, per indicare precisione
                ctrans.scale = fw::Vec3(1.0f, 4.0f, 1.0f);
                
                auto& cmesh = m_previewWorld->GetRegistry().get<fw::MeshComponent>(m_cursorMarker);
                cmesh.colorOverride[0] = 0.0f; // Ciano brillante
                cmesh.colorOverride[1] = 1.0f;
                cmesh.colorOverride[2] = 1.0f;
                cmesh.colorOverride[3] = 1.0f;
            }
            // --- FINE GESTIONE CURSORE ---
            
        }
        // --- Fine Sincronizzazione ---
        
        m_previewWorld->Update(dt);
    }
}

void PlanetMapperState::RenderApp() {
    if (m_isBuilderMode) {
        DrawBuilderUI();
    } else {
        DrawRuntimeUI();
    }
}

void PlanetMapperState::DrawBuilderUI() {
    if (!m_context || !m_context->projectManager) return;
    auto& doc = m_context->projectManager->GetDocumentMutable();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float leftWidth = viewport->Size.x * 0.45f;
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(ImVec2(leftWidth, viewport->Size.y));
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | 
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    ImGui::Begin("PlanetMapperLeft", nullptr, windowFlags);
    
    // Invocazione della Madre: gestisce la barra superiore e l'uscita in sicurezza verso l'Hub
    if (DrawMotherHeader("PLANET MAPPER - GLOBO SFERICO & SISTEMA SOLARE")) {
        ImGui::End();
        return;
    }

    ImGui::BeginChild("PlanetControls", ImVec2(0, -135.0f), true);

    if (!doc.planets.empty() && m_activePlanetIndex >= 0 && m_activePlanetIndex < (int)doc.planets.size()) {
        auto& p = doc.planets[m_activePlanetIndex];
        char nameBuf[128];
        strncpy_s(nameBuf, p.name.c_str(), sizeof(nameBuf));
        if (ImGui::InputText("Nome Pianeta", nameBuf, sizeof(nameBuf))) {
            p.name = nameBuf;
        }

        // Limite della grandezza abbassato a 1000.0f per ragioni di performance e stabilità memoria
        if (ImGui::SliderFloat("Raggio del Pianeta (m)", &p.planetRadius, 50.0f, 1000.0f, "%.1f")) {
            m_lodSystem.SetPlanetRadius(p.planetRadius);
            RebuildPlanetRoots();
        }
        
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.2f, 1.0f), "Parametri Astronomici (Sistema Solare)");
        ImGui::SliderFloat("Inclinazione Asse (Gradi)", &p.axialTilt, -90.0f, 90.0f, "%.2f");
        ImGui::SliderFloat("Durata Anno (Giorni)", &p.yearLength, 10.0f, 1000.0f, "%.0f");
        ImGui::Spacing();

        float S = 16.0f;
        float N_lato = std::ceil((glm::pi<float>() * p.planetRadius) / (2.0f * S));
        int C_totale = 6 * (int)(N_lato * N_lato);
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Legge della Superficie Sferica (C = 4*PI*R^2 / S^2)");
        ImGui::Text("Dimensione Chunk Base: %.1f m | Risoluzione Faccia: %d x %d", S, (int)N_lato, (int)N_lato);
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Chunk Totali Generati: %d | Cursore Zoom Distanza: %.1f", C_totale, m_orbitDistance);
        ImGui::SameLine();
        if (ImGui::Button("Zoom +", ImVec2(60, 20))) m_orbitDistance = std::max(10.0f, m_orbitDistance - 25.0f);
        ImGui::SameLine();
        if (ImGui::Button("Zoom -", ImVec2(60, 20))) m_orbitDistance += 25.0f;
    }
    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Catalogo Chunk dalla Libreria (Progettati col Chunk Editor):");
    if (doc.terrainLibrary.empty()) {
        ImGui::TextDisabled("Nessun modello chunk presente nella libreria.");
    } else {
        ImGui::BeginChild("TemplateList", ImVec2(0, 140), true);
        for (int i = 0; i < (int)doc.terrainLibrary.size(); ++i) {
            bool isSelected = (m_activeTemplateIndex == i);
            if (ImGui::Selectable((std::to_string(i+1) + ". " + doc.terrainLibrary[i].name + " [" + doc.terrainLibrary[i].id + "]").c_str(), isSelected)) {
                m_activeTemplateIndex = i;
            }
        }
        ImGui::EndChild();
    }
    ImGui::Spacing();

    if (ImGui::Button("📊 APRI TABELLA CHUNKS EXCEL (COLLOCAMENTO GEOGRAFICO)", ImVec2(-1, 35))) {
        m_showPlacementTable = true;
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("📍 Punti di Inizio (Spawn Points)")) {
        if (!doc.planets.empty() && m_activePlanetIndex >= 0 && m_activePlanetIndex < (int)doc.planets.size()) {
            auto& p = doc.planets[m_activePlanetIndex];
            
            if (ImGui::Button("➕  Aggiungi Punto di Inizio", ImVec2(-1, 25))) {
                fw::SpawnPoint sp;
                sp.name = "Spawn " + std::to_string(p.spawnPoints.size() + 1);
                
                // --- CALCOLA LA POSIZIONE DAL PUNTATORE (CAMERA) ---
                if (m_context) {
                    glm::vec3 pos = glm::normalize(m_context->activeCameraView.cameraPosition);
                    float ax = std::abs(pos.x);
                    float ay = std::abs(pos.y);
                    float az = std::abs(pos.z);
                    int faceIndex = 0;
                    float u = 0.0f, v = 0.0f;
                    if (az >= ax && az >= ay) {
                        if (pos.z > 0) { faceIndex = 0; u = pos.x / az; v = pos.y / az; }
                        else           { faceIndex = 1; u = -pos.x / az; v = pos.y / az; }
                    } else if (ax >= ay && ax >= az) {
                        if (pos.x > 0) { faceIndex = 2; u = -pos.z / ax; v = pos.y / ax; }
                        else           { faceIndex = 3; u = pos.z / ax; v = pos.y / ax; }
                    } else {
                        if (pos.y > 0) { faceIndex = 4; u = pos.x / ay; v = -pos.z / ay; }
                        else           { faceIndex = 5; u = pos.x / ay; v = pos.z / ay; }
                    }
                    sp.faceIndex = faceIndex;
                    sp.localX = u * p.planetRadius;
                    sp.localZ = v * p.planetRadius;
                }
                
                p.spawnPoints.push_back(sp);
                // Salvataggio immediato con feedback visivo
                PMS_DoSave(m_context->projectManager, m_saveFlashTimer, m_saveFlashMsg);
            }
            ImGui::Spacing();
            
            for (int i = 0; i < (int)p.spawnPoints.size(); ++i) {
                auto& sp = p.spawnPoints[i];
                ImGui::PushID(i);
                
                char spName[64];
                strncpy_s(spName, sp.name.c_str(), sizeof(spName));
                if (ImGui::InputText("Nome", spName, sizeof(spName))) {
                    sp.name = spName;
                }
                
                const char* faceNames[] = { "+Z (Nord)", "-Z (Sud)", "+X (Est)", "-X (Ovest)", "+Y (Top/Cielo)", "-Y (Bottom/Nucleo)" };
                ImGui::Combo("Faccia Base", &sp.faceIndex, faceNames, IM_ARRAYSIZE(faceNames));
                
                ImGui::SliderFloat("Offset X", &sp.localX, -p.planetRadius, p.planetRadius, "%.1f");
                ImGui::SliderFloat("Offset Z", &sp.localZ, -p.planetRadius, p.planetRadius, "%.1f");
                ImGui::SliderFloat("Offset Y (Altezza)", &sp.heightOffset, 0.0f, p.planetRadius + 300.0f, "%.1f");
                
                float c[4] = { sp.color.r, sp.color.g, sp.color.b, sp.color.a };
                if (ImGui::ColorEdit4("Colore Prisma", c)) {
                    sp.color = glm::vec4(c[0], c[1], c[2], c[3]);
                }
                
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.1f, 1.0f));
                if (ImGui::Button("Rimuovi", ImVec2(80, 20))) {
                    p.spawnPoints.erase(p.spawnPoints.begin() + i);
                    PMS_DoSave(m_context->projectManager, m_saveFlashTimer, m_saveFlashMsg);
                    ImGui::PopStyleColor();
                    ImGui::PopID();
                    break;
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.5f, 0.2f, 1.0f));
                if (ImGui::Button("\xF0\x9F\x92\xBE Salva", ImVec2(-1, 20))) {
                    PMS_DoSave(m_context->projectManager, m_saveFlashTimer, m_saveFlashMsg);
                }
                ImGui::PopStyleColor();
                
                ImGui::Separator();
                ImGui::PopID();
            }
        }
    }

    ImGui::EndChild();

    ImGui::BeginChild("BottomBar", ImVec2(0, 130.0f), true);
    
    // --- Flash di conferma salvataggio ---
    if (m_saveFlashTimer > 0.0f) {
        ImVec4 flashColor = (m_saveFlashMsg[0] == '\xE2') // UTF-8 ✅
            ? ImVec4(0.2f, 1.0f, 0.4f, 1.0f)
            : ImVec4(1.0f, 0.3f, 0.2f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, flashColor);
        ImGui::Text("%s  (saves/map/world_map.json)", m_saveFlashMsg.c_str());
        ImGui::PopStyleColor();
    }
    
    if (ImGui::Button("\xF0\x9F\x92\xBE SALVA MONDO E MAPPA 3D (WORLD PROJECT)", ImVec2(-1, 30))) {
        PMS_DoSave(m_context->projectManager, m_saveFlashTimer, m_saveFlashMsg);
        m_showSaveConfirmPopup = true;
    }
    if (ImGui::Button("🚀 ESPLORA MAPPA IN PRIMA PERSONA (VOXEL TEST)", ImVec2(-1, 30))) {
        CompileAndGenerate();
    }
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.3f, 1.0f));
    if (ImGui::Button("🌍 CARICA MAPPA COME PRINCIPALE E APRI IN FAIRWORLD PLAY", ImVec2(-1, 30))) {
        m_context->projectManager->SaveProject();
        m_context->targetGameJsonPath = "saves/map/world_map.json";
        m_context->engine->SetGameMode(GameMode::Play);
        m_context->stateManager->ChangeState(std::make_unique<PlayState>(m_context));
    }
    ImGui::PopStyleColor();
    ImGui::EndChild();

    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + leftWidth + 20.0f, viewport->Pos.y + 20.0f));
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::Begin("OverlayGlobe", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), ">>> GLOBO SFERICO LOD 3D (ANTEPRIMA PIANETA IN TEMPO REALE) <<<");
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 0.8f), "Drag mouse: ruota  |  Rotellina: zoom  |  W/S: avvicina/allontana  |  A/D: ruota  |  R/F: su/giu");
    
    ImGui::Separator();
    
    // --- TOOL POSIZIONE CAMERA ---
    // Calcola lat/lon dal yaw/pitch orbitale
    float pitch = m_orbitPitch;
    float yaw   = m_orbitYaw;
    
    // La camera guarda verso il centro del pianeta, quindi la direzione "al suolo" è l'opposto della camera
    float pitchRad = glm::radians(pitch);
    float yawRad   = glm::radians(yaw);
    glm::vec3 camNorm;
    camNorm.x = cos(pitchRad) * sin(yawRad);
    camNorm.y = sin(pitchRad);
    camNorm.z = cos(pitchRad) * cos(yawRad);
    // camNorm è la direzione dalla camera verso il pianeta (già normalizzata)
    
    // Latitudine e Longitudine geografiche
    float latDeg = glm::degrees(asin(std::clamp(camNorm.y, -1.0f, 1.0f)));
    float lonDeg = glm::degrees(atan2(camNorm.x, camNorm.z));
    
    // Faccia del cubo sferico
    float camAx = std::abs(camNorm.x), camAy = std::abs(camNorm.y), camAz = std::abs(camNorm.z);
    const char* faceName = "";
    if      (camAz >= camAx && camAz >= camAy) faceName = camNorm.z > 0 ? "NORD (+Z)" : "SUD (-Z)";
    else if (camAx >= camAy && camAx >= camAz) faceName = camNorm.x > 0 ? "EST (+X)"  : "OVEST (-X)";
    else                                        faceName = camNorm.y > 0 ? "POLO NORD (+Y)" : "POLO SUD (-Y)";
    
    // Distanza dalla superficie (se c'è un pianeta caricato)
    float surfaceDist = m_orbitDistance;
    if (!doc.planets.empty() && m_activePlanetIndex >= 0 && m_activePlanetIndex < (int)doc.planets.size()) {
        surfaceDist = m_orbitDistance - doc.planets[m_activePlanetIndex].planetRadius;
    }
    
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "POSIZIONE CAMERA:");
    ImGui::Text("  Latitudine: %.1f deg  |  Longitudine: %.1f deg", latDeg, lonDeg);
    ImGui::Text("  Faccia:     %s", faceName);
    ImGui::Text("  Dist. dalla superficie: %.1f m  |  Orbita: %.1f m", surfaceDist, m_orbitDistance);
    
    // --- SLIDER LOD DISTANCE ---
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "LOD (distanza divisione chunk):");
    float lodMul = m_lodSystem.GetDistanceMultiplier();
    if (ImGui::SliderFloat("Moltiplicatore LOD", &lodMul, 1.0f, 8.0f, "%.1fx")) {
        m_lodSystem.SetDistanceMultiplier(lodMul);
    }
    ImGui::TextDisabled("Basso = chunk vicini e dettagliati  |  Alto = chunk piu' distanti");
    
    ImGui::End();

    if (m_showSaveConfirmPopup) {
        ImGui::OpenPopup("PianetaSalvato");
        m_showSaveConfirmPopup = false;
    }
    if (ImGui::BeginPopupModal("PianetaSalvato", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Mappa planetaria salvata con successo via WorldProjectManager in world_map.json.");
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (m_showPlacementTable && !doc.planets.empty() && m_activePlanetIndex >= 0 && m_activePlanetIndex < (int)doc.planets.size()) {
        auto& currentPlanet = doc.planets[m_activePlanetIndex];
        float pRadius = currentPlanet.planetRadius;
        int N_lato = (int)std::ceil((glm::pi<float>() * pRadius) / (2.0f * 16.0f));
        if (N_lato < 1) N_lato = 1;

        ImGui::SetNextWindowSize(ImVec2(1000, 600), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Tabella Collocamento Chunks - Excel Style (Legge Sferica)", &m_showPlacementTable)) {
            std::map<int, int> gridLookup;
            for (int i = 0; i < (int)currentPlanet.chunkInstances.size(); ++i) {
                if (currentPlanet.chunkInstances[i].isGridAligned) {
                    int key = currentPlanet.chunkInstances[i].faceIndex * 1000000 + currentPlanet.chunkInstances[i].gridY * 1000 + currentPlanet.chunkInstances[i].gridX;
                    gridLookup[key] = i;
                }
            }

            int toDeleteIndex = -1;
            bool shouldPushToAdd = false;
            bool needsRebuild = false;
            fw::PlanetChunkInstance toAdd;

            if (ImGui::BeginTabBar("FacesTabBar")) {
                const char* faceNames[] = { "+Z (Nord)", "-Z (Sud)", "+X (Est)", "-X (Ovest)", "+Y (Top/Cielo)", "-Y (Bottom/Nucleo)" };
                for (int f = 0; f < 6; ++f) {
                    if (ImGui::BeginTabItem(faceNames[f])) {
                        ImGui::Text("Faccia %d - Risoluzione Griglia: %d x %d Cella (Ogni cella rappresenta una porzione del globo)", f, N_lato, N_lato);
                        ImGui::TextDisabled("Seleziona un modello chunk a sinistra nel catalogo e clicca su una cella vuota (---) per piazzare il chunk.");
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Clicca su una cella verde (occupata) per rimuovere quel chunk dalla sfera.");
                        
                        if (ImGui::Button("Riempi Tutti i Vuoti", ImVec2(150, 25))) {
                            if (m_activeTemplateIndex >= 0 && m_activeTemplateIndex < (int)doc.terrainLibrary.size()) {
                                for (int row = 0; row < N_lato; ++row) {
                                    for (int col = 0; col < N_lato; ++col) {
                                        int key = f * 1000000 + row * 1000 + col;
                                        if (gridLookup.find(key) == gridLookup.end()) {
                                            fw::PlanetChunkInstance addInst;
                                            addInst.name = "Chunk_" + std::to_string(f) + "_" + std::to_string(col) + "_" + std::to_string(row);
                                            addInst.templateId = doc.terrainLibrary[m_activeTemplateIndex].id;
                                            addInst.isGridAligned = true;
                                            addInst.faceIndex = f;
                                            addInst.gridX = col;
                                            addInst.gridY = row;
                                            float cx = (col + 0.5f) / N_lato * 2.0f - 1.0f;
                                            float cy = 1.0f - (row + 0.5f) / N_lato * 2.0f;
                                            glm::vec3 dir(0.0f);
                                            switch(f) {
                                                case 0: dir = glm::vec3(cx, cy, 1.0f); break;
                                                case 1: dir = glm::vec3(-cx, cy, -1.0f); break;
                                                case 2: dir = glm::vec3(1.0f, cy, -cx); break;
                                                case 3: dir = glm::vec3(-1.0f, cy, cx); break;
                                                case 4: dir = glm::vec3(cx, 1.0f, -cy); break;
                                                case 5: dir = glm::vec3(cx, -1.0f, cy); break;
                                            }
                                            dir = glm::normalize(dir);
                                            addInst.eulerAngles.x = glm::degrees(asin(dir.y));
                                            addInst.eulerAngles.y = glm::degrees(atan2(dir.z, dir.x));
                                            addInst.angularRadius = (glm::pi<float>() / 2.0f) / N_lato * 1.5f;
                                            currentPlanet.chunkInstances.push_back(addInst);
                                            needsRebuild = true;
                                        }
                                    }
                                }
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Svuota Tutta la Faccia", ImVec2(180, 25))) {
                            for (int row = 0; row < N_lato; ++row) {
                                for (int col = 0; col < N_lato; ++col) {
                                    int key = f * 1000000 + row * 1000 + col;
                                    auto it = gridLookup.find(key);
                                    if (it != gridLookup.end()) {
                                        // Segna per l'eliminazione (richiede un po' di attenzione con gli indici, ma lo facciamo ricreando l'array)
                                        toDeleteIndex = it->second;
                                    }
                                }
                            }
                            // Metodo più sicuro: rimuovere tutti gli elementi di questa faccia
                            auto newEnd = std::remove_if(currentPlanet.chunkInstances.begin(), currentPlanet.chunkInstances.end(), [f](const fw::PlanetChunkInstance& inst) {
                                return inst.isGridAligned && inst.faceIndex == f;
                            });
                            if (newEnd != currentPlanet.chunkInstances.end()) {
                                currentPlanet.chunkInstances.erase(newEnd, currentPlanet.chunkInstances.end());
                                needsRebuild = true;
                            }
                        }
                        ImGui::Spacing();
                        
                        ImGui::BeginChild(std::string("GridScroll_" + std::to_string(f)).c_str(), ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
                        ImGuiListClipper clipper;
                        clipper.Begin(N_lato, 35.0f);
                        while (clipper.Step()) {
                            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                                for (int col = 0; col < N_lato; ++col) {
                                    int key = f * 1000000 + row * 1000 + col;
                                    auto it = gridLookup.find(key);
                                    ImGui::PushID(key);
                                    if (col > 0) ImGui::SameLine(0, 2.0f);

                                    if (it != gridLookup.end()) {
                                        auto& inst = currentPlanet.chunkInstances[it->second];
                                        std::string shortName = "CH";
                                        for (const auto& t : doc.terrainLibrary) {
                                            if (t.id == inst.templateId) { shortName = t.name.substr(0, std::min<size_t>(t.name.size(), 4)); break; }
                                        }
                                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
                                        if (ImGui::Button(shortName.c_str(), ImVec2(45, 30))) {
                                            toDeleteIndex = it->second;
                                        }
                                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Chunk: %s\nTemplate ID: %s", inst.name.c_str(), inst.templateId.c_str());
                                        ImGui::PopStyleColor();
                                    } else {
                                        if (ImGui::Button("---", ImVec2(45, 30))) {
                                            if (m_activeTemplateIndex >= 0 && m_activeTemplateIndex < (int)doc.terrainLibrary.size()) {
                                                toAdd.name = "Chunk_" + std::to_string(f) + "_" + std::to_string(col) + "_" + std::to_string(row);
                                                toAdd.templateId = doc.terrainLibrary[m_activeTemplateIndex].id;
                                                toAdd.isGridAligned = true;
                                                toAdd.faceIndex = f;
                                                toAdd.gridX = col;
                                                toAdd.gridY = row;
                                                
                                                float cx = (col + 0.5f) / N_lato * 2.0f - 1.0f;
                                                float cy = 1.0f - (row + 0.5f) / N_lato * 2.0f;
                                                glm::vec3 dir(0.0f);
                                                switch(f) {
                                                    case 0: dir = glm::vec3(cx, cy, 1.0f); break;
                                                    case 1: dir = glm::vec3(-cx, cy, -1.0f); break;
                                                    case 2: dir = glm::vec3(1.0f, cy, -cx); break;
                                                    case 3: dir = glm::vec3(-1.0f, cy, cx); break;
                                                    case 4: dir = glm::vec3(cx, 1.0f, -cy); break;
                                                    case 5: dir = glm::vec3(cx, -1.0f, cy); break;
                                                }
                                                dir = glm::normalize(dir);
                                                toAdd.eulerAngles.x = glm::degrees(asin(dir.y));
                                                toAdd.eulerAngles.y = glm::degrees(atan2(dir.z, dir.x));
                                                toAdd.angularRadius = (glm::pi<float>() / 2.0f) / N_lato * 1.5f;
                                                shouldPushToAdd = true;
                                                needsRebuild = true;
                                            }
                                        }
                                    }
                                    ImGui::PopID();
                                }
                            }
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }

            if (toDeleteIndex >= 0) {
                currentPlanet.chunkInstances.erase(currentPlanet.chunkInstances.begin() + toDeleteIndex);
                needsRebuild = true;
            }
            if (shouldPushToAdd) {
                currentPlanet.chunkInstances.push_back(toAdd);
                needsRebuild = true;
            }
            if (needsRebuild) {
                RebuildPlanetRoots();
            }
        }
        ImGui::End();
    }
}

void PlanetMapperState::DrawRuntimeUI() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 20, viewport->Pos.y + 20));
    ImGui::Begin("RuntimeControls", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground);
    if (ImGui::Button("<< TORNA ALL'EDITOR GLOBO 3D", ImVec2(250, 35))) {
        m_isBuilderMode = true;
        RebuildPlanetRoots();
        m_orbitTarget = glm::vec3(0.0f, 0.0f, 0.0f);
        m_orbitDistance = 250.0f;
    }
    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "ESPLORAZIONE VOXEL 3D ATTIVA (Collaudo Pianeta Unificato)");
    ImGui::Text("Camera Orbitale a Distanza: %.1f", m_orbitDistance);
    ImGui::End();
}

void PlanetMapperState::CompileAndGenerate() {
    if (!m_context || !m_context->projectManager) return;
    auto& doc = m_context->projectManager->GetDocumentMutable();

    std::cout << "[PlanetMapperState] Inizio compilazione e generazione voxel dell'intero pianeta...\n";
    if (m_context && m_context->jobSystem) {
        m_context->jobSystem->WaitAll();
    }
    if (m_context) {
        if (m_previewWorld && m_context->forgeWorld == m_previewWorld.get()) m_context->forgeWorld = nullptr;
        if (m_previewWorld && m_context->activeRegistry == &m_previewWorld->GetRegistry()) m_context->activeRegistry = nullptr;
    }
    m_previewWorld = std::make_unique<fw::GameWorld>();
    m_previewWorld->Initialize(m_context);

    m_context->activeRegistry = &m_previewWorld->GetRegistry();
    m_context->forgeWorld = m_previewWorld.get();
    m_context->isForgeMode = false;

    if (m_context->cacheManager) {
        m_context->cacheManager->FlushGpuRenderCaches(m_context);
        m_context->cacheManager->FlushCpuTransientCaches(m_context);
    } else if (m_context->engine && m_context->engine->GetRenderManager()) {
        m_context->engine->GetRenderManager()->InvalidateForgeCache();
    }
    if (m_context->engine) {
        m_context->engine->SetGameMode(GameMode::PlanetMapper);
    }

    if (doc.planets.empty() || m_activePlanetIndex < 0 || m_activePlanetIndex >= (int)doc.planets.size()) return;
    auto& currentPlanet = doc.planets[m_activePlanetIndex];
    currentPlanet.regions.clear();

    if (!currentPlanet.chunkInstances.empty()) {
        for (const auto& inst : currentPlanet.chunkInstances) {
            for (const auto& tmpl : doc.terrainLibrary) {
                if (tmpl.id == inst.templateId) {
                    fw::MapRegion baseRegion;
                    baseRegion.eulerAngles = inst.eulerAngles;
                    baseRegion.angularRadius = inst.angularRadius;
                    baseRegion.isGridAligned = inst.isGridAligned;
                    baseRegion.faceIndex = inst.faceIndex;
                    baseRegion.gridX = inst.gridX;
                    baseRegion.gridY = inst.gridY;
                    baseRegion.type = tmpl.baseType;
                    baseRegion.perlinFrequency = tmpl.basePerlinFrequency;
                    baseRegion.gravityModifier = tmpl.baseGravityModifier;
                    baseRegion.seed = tmpl.seed;

                    if (inst.isGridAligned && inst.gridX != -1 && inst.gridY != -1) {
                        int radiusTiles = (int)std::max(1.0f, inst.angularRadius * 10.0f);
                        baseRegion.rectMin = glm::ivec2(inst.gridX - radiusTiles, inst.gridY - radiusTiles);
                        baseRegion.rectMax = glm::ivec2(inst.gridX + radiusTiles, inst.gridY + radiusTiles);
                    }
                    currentPlanet.regions.push_back(baseRegion);

                    for (const auto& sub : tmpl.subRegions) {
                        fw::MapRegion projectedSub = sub;
                        if (inst.isGridAligned && inst.gridX != -1 && inst.gridY != -1) {
                            projectedSub.rectMin += glm::ivec2(inst.gridX, inst.gridY);
                            projectedSub.rectMax += glm::ivec2(inst.gridX, inst.gridY);
                        }
                        currentPlanet.regions.push_back(projectedSub);
                    }
                    break;
                }
            }
        }
    } else if (m_activeTemplateIndex >= 0 && m_activeTemplateIndex < (int)doc.terrainLibrary.size()) {
        const auto& tmpl = doc.terrainLibrary[m_activeTemplateIndex];
        fw::MapRegion baseRegion;
        baseRegion.eulerAngles = glm::vec3(0.0f);
        baseRegion.angularRadius = tmpl.baseAngularRadius;
        baseRegion.type = tmpl.baseType;
        baseRegion.perlinFrequency = tmpl.basePerlinFrequency;
        baseRegion.gravityModifier = tmpl.baseGravityModifier;
        baseRegion.seed = tmpl.seed;
        currentPlanet.regions.push_back(baseRegion);

        for (const auto& sub : tmpl.subRegions) {
            fw::MapRegion projectedSub = sub;
            projectedSub.eulerAngles = glm::vec3(0.0f);
            currentPlanet.regions.push_back(projectedSub);
        }
    }

    fw::MapWorldGenerator::Generate(doc, m_activePlanetIndex, *m_previewWorld, m_context->jobSystem);

    m_isBuilderMode = false;
    
    if (currentPlanet.planetRadius > 0.0f) {
        // Telecamera sferica
        m_orbitTarget = glm::vec3(0.0f, 0.0f, 0.0f);
        m_orbitDistance = currentPlanet.planetRadius + 100.0f;
        m_orbitPitch = 40.0f;
        m_orbitYaw = 45.0f;
        std::cout << "[PlanetMapperState] Anteprima Voxel sferica completata! Camera in orbita a raggio: " << m_orbitDistance << "\n";
    } else {
        // Telecamera piana (legacy)
        float midX = ((float)(currentPlanet.maxX + currentPlanet.minX) * 0.5f) * 16.0f;
        float midZ = ((float)(currentPlanet.maxZ + currentPlanet.minZ) * 0.5f) * 16.0f;
        float midY = 20.0f;
        m_orbitTarget = glm::vec3(midX, midY, midZ);
        float mapSpan = std::max((float)(currentPlanet.maxX - currentPlanet.minX), (float)(currentPlanet.maxZ - currentPlanet.minZ)) * 16.0f;
        m_orbitDistance = std::max(mapSpan * 1.5f, 60.0f);
        m_orbitPitch = 40.0f;
        m_orbitYaw = 45.0f;
        std::cout << "[PlanetMapperState] Anteprima Voxel del Pianeta completata! Camera centrata su: (" << midX << ", " << midY << ", " << midZ << ")\n";
    }
}
