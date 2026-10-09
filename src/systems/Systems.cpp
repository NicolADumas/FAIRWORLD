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
        bool usePlanetaryFrame = false;
        bool rawLogicalSphere = false;
        bool planetEntityFound = false;
        glm::vec3 planetCenter(0.0f, 0.0f, 0.0f);
        glm::vec3 planetNorth(0.0f, 1.0f, 0.0f);

        if (context && context->forgeWorld) {
            auto planetEnt = context->forgeWorld->GetPlanetEntity();
            auto& fRegistry = context->forgeWorld->GetRegistry();
            if (fRegistry.valid(planetEnt) && fRegistry.all_of<fw::PlanetGeometryComponent>(planetEnt)) {
                auto& planet = fRegistry.get<fw::PlanetGeometryComponent>(planetEnt);
                planetEntityFound = true;
                rawLogicalSphere = planet.isLogicalSphere;
                usePlanetaryFrame = planet.isLogicalSphere;
                
                // planet center ownership will migrate to the future SolarSystem/root-transform architecture
                planetCenter = glm::vec3(0.0f, 0.0f, 0.0f);
                planetNorth = glm::vec3(0.0f, 1.0f, 0.0f);
            }
        }

        auto view = registry.view<::CameraComponent, ::TransformComponent>();
        for (auto [entity, cam, transform] : view.each()) {
            if (!cam.isMain) continue;
            
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            if (usePlanetaryFrame) {
                glm::vec3 camPos(transform.x, transform.y, transform.z);
                float dist = glm::length(camPos - planetCenter);
                if (dist > 0.01f) {
                    up = glm::normalize(camPos - planetCenter);
                }
            }
            glm::vec3 initialLocalUp = up;

            // Calcolo del Front basato su Yaw (rotazione attorno a UP) e Pitch (rotazione locale X)
            glm::quat qYaw = glm::angleAxis(glm::radians(-cam.yaw), up); 
            
            // Front base "nord" fittizio (tangente alla sfera rispetto al polo nord del pianeta)
            glm::vec3 baseForward;
            if (std::abs(glm::dot(up, planetNorth)) < 0.99f) {
                baseForward = glm::normalize(glm::cross(up, planetNorth));
            } else {
                baseForward = (glm::dot(up, planetNorth) > 0.0f) ? glm::vec3(0,0,-1) : glm::vec3(0,0,1);
            }
            
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
            
            static bool s_loggedCamera = false;
            if (!s_loggedCamera) {
                s_loggedCamera = true;
                glm::vec3 camPos(transform.x, transform.y, transform.z);
                glm::vec3 dirToCenter = (glm::distance(camPos, planetCenter) > 0.001f) ? glm::normalize(planetCenter - camPos) : glm::vec3(0,0,-1);
                
                char logBuf[2048];
                snprintf(logBuf, sizeof(logBuf), 
                    "[D7.4F.6E][CAMERA]\n"
                    "  Entity: %u\n"
                    "  planet entity found: %s\n"
                    "  Camera Pos: (%.3f, %.3f, %.3f)\n"
                    "  Planet Center: (%.3f, %.3f, %.3f)\n"
                    "  isLogicalSphere raw: %s\n"
                    "  usePlanetaryFrame: %s\n"
                    "  Local UP: (%.3f, %.3f, %.3f)\n"
                    "  Yaw: %.3f, Pitch: %.3f\n"
                    "  Final Forward: (%.3f, %.3f, %.3f)\n"
                    "  DirectionToCenter: (%.3f, %.3f, %.3f)\n"
                    "  Dot(Forward, ToCenter): %.3f\n",
                    (unsigned)entity,
                    planetEntityFound ? "YES" : "NO",
                    camPos.x, camPos.y, camPos.z,
                    planetCenter.x, planetCenter.y, planetCenter.z,
                    rawLogicalSphere ? "true" : "false",
                    usePlanetaryFrame ? "true" : "false",
                    initialLocalUp.x, initialLocalUp.y, initialLocalUp.z,
                    cam.yaw, cam.pitch,
                    front.x, front.y, front.z,
                    dirToCenter.x, dirToCenter.y, dirToCenter.z,
                    glm::dot(front, dirToCenter)
                );
                std::cout << logBuf;
                
                // Construct View matrix manually for truth
                glm::mat4 vMat = glm::lookAt(camPos, camPos + front, up);
                snprintf(logBuf, sizeof(logBuf),
                    "[D7.4F.6E][VIEW]\n"
                    "  Col 0: (%.3f, %.3f, %.3f, %.3f)\n"
                    "  Col 1: (%.3f, %.3f, %.3f, %.3f)\n"
                    "  Col 2: (%.3f, %.3f, %.3f, %.3f)\n"
                    "  Col 3: (%.3f, %.3f, %.3f, %.3f)\n",
                    vMat[0][0], vMat[0][1], vMat[0][2], vMat[0][3],
                    vMat[1][0], vMat[1][1], vMat[1][2], vMat[1][3],
                    vMat[2][0], vMat[2][1], vMat[2][2], vMat[2][3],
                    vMat[3][0], vMat[3][1], vMat[3][2], vMat[3][3]
                );
                std::cout << logBuf;
            }
        }
    }

    void PlayerMovementSystem::Update(entt::registry& registry, SharedContext* context, float dt) {
        using namespace entt::literals;
        bool usePlanetaryFrame = false;
        glm::vec3 planetCenter(0.0f, 0.0f, 0.0f);
        glm::vec3 planetNorth(0.0f, 1.0f, 0.0f);

        if (context && context->forgeWorld) {
            auto planetEnt = context->forgeWorld->GetPlanetEntity();
            auto& fRegistry = context->forgeWorld->GetRegistry();
            if (fRegistry.valid(planetEnt) && fRegistry.all_of<fw::PlanetGeometryComponent>(planetEnt)) {
                auto& planet = fRegistry.get<fw::PlanetGeometryComponent>(planetEnt);
                usePlanetaryFrame = planet.isLogicalSphere;
                
                // planet center ownership will migrate to the future SolarSystem/root-transform architecture
                planetCenter = glm::vec3(0.0f, 0.0f, 0.0f);
                planetNorth = glm::vec3(0.0f, 1.0f, 0.0f);
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
            if (usePlanetaryFrame) {
                float dist = glm::length(rbComp.body.position - planetCenter);
                if (dist > 0.01f) up = glm::normalize(rbComp.body.position - planetCenter);
            }

            // Stessa logica della camera per trovare "avanti" e "destra" sulla superficie
            glm::quat qYaw = glm::angleAxis(glm::radians(-cam.yaw), up);
            
            glm::vec3 baseForward;
            if (std::abs(glm::dot(up, planetNorth)) < 0.99f) {
                baseForward = glm::normalize(glm::cross(up, planetNorth));
            } else {
                baseForward = (glm::dot(up, planetNorth) > 0.0f) ? glm::vec3(0,0,-1) : glm::vec3(0,0,1);
            }
            
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
            glm::vec3 camPos = rbComp.body.position + (up * rbComp.body.eyeOffset);
            transform.x = camPos.x;
            transform.y = camPos.y;
            transform.z = camPos.z;
        }
    }

    void PhysicsSystem::Update(entt::registry& registry, SharedContext* context, float dt) {
        if (!context->forgeWorld) return;
        
        static float s_dbgTimer = 0.0f;
        s_dbgTimer += dt;
        bool shouldLog = (s_dbgTimer >= 1.0f);
        if (shouldLog) s_dbgTimer = 0.0f;
        
        PhysicsEngine engine;
        auto view = registry.view<::RigidBodyComponent>();
        for (auto [entity, rbComp] : view.each()) {
            bool isPlayer = registry.all_of<::PlayerControllerComponent>(entity);
            
            glm::vec3 preVel = rbComp.body.velocity;
            glm::vec3 prePos = rbComp.body.position;
            
            engine.StepSimulation(rbComp.body, dt, *context->forgeWorld);
            
            if (isPlayer && shouldLog) {
                std::cout << "[D7.4L][PLAYER PHYSICS]\n";
                auto planetEnt = context->forgeWorld->GetPlanetEntity();
                uint32_t pId = context->forgeWorld->GetRegistry().valid(planetEnt) ? (uint32_t)planetEnt : 0;
                std::cout << "- PlanetID: " << pId << "\n";
                std::cout << "- Player entity valid: YES\n";
                std::cout << "- Physics update executed: YES\n";
                std::cout << "- Mode: " << (rbComp.body.isFlying ? "Creative" : "Survival") << "\n";
                std::cout << "- Cartesian position: (" << rbComp.body.position.x << ", " << rbComp.body.position.y << ", " << rbComp.body.position.z << ")\n";
                std::cout << "- Planet center: (0, 0, 0)\n";
                float dist = glm::length(rbComp.body.position);
                std::cout << "- Distance from center: " << dist << "\n";
                glm::vec3 up = (dist > 0.01f) ? (rbComp.body.position / dist) : glm::vec3(0,1,0);
                std::cout << "- Local UP: (" << up.x << ", " << up.y << ", " << up.z << ")\n";
                std::cout << "- Gravity enabled: " << (rbComp.body.isFlying ? "NO" : "YES") << "\n";
                std::cout << "- Gravity acceleration: (" << rbComp.body.dbg_gravityAccel.x << ", " << rbComp.body.dbg_gravityAccel.y << ", " << rbComp.body.dbg_gravityAccel.z << ")\n";
                std::cout << "- Velocity before/after integration: (" << preVel.x << ", " << preVel.y << ", " << preVel.z << ") -> (" << rbComp.body.velocity.x << ", " << rbComp.body.velocity.y << ", " << rbComp.body.velocity.z << ")\n";
                std::cout << "- Position before/after integration: (" << prePos.x << ", " << prePos.y << ", " << prePos.z << ") -> (" << rbComp.body.position.x << ", " << rbComp.body.position.y << ", " << rbComp.body.position.z << ")\n";
                std::cout << "- Grounded: " << (rbComp.body.isGrounded ? "YES" : "NO") << "\n";
                std::cout << "- Number of solid voxel contacts: " << rbComp.body.dbg_voxelContacts << "\n";
                std::cout << "- Last voxel lookup coordinates: (" << rbComp.body.dbg_lastLookupCoord.x << ", " << rbComp.body.dbg_lastLookupCoord.y << ", " << rbComp.body.dbg_lastLookupCoord.z << ")\n";
                std::cout << "- Last voxel lookup solid/empty: " << (rbComp.body.dbg_lastLookupSolid ? "SOLID" : "EMPTY") << "\n";
            }
            
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
