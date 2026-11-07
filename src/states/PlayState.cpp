#include "pch.h"
#include "PlayState.h"
#include "SharedContext.h"
#include "DeviceManager.h"
#include "FAIRWORLD.h"
#include "Components.h"
#include "BiomeComponents.h"
#include "BlockRegistry.h"
#include "../components/Skeleton.h"
#include "PlanetComponents.h"
#include "PlanetSystems.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include "TimeManager.h"
#include "StateManager.h"
#include <iostream>
#include <fstream>
#include "json.hpp"
#include <windows.h>
#include "PortalSystem.h"
#include "ForgeComponents.h"
#include "ForgeWorld.h"
#include "GameWorld.h"
#include "RenderManager.h"
#include "AsyncInput.h"
#include "MapDocument.h"
#include "WorldProjectManager.h"
#include "MapWorldGenerator.h"
#include "PlanetarySpawnPolicy.h"
#include "CubeSphereMapping.h"
#include "PlanetaryCameraFrame.h"
#include "JobSystem.h"
#include "VulkanDmaManager.h"
#include "../core/app/RuntimeManager.h"
#include <imgui.h>

using json = nlohmann::json;

PlayState::PlayState(SharedContext* context) : m_context(context) {
    std::cout << "[PlayState] Costruito.\n";
}

PlayState::~PlayState() {
    std::cout << "[PlayState] Distrutto.\n";
    if (m_context && m_context->forgeWorld) {
        m_context->forgeWorld->ClearWorld(true); // Salva in saves/world e svuota
        m_context->activeRegistry = nullptr;
    }
}

#include <filesystem>

// (End of previous code)



