#include "Systems.h"
#include "SharedContext.h"
#include "Components.h"
#include "DeviceManager.h"
#include "FAIRWORLD.h"
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

namespace fw {
    void CameraSystem::Update(entt::registry& registry, SharedContext* context, float dt) {
        auto view = registry.view<CameraComponent, TransformComponent>();
        for (auto [entity, cam, transform] : view.each()) {
            if (!cam.isMain) continue;
            
            glm::quat qPitch = glm::angleAxis(glm::radians(cam.pitch), glm::vec3(1.0f, 0.0f, 0.0f));
            glm::quat qYaw   = glm::angleAxis(glm::radians(cam.yaw), glm::vec3(0.0f, 1.0f, 0.0f));
            transform.rotation = qYaw * qPitch;
        }
    }

    void PlayerMovementSystem::Update(entt::registry& registry, SharedContext* context, float dt) {
        using namespace entt::literals;
        auto view = registry.view<PlayerControllerComponent, TransformComponent, CameraComponent>();
        for (auto [entity, player, transform, cam] : view.each()) {
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

            glm::vec3 flatFront(cos(glm::radians(cam.yaw)), 0.0f, sin(glm::radians(cam.yaw)));
            flatFront = glm::normalize(flatFront);
            glm::vec3 flatRight = glm::normalize(glm::cross(flatFront, glm::vec3(0.0f, 1.0f, 0.0f)));

            glm::vec3 pos(transform.x, transform.y, transform.z);
            
            if (input.moveForward != 0.0f) pos += flatFront * input.moveForward * moveSpeed * dt;
            if (input.moveRight != 0.0f) pos += flatRight * input.moveRight * moveSpeed * dt;
            if (input.isJumping) {
                pos.y += player.jumpForce * dt; 
            }
            
            transform.x = pos.x;
            transform.y = pos.y;
            transform.z = pos.z;
        }
    }

    void PhysicsSystem::Update(entt::registry& registry, SharedContext* context, float dt) {
    }

    void CameraSyncSystem::Update(entt::registry& registry, SharedContext* context, float dt) {
    }
}
