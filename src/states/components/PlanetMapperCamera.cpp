#include "pch.h"
#include "PlanetMapperCamera.h"
#include "imgui.h"
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include "CubeSphereMapping.h"

PlanetMapperCamera::PlanetMapperCamera() {}

void PlanetMapperCamera::Init(float initialDistance, float initialPitch, float initialYaw) {
    m_orbitDistance = initialDistance;
    m_orbitPitch = initialPitch;
    m_orbitYaw = initialYaw;
    m_orbitTarget = glm::vec3(0.0f);
    m_lastRayHit.hit = false;
}

bool PlanetMapperCamera::Update(float dt, SharedContext* context, uint32_t windowWidth, uint32_t windowHeight) {
    if (!context) return false;

    ImGuiIO& io = ImGui::GetIO();
    bool allowCameraControl = false;
    
    // Check if mouse is in the 3D viewport (right 65% of screen)
    if (!io.WantCaptureMouse) {
        if (io.MousePos.x >= windowWidth * 0.45f) {
            allowCameraControl = true;
        }
    }

    if (allowCameraControl) {
        // --- Orbit via mouse drag ---
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            m_orbitYaw -= io.MouseDelta.x * 0.5f;
            m_orbitPitch += io.MouseDelta.y * 0.5f;
            m_orbitPitch = std::clamp(m_orbitPitch, -89.0f, 89.0f);
        }
        // --- Zoom via mouse wheel ---
        if (io.MouseWheel != 0.0f) {
            float scrollSpeed = std::max(m_orbitDistance * 0.1f, 5.0f);
            m_orbitDistance -= io.MouseWheel * scrollSpeed;
            m_orbitDistance = std::max(m_orbitDistance, 2.0f);
        }
        // --- Free-fly WASD ---
        float flySpeed = std::max(m_orbitDistance * 0.03f, 1.5f) * dt * 60.0f;
        if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
            m_orbitDistance = std::max(m_orbitDistance - flySpeed, 2.0f);
        }
        if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
            m_orbitDistance += flySpeed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
            m_orbitYaw -= flySpeed * 0.5f;
        }
        if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
            m_orbitYaw += flySpeed * 0.5f;
        }
        if (ImGui::IsKeyDown(ImGuiKey_R) || ImGui::IsKeyDown(ImGuiKey_PageUp)) {
            m_orbitPitch = std::min(m_orbitPitch + flySpeed * 0.4f, 89.0f);
        }
        if (ImGui::IsKeyDown(ImGuiKey_F) || ImGui::IsKeyDown(ImGuiKey_PageDown)) {
            m_orbitPitch = std::max(m_orbitPitch - flySpeed * 0.4f, -89.0f);
        }
    }

    // --- Update active camera view in SharedContext ---
    float pitchRad = glm::radians(m_orbitPitch);
    float yawRad = glm::radians(m_orbitYaw);
    
    glm::vec3 camPos;
    camPos.x = m_orbitTarget.x + m_orbitDistance * cos(pitchRad) * sin(yawRad);
    camPos.y = m_orbitTarget.y + m_orbitDistance * sin(pitchRad);
    camPos.z = m_orbitTarget.z + m_orbitDistance * cos(pitchRad) * cos(yawRad);

    context->activeCameraView.cameraPosition = camPos;
    context->activeCameraView.cameraFront = glm::normalize(m_orbitTarget - camPos);
    context->activeCameraView.viewMatrix = glm::lookAt(camPos, m_orbitTarget, glm::vec3(0, 1, 0));
    
    float aspect = 16.0f / 9.0f; 
    if (windowHeight > 0) {
        aspect = (windowWidth * 0.65f) / (float)windowHeight;
    }
    context->activeCameraView.projectionMatrix = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 3000.0f);
    context->activeCameraView.projectionMatrix[1][1] *= -1;

    // --- Raycast against the planet sphere using the global RaycastSystem ---
    if (allowCameraControl && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
        float viewX = (windowWidth * 0.35f);
        float viewW = (windowWidth * 0.65f);
        float viewY = 0.0f;
        float viewH = (float)windowHeight;
        
        float mouseX_inView = io.MousePos.x - viewX;
        float mouseY_inView = io.MousePos.y - viewY;
        
        float mouseX_NDC = (2.0f * mouseX_inView) / viewW - 1.0f;
        float mouseY_NDC = 1.0f - (2.0f * mouseY_inView) / viewH;
        
        glm::vec4 rayClip = glm::vec4(mouseX_NDC, mouseY_NDC, -1.0f, 1.0f);
        glm::vec4 rayEye = glm::inverse(context->activeCameraView.projectionMatrix) * rayClip;
        rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
        
        glm::vec3 rayWorld = glm::normalize(glm::vec3(glm::inverse(context->activeCameraView.viewMatrix) * rayEye));
        
        fw::RaycastQuery query;
        query.ray.origin = context->activeCameraView.cameraPosition;
        query.ray.direction = rayWorld;
        query.mode = fw::RaycastMode::Voxel;
        query.maxDistance = 10000.0f;
        
        fw::RaycastHit hit = fw::RaycastSystem::Cast(context, query);
        if (hit.hit && hit.type == fw::RaycastHitType::Voxel) {
            // Usa CubeSphereMapping SOLO per tradurre il punto fisico in coordinate logiche (face, uv)
            glm::vec3 dir = glm::normalize(hit.worldPosition);
            fw::CubeSphereMapping::DirectionToFaceUV(dir, hit.faceIndex, hit.uv);
        }
        m_lastRayHit = hit;
    } else {
        m_lastRayHit.hit = false;
    }

    return allowCameraControl;
}
