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
#include "RenderManager.h"
#include "AsyncInput.h"
#include "MapDocument.h"
#include "WorldProjectManager.h"
#include "MapWorldGenerator.h"
#include "JobSystem.h"
#include "VulkanDmaManager.h"
#include <imgui.h>

using json = nlohmann::json;

PlayState::PlayState(SharedContext* context) : m_context(context) {
    std::cout << "[PlayState] Costruito.\n";
}

PlayState::~PlayState() {
    std::cout << "[PlayState] Distrutto.\n";
    if (m_context && m_context->jobSystem) {
        m_context->jobSystem->Shutdown(); // Attende che tutti i job finiscano
        m_context->jobSystem->Initialize(); // Riaccende i thread
    }
    
    if (m_context && m_context->forgeWorld) {
        m_context->forgeWorld->ClearWorld(true); // Salva in saves/world e svuota
        m_context->activeRegistry = nullptr;
    }
}

#include <filesystem>

// (End of previous code)



bool PlayState::Init() {
    // --- CRITICAL FIX FOR GPU RENDERING BUG ---
    // Ensure JobSystem is initialized so background meshes can be generated and sent to GPU.
    if (m_context) {
        if (!m_context->jobSystem) {
            m_context->jobSystem = new fw::JobSystem();
            m_context->jobSystem->Initialize();
        }
    }

    // (Action Map registrata precedentemente nel DeviceManager)
    auto& bindings = m_context->deviceManager->GetActionMap().bindings;
    std::cout << "[PlayState] Action Map caricata: "
              << bindings.size() << " azioni logiche nel Kernel Bus.\n";

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
    
    // --- Creazione Telecamera Principale (Player) ---
    auto cameraEntity = m_registry.create();
    m_registry.emplace<NameComponent>(cameraEntity, "MainCamera");
    
    float spawnY = 100.0f;
    if (hasCustomMap && m_context->forgeWorld && m_context->forgeWorld->GetRegistry().valid(m_context->forgeWorld->GetPlanetEntity())) {
        auto& geom = m_context->forgeWorld->GetRegistry().get<fw::PlanetGeometryComponent>(m_context->forgeWorld->GetPlanetEntity());
        if (geom.planetRadius > 0.0f) {
            spawnY = geom.planetRadius + 10.0f;
        }
    }
    
    // Posizione iniziale al Polo Nord della sfera (+Y)
    m_registry.emplace<TransformComponent>(cameraEntity, 0.0f, spawnY, 0.0f);
    auto& cam = m_registry.emplace<CameraComponent>(cameraEntity);
    cam.yaw   = 0.0f;
    cam.pitch = 0.0f;
    m_registry.emplace<PlayerControllerComponent>(cameraEntity);
    
    // Inizializza il RigidBody per la fisica
    auto& rbOpt = m_registry.emplace<RigidBodyComponent>(cameraEntity);
    rbOpt.body.position = glm::vec3(0.0f, spawnY, 0.0f);
    rbOpt.body.mass = 70.0f;

    // Genera lo scheletro fisico del Player
    auto& playerSkeleton = m_registry.emplace<fw::Skeleton>(cameraEntity);
    fw::Skeleton::GenerateBiped(playerSkeleton);
    // --- INIZIALIZZAZIONE WORLD ---
    m_context->isForgeMode = false;
    m_context->isBlockMakerMode = false;

    if (!m_context->forgeWorld) {
        if (m_context->gameWorld) {
            m_context->forgeWorld = m_context->gameWorld;
        } else {
            std::cout << "[PlayState] WARNING: forgeWorld e gameWorld non trovati nel contesto! Utilizzo di un nuovo GameWorld.\n";
            static auto fallbackWorld = std::make_unique<fw::GameWorld>();
            fallbackWorld->Initialize(m_context);
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
                    geom.planetRadius = doc.planets[0].planetRadius;
                    std::cout << "[PlayState] Raggio pianeta impostato a: " << geom.planetRadius << "\n";
                }
            }
        } else {
            std::cerr << "[PlayState] ERRORE: Impossibile leggere world_map.json. Fallback attivato.\n";
            hasCustomMap = false;
        }
    }

    std::cout << "[DEBUG] [PlayState] Generazione Terreno iniziale attorno al giocatore...\n";
    int currentChunkX = 0;
    int currentChunkZ = 0;
    
    glm::vec3 cameraPos(0.0f, spawnY, 0.0f);
    float pRadius = 0.0f;
    if (m_context->forgeWorld && m_context->forgeWorld->GetRegistry().valid(m_context->forgeWorld->GetPlanetEntity())) {
        auto planetEnt = m_context->forgeWorld->GetPlanetEntity();
        auto& reg = m_context->forgeWorld->GetRegistry();
        if (reg.all_of<fw::PlanetGeometryComponent>(planetEnt)) {
            pRadius = reg.get<fw::PlanetGeometryComponent>(planetEnt).planetRadius;
            fw::MapWorldGenerator::GetChunkCoordFromPosition(pRadius, cameraPos, currentChunkX, currentChunkZ);
        } else {
            currentChunkX = (int)cameraPos.x / 16;
            currentChunkZ = (int)cameraPos.z / 16;
        }
    } else {
        currentChunkX = (int)cameraPos.x / 16;
        currentChunkZ = (int)cameraPos.z / 16;
    }
    
    int chunkRadius = 8;
    for (int cx = currentChunkX - chunkRadius; cx <= currentChunkX + chunkRadius; ++cx) {
        for (int cz = currentChunkZ - chunkRadius; cz <= currentChunkZ + chunkRadius; ++cz) {
            std::string chunkName = "WorldChunk_" + std::to_string(cx) + "_" + std::to_string(cz);
            
            glm::vec3 pos;
            glm::quat rot;
            if (pRadius > 0.0f) {
                fw::MapWorldGenerator::GetSphericalChunkTransform(pRadius, cx, cz, pos, rot);
            } else {
                pos = {cx * 16.0f, 0.0f, cz * 16.0f};
                rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            }
            
            entt::entity chunkEnt = m_context->forgeWorld->CreateChunkEntity(chunkName, fw::Vec3{pos.x, pos.y, pos.z});
            
            if (pRadius > 0.0f) {
                auto& trans = registry.get<fw::TransformComponent>(chunkEnt);
                trans.rotation = {rot.x, rot.y, rot.z, rot.w};
            }
            
            fw::BiomeDataComponent biomeData;
            biomeData.planetRadius = pRadius;
            biomeData.chunkCenterWorld = pos;
            
            // Dummy forest biome for spherical test
            biomeData.hasBaseRegion = true;
            biomeData.baseRegion.type = fw::MapRegionType::Forest;
            biomeData.baseRegion.gravityModifier = 1.0f;
            biomeData.baseRegion.perlinFrequency = 0.005f;
            uint8_t idGrass = 255, idDirt = 255;
            if (auto reg = m_context->forgeWorld->GetBlockRegistry()) {
                idGrass = reg->GetBlock("fairworld:grass").id;
                idDirt = reg->GetBlock("fairworld:dirt").id;
            }
            biomeData.baseRegion.surfaceBlockId = idGrass;
            biomeData.baseRegion.subsurfaceBlockId = idDirt;
            biomeData.surfaceBlockId = idGrass;
            biomeData.subsurfaceBlockId = idDirt;
            
            registry.emplace_or_replace<fw::BiomeDataComponent>(chunkEnt, biomeData);
            registry.emplace_or_replace<fw::TerrainGenTag>(chunkEnt);
        }
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
    m_systems.push_back(std::make_unique<fw::CameraSystem>()); // AGGIORNA TELECAMERA
    m_systems.push_back(std::make_unique<fw::PhysicsSystem>()); // AGGIORNA FISICA
    
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
    if (!m_context) return;
    using namespace entt::literals;

    m_context->isForgeMode = false;
    m_context->isBlockMakerMode = false;
    if (m_context->forgeWorld) {
        m_context->activeRegistry = &m_context->forgeWorld->GetRegistry();
    }

    // Aggiorna le matrici di tutti i portali
    fw::PortalSystem::UpdatePortals(m_registry);

    // --- F1: CAMBIO MODALITÀ ---
    static bool f1WasDown = false;
    bool f1Down = m_context->deviceManager->IsActionActive("PAUSE"_hs) == false && (GetAsyncKeyState(VK_F1) & 0x8000) != 0; 
    if (f1Down && !f1WasDown) {
        GameMode currentMode = m_context->engine->GetGameMode();
        m_context->engine->SetGameMode(currentMode == GameMode::Dev ? GameMode::Play : GameMode::Dev);
        m_context->engine->GetPlayer().SaveToJson("assets/player.json");
        std::cout << "[PlayState] GameMode cambiata in: " << (m_context->engine->GetGameMode() == GameMode::Dev ? "Dev" : "Play") << "\n";
    }
    f1WasDown = f1Down;

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
        m_context->forgeWorld->Update(dt);
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
