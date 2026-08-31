#define NOMINMAX
#include "Systems.h"
#include "SharedContext.h"
#include "Components.h"
#include "DeviceManager.h"
#include "FAIRWORLD.h"
#include "ForgeWorld.h"
#include "ForgeComponents.h"
#include "PhysicsEngine.h"
#include "../components/Skeleton.h"
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

namespace fw {
    void CameraSystem::Update(entt::registry& registry, SharedContext* context, float dt) {
        bool isSpherical = false;
        if (context && context->forgeWorld) {
            auto planetView = context->forgeWorld->GetRegistry().view<fw::PlanetGeometryComponent>();
            if (!planetView.empty()) {
                auto& planet = planetView.get<fw::PlanetGeometryComponent>(planetView.front());
                isSpherical = planet.isLogicalSphere;
            }
        }

        auto view = registry.view<::CameraComponent, ::TransformComponent>();
        for (auto [entity, cam, transform] : view.each()) {
            if (!cam.isMain) continue;
            
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            if (isSpherical) {
                float dist = glm::length(glm::vec3(transform.x, transform.y, transform.z));
                if (dist > 0.01f) {
                    up = glm::normalize(glm::vec3(transform.x, transform.y, transform.z));
                }
            }

            // Calcolo del Front basato su Yaw (rotazione attorno a UP) e Pitch (rotazione locale X)
            // Se usiamo semplicemente angoli di Eulero classici, il polo nord funziona, ma all'equatore lo yaw ruoterebbe attorno all'asse sbagliato.
            // Soluzione Quaternion:
            glm::quat qYaw = glm::angleAxis(glm::radians(-cam.yaw), up); // Invertito per convenzione
            
            // Front base "nord" fittizio (tangente alla sfera)
            glm::vec3 baseForward = (std::abs(up.y) < 0.99f) ? glm::normalize(glm::cross(up, glm::vec3(0,1,0))) : glm::vec3(1,0,0);
            if (up.y > 0.99f) baseForward = glm::vec3(0,0,-1);
            else if (up.y < -0.99f) baseForward = glm::vec3(0,0,1);
            
            glm::vec3 front = qYaw * baseForward;
            glm::vec3 right = glm::normalize(glm::cross(front, up));
            
            glm::quat qPitch = glm::angleAxis(glm::radians(cam.pitch), right);
            front = qPitch * front;
            up = glm::normalize(glm::cross(right, front));
            
            glm::mat3 rotMat;
            rotMat[0] = right;
            rotMat[1] = up;
            rotMat[2] = -front;
            transform.rotation = glm::quat_cast(rotMat);
        }
    }

