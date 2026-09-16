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
#include "vulkan/ChunkCullingTypes.h"
#include "CacheManager.h"
#include "imgui.h"
#include "PlayState.h"
#include "RaycastSystem.h"
#include "CubeSphereMapping.h"
#include <iostream>
#include <algorithm>
#include <cmath>

PlanetMapperState::PlanetMapperState(SharedContext* context) : AppBaseState(context) {
    std::cout << "[PlanetMapperState] Costruito come estensione di AppBaseState.\n";
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
        m_context->projectManager->ValidateBlocks(m_context->blockRegistry);
    }

    m_previewWorld = std::make_unique<fw::GameWorld>();
    m_previewWorld->Initialize(m_context);
    m_previewWorld->InitializePhysics();

    if (m_context && m_context->engine) {
        m_context->engine->SetGameMode(GameMode::PlanetMapper);
        m_context->activeRegistry = &m_previewWorld->GetRegistry();
        m_context->forgeWorld = m_previewWorld.get();
    }

    m_camera.Init(250.0f, 20.0f, -45.0f);
    
    RebuildPlanetRoots();
    m_compiler.Update(m_context, m_activePlanetIndex);

    m_appName = "PLANET MAPPER";
    std::cout << "[SYSTEM] " << m_appName << " caricato con successo e pronto all'uso!\n";

    return true;
}