bool PlayState::Init() {
    // --- CRITICAL FIX FOR GPU RENDERING BUG ---
    // Safely migrate JobSystem ownership to RuntimeManager and ensure PBR textures are loaded.
    if (m_context && m_context->runtimeManager) {
        m_context->runtimeManager->RequireFeaturesAsync(
            fw::RuntimeFeature::GlobalVRAM | 
            fw::RuntimeFeature::JobSystem | 
            fw::RuntimeFeature::PBRTextures
        );
    }

    // (Action Map registrata precedentemente nel DeviceManager)
    auto& bindings = m_context->deviceManager->GetActionMap().bindings;
    if (bindings.empty()) {
        m_context->deviceManager->InitDefaultBindings();
    }
    std::cout << "[PlayState] Action Map caricata: "
              << bindings.size() << " azioni logiche nel Kernel Bus.\n";

    if (!m_context->vramAllocator) {
        m_context->vramAllocator = new fw::VramSlabAllocator(2048ULL * 1024ULL * 1024ULL);
        m_context->vramAllocator->SetAllocateCompartmentCallback([ctx = m_context](uint32_t compIdx) {
            auto* rm = ctx && ctx->engine ? ctx->engine->GetRenderManager() : nullptr;
            if (!rm || rm->GetVramCompartments().size() != compIdx || !rm->AddVramCompartment()) return false;
            const auto& compartments = rm->GetVramCompartments();
            if (compartments.size() != static_cast<size_t>(compIdx) + 1 || compartments[compIdx] == VK_NULL_HANDLE) return false;
            if (ctx->dmaManager) ctx->dmaManager->UpdateStagingBuffer(
                rm->GetStagingRingBuffer(), rm->GetStagingDeviceMemory(), rm->GetMappedStagingData());
            return true;
        });
    }
    if (!m_context->dmaManager) {
        m_context->dmaManager = new fw::VulkanDmaManager();
        if (auto* rm = m_context->engine->GetRenderManager()) {
            m_context->dmaManager->Initialize(
                rm->GetDevice(), rm->GetTransferQueue(), rm->GetTransferCommandPool(),
                rm->GetStagingRingBuffer(), rm->GetStagingDeviceMemory(), rm->GetMappedStagingData(),
                rm->GetStagingBufferSize(), VK_NULL_HANDLE, rm->GetQueueMutex(),
                rm->GetTransferCommandPoolMutex()
            );
        }
    }

    // === CARICAMENTO CONFIGURAZIONE DATA-DRIVEN (JSON) ===
    std::string configPath = m_context->targetGameJsonPath;
    
    // CONTROLLO CARTUCCIA JSON DA MAP BUILDER ritardato dopo ClearWorld
    bool hasCustomMap = false;
    if (!configPath.empty() && configPath.find("world_map.json") != std::string::npos) {
        hasCustomMap = true;
    } 
    
    json data;
    if (configPath.empty() || configPath.find("world_map.json") == std::string::npos) {
        std::cout << "[PlayState] Nessuna mappa custom o errore. Generazione standard...\n";
        configPath = "projects/default_game.json"; // Fallback fittizio
        std::ifstream file(configPath);
        if (!file.is_open()) {
            data = json::parse(R"({"entities": []})");
        } else {
            try { data = json::parse(file); } catch (...) { data = json::parse(R"({"entities": []})"); }
        }
        std::cout << "[PlayState] Creazione entities EnTT dal JSON (o Fallback) in corso...\n";
    }
    
    int entityCount = 0;
    if (data.contains("entities") && data["entities"].is_array()) {
        for (const auto& entityJson : data["entities"]) {
            auto entity = m_registry.create();
            entityCount++;
            
            if (entityJson.contains("name")) {
                m_registry.emplace<NameComponent>(entity, entityJson["name"].get<std::string>());
            }
            
            if (entityJson.contains("transform")) {
                auto& tf = entityJson["transform"];
                m_registry.emplace<TransformComponent>(entity, 
                    tf.value("x", 0.0f), 
                    tf.value("y", 0.0f), 
                    tf.value("z", 0.0f)
                );
            }
        }
    }
    
    // --- PRE-CARICAMENTO MAP DOCUMENT PER GLI SPAWN POINTS ---
    fw::MapDocument mapDoc;
    bool mapDocLoaded = false;
    if (hasCustomMap) {
        if (m_context->projectManager) {
            m_context->projectManager->LoadProject(configPath, m_context->blockRegistry);
            mapDoc = m_context->projectManager->GetDocument();
            mapDocLoaded = true;
        } else if (mapDoc.LoadJSON(configPath)) {
            mapDocLoaded = true;
        }
    }

    // --- Creazione Telecamera Principale (Player) ---
    auto cameraEntity = m_registry.create();
    m_registry.emplace<NameComponent>(cameraEntity, "MainCamera");
    
    fw::PlanetarySpawnPlan fallbackSpawn = fw::MakePlanetaryFallbackSpawn(fw::PlanetSize::Medium);
    float spawnY = fallbackSpawn.temporaryBodyPosition.y;
    glm::vec3 spawnPos = fallbackSpawn.temporaryBodyPosition;
    m_pendingPlanetarySpawn = fallbackSpawn.pending;
    m_pendingSpawnDirection = fallbackSpawn.direction;
    m_pendingSpawnHeightOffset = fallbackSpawn.heightOffset;
    m_pendingSpawnWaitLogged = false;
    
    if (mapDocLoaded && !mapDoc.planets.empty()) {
        auto& p = mapDoc.planets[0];
        // PlayState is always a logical sphere, including legacy documents whose
        // isFlat flag is retained only for document compatibility.
        fallbackSpawn = fw::MakePlanetaryFallbackSpawn(p.planetSize);
        spawnPos = fallbackSpawn.temporaryBodyPosition;
        spawnY = spawnPos.y;
        m_pendingPlanetarySpawn = fallbackSpawn.pending;
        m_pendingSpawnDirection = fallbackSpawn.direction;
        m_pendingSpawnHeightOffset = fallbackSpawn.heightOffset;
        
        if (!p.spawnPoints.empty()) {
            auto& sp = p.spawnPoints[0];
            float R = fw::PlanetMath::GetPlanetRadius(p.planetSize);
            if (sp.faceIndex < 0 || sp.faceIndex > 5 || !std::isfinite(R) || R <= 0.0f ||
                !std::isfinite(sp.localX) || !std::isfinite(sp.localZ) || !std::isfinite(sp.heightOffset)) {
                std::cerr << "[PlayState] Spawn planetario ignorato: coordinate documentali non valide.\n";
            } else {
                const glm::vec2 chart(sp.localX / R, sp.localZ / R);
                glm::vec3 dir = fw::CubeSphereMapping::FaceChartToDirection(sp.faceIndex, chart);
                // Terrain chunks are generated below and solved asynchronously during
                // GameWorld::Update. Keep the player above the existing safe fallback
                // until the actual voxel surface and collider footprint are queryable.
                m_pendingPlanetarySpawn = true;
                m_pendingSpawnDirection = dir;
                m_pendingSpawnHeightOffset = sp.heightOffset;
                spawnPos = dir * (R + 150.0f);
                spawnY = spawnPos.y; // Update for rigidity below
                std::cout << "[PlayState] Spawn custom '" << sp.name
                          << "' in attesa della superficie voxel; posizione temporanea sicura: "
                          << spawnPos.x << ", " << spawnPos.y << ", " << spawnPos.z << "\n";
            }
        }
    }
    
    // Posizione iniziale
    auto& cameraTransform = m_registry.emplace<TransformComponent>(cameraEntity, spawnPos.x, spawnPos.y, spawnPos.z);
    auto& cam = m_registry.emplace<CameraComponent>(cameraEntity);
    
    glm::vec3 initialUp(0.0f, 1.0f, 0.0f);
    if (!fw::PlanetaryCameraFrame::Normalize(spawnPos, initialUp)) initialUp = glm::vec3(0.0f, 1.0f, 0.0f);
    const auto initialCameraFrame = fw::PlanetaryCameraFrame::MakeInitial(initialUp);
    cam.planetaryFrameUp = initialCameraFrame.up;
    cam.planetaryFrameForward = initialCameraFrame.forward;
    cam.planetaryFrameInitialized = initialCameraFrame.initialized;
    cam.yaw = 0.0f;
    cam.pitch = 0.0f;
    fw::PlanetaryCameraFrame::Basis initialCameraBasis;
    if (fw::PlanetaryCameraFrame::BuildBasis(initialCameraFrame, cam.yaw, cam.pitch, initialCameraBasis)) {
        cam.front = initialCameraBasis.forward;
        cam.right = initialCameraBasis.right;
        cam.up = initialCameraBasis.up;
        cam.worldUp = initialCameraFrame.up;
        glm::mat3 cameraRotation;
        cameraRotation[0] = initialCameraBasis.right;
        cameraRotation[1] = initialCameraBasis.up;
        cameraRotation[2] = -initialCameraBasis.forward;
        cameraTransform.rotation = glm::quat_cast(cameraRotation);
        cameraTransform.prev_rotation = cameraTransform.rotation;
    }

    auto& playerController = m_registry.emplace<PlayerControllerComponent>(cameraEntity);
    playerController.planetarySpawnPending = m_pendingPlanetarySpawn;
    
    // Inizializza il RigidBody per la fisica
    auto& rbOpt = m_registry.emplace<RigidBodyComponent>(cameraEntity);
    rbOpt.body.position = spawnPos;
    rbOpt.body.mass = 70.0f;

    // Genera lo scheletro fisico del Player
    auto& playerSkeleton = m_registry.emplace<fw::Skeleton>(cameraEntity);
    fw::Skeleton::GenerateBiped(playerSkeleton);
    // --- INIZIALIZZAZIONE WORLD ---
    m_context->isForgeMode = false;
    m_context->isBlockMakerMode = false;

    if (!m_context->forgeWorld) {
        if (m_context->gameWorld) {
            m_context->gameWorld->InitializePhysics();
            m_context->forgeWorld = m_context->gameWorld;
        } else {
            static auto fallbackWorld = std::make_unique<fw::GameWorld>();
            fallbackWorld->Initialize(m_context);
            fallbackWorld->InitializePhysics();
            m_context->forgeWorld = fallbackWorld.get();
            m_context->gameWorld = fallbackWorld.get();
        }
    }

    m_context->activeRegistry = &m_context->forgeWorld->GetRegistry();

    // Il PlayState usa la directory saves/world/ — separata dalla Forge (saves/forge/)
    // I chunk già esistenti vengono caricati dal disco, quelli mancanti sono generati proceduralmente
    m_context->forgeWorld->SetSaveDirectory("saves/world");
    m_context->forgeWorld->ClearWorld();
    
    // Setup Base Palette per Fairworld (rimosso in favore del BlockRegistry json-driven)

    auto& registry = m_context->forgeWorld->GetRegistry();

    if (hasCustomMap) {
        std::cout << "[PlayState] Cartuccia Mappa rilevata: " << configPath << "\n";
        fw::MapDocument doc;
        bool loaded = false;
        if (m_context->projectManager) {
            std::cout << "[PlayState] Caricamento mappa verificata in memoria via WorldProjectManager...\n";
            m_context->projectManager->LoadProject(configPath, m_context->blockRegistry);
            doc = m_context->projectManager->GetDocument();
            loaded = true;
        } else if (doc.LoadJSON(configPath)) {
            loaded = true;
        }

        if (loaded) {
            std::cout << "[PlayState] Configurazione Pianeta caricata. Impostazione Geometria Sferica...\n";
            if (!doc.planets.empty()) {
                auto planetEnt = m_context->forgeWorld->GetPlanetEntity();
                if (registry.valid(planetEnt)) {
                    auto& geom = registry.get_or_emplace<fw::PlanetGeometryComponent>(planetEnt);
                    geom.planetSize = doc.planets[0].planetSize;
                    geom.isLogicalSphere = !doc.planets[0].isFlat;
                    std::cout << "[PlayState] Raggio pianeta impostato a size: " << (int)geom.planetSize << "\n";
                }
            }
        } else {
            std::cerr << "[PlayState] ERRORE: Impossibile leggere world_map.json. Fallback attivato.\n";
            hasCustomMap = false;
        }
    }
    // --- IMPOSTAZIONE GAME MODE PER L'HUD ---
    if (m_context && m_context->engine) {
        m_context->engine->SetGameMode(GameMode::Play);
    }
    
    std::cout << "[DEBUG] [PlayState] Generazione Terreno iniziale attorno al giocatore...\n";
    
    fw::PlanetSize originalSize = fw::PlanetSize::Medium;
    bool originalFlat = false;

    // Al posto di spawnare chunk fittizi, utilizziamo il MapWorldGenerator
    // per generare i voxel chunk partendo dal JSON del pianeta!
    if (hasCustomMap && m_context->projectManager && m_context->jobSystem) {
        auto docCopy = m_context->projectManager->GetDocumentMutable();
        if (!docCopy.planets.empty()) {
            originalSize = docCopy.planets[0].planetSize;
            originalFlat = docCopy.planets[0].isFlat;
            // docCopy.planets[0].planetSize viene mantenuto per la generazione sferica 3D
        }
        // Limitiamo la generazione iniziale della fisica a 10 chunk di distanza (160 unità)
        fw::MapWorldGenerator::Generate(docCopy, 0, *m_context->forgeWorld, m_context->jobSystem, 160.0f, spawnPos);
        RebuildPlanetRoots(docCopy, 0); // Creiamo la mesh LOD sferica per il rendering a distanza
    } else {
        // Fallback: Generiamo un pianeta standard se non c'è mappa custom
        fw::MapDocument dummyDoc;
        fw::PlanetMap pm;
        pm.planetSize = fw::PlanetSize::Medium; // SFERA VERA DI DEFAULT
        pm.isFlat = false;
        fw::MapRegion baseReg;
        baseReg.type = fw::MapRegionType::Forest;
        baseReg.overrides.height = fw::HeightRuleOverrides(); baseReg.overrides.height->common.frequency = 0.005f;
        pm.regions.push_back(baseReg);
        dummyDoc.planets.push_back(pm);
        // Limitiamo la generazione iniziale della fisica a 10 chunk di distanza
        fw::MapWorldGenerator::Generate(dummyDoc, 0, *m_context->forgeWorld, m_context->jobSystem, 160.0f, spawnPos);
        RebuildPlanetRoots(dummyDoc, 0);
    }
    
    if (m_context->forgeWorld && m_context->forgeWorld->GetRegistry().valid(m_context->forgeWorld->GetPlanetEntity())) {
        auto& geom = m_context->forgeWorld->GetRegistry().get<fw::PlanetGeometryComponent>(m_context->forgeWorld->GetPlanetEntity());
        geom.planetSize = originalSize;
        geom.isLogicalSphere = !originalFlat;
        geom.isLogicalSphere = true; // Il mondo per la logica di gioco e' una VERA SFERA 3D
    }
    
    // Creiamo due portali di test per il rendering non-euclideo!
    auto portalA = registry.create();
    registry.emplace<fw::PortalComponent>(portalA);
    registry.get<fw::PortalComponent>(portalA).isActive = true;
    auto& transA = registry.emplace<fw::TransformComponent>(portalA);
    transA.location = fw::Vec3{5.0f, 52.0f, 0.0f}; // Rialzati, per stare sopra l'erba
    transA.scale = fw::Vec3{2.0f, 3.0f, 1.0f};
    registry.emplace<fw::VolumeComponent>(portalA, 3.0f);
    
    auto portalB = registry.create();
    registry.emplace<fw::PortalComponent>(portalB);
    registry.get<fw::PortalComponent>(portalB).isActive = true;
    auto& transB = registry.emplace<fw::TransformComponent>(portalB);
    transB.location = fw::Vec3{30.0f, 52.0f, 30.0f};
    transB.scale = fw::Vec3{2.0f, 3.0f, 1.0f};
    transB.rotation = fw::Quat::angleAxis(glm::radians(180.0f), {0.0f, 1.0f, 0.0f});
    registry.emplace<fw::VolumeComponent>(portalB, 3.0f);
    
    // Colleghiamoli tra loro!
    registry.get<fw::PortalComponent>(portalA).targetPortal = portalB;
    registry.get<fw::PortalComponent>(portalB).targetPortal = portalA;

    // --- I CORPI CELESTI (SOLE E LUNA) SONO GESTITI ANALITICAMENTE DALL'ASTRONOMY SYSTEM SULL'ENTITA PIANETA ---

    // --- REGISTRAZIONE SISTEMI ECS ---
    m_systems.push_back(std::make_unique<fw::CameraSyncSystem>()); // SALVA LO STATO PRECEDENTE
    m_systems.push_back(std::make_unique<fw::InventorySyncSystem>()); // AGGIORNA ARMA EQUIPAGGIATA DALL'HOTBAR
    m_systems.push_back(std::make_unique<fw::MeleeCombatSystem>()); // GESTISCE CARICA E ATTACCHI
    m_systems.push_back(std::make_unique<fw::PlayerMovementSystem>()); // MODIFICA LO STATO (Fisica Input)
    m_systems.push_back(std::make_unique<fw::PhysicsSystem>()); // AGGIORNA FISICA
    m_systems.push_back(std::make_unique<fw::CameraSystem>()); // ORIENTAMENTO DOPO POSIZIONE FISICA
    
    // Generiamo l'anteprima in VRAM per la sfera di Brush (se serve)
    auto previewMesh = fw::MeshGenerators::MakeVoxelPreview(1, m_context);
    std::cout << "[PlayState] Generazione procedurale prato e montagne completata. Avvio ciclo di gioco...\n";

    // Inizializza le statistiche del giocatore per non farlo nascere morto (con 0 HP)
    if (m_context && m_context->engine) {
        m_context->engine->GetPlayer().stats.Initialize();
    }

    return true;
}

