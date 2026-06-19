#include "pch.h"
#include "PlayState.h"
#include "SharedContext.h"
#include "FAIRWORLD.h"
#include "Components.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <fstream>
#include "json.hpp"

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
    if (!file.is_open()) {
        return std::unexpected("Impossibile aprire il file di configurazione: " + m_context->targetGameJsonPath);
    }

    json data = json::parse(file, nullptr, false);
    if (data.is_discarded()) {
        return std::unexpected("Errore di sintassi nel JSON del livello.");
    }

    std::cout << "[PlayState] Creazione entities EnTT dal JSON in corso...\n";
    
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
    m_registry.emplace<TransformComponent>(cameraEntity, 0.0f, 30.0f, 0.0f, -20.0f, -90.0f, 0.0f);
    m_registry.emplace<CameraComponent>(cameraEntity);
    m_registry.emplace<PlayerControllerComponent>(cameraEntity);
    
    // Inizializza il RigidBody per la fisica
    auto& rbOpt = m_registry.emplace<RigidBodyComponent>(cameraEntity);
    rbOpt.body.position = glm::vec3(0.0f, 30.0f, 0.0f);
    rbOpt.body.mass = 70.0f;

    std::cout << "[PlayState] Create " << entityCount << " entità EnTT base!\n";
    std::cout << "[PlayState] Inizializzato con successo.\n";
    
    return {};
}

void PlayState::Update(float dt) {
    using namespace entt::literals;

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

            glm::vec3 targetVelocity = moveDir * controller.walkSpeed;

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
                // Modalità Play: Fisica RigidBody con collisioni e gravità
                rb.velocity.x = targetVelocity.x;
                rb.velocity.z = targetVelocity.z;
                
                // Salto
                if (rb.isGrounded && m_context->currentInput.isJumping) {
                    rb.velocity.y = controller.jumpForce;
                }

                // Eseguiamo il passo di simulazione fisica (chiama il motore)
                float oldVelY = rb.velocity.y;
                if (m_context->forgeWorld) {
                    m_context->engine->GetPhysicsEngine().StepSimulation(rb, dt, *(m_context->forgeWorld));
                }

                // Danno da caduta automatico (Cap. 12/13 - perdita di energia cinetica)
                if (rb.isGrounded && oldVelY < -10.0f) {
                    float deltaV = abs(oldVelY - rb.velocity.y);
                    float damage = m_context->engine->GetPhysicsEngine().ComputeFallDamage(deltaV, rb.mass);
                    if (damage > 0.0f) {
                        m_context->engine->GetPlayer().stats.currentHP -= (int)damage;
                        std::cout << "[PlayState] Danno da caduta subito: " << damage << " HP\n";
                    }
                }
                
                // TODO: Gestione Nuoto Danni e Stargate (possono essere portati qui o lasciati ai trigger)
            }

            // Sincronizzazione: RigaBody (fisica) -> Transform (Visuale)
            trans.x = rb.position.x;
            trans.y = rb.position.y + rb.eyeOffset; // Telecamera all'altezza degli occhi
            trans.z = rb.position.z;

            // Aggiorna i dati crudi per il renderer usando la posizione della telecamera (Transform)
            glm::vec3 pos(trans.x, trans.y, trans.z);
            m_context->activeCameraView.viewMatrix = glm::lookAt(pos, pos + cam.front, cam.up);
            m_context->activeCameraView.projectionMatrix = glm::perspective(glm::radians(cam.fov), 16.0f / 9.0f, cam.nearPlane, cam.farPlane);
            m_context->activeCameraView.cameraPosition = pos;
            m_context->activeCameraView.cameraFront = cam.front;

            break; // Assumiamo una sola telecamera attiva principale
        }

    m_context->engine->Update(dt);
}

void PlayState::Render() {
    m_context->engine->Render();
}