    void PlayerMovementSystem::Update(entt::registry& registry, SharedContext* context, float dt) {
        using namespace entt::literals;
        bool isSpherical = false;
        if (context && context->forgeWorld) {
            auto planetView = context->forgeWorld->GetRegistry().view<fw::PlanetGeometryComponent>();
            if (!planetView.empty()) {
                auto& planet = planetView.get<fw::PlanetGeometryComponent>(planetView.front());
                isSpherical = planet.isLogicalSphere;
            }
        }

        auto view = registry.view<::PlayerControllerComponent, ::TransformComponent, ::CameraComponent, ::RigidBodyComponent>();
        for (auto [entity, player, transform, cam, rbComp] : view.each()) {
            if (context->deviceManager->requireFreeCursor) continue;

            auto& input = context->deviceManager->GetInput();

            float mouseSens = cam.mouseSensitivity;
            float dx = input.lookYaw;
            float dy = input.lookPitch;
            cam.yaw += dx * mouseSens;
            cam.pitch -= dy * mouseSens;
            if (cam.pitch > 89.0f) cam.pitch = 89.0f;
            if (cam.pitch < -89.0f) cam.pitch = -89.0f;

            float moveSpeed = input.isRunning ? player.runSpeed : player.walkSpeed;

            glm::vec3 up(0.0f, 1.0f, 0.0f);
            if (isSpherical) {
                float dist = glm::length(rbComp.body.position);
                if (dist > 0.01f) up = glm::normalize(rbComp.body.position);
            }

            // Stessa logica della camera per trovare "avanti" e "destra" sulla superficie
            glm::quat qYaw = glm::angleAxis(glm::radians(-cam.yaw), up);
            glm::vec3 baseForward = (std::abs(up.y) < 0.99f) ? glm::normalize(glm::cross(up, glm::vec3(0,1,0))) : glm::vec3(1,0,0);
            if (up.y > 0.99f) baseForward = glm::vec3(0,0,-1);
            else if (up.y < -0.99f) baseForward = glm::vec3(0,0,1);
            
            glm::vec3 surfaceFront = qYaw * baseForward;
            glm::vec3 surfaceRight = glm::normalize(glm::cross(surfaceFront, up));

            rbComp.body.isFlying = context->engine->GetPlayer().isCreativeMode;

            glm::vec3 targetVelocity(0.0f);
            
            auto* combatState = registry.try_get<::CombatStateComponent>(entity);
            bool canMove = true;
            if (combatState && (combatState->state == CombatState::CHARGING || combatState->state == CombatState::SWINGING || combatState->state == CombatState::PARRYING)) {
                canMove = false;
            }
            
            if (canMove) {
                if (rbComp.body.isFlying) {
                    if (input.moveForward != 0.0f) targetVelocity += surfaceFront * input.moveForward * moveSpeed;
                    if (input.moveRight != 0.0f) targetVelocity += surfaceRight * input.moveRight * moveSpeed;
                    if (input.isJumping) targetVelocity += up * moveSpeed;
                    if (context->deviceManager->IsActionActive(entt::hashed_string("CROUCH"))) targetVelocity -= up * moveSpeed;
                    
                    rbComp.body.velocity = targetVelocity;
                } else {
                    if (input.moveForward != 0.0f) targetVelocity += surfaceFront * input.moveForward * moveSpeed;
                    if (input.moveRight != 0.0f) targetVelocity += surfaceRight * input.moveRight * moveSpeed;
                    
                    // Modifica la velocità planare senza toccare quella verticale (lungo l'Up)
                    float verticalVel = glm::dot(rbComp.body.velocity, up);
                    
                    if (input.isJumping) {
                        if (rbComp.body.isGrounded) {
                            verticalVel = player.jumpForce; 
                        }
                        input.ConsumeJump(); 
                    }
                    
                    rbComp.body.velocity = targetVelocity + (up * verticalVel);
                }
            }
            
            // Camera position matches the rigid body position + eye offset
            transform.x = rbComp.body.position.x;
            transform.y = rbComp.body.position.y + rbComp.body.eyeOffset;
            transform.z = rbComp.body.position.z;
        }
    }

    void PhysicsSystem::Update(entt::registry& registry, SharedContext* context, float dt) {
        if (!context->forgeWorld) return;
        
        PhysicsEngine engine;
        auto view = registry.view<::RigidBodyComponent>();
        for (auto [entity, rbComp] : view.each()) {
            engine.StepSimulation(rbComp.body, dt, *context->forgeWorld);
            
            // Processa eventi pendenti (es. danno da caduta)
            for (auto& ev : rbComp.body.pendingEvents) {
                if (ev.type == PhysicsEvent::Type::FallDamage) {
                    std::cout << "[Fisica] Danno da caduta: " << ev.value << " HP\n";
                    // TODO: Sottrarre dalla vita reale se implementata
                }
            }
            rbComp.body.pendingEvents.clear();
        }
    }

    void CameraSyncSystem::Update(entt::registry& registry, SharedContext* context, float dt) {
        auto view = registry.view<::TransformComponent>();
        for (auto [entity, trans] : view.each()) {
            trans.prev_x = trans.x;
            trans.prev_y = trans.y;
            trans.prev_z = trans.z;
            trans.prev_rotation = trans.rotation;
        }
    }