void PlayState::Update(float dt) {
    static int s_d74f6l_r1_ps_update = 0;
    if (s_d74f6l_r1_ps_update < 5) {
        std::cout << "[D7.4F.6L-R1] PlayState::Update Entered (Snapshot " << (s_d74f6l_r1_ps_update + 1) << "/5)\n";
        s_d74f6l_r1_ps_update++;
    }

    if (!m_context) return;
    
    if (m_context->runtimeManager && !m_context->runtimeManager->IsReady()) {
        return; // Attende il completamento delle feature asincrone (JobSystem, PBRTextures) senza bloccare l'UI
    }
    using namespace entt::literals;

    m_context->isForgeMode = false;
    m_context->isBlockMakerMode = false;
    if (m_context->forgeWorld) {
        m_context->activeRegistry = &m_context->forgeWorld->GetRegistry();
    }

    // Aggiorna le matrici di tutti i portali
    fw::PortalSystem::UpdatePortals(m_registry);

    // --- F1: CAMBIO ABILITÀ (Implementation/Creative Mode) ---
    static bool f1WasDown = false;
    bool f1Down = m_context->deviceManager->IsActionActive("PAUSE"_hs) == false && (GetAsyncKeyState(VK_F1) & 0x8000) != 0; 
    if (f1Down && !f1WasDown) {
        bool& creative = m_context->engine->GetPlayer().isCreativeMode;
        creative = !creative;
        m_context->engine->GetPlayer().SaveToJson("assets/player.json");
        std::cout << "[PlayState] Player isCreativeMode cambiato a: " << (creative ? "Attivo" : "Disattivo") << "\n";
    }
    f1WasDown = f1Down;

    // --- AGGIORNAMENTO PIANETA LOD ---
    // Aggiorniamo i nodi LOD sferici visibili a grande distanza (montagne distanti, continenti, etc.)
    glm::mat4 vpMatrix = m_context->activeCameraView.projectionMatrix * m_context->activeCameraView.viewMatrix;
    
    // Costruiamo la lista di regioni attive da passare al LOD system
    std::vector<fw::MapRegion> activeRegions;
    fw::PlanetBaseTerrain baseTerrain;
    if (m_context->projectManager) {
        const auto& doc = m_context->projectManager->GetDocument();
        if (!doc.planets.empty()) {
            const auto& pMap = doc.planets[0];
            activeRegions = pMap.regions;
            baseTerrain = pMap.baseTerrain;
            
            for (const auto& inst : pMap.chunkInstances) {
                if (!inst.isActive) continue;
                
                for (const auto& tpl : doc.terrainLibrary) {
                    if (tpl.id == inst.templateId) {
                        fw::MapRegion baseR;
                        baseR.eulerAngles = inst.eulerAngles;
                        baseR.angularRadius = inst.angularRadius;
                        baseR.isGridAligned = inst.isGridAligned;
                        baseR.faceIndex = inst.faceIndex;
                        baseR.gridX = inst.gridX;
                        baseR.gridY = inst.gridY;
                        baseR.rectMin = glm::ivec2(inst.gridX, inst.gridY);
                        baseR.rectMax = glm::ivec2(inst.gridX, inst.gridY);
                        baseR.type = tpl.baseType;
                        baseR.overrides.height = fw::HeightRuleOverrides();
                        baseR.overrides.height->common.frequency = tpl.baseRules.height.common.frequency;
                        activeRegions.push_back(baseR);
                        
                        for (const auto& sub : tpl.subRegions) {
                            fw::MapRegion projected = sub;
                            projected.faceIndex = inst.faceIndex;
                            projected.isGridAligned = inst.isGridAligned;
                            projected.rectMin += glm::ivec2(inst.gridX, inst.gridY);
                            projected.rectMax += glm::ivec2(inst.gridX, inst.gridY);
                            activeRegions.push_back(projected);
                        }
                        break;
                    }
                }
            }
        }
    }
    
    // Independent LOD terrain removed from PlayState. Only authoritative voxel chunks are used.

    // --- ESECUZIONE SISTEMI ECS ---
    for (auto& system : m_systems) {
        system->Update(m_registry, m_context, dt);
    }

    // Aggiornamento orbite planetarie indipendenti dismesso in favore di AstronomySystem analitico

    // Aggiorna ciclo Giorno-Notte (Sole/Luna)
    if (m_context && m_context->engine) {
        m_context->engine->GetTimeManager().Update(dt);
    }

    // Aggiunto l'aggiornamento di ForgeWorld per permettere la generazione asincrona dei chunk
    if (m_context && m_context->forgeWorld) {
        static int s_d74f6l_r1_gw_update = 0;
        if (s_d74f6l_r1_gw_update < 5) {
            std::cout << "[D7.4F.6L-R1] PlayState before GameWorld::Update (Snapshot " << (s_d74f6l_r1_gw_update + 1) << "/5)\n";
        }
        
        m_context->forgeWorld->Update(dt);

        if (m_pendingPlanetarySpawn) {
            auto playerView = m_registry.view<PlayerControllerComponent, TransformComponent,
                                               CameraComponent, RigidBodyComponent>();
            for (auto entity : playerView) {
                auto& player = playerView.get<PlayerControllerComponent>(entity);
                if (!player.planetarySpawnPending) continue;

                glm::vec3 resolvedBodyPosition(0.0f);
                const auto status = m_context->forgeWorld->TryResolvePlanetarySpawn(
                    m_pendingSpawnDirection, m_pendingSpawnHeightOffset,
                    playerView.get<RigidBodyComponent>(entity).body.radius, resolvedBodyPosition);
                if (status == fw::PlanetarySpawnResolveStatus::Ready) {
                    auto& body = playerView.get<RigidBodyComponent>(entity).body;
                    body.position = resolvedBodyPosition;
                    body.velocity = glm::vec3(0.0f);
                    body.isGrounded = false;

                    auto& transform = playerView.get<TransformComponent>(entity);
                    const glm::vec3 eyePosition = resolvedBodyPosition +
                        m_pendingSpawnDirection * body.eyeOffset;
                    transform.x = eyePosition.x;
                    transform.y = eyePosition.y;
                    transform.z = eyePosition.z;
                    transform.prev_x = transform.x;
                    transform.prev_y = transform.y;
                    transform.prev_z = transform.z;

                    player.planetarySpawnPending = false;
                    m_pendingPlanetarySpawn = false;
                    std::cout << "[PlayState] Spawn planetario risolto sulla superficie voxel: body=("
                              << resolvedBodyPosition.x << ',' << resolvedBodyPosition.y << ','
                              << resolvedBodyPosition.z << ") radius=" << glm::length(resolvedBodyPosition)
                              << " clearance=" << m_pendingSpawnHeightOffset << '\n';
                } else if (!m_pendingSpawnWaitLogged &&
                           status != fw::PlanetarySpawnResolveStatus::TerrainPending) {
                    std::cerr << "[PlayState] Spawn planetario ancora pending (surface query status="
                              << static_cast<unsigned>(status)
                              << "); il player resta fermo alla posizione temporanea sicura.\n";
                    m_pendingSpawnWaitLogged = true;
                }
                break;
            }
        }
        
        if (s_d74f6l_r1_gw_update < 5) {
            std::cout << "[D7.4F.6L-R1] PlayState after GameWorld::Update (Snapshot " << (s_d74f6l_r1_gw_update + 1) << "/5)\n";
            s_d74f6l_r1_gw_update++;
        }
    }

    // --- ASSET BROWSER (Key 'B') ---
    static bool bKeyWasDown = false;
    bool bKeyDown = (GetAsyncKeyState('B') & 0x8000) != 0;
    if (bKeyDown && !bKeyWasDown) {
        m_showAssetBrowser = !m_showAssetBrowser;
        if (m_showAssetBrowser) {
            m_assetBrowser.RefreshAssets();
            m_context->deviceManager->requireFreeCursor = true;
        } else {
            m_context->deviceManager->requireFreeCursor = false;
            m_isPlacingRig = false;
        }
    }
    bKeyWasDown = bKeyDown;

    if (m_showAssetBrowser) {
        std::string toSpawn = m_assetBrowser.GetSelectedAssetToSpawn();
        if (!toSpawn.empty()) {
            m_isPlacingRig = true;
            m_rigToPlace = toSpawn;
            m_showAssetBrowser = false; // Nascondi browser per piazzare
            m_assetBrowser.ClearSelectedAsset();
        }
        
        if (!m_showAssetBrowser && !m_isPlacingRig) {
            m_context->deviceManager->requireFreeCursor = false;
        }
    }

    if (m_isPlacingRig) {
        glm::vec3 rayOrigin = m_context->activeCameraView.cameraPosition;
        glm::vec3 rayDir = glm::normalize(m_context->activeCameraView.cameraFront);
        
        // Raycast semplificato al piano Y=0 per spawnare veicoli/mob
        // In un caso reale useresti m_context->engine->GetPhysicsEngine()
        float t = -rayOrigin.y / rayDir.y;
        if (t > 0 && t < 50.0f) {
            m_ghostPos = rayOrigin + rayDir * t;
            m_ghostPos.y += 0.5f; // Offset altezza
            
            // La logica di ImGui per m_isPlacingRig è spostata in Render()
        }
    }

    m_context->engine->Update(dt);
}

