#define NOMINMAX
#include "Systems.h"
#include "SharedContext.h"
#include "Components.h"
#include "DeviceManager.h"
#include "FAIRWORLD.h"
#include "ForgeWorld.h"
#include "ForgeComponents.h"
#include "PhysicsEngine.h"
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

namespace fw {
    void CameraSystem::Update(entt::registry& registry, SharedContext* context, float dt) {
        auto view = registry.view<::CameraComponent, ::TransformComponent>();
        for (auto [entity, cam, transform] : view.each()) {
            if (!cam.isMain) continue;
            
            glm::vec3 front;
            front.x = cos(glm::radians(cam.pitch)) * sin(glm::radians(cam.yaw));
            front.y = sin(glm::radians(cam.pitch));
            front.z = -cos(glm::radians(cam.pitch)) * cos(glm::radians(cam.yaw));
            front = glm::normalize(front);
            
            glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
            glm::vec3 up = glm::normalize(glm::cross(right, front));
            
            glm::mat3 rotMat;
            rotMat[0] = right;
            rotMat[1] = up;
            rotMat[2] = -front; // Convenzione Right-Handed (OpenGL): Forward è -Z
            
            transform.rotation = glm::quat_cast(rotMat);
        }
    }

    void PlayerMovementSystem::Update(entt::registry& registry, SharedContext* context, float dt) {
        using namespace entt::literals;
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

            glm::vec3 flatFront(sin(glm::radians(cam.yaw)), 0.0f, -cos(glm::radians(cam.yaw)));
            flatFront = glm::normalize(flatFront);
            glm::vec3 flatRight = glm::normalize(glm::cross(flatFront, glm::vec3(0.0f, 1.0f, 0.0f)));

            glm::vec3 targetVelocity(0.0f);
            if (input.moveForward != 0.0f) targetVelocity += flatFront * input.moveForward * moveSpeed;
            if (input.moveRight != 0.0f) targetVelocity += flatRight * input.moveRight * moveSpeed;
            
            // Aggiorna velocita' orizzontale
            rbComp.body.velocity.x = targetVelocity.x;
            rbComp.body.velocity.z = targetVelocity.z;
            
            if (input.isJumping && rbComp.body.isGrounded) {
                rbComp.body.velocity.y = player.jumpForce; 
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
}