void PlanetMapperState::RebuildPlanetRoots() {
    if (!m_context || !m_context->projectManager) return;
    const auto& doc = m_context->projectManager->GetDocument();
    fw::PlanetSize pSize = fw::PlanetSize::Small;
    bool isFlat = false;
    if (!doc.planets.empty() && m_activePlanetIndex >= 0 && m_activePlanetIndex < (int)doc.planets.size()) {
        pSize = doc.planets[m_activePlanetIndex].planetSize;
        isFlat = doc.planets[m_activePlanetIndex].isFlat;
    }
    float R = fw::PlanetMath::GetPlanetRadius(pSize);
    m_lodSystem.SetPlanetSize(pSize, isFlat);

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
    auto& doc = m_context->projectManager->GetDocumentMutable();

    m_ui.Update(dt);

    if (m_context) {
        m_context->isMapBuilderMode = true;
        m_context->isForgeMode = false;
        if (m_previewWorld) {
            m_context->forgeWorld = m_previewWorld.get();
            m_context->activeRegistry = &m_previewWorld->GetRegistry();
        }
        if (m_context->engine) {
            m_context->engine->SetGameMode(GameMode::PlanetMapper);
        }
    }

    uint32_t w = 1920, h = 1080;
    if (m_context->engine && m_context->engine->GetRenderManager()) {
        w = m_context->engine->GetRenderManager()->GetWindowWidth();
        h = m_context->engine->GetRenderManager()->GetWindowHeight();
    }

    // Aggiornamento telecamera
    m_camera.Update(dt, m_context, w, h);

    // m_ui.Draw() viene chiamato in RenderApp() dove il frame ImGui e' attivo.
    // Qui consumiamo solo il risultato dell'ultimo frame Draw.
    PlanetMapperUIResult uiResult = m_lastUIResult;
    m_lastUIResult = {}; // Reset per il prossimo frame

    if (uiResult.requestRebuildRoots || uiResult.documentChanged) {
        RebuildPlanetRoots();
        m_compiler.Update(m_context, m_activePlanetIndex); // Refresh GPU chunks after root change
    }

    if (uiResult.goToPlayState) {
        if (m_previewWorld) {
            m_previewWorld->CancelJobs();
            if (m_context->jobSystem) m_context->jobSystem->WaitAll();
            m_previewWorld->GetChunkManager().ClearDiskCache();
        }
        m_context->targetGameJsonPath = "saves/map/world_map.json";
        m_context->engine->SetGameMode(GameMode::Play);
        m_context->stateManager->ChangeState(std::make_unique<PlayState>(m_context));
        return;
    } else if (uiResult.goToHubState) {
        if (m_previewWorld) {
            m_previewWorld->CancelJobs();
            if (m_context->jobSystem) m_context->jobSystem->WaitAll();
        }
        m_context->engine->SetGameMode(GameMode::Hub);
        m_context->stateManager->ChangeState(std::make_unique<HubState>(m_context));
        return;
    }

    if (m_context && m_context->jobSystem && m_context->assetManager) {
        const fw::PlanetMap* pMap = nullptr;
        std::vector<fw::MapRegion> activeRegions;
        if (!doc.planets.empty() && m_activePlanetIndex >= 0 && m_activePlanetIndex < (int)doc.planets.size()) {
            pMap = &doc.planets[m_activePlanetIndex];
            activeRegions = pMap->regions;
            
            for (const auto& inst : pMap->chunkInstances) {
                if (!inst.isActive) continue;
                
                for (const auto& tpl : doc.terrainLibrary) {
                    if (tpl.id == inst.templateId) {
                        int baseX = inst.gridX;
                        int baseZ = inst.gridY;
                        
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
                        baseR.overrides.height = fw::HeightRuleOverrides();
                        baseR.overrides.height->frequency = tpl.baseRules.height.frequency;
                        activeRegions.push_back(baseR);
                        
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
            m_lodSystem.SetPlanetSize(pMap->planetSize, pMap->isFlat);
        }
        for (auto& root : m_planetRootNodes) {
            m_lodSystem.UpdateLODTree(root, m_context->activeCameraView.cameraPosition, m_previewWorld.get(), m_context->jobSystem, m_context->assetManager, activeRegions, vpMatrix, m_context->blockRegistry, pMap ? pMap->baseTerrain : fw::PlanetBaseTerrain());
        }
    }

    if (m_previewWorld) {
        if (m_context && m_context->blockRegistry) {
            fw::BiomeTerrainSystem::Update(m_previewWorld->GetRegistry(), 15, m_context->blockRegistry);
            fw::BiomeDecoratorSystem::Update(m_previewWorld->GetRegistry(), 15, m_context->blockRegistry);
        }
        m_previewWorld->Update(dt);
        
        if (!doc.planets.empty() && m_activePlanetIndex >= 0 && m_activePlanetIndex < (int)doc.planets.size()) {
            auto& p = doc.planets[m_activePlanetIndex];
            while (m_spawnPointMarkers.size() > p.spawnPoints.size()) {
                auto ent = m_spawnPointMarkers.back();
                if (m_previewWorld->GetRegistry().valid(ent)) m_previewWorld->DestroyEntity(ent);
                m_spawnPointMarkers.pop_back();
            }
            while (m_spawnPointMarkers.size() < p.spawnPoints.size()) {
                entt::entity newMarker = m_previewWorld->CreatePrimitive("SpawnMarker", fw::Vec3(0.0f, 0.0f, 0.0f), "obelisk");
                auto& mesh = m_previewWorld->GetRegistry().get<fw::MeshComponent>(newMarker);
                mesh.type = fw::MeshType::Standard;
                m_previewWorld->UploadMeshToVram(newMarker);
                m_spawnPointMarkers.push_back(newMarker);
            }
            
            for (size_t i = 0; i < p.spawnPoints.size(); ++i) {
                auto& sp = p.spawnPoints[i];
                auto ent = m_spawnPointMarkers[i];
                if (!m_previewWorld->GetRegistry().valid(ent)) continue;
                
                float R = fw::PlanetMath::GetPlanetRadius(p.planetSize);
                float cx = sp.localX / R;
                float cy = sp.localZ / R;
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
                glm::vec3 pos = dir * (R + sp.heightOffset);
                
                auto& trans = m_previewWorld->GetRegistry().get<fw::TransformComponent>(ent);
                trans.location = fw::Vec3(pos.x, pos.y, pos.z);

                glm::vec3 worldUp = dir;
                glm::vec3 forward(1.0f, 0.0f, 0.0f);
                if (glm::abs(glm::dot(worldUp, forward)) > 0.99f) forward = glm::vec3(0.0f, 0.0f, 1.0f);
                glm::vec3 right = glm::normalize(glm::cross(worldUp, forward));
                forward = glm::normalize(glm::cross(right, worldUp));
                glm::mat3 rotMat(right, worldUp, forward);
                glm::quat oq = glm::quat_cast(rotMat);
                trans.rotation = fw::Quat(oq.x, oq.y, oq.z, oq.w);
                trans.scale = fw::Vec3(1.0f, 1.0f, 1.0f);
                
                auto& meshComp = m_previewWorld->GetRegistry().get<fw::MeshComponent>(ent);
                meshComp.colorOverride[0] = sp.color.r;
                meshComp.colorOverride[1] = sp.color.g;
                meshComp.colorOverride[2] = sp.color.b;
                meshComp.colorOverride[3] = sp.color.a;
            }
            
            if (!m_previewWorld->GetRegistry().valid(m_cursorMarker)) {
                m_cursorMarker = m_previewWorld->CreatePrimitive("CursorMarker", fw::Vec3(0.0f, 0.0f, 0.0f), "cube");
                auto& mesh = m_previewWorld->GetRegistry().get<fw::MeshComponent>(m_cursorMarker);
                mesh.type = fw::MeshType::Standard;
                m_previewWorld->UploadMeshToVram(m_cursorMarker);
            }
            
            if (m_previewWorld->GetRegistry().valid(m_cursorMarker)) {
                auto& ctrans = m_previewWorld->GetRegistry().get<fw::TransformComponent>(m_cursorMarker);
                auto& cmesh = m_previewWorld->GetRegistry().get<fw::MeshComponent>(m_cursorMarker);
                
                if (m_camera.GetLastRayHit().hit) {
                    glm::vec3 cpos = m_camera.GetLastRayHit().worldPosition + m_camera.GetLastRayHit().faceNormal * 0.5f;
                    
                    ctrans.location = fw::Vec3(cpos.x, cpos.y, cpos.z);
                    
                    glm::vec3 worldUp = m_camera.GetLastRayHit().faceNormal;
                    glm::vec3 forward(1.0f, 0.0f, 0.0f);
                    if (glm::abs(glm::dot(worldUp, forward)) > 0.99f) forward = glm::vec3(0.0f, 0.0f, 1.0f);
                    glm::vec3 right = glm::normalize(glm::cross(worldUp, forward));
                    forward = glm::normalize(glm::cross(right, worldUp));
                    glm::mat3 rotMat(right, worldUp, forward);
                    glm::quat cq = glm::quat_cast(rotMat);
                    
                    ctrans.rotation = fw::Quat(cq.x, cq.y, cq.z, cq.w);
                    ctrans.scale = fw::Vec3(1.0f, 4.0f, 1.0f);
                    
                    cmesh.colorOverride[0] = 0.0f;
                    cmesh.colorOverride[1] = 1.0f;
                    cmesh.colorOverride[2] = 1.0f;
                    cmesh.colorOverride[3] = 1.0f;
                } else {
                    cmesh.colorOverride[3] = 0.0f;
                }
            }
        }
    }
}

void PlanetMapperState::RenderApp() {
    if (!m_context || !m_context->projectManager) return;

    // Draw UI (ImGui e' attivo in questo scope)
    float dist = m_camera.GetOrbitDistance();
    m_lastUIResult = m_ui.Draw(
        m_context,
        m_activePlanetIndex,
        m_activeTemplateIndex,
        dist,
        m_camera.GetOrbitYaw(),
        m_camera.GetOrbitPitch(),
        m_camera.GetLastRayHit(),
        m_lodSystem
    );
    // La distanza dell'orbita e' aggiornabile anche qui perche' e' solo un float.
    if (dist != m_camera.GetOrbitDistance()) {
        m_camera.SetOrbitDistance(dist);
    }
}