void PlayState::Render() {
    // === INTERPOLAZIONE FRAME ===
    // Fonde la posizione e la rotazione del frame precedente con quella corrente
    // usando il fattore alpha = accumulator / FIXED_DT (in [0, 1]).
    // In questo modo, il renderer gira a velocità massima (es. 144 Hz) senza
    // aspettare il tick fisico a 60 Hz, ottenendo movimento impeccabilmente fluido.
    const float alpha = m_context->interpolationAlpha;

    auto view = m_registry.view<CameraComponent, TransformComponent, PlayerControllerComponent>();
    for (auto entity : view) {
        const auto& cam   = view.get<CameraComponent>(entity);
        const auto& trans = view.get<TransformComponent>(entity);

        // LERP posizione (interpolazione lineare, perfetta per vettori)
        const glm::vec3 prevPos(trans.prev_x, trans.prev_y, trans.prev_z);
        const glm::vec3 currPos(trans.x,      trans.y,      trans.z);
        const glm::vec3 iPos = glm::mix(prevPos, currPos, alpha);

        // SLERP rotazione (interpolazione sferica, corretta per quaternioni)
        // glm::slerp gestisce autonomamente il wrap-around a 360 deg senza scatti
        const glm::quat iRot = glm::slerp(trans.prev_rotation, trans.rotation, alpha);

        // Rigenera il vettore front dal quaternione interpolato
        const glm::vec3 iFront = glm::normalize(iRot * glm::vec3(0.0f, 0.0f, -1.0f));
        const glm::vec3 iUp    = glm::normalize(iRot * glm::vec3(0.0f, 1.0f,  0.0f));

        // Scrivi nel bus: il RenderManager leggerà queste matrici già interpolate
        m_context->activeCameraView.viewMatrix       = glm::lookAt(iPos, iPos + iFront, iUp);
        float aspect = 16.0f / 9.0f;
        if (m_context->engine && m_context->engine->GetRenderManager()) {
            uint32_t width = m_context->engine->GetRenderManager()->GetWindowWidth();
            uint32_t height = m_context->engine->GetRenderManager()->GetWindowHeight();
            if (height > 0) aspect = static_cast<float>(width) / static_cast<float>(height);
        }

        m_context->activeCameraView.projectionMatrix = glm::perspective(
            glm::radians(cam.fov), aspect, cam.nearPlane, cam.farPlane);
        m_context->activeCameraView.projectionMatrix[1][1] *= -1; // Y flip per Vulkan
        m_context->activeCameraView.cameraPosition   = iPos;
        m_context->activeCameraView.cameraFront      = iFront;

        break; // Una sola telecamera principale
    }

    m_context->engine->Render();

    // Renderizza elementi UI interni al PlayState (es. HUD o Placing)
    if (m_showAssetBrowser && m_context && m_context->engine) {
        m_assetBrowser.DrawUI(&m_showAssetBrowser, &m_context->engine->GetPlayer(), m_context->forgeWorld);
    }
    
    // --- UI TELETRASPORTO MAPPA ---
    if (m_context && m_context->projectManager) {
        const auto& doc = m_context->projectManager->GetDocument();
        if (!doc.planets.empty()) {
            const auto& planet = doc.planets[0];
            if (!planet.spawnPoints.empty()) {
                ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
                ImGui::Begin("Teletrasporto", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
                ImGui::Text("Punti di Spawn disponibili:");
                ImGui::Separator();
                
                for (size_t i = 0; i < planet.spawnPoints.size(); ++i) {
                    const auto& sp = planet.spawnPoints[i];
                    std::string label = sp.name.empty() ? ("Punto " + std::to_string(i + 1)) : sp.name;
                    ImGui::PushID((int)i);
                    ImColor btnColor(sp.color.r, sp.color.g, sp.color.b, 1.0f);
                    ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)btnColor);
                    if (ImGui::Button(label.c_str(), ImVec2(200, 30))) {
                        // Calcola la posizione 3D globale partendo dalle coordinate sferiche locali
                        float R = fw::PlanetMath::GetPlanetRadius(planet.planetSize);
                        const glm::vec2 chart(sp.localX / R, sp.localZ / R);
                        glm::vec3 dir = fw::CubeSphereMapping::FaceChartToDirection(sp.faceIndex, chart);
                        glm::vec3 worldPos(0.0f);
                        float colliderRadius = 0.3f;
                        auto playerView = m_registry.view<CameraComponent, TransformComponent,
                                                           PlayerControllerComponent, RigidBodyComponent>();
                        for (auto playerEntity : playerView) {
                            colliderRadius = playerView.get<RigidBodyComponent>(playerEntity).body.radius;
                            break;
                        }
                        const auto spawnStatus = m_context->forgeWorld
                            ? m_context->forgeWorld->TryResolvePlanetarySpawn(dir, sp.heightOffset, colliderRadius, worldPos)
                            : fw::PlanetarySpawnResolveStatus::TerrainPending;
                        if (spawnStatus != fw::PlanetarySpawnResolveStatus::Ready) {
                            std::cout << "[PlayState] Teletrasporto non eseguito: superficie voxel non pronta (status="
                                      << static_cast<unsigned>(spawnStatus) << ").\n";
                        } else {
                            // Teletrasporta il giocatore!
                            for (auto playerEnt : playerView) {
                                auto& rb = playerView.get<RigidBodyComponent>(playerEnt);
                                // D1.2.11 TEMP DIAGNOSTICS BEGIN: observation only; assignments below are unchanged.
                                const glm::vec3 previousPosition = rb.body.position;
                                // D1.2.11 TEMP DIAGNOSTICS END.
                                rb.body.position = worldPos;
                                rb.body.velocity = glm::vec3(0.0f);

                                auto& trans = playerView.get<TransformComponent>(playerEnt);
                                trans.x = rb.body.position.x;
                                trans.y = rb.body.position.y + rb.body.eyeOffset;
                                trans.z = rb.body.position.z;
                                trans.prev_x = trans.x;
                                trans.prev_y = trans.y;
                                trans.prev_z = trans.z;

                                // D1.2.11 TEMP DIAGNOSTICS BEGIN: report actual post-assignment body position.
                                const glm::vec3 nextPosition = rb.body.position;
                                const glm::vec3 teleportDelta = nextPosition - previousPosition;
                                std::cout << "[D1.2.11][TELEPORT] previous=(" << previousPosition.x << ','
                                          << previousPosition.y << ',' << previousPosition.z << ") face=" << sp.faceIndex
                                          << " local=(" << sp.localX << ',' << sp.localZ << ") chart=(" << chart.x << ','
                                          << chart.y << ") heightOffset=" << sp.heightOffset << " target=(" << worldPos.x
                                          << ',' << worldPos.y << ',' << worldPos.z << ") next=(" << nextPosition.x << ','
                                          << nextPosition.y << ',' << nextPosition.z << ") delta=(" << teleportDelta.x << ','
                                          << teleportDelta.y << ',' << teleportDelta.z << ") distance="
                                          << glm::length(teleportDelta) << '\n';
                                // D1.2.11 TEMP DIAGNOSTICS END.
                            }
                        }
                    }
                    ImGui::PopStyleColor();
                    ImGui::PopID();
                }
                ImGui::End();
            }
        }
    }
    // --- FINE UI TELETRASPORTO ---

    if (m_isPlacingRig) {
        glm::vec3 rayOrigin = m_context->activeCameraView.cameraPosition;
        glm::vec3 rayDir = glm::normalize(m_context->activeCameraView.cameraFront);
        float t = -rayOrigin.y / rayDir.y;
        
        if (t > 0 && t < 50.0f) {
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2 - 150, ImGui::GetIO().DisplaySize.y - 100));
            ImGui::Begin("Placing UI", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Click Sinistro: Piazza | Click Destro: Annulla");
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Target: %.1f, %.1f, %.1f", m_ghostPos.x, m_ghostPos.y, m_ghostPos.z);
            ImGui::End();
            
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().WantCaptureMouse) {
                SpawnRig(m_rigToPlace, m_ghostPos);
                m_isPlacingRig = false;
                m_context->deviceManager->requireFreeCursor = false;
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::GetIO().WantCaptureMouse) {
                m_isPlacingRig = false;
                m_context->deviceManager->requireFreeCursor = false;
            }
        }
    }
}