    void MeleeCombatSystem::Update(entt::registry& registry, SharedContext* context, float dt) {
        if (!context || !context->deviceManager) return;
        using namespace entt::literals;
        auto& input = context->deviceManager->GetInput();
        auto* devMgr = context->deviceManager;
        
        bool mouseLeftHeld = devMgr->IsActionActive("ATTACK_BASE"_hs);
        bool mouseRightHeld = devMgr->IsActionActive("PARRY"_hs);
        
        auto view = registry.view<::EquippedWeaponComponent, ::CombatStateComponent, ::CameraComponent, ::TransformComponent>();
        for (auto [entity, weapon, combat, cam, trans] : view.each()) {
            // Parata
            if (mouseRightHeld) {
                combat.state = CombatState::PARRYING;
                continue;
            }
            
            // Tasti direzionali mappati nel Kernel Bus Action Map
            bool isChargingFront = mouseLeftHeld && (devMgr->IsActionActive("ATTACK_FRONT_1"_hs) || devMgr->IsActionActive("ATTACK_FRONT_2"_hs) || 
                                                     devMgr->IsActionActive("ATTACK_FRONT_3"_hs) || devMgr->IsActionActive("ATTACK_FRONT_4"_hs) ||
                                                     input.moveForward > 0.0f);
                                                     
            bool isChargingBack = mouseLeftHeld && (devMgr->IsActionActive("ATTACK_BACK_1"_hs) || devMgr->IsActionActive("ATTACK_BACK_2"_hs) || 
                                                    devMgr->IsActionActive("ATTACK_BACK_3"_hs) || devMgr->IsActionActive("ATTACK_BACK_4"_hs) ||
                                                    input.moveForward < 0.0f);
            
            if (isChargingFront || isChargingBack) {
                if (combat.state == CombatState::IDLE) {
                    combat.state = CombatState::CHARGING;
                    combat.chargeTimer = 0.0f;
                    combat.isPosterior = isChargingBack;
                }
                
                if (combat.state == CombatState::CHARGING) {
                    combat.chargeTimer += dt;
                    combat.attackDirection = combat.isPosterior ? -cam.front : cam.front;
                }
            } else {
                // Rilascio! Sweep Cast / Damage
                if (combat.state == CombatState::CHARGING && combat.chargeTimer > 0.2f) {
                    combat.state = CombatState::SWINGING;
                    
                    float damageMult = 1.0f + (combat.chargeTimer * 2.0f); // Es: 1s carica = 3x danni
                    if (damageMult > 5.0f) damageMult = 5.0f;
                    float finalDamage = weapon.baseDamage * damageMult;
                    
                    glm::vec3 rayOrigin = glm::vec3(trans.x, trans.y, trans.z);
                    glm::vec3 rayDir = combat.attackDirection;
                    
                    // Se l'entità ha uno Skeleton, usiamo la posizione della Mano Destra!
                    if (auto* skeleton = registry.try_get<fw::Skeleton>(entity)) {
                        const auto& globals = skeleton->GetGlobalTransforms();
                        for (size_t i = 0; i < skeleton->m_joints.size(); ++i) {
                            if (skeleton->m_joints[i].name == "Hand_R") {
                                // Aggiungi la posizione del player all'offset della mano
                                glm::vec3 handOffset = glm::vec3(globals[i][3]);
                                rayOrigin = glm::vec3(trans.x, trans.y, trans.z) + handOffset;
                                break;
                            }
                        }
                    }
                    
                    std::cout << "[Combat] SWEEP " << (combat.isPosterior ? "POSTERIORE" : "FRONTALE") << "! " 
                              << "Danno: " << finalDamage << " (Carica: " << combat.chargeTimer << "s)\n";
                              
                    // Segnala al motore fisico/grafico (PhysicsLabState) di eseguire il raycast/shapecast
                    combat.hasPendingSweep = true;
                    combat.sweepDamage = finalDamage;
                    combat.sweepOrigin = rayOrigin;
                    combat.sweepDirection = rayDir;
                    combat.sweepReach = weapon.reach;
                }
                
                if (combat.state == CombatState::SWINGING) {
                    // Finita l'animazione di sweep
                    combat.state = CombatState::IDLE;
                    combat.chargeTimer = 0.0f;
                } else if (combat.state == CombatState::PARRYING) {
                    combat.state = CombatState::IDLE;
                }
            }
        }
    }

