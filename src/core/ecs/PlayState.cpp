#include "pch.h"
#include "PlayState.h"
#include "SharedContext.h"
#include "FAIRWORLD.h"
#include "Components.h"
#include <glm/gtc/matrix_transform.hpp>
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

    auto& bindings = m_context->actionMap.bindings;

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
    m_registry.emplace<TransformComponent>(cameraEntity, 0.0f, 70.0f, 0.0f, 0.0f, -90.0f, 0.0f);
    m_registry.emplace<CameraComponent>(cameraEntity);
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

    std::cout << "[PlayState] Inizializzato (ECS + ForgeWorld + Portali)\n";
    return {};
}

void PlayState::Update(float dt) {
    using namespace entt::literals;

    // Aggiorna le matrici di tutti i portali
    fw::PortalSystem::UpdatePortals(m_registry);

    // main.cpp gestisce già il fixed timestep esterno a 60fps:
    // PlayState::Update riceve sempre dt == PhysicsEngine::FIXED_DT.
    // Eseguiamo un singolo passo fisico per frame.
    constexpr int numPhysicsSteps = 1;


        // --- F1: CAMBIO MODALITÀ ---
        static bool f1WasDown = false;
        bool f1Down = fw::IsActionActive("PAUSE"_hs, m_context) == false && (GetAsyncKeyState(VK_F1) & 0x8000) != 0; 
        if (f1Down && !f1WasDown) {
            GameMode currentMode = m_context->engine->GetGameMode();
            m_context->engine->SetGameMode(currentMode == GameMode::Dev ? GameMode::Play : GameMode::Dev);
            m_context->engine->GetPlayer().SaveToJson("assets/player.json");
            std::cout << "[PlayState] GameMode cambiata in: " << (m_context->engine->GetGameMode() == GameMode::Dev ? "Dev" : "Play") << "\n";
        }
        f1WasDown = f1Down;

        // --- CAMERA SYSTEM, INPUT HAL & FISICA ---
        auto view = m_registry.view<CameraComponent, TransformComponent, PlayerControllerComponent, RigidBodyComponent>();
        for (auto entity : view) {
            auto& cam = view.get<CameraComponent>(entity);
            auto& trans = view.get<TransformComponent>(entity);
            auto& controller = view.get<PlayerControllerComponent>(entity);
            auto& rbComp = view.get<RigidBodyComponent>(entity);
            RigidBody& rb = rbComp.body;

            // Lettura pulita dal demone
            float forward = m_context->currentInput.moveForward;
            float right = m_context->currentInput.moveRight;
            float yawDelta = m_context->currentInput.lookYaw;
            float pitchDelta = m_context->currentInput.lookPitch;

            // Rotazione Camera
            trans.yaw += yawDelta;
            trans.pitch += pitchDelta;
            if (trans.pitch > 89.0f) trans.pitch = 89.0f;
            if (trans.pitch < -89.0f) trans.pitch = -89.0f;

            // Ricalcolo vettori direzionali della camera
            glm::vec3 front;
            front.x = cos(glm::radians(trans.yaw)) * cos(glm::radians(trans.pitch));
            front.y = sin(glm::radians(trans.pitch));
            front.z = sin(glm::radians(trans.yaw)) * cos(glm::radians(trans.pitch));
            cam.front = glm::normalize(front);
            cam.right = glm::normalize(glm::cross(cam.front, cam.worldUp));
            cam.up = glm::normalize(glm::cross(cam.right, cam.front));

            // Vettori "piatti" (senza componente Y) per il movimento a terra
            glm::vec3 flatFront = glm::normalize(glm::vec3(cam.front.x, 0.0f, cam.front.z));
            glm::vec3 flatRight = glm::normalize(glm::vec3(cam.right.x, 0.0f, cam.right.z));
            glm::vec3 moveDir = (flatFront * forward) + (flatRight * right);
            float hLen = glm::length(moveDir);
            if (hLen > 0.0f) moveDir = (moveDir / hLen);

            if (m_context->engine->GetGameMode() == GameMode::Dev) {
                // Modalità Dev: Noclip (Volo Libero senza gravità)
                glm::vec3 flyMoveVec = (cam.front * forward) + (cam.right * right);
                if (glm::length(flyMoveVec) > 0.0f) flyMoveVec = glm::normalize(flyMoveVec);
                
                rb.velocity = flyMoveVec * controller.walkSpeed;
                if (m_context->currentInput.isJumping) rb.velocity.y = controller.walkSpeed;
                else if (GetAsyncKeyState(VK_SHIFT) & 0x8000) rb.velocity.y = -controller.walkSpeed;
                else rb.velocity.y = 0.0f; // Azzera Y se non stiamo salendo o scendendo in Noclip
                
                rb.position += rb.velocity * dt;
                rb.isGrounded = false;
            } else {
                for (int step = 0; step < numPhysicsSteps; ++step) {
                    float stepDt = PhysicsEngine::FIXED_DT;
                    
                    // Gestione Timer Coyote Time e Jump Buffer
                    if (rb.isGrounded) {
                        rb.coyoteTimer = 0.15f; // 150ms di coyote time
                    } else {
                        rb.coyoteTimer = std::max(0.0f, rb.coyoteTimer - stepDt);
                    }
                    
                    if (m_context->currentInput.isJumping) {
                        rb.jumpBuffer = 0.1f; // 100ms di jump buffer
                    } else {
                        rb.jumpBuffer = std::max(0.0f, rb.jumpBuffer - stepDt);
                    }
                    
                    bool canJump = rb.isGrounded || rb.coyoteTimer > 0.0f;

                    // --- MASSA DINAMICA: corpo + inventario ---
                    // rb.mass viene aggiornata ogni step con la massa totale reale del player.
                    // Questo influenza: gravità (F=ma), fall damage (E=0.5mv^2),
                    // galleggiamento (spinta Archimede) e inerzia in acqua.
                    const Player& playerRef = m_context->engine->GetPlayer();
                    rb.mass = playerRef.GetTotalMassKg();

                    // Penale velocità per sovraccarico (encumbrance > 1.0)
                    float encumbrance = playerRef.GetEncumbranceRatio();
                    float speedMultiplier = 1.0f;
                    if (encumbrance > 1.0f) {
                        // Penale lineare: al 200% carico (enc=2.0) la velocità è dimezzata
                        // Formula: mult = 1.0 / encumbrance, clampato a min 0.2 (passo faticoso)
                        speedMultiplier = std::max(0.2f, 1.0f / encumbrance);
                    }

                    // QUAKE-STYLE MOVEMENT (Friction & Acceleration)
                    float friction = rb.isGrounded ? 8.0f : 0.0f;
                    float accel = rb.isGrounded ? 10.0f : 2.0f; // Air control limitato
                    // Usa runSpeed se W+Shift sono premuti, altrimenti walkSpeed
                    // (la corsa è disabilitata se sovraccarico)
                    bool canRun = m_context->currentInput.isRunning && encumbrance <= 1.0f;
                    float maxSpeed = (canRun ? controller.runSpeed : controller.walkSpeed) * speedMultiplier;
                    
                    // 1. Applica attrito al suolo
                    float speed = sqrt(rb.velocity.x * rb.velocity.x + rb.velocity.z * rb.velocity.z);
                    if (speed > 0.01f) {
                        float drop = speed * friction * stepDt;
                        float newSpeed = speed - drop;
                        if (newSpeed < 0.0f) newSpeed = 0.0f;
                        newSpeed /= speed;
                        rb.velocity.x *= newSpeed;
                        rb.velocity.z *= newSpeed;
                    } else {
                        rb.velocity.x = 0.0f;
                        rb.velocity.z = 0.0f;
                    }
                    
                    // 2. Accelerazione verso la direzione di input
                    if (hLen > 0.0f) {
                        float currentSpeed = glm::dot(glm::vec3(rb.velocity.x, 0.0f, rb.velocity.z), moveDir);
                        float addSpeed = maxSpeed - currentSpeed;
                        if (addSpeed > 0.0f) {
                            float accelSpeed = accel * maxSpeed * stepDt;
                            if (accelSpeed > addSpeed) accelSpeed = addSpeed;
                            rb.velocity.x += accelSpeed * moveDir.x;
                            rb.velocity.z += accelSpeed * moveDir.z;
                        }
                    }
                    
                    // Salto (Jump Buffer & Coyote Time)
                    if (rb.jumpBuffer > 0.0f && canJump) {
                        rb.velocity.y = controller.jumpForce;
                        rb.jumpBuffer = 0.0f;
                        rb.coyoteTimer = 0.0f; // Consuma il salto
                    }

                    // Eseguiamo il passo di simulazione fisica (chiama il motore)
                    glm::vec3 oldPos = rb.position;
                    
                    if (m_context->forgeWorld) {
                        // FIX CHUNK: divisione per 16.0f prima del floor per gestire coordinate negative in sicurezza.
                        int cx = (int)std::floor(rb.position.x / 16.0f);
                        int cz = (int)std::floor(rb.position.z / 16.0f);
                        // Se il chunk sotto i piedi non è pronto, congegliamo la fisica
                        if (m_context->forgeWorld->IsChunkReady(cx, cz)) {
                            m_context->engine->GetPhysicsEngine().StepSimulation(rb, stepDt, *(m_context->forgeWorld));
                        } else {
                            rb.velocity = glm::vec3(0.0f);
                        }
                    }
                    
                    // PORTAL CHECK: Verifichiamo se il giocatore ha attraversato un portale
                    auto portalView = m_registry.view<fw::PortalComponent, fw::TransformComponent>();
                    for (auto pEntity : portalView) {
                        const auto& portal = portalView.get<fw::PortalComponent>(pEntity);
                        const auto& pTrans = portalView.get<fw::TransformComponent>(pEntity);
                        
                        fw::Vec3 pos = {rb.position.x, rb.position.y, rb.position.z};
                        fw::Vec3 vel = {rb.velocity.x, rb.velocity.y, rb.velocity.z};
                        fw::Vec3 old = {oldPos.x, oldPos.y, oldPos.z};
                        
                        if (fw::PortalSystem::CheckAndTeleport(pos, vel, old, portal, pTrans)) {
                            rb.position = glm::vec3(pos.x, pos.y, pos.z);
                            rb.velocity = glm::vec3(vel.x, vel.y, vel.z);
                            std::cout << "[PlayState] PORTALE ATTRAVERSATO! Teletrasporto non-euclideo completato.\n";
                            break; // Ne attraversiamo uno solo alla volta
                        }
                    }
                } // End Fixed Timestep Loop

                // Svuotamento coda eventi fisica (Danni da caduta, ecc.)
                for (const auto& ev : rb.pendingEvents) {
                    if (ev.type == PhysicsEvent::Type::FallDamage) {
                        m_context->engine->GetPlayer().stats.currentHP -= (int)ev.value;
                        std::cout << "[PlayState] Danno da caduta subito: " << ev.value << " HP\n";
                    }
                }
                rb.pendingEvents.clear();
                
                // TODO: Gestione Nuoto Danni e Stargate (possono essere portati qui o lasciati ai trigger)
            }

            // Sincronizzazione: RigaBody (fisica) -> Transform (Visuale)
            trans.x = rb.position.x;
            trans.y = rb.position.y + rb.eyeOffset; // Telecamera all'altezza degli occhi
            trans.z = rb.position.z;
            
            // Applica Head Bobbing
            float hSpeed = sqrt(rb.velocity.x * rb.velocity.x + rb.velocity.z * rb.velocity.z);
            if (rb.isGrounded && hSpeed > 0.1f) {
                static float bobTime = 0.0f;
                bobTime += hSpeed * dt * 0.15f;
                trans.y += sin(bobTime * 10.0f) * 0.1f; // Oscillazione verticale
            }

            // Aggiorna i dati crudi per il renderer usando la posizione della telecamera (Transform)
            glm::vec3 pos(trans.x, trans.y, trans.z);
            m_context->activeCameraView.viewMatrix = glm::lookAt(pos, pos + cam.front, cam.up);
            m_context->activeCameraView.projectionMatrix = glm::perspective(glm::radians(cam.fov), 16.0f / 9.0f, cam.nearPlane, cam.farPlane);
            m_context->activeCameraView.cameraPosition = pos;
            
            // Propaga velocità fisica all'HUD
            m_context->playerVelocity = rb.velocity;
            break; // Assumiamo una sola telecamera attiva principale
        }

    // Aggiunto l'aggiornamento di ForgeWorld per permettere la generazione asincrona dei chunk
    if (m_context && m_context->forgeWorld) {
        m_context->forgeWorld->Update(dt);
    }

    m_context->engine->Update(dt);
}

void PlayState::Render() {
    m_context->engine->Render();
}
