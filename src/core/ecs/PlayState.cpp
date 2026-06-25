#include "pch.h"
#include "PlayState.h"
#include "SharedContext.h"
#include "DeviceManager.h"
#include "FAIRWORLD.h"
#include "Components.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <iostream>
#include <fstream>
#include "json.hpp"
#include "PortalSystem.h"
#include "ForgeComponents.h"
#include "ForgeWorld.h"

using json = nlohmann::json;

PlayState::PlayState(SharedContext* context) : m_context(context) {
    std::cout << "[PlayState] Costruito.\n";
}

PlayState::~PlayState() {
    std::cout << "[PlayState] Distrutto.\n";
}

std::expected<void, std::string> PlayState::Init() {
    // === REGISTRAZIONE DEL KERNEL BUS INPUT (Action Mapping) ===
    // Le stringhe vengono hashate a compile-time da EnTT: costo zero a runtime
    using namespace entt::literals;

    auto& bindings = m_context->deviceManager->GetActionMap().bindings;

    // Movimento
    bindings["MOVE_FORWARD"_hs].push_back({fw::InputID::KEY_W});
    bindings["MOVE_BACKWARD"_hs].push_back({fw::InputID::KEY_S});
    bindings["MOVE_LEFT"_hs].push_back({fw::InputID::KEY_A});
    bindings["MOVE_RIGHT"_hs].push_back({fw::InputID::KEY_D});

    // Corsa: combo W + Shift
    bindings["RUN_FORWARD"_hs].push_back({fw::InputID::KEY_W, fw::InputID::KEY_SHIFT});

    // Salto: tasto + grilletto gamepad
    bindings["JUMP"_hs].push_back({fw::InputID::KEY_SPACE});
    bindings["JUMP"_hs].push_back({fw::InputID::PAD_FACE_DOWN});

    // Distruzione blocco: mouse sinistro O grilletto destro gamepad
    bindings["DESTROY_BLOCK"_hs].push_back({fw::InputID::MOUSE_LEFT});
    bindings["DESTROY_BLOCK"_hs].push_back({fw::InputID::PAD_TRIGGER_R});

    // Posizionamento blocco: mouse destro O grilletto sinistro gamepad
    bindings["PLACE_BLOCK"_hs].push_back({fw::InputID::MOUSE_RIGHT});
    bindings["PLACE_BLOCK"_hs].push_back({fw::InputID::PAD_TRIGGER_L});

    // Menu / Pausa
    bindings["PAUSE"_hs].push_back({fw::InputID::KEY_ESC});
    bindings["PAUSE"_hs].push_back({fw::InputID::PAD_START});

    std::cout << "[PlayState] Action Map registrata: "
              << bindings.size() << " azioni logiche nel Kernel Bus.\n";

    // === CARICAMENTO CONFIGURAZIONE DATA-DRIVEN (JSON) ===
    if (m_context->targetGameJsonPath.empty()) {
        return std::unexpected("Percorso JSON non specificato dal contesto globale!");
    }

    std::cout << "[PlayState] Avvio parsing configurazione Data-Driven da: "
              << m_context->targetGameJsonPath << "\n";
    
    std::ifstream file(m_context->targetGameJsonPath);
    json data;
    
    if (!file.is_open()) {
        std::cerr << "[StateManager WARNING] Impossibile aprire il file di configurazione: " 
                  << m_context->targetGameJsonPath 
                  << " -> Caricamento FALLBACK di emergenza.\n";
                  
        // Costruiamo un JSON di default in memoria per salvare la situazione
        data = {
            {"entities", {
                {
                    {"name", "SpawnPoint"},
                    {"transform", {{"x", 0.0f}, {"y", 50.0f}, {"z", 0.0f}}}
                }
            }}
        };
    } else {
        data = json::parse(file, nullptr, false);
        if (data.is_discarded()) {
            std::cerr << "[StateManager ERROR] Errore di sintassi nel JSON -> Fallback.\n";
            data = {{"entities", json::array()}};
        }
    }

    std::cout << "[PlayState] Creazione entities EnTT dal JSON (o Fallback) in corso...\n";
    
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
    // Posizione iniziale — rotazione inizializzata a identità (forward = -Z)
    m_registry.emplace<TransformComponent>(cameraEntity, 0.0f, 70.0f, 0.0f);
    auto& cam = m_registry.emplace<CameraComponent>(cameraEntity);
    // Yaw -90 gradi così il forward di default punta verso -Z
    cam.yaw   = -90.0f;
    cam.pitch =   0.0f;
    m_registry.emplace<PlayerControllerComponent>(cameraEntity);
    
    // Inizializza il RigidBody per la fisica
    auto& rbOpt = m_registry.emplace<RigidBodyComponent>(cameraEntity);
    rbOpt.body.position = glm::vec3(0.0f, 70.0f, 0.0f);
    rbOpt.body.mass = 70.0f;

    // --- INIZIALIZZAZIONE FORGE ---
    m_context->forgeWorld->Initialize(m_context);
    
    // Creiamo due portali di test per il rendering non-euclideo!
    auto& registry = m_context->forgeWorld->GetRegistry();
    
    auto portalA = registry.create();
    registry.emplace<fw::PortalComponent>(portalA);
    registry.get<fw::PortalComponent>(portalA).isActive = true;
    auto& transA = registry.emplace<fw::TransformComponent>(portalA);
    transA.location = fw::Vec3{5.0f, 32.0f, 0.0f};
    transA.scale = fw::Vec3{2.0f, 3.0f, 1.0f};
    registry.emplace<fw::VolumeComponent>(portalA, 3.0f);
    
    auto portalB = registry.create();
    registry.emplace<fw::PortalComponent>(portalB);
    registry.get<fw::PortalComponent>(portalB).isActive = true;
    auto& transB = registry.emplace<fw::TransformComponent>(portalB);
    transB.location = fw::Vec3{100.0f, 60.0f, 100.0f}; // Lontano, magari nel cielo!
    transB.scale = fw::Vec3{2.0f, 3.0f, 1.0f};
    transB.rotation = fw::Vec3{0.0f, 180.0f, 0.0f}; // Girato per entrare/uscire correttamente
    registry.emplace<fw::VolumeComponent>(portalB, 3.0f);
    
    // Colleghiamoli tra loro!
    registry.get<fw::PortalComponent>(portalA).targetPortal = portalB;
    registry.get<fw::PortalComponent>(portalB).targetPortal = portalA;

    // --- REGISTRAZIONE SISTEMI ECS ---
    m_systems.push_back(std::make_unique<fw::CameraSystem>());
    m_systems.push_back(std::make_unique<fw::PlayerMovementSystem>());
    m_systems.push_back(std::make_unique<fw::PhysicsSystem>());
    m_systems.push_back(std::make_unique<fw::CameraSyncSystem>());

    std::cout << "[PlayState] Inizializzato (ECS + ForgeWorld + Portali + Systems)\n";
    return {};
}

void PlayState::Update(float dt) {
    using namespace entt::literals;

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

    // Aggiunto l'aggiornamento di ForgeWorld per permettere la generazione asincrona dei chunk
    if (m_context && m_context->forgeWorld) {
        m_context->forgeWorld->Update(dt);
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
        const glm::vec3 iFront = glm::normalize(iRot * glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::vec3 iUp    = glm::normalize(iRot * glm::vec3(0.0f, 1.0f,  0.0f));

        // Scrivi nel bus: il RenderManager leggerà queste matrici già interpolate
        m_context->activeCameraView.viewMatrix       = glm::lookAt(iPos, iPos + iFront, iUp);
        m_context->activeCameraView.projectionMatrix = glm::perspective(
            glm::radians(cam.fov), 16.0f / 9.0f, cam.nearPlane, cam.farPlane);
        m_context->activeCameraView.cameraPosition   = iPos;
        m_context->activeCameraView.cameraFront      = iFront;

        break; // Una sola telecamera principale
    }

    m_context->engine->Render();
}