void PlayState::RebuildPlanetRoots(const fw::MapDocument& doc, int activePlanetIndex) {
    fw::PlanetSize pSize = fw::PlanetSize::Medium;
    bool isFlat = false;
    float R = 50.0f;
    if (!doc.planets.empty() && activePlanetIndex >= 0 && activePlanetIndex < (int)doc.planets.size()) {
       pSize = doc.planets[activePlanetIndex].planetSize;
       isFlat = doc.planets[activePlanetIndex].isFlat;
       R = fw::PlanetMath::GetPlanetRadius(pSize);
    }
    m_lodSystem.SetPlanetSize(pSize, isFlat);

    std::function<void(fw::ChunkNode&)> destroyTree = [&](fw::ChunkNode& n) {
        if (n.targetEntity != entt::null && m_context->forgeWorld->GetRegistry().valid(n.targetEntity)) {
            m_context->forgeWorld->DestroyEntity(n.targetEntity);
        }
        for (int i = 0; i < 4; ++i) {
            if (n.children[i]) destroyTree(*n.children[i]);
        }
    };
    
    for (auto& root : m_planetRootNodes) {
        destroyTree(root);
    }
    m_planetRootNodes.clear();

    if (R > 0.0f) {
        // Le 6 facce base della Sfera a Cubo (Cube-Sphere)
        m_planetRootNodes.emplace_back(glm::vec3(0, 0, R), R, 2, glm::vec3(-1,-1,1), glm::vec3(1,-1,1), glm::vec3(-1,1,1), glm::vec3(1,1,1));
        m_planetRootNodes.emplace_back(glm::vec3(0, 0, -R), R, 2, glm::vec3(1,-1,-1), glm::vec3(-1,-1,-1), glm::vec3(1,1,-1), glm::vec3(-1,1,-1));
        m_planetRootNodes.emplace_back(glm::vec3(-R, 0, 0), R, 2, glm::vec3(-1,-1,-1), glm::vec3(-1,-1,1), glm::vec3(-1,1,-1), glm::vec3(-1,1,1));
        m_planetRootNodes.emplace_back(glm::vec3(R, 0, 0), R, 2, glm::vec3(1,-1,1), glm::vec3(1,-1,-1), glm::vec3(1,1,1), glm::vec3(1,1,-1));
        m_planetRootNodes.emplace_back(glm::vec3(0, R, 0), R, 2, glm::vec3(-1,1,1), glm::vec3(1,1,1), glm::vec3(-1,1,-1), glm::vec3(1,1,-1));
        m_planetRootNodes.emplace_back(glm::vec3(0, -R, 0), R, 2, glm::vec3(-1,-1,-1), glm::vec3(1,-1,-1), glm::vec3(-1,-1,1), glm::vec3(1,-1,1));
    }
}