    void InventorySyncSystem::Update(entt::registry& registry, SharedContext* context, float dt) {
        if (!context || !context->engine || !context->deviceManager) return;
        
        auto& player = context->engine->GetPlayer();
        const auto& actionMap = context->deviceManager->GetActionMap();
        
        // Controlla gli input per selezionare lo slot dell'hotbar (0-9)
        using namespace entt::literals;
        for (int i = 0; i < 10; ++i) {
            std::string actionName = "HOTBAR_" + std::to_string(i + 1);
            if (context->deviceManager->IsActionActive(entt::hashed_string(actionName.c_str()))) {
                player.inventory.SetActiveSlotIndex(i);
                break;
            }
        }
        
        int currentSlot = player.inventory.GetActiveSlotIndex();
        
        // Se lo slot attivo è lo stesso del frame precedente, non fare nulla (costo zero a runtime!)
        if (currentSlot == m_lastActiveSlot) return;
        
        // Cerca l'entità Player nell'ECS (quella con PlayerControllerComponent)
        auto view = registry.view<::PlayerControllerComponent>();
        if (view.empty()) return;
        
        entt::entity playerEntity = view.front();
        
        // FASE 1: CLEANUP (Rimuovi l'arma precedente se esiste)
        if (m_lastActiveSlot != -1) {
            registry.remove<::EquippedWeaponComponent>(playerEntity);
            registry.remove<::CombatStateComponent>(playerEntity);
            
            // Distruggi l'entità mesh dell'arma se è agganciata allo scheletro
            if (auto* skeleton = registry.try_get<fw::Skeleton>(playerEntity)) {
                for (auto& joint : skeleton->m_joints) {
                    if (joint.name == "Hand_R" && joint.voxelEntity != 0xFFFFFFFF) {
                        entt::entity oldWeaponEntity = static_cast<entt::entity>(joint.voxelEntity);
                        if (registry.valid(oldWeaponEntity)) {
                            registry.destroy(oldWeaponEntity);
                        }
                        joint.voxelEntity = 0xFFFFFFFF; // Sgancia l'arma dall'osso
                        break;
                    }
                }
            }
            std::cout << "[InventorySync] Arma disequipaggiata. (Slot " << m_lastActiveSlot << " -> " << currentSlot << ")\n";
        }
        
        // FASE 2: EQUIP (Istanzia e aggancia la nuova arma se è di tipo Weapon)
        const auto& activeItem = player.inventory.GetActiveItem();
        
        if (!activeItem.IsEmpty() && activeItem.type == ItemType::Weapon) {
            // Aggiungi i componenti per sbloccare la CombatStance (Hold-to-Charge e logiche fisiche)
            registry.emplace<::EquippedWeaponComponent>(playerEntity);
            registry.emplace<::CombatStateComponent>(playerEntity);
            
            // Genera l'entità mesh dell'arma
            auto weaponEntity = registry.create();
            registry.emplace<NameComponent>(weaponEntity, "Sword");
            registry.emplace<fw::TransformComponent>(weaponEntity);
            auto& weaponMesh = registry.emplace<fw::MeshComponent>(weaponEntity);
            weaponMesh.name = activeItem.stringId.empty() ? "sword_placeholder" : activeItem.stringId;
            weaponMesh.type = fw::MeshType::Prefab;
            
            // Associa l'entità dell'arma all'osso "Hand_R" dello scheletro
            if (auto* skeleton = registry.try_get<fw::Skeleton>(playerEntity)) {
                for (auto& joint : skeleton->m_joints) {
                    if (joint.name == "Hand_R") {
                        joint.voxelEntity = static_cast<uint32_t>(weaponEntity);
                        break;
                    }
                }
            }
            
            std::cout << "[InventorySync] Nuova arma equipaggiata! Mesh: " << weaponMesh.name << " (Slot " << currentSlot << ")\n";
        }
        
        // Aggiorna lo stato
        m_lastActiveSlot = currentSlot;
    }

} // namespace fw