void PlayState::RefreshAvailableRigs() {
    // Deprecated, now handled by m_assetBrowser
}

void PlayState::SpawnRig(const std::string& rigPath, const glm::vec3& position) {
    std::cout << "[PlayState] Spawning Rig " << rigPath << " a " << position.x << ", " << position.y << ", " << position.z << std::endl;
    try {
        std::ifstream file(rigPath);
        if (!file.is_open()) {
            std::cerr << "Errore: Impossibile aprire " << rigPath << std::endl;
            return;
        }
        json j;
        file >> j;
        
        entt::entity rootEntity = entt::null;
        
        if (j.contains("joints") && j["joints"].is_array()) {
            // Per ora lo spawn carica le entità visive associate alla rig
            for (const auto& jointJson : j["joints"]) {
                std::string meshPath = jointJson.value("meshPath", "");
                if (!meshPath.empty()) {
                    auto entity = m_registry.create();
                    if (rootEntity == entt::null) rootEntity = entity;
                    
                    m_registry.emplace<NameComponent>(entity, jointJson.value("name", "Bone"));
                    
                    auto& tc = m_registry.emplace<TransformComponent>(entity);
                    // Applica l'offset locale combinato alla posizione base
                    tc.x = position.x;
                    tc.y = position.y;
                    tc.z = position.z;
                    // L'integrazione completa del PhysicsEngine creerebbe i Constraint SixDOF qui.
                }
            }
        }
        std::cout << "[PlayState] Rig instanziata con successo!\n";
    } catch (const std::exception& e) {
        std::cerr << "[PlayState] Errore nello spawn del rig: " << e.what() << std::endl;
    }
}
