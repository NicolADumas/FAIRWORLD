#include "pch.h"
#include "PlanetMapperCamera.h"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include "CubeSphereMapping.h"
#include "GameWorld.h"
#include "WorldProjectManager.h"
#include "BiomeComponents.h"
#include "ForgeComponents.h"

namespace {
    struct RaycastMissSample {
        glm::vec3 origin{0.0f};
        glm::vec3 direction{0.0f};
        uint32_t voxelTargets = 0;
        bool hasForgeWorld = false;
    };

    struct PlanetMapperRaycastDiagnostics {
        uint64_t updateCalls = 0;
        uint64_t nullContextUpdates = 0;
        uint64_t mouseInViewport = 0;
        uint64_t mousePassedCameraXGate = 0;
        uint64_t blockedByWantCaptureMouse = 0;
        uint64_t outsideViewportGate = 0;
        uint64_t blockedByImGuiWindowHover = 0;
        uint64_t queriesSent = 0;
        uint64_t voxelModeQueries = 0;
        uint64_t hits = 0;
        uint64_t misses = 0;
        uint64_t voxelHits = 0;
        uint64_t sphereHits = 0;
        uint64_t physicsHits = 0;
        uint64_t otherTypeHits = 0;
        uint64_t d1211LogsEmitted = 0;
        uint64_t documentPlanets = 0;
        uint64_t documentChunkInstances = 0;
        uint64_t registryEntities = 0;
        uint64_t renderableMeshEntities = 0;
        uint64_t voxelRaycastTargets = 0;
        bool sceneSnapshotTaken = false;
        std::array<RaycastMissSample, 3> missSamples{};
        size_t missSampleCount = 0;
    } g_planetMapperRaycastDiagnostics;

    void SnapshotPlanetMapperScene(SharedContext* context) {
        auto& stats = g_planetMapperRaycastDiagnostics;
        if (stats.sceneSnapshotTaken) return;
        stats.sceneSnapshotTaken = true;

        if (context && context->projectManager) {
            const auto& document = context->projectManager->GetDocument();
            stats.documentPlanets = document.planets.size();
            for (const auto& planet : document.planets) {
                stats.documentChunkInstances += planet.chunkInstances.size();
            }
        }

        if (context && context->forgeWorld) {
            auto& registry = context->forgeWorld->GetRegistry();
            stats.registryEntities = context->forgeWorld->GetLiveEntityCount();
            auto meshView = registry.view<fw::MeshComponent, fw::TransformComponent>();
            for (auto entity : meshView) {
                (void)entity;
                ++stats.renderableMeshEntities;
            }
            auto voxelView = registry.view<fw::VoxelChunkComponent, fw::TransformComponent>();
            for (auto entity : voxelView) {
                (void)entity;
                ++stats.voxelRaycastTargets;
            }
        }
    }
}

PlanetMapperCamera::PlanetMapperCamera() {}

PlanetMapperCamera::~PlanetMapperCamera() {
    const auto& stats = g_planetMapperRaycastDiagnostics;
    std::cout << "[D1.2.12a][RAYCAST SUMMARY] updates=" << stats.updateCalls
              << " nullContext=" << stats.nullContextUpdates
              << " mouseInViewport=" << stats.mouseInViewport
              << " mousePassedCameraXGate=" << stats.mousePassedCameraXGate
              << " blockedWantCaptureMouse=" << stats.blockedByWantCaptureMouse
              << " outsideViewportGate=" << stats.outsideViewportGate
              << " blockedImGuiWindowHover=" << stats.blockedByImGuiWindowHover
              << " queriesSent=" << stats.queriesSent
              << " queryModeVoxel=" << stats.voxelModeQueries
              << " hits=" << stats.hits << " misses=" << stats.misses
              << " hitVoxel=" << stats.voxelHits << " hitSphere=" << stats.sphereHits
              << " hitPhysics=" << stats.physicsHits << " hitOther=" << stats.otherTypeHits
              << " d1211LogsEmitted=" << stats.d1211LogsEmitted << '\n';

    std::cout << "[D1.2.12a][SCENE SNAPSHOT] taken=" << (stats.sceneSnapshotTaken ? "yes" : "no")
              << " planets=" << stats.documentPlanets
              << " documentChunkInstances=" << stats.documentChunkInstances
              << " registryEntities=" << stats.registryEntities
              << " renderableMeshEntities=" << stats.renderableMeshEntities
              << " voxelRaycastTargets=" << stats.voxelRaycastTargets << '\n';

    for (size_t i = 0; i < stats.missSampleCount; ++i) {
        const auto& sample = stats.missSamples[i];
        const char* reason = !sample.hasForgeWorld ? "forgeWorld-null" :
            (sample.voxelTargets == 0 ? "no-voxel-transform-targets" :
                "no-hit-result; CastVoxel does not expose narrow miss reason");
        std::cout << "[D1.2.12a][RAYCAST MISS SAMPLE] index=" << i
                  << " mode=Voxel origin=(" << sample.origin.x << ',' << sample.origin.y << ',' << sample.origin.z
                  << ") direction=(" << sample.direction.x << ',' << sample.direction.y << ',' << sample.direction.z
                  << ") maxDistance=10000 voxelTargets=" << sample.voxelTargets
                  << " reason=" << reason << '\n';
    }
}

void PlanetMapperCamera::Init(float initialDistance, float initialPitch, float initialYaw) {
    m_orbitDistance = initialDistance;
    m_orbitPitch = initialPitch;
    m_orbitYaw = initialYaw;
    m_orbitTarget = glm::vec3(0.0f);
    m_lastRayHit.hit = false;
}

bool PlanetMapperCamera::Update(float dt, SharedContext* context, uint32_t windowWidth, uint32_t windowHeight) {
    auto& diagnostics = g_planetMapperRaycastDiagnostics;
    ++diagnostics.updateCalls;
    if (!context) {
        ++diagnostics.nullContextUpdates;
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    bool allowCameraControl = false;
    
    // Check if mouse is in the 3D viewport (right 65% of screen)
    if (!io.WantCaptureMouse) {
        if (io.MousePos.x >= windowWidth * 0.45f) {
            allowCameraControl = true;
        }
    }

    if (io.MousePos.x >= windowWidth * 0.35f) ++diagnostics.mouseInViewport;
    if (io.WantCaptureMouse) ++diagnostics.blockedByWantCaptureMouse;
    if (io.MousePos.x < windowWidth * 0.45f) ++diagnostics.outsideViewportGate;
    if (allowCameraControl) ++diagnostics.mousePassedCameraXGate;

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
    const bool blockedByImGuiWindowHover = allowCameraControl && ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
    if (blockedByImGuiWindowHover) ++diagnostics.blockedByImGuiWindowHover;
    if (allowCameraControl && !blockedByImGuiWindowHover) {
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
        ++diagnostics.queriesSent;
        ++diagnostics.voxelModeQueries;
        SnapshotPlanetMapperScene(context);

        fw::RaycastHit hit = fw::RaycastSystem::Cast(context, query);
        if (hit.hit) {
            ++diagnostics.hits;
            switch (hit.type) {
                case fw::RaycastHitType::Voxel: ++diagnostics.voxelHits; break;
                case fw::RaycastHitType::Sphere: ++diagnostics.sphereHits; break;
                case fw::RaycastHitType::Physics: ++diagnostics.physicsHits; break;
                default: ++diagnostics.otherTypeHits; break;
            }
        } else {
            ++diagnostics.misses;
            if (diagnostics.missSampleCount < diagnostics.missSamples.size()) {
                auto& sample = diagnostics.missSamples[diagnostics.missSampleCount++];
                sample.origin = query.ray.origin;
                sample.direction = query.ray.direction;
                sample.hasForgeWorld = context->forgeWorld != nullptr;
                if (sample.hasForgeWorld) {
                    auto& registry = context->forgeWorld->GetRegistry();
                    auto voxelView = registry.view<fw::VoxelChunkComponent, fw::TransformComponent>();
                    for (auto entity : voxelView) {
                        (void)entity;
                        ++sample.voxelTargets;
                    }
                }
            }
        }
        if (hit.hit && hit.type == fw::RaycastHitType::Voxel) {
            // Usa CubeSphereMapping SOLO per tradurre il punto fisico in coordinate logiche (face, uv)
            glm::vec3 dir = glm::normalize(hit.worldPosition);
            fw::CubeSphereMapping::DirectionToFaceUV(dir, hit.faceIndex, hit.uv);

            // D1.2.11 TEMP DIAGNOSTICS BEGIN: observation only; does not alter the hit/query.
            fw::PlanetSize hitPlanetSize = fw::PlanetSize::Medium;
            const char* sizeSource = "default-fallback";
            if (context->projectManager) {
                const auto& document = context->projectManager->GetDocument();
                if (!document.planets.empty()) {
                    hitPlanetSize = document.planets.front().planetSize;
                    sizeSource = "first-planet-fallback";
                }
            }
            if (context->forgeWorld) {
                auto& registry = context->forgeWorld->GetRegistry();
                if (registry.valid(hit.targetEntity) && registry.all_of<fw::BiomeDataComponent>(hit.targetEntity)) {
                    hitPlanetSize = registry.get<fw::BiomeDataComponent>(hit.targetEntity).planetSize;
                    sizeSource = "hit-chunk";
                }
            }

            const int resolution = static_cast<int>(fw::PlanetMath::GetFaceResolution(hitPlanetSize));
            const glm::vec2 chart = fw::CubeSphereMapping::FaceUVToChartCoordinates(hit.uv);
            int col = -1;
            int row = -1;
            fw::CubeSphereMapping::UVToGrid(hit.uv, resolution, col, row);

            static int lastLoggedFace = -1;
            static int lastLoggedResolution = -1;
            static int lastLoggedCol = -1;
            static int lastLoggedRow = -1;
            if (hit.faceIndex != lastLoggedFace || resolution != lastLoggedResolution ||
                col != lastLoggedCol || row != lastLoggedRow) {
            ++diagnostics.d1211LogsEmitted;
                const glm::vec3 cameraDirection = context->activeCameraView.cameraFront;
                std::cout << "[D1.2.11][RAYCAST] camPos=(" << camPos.x << ',' << camPos.y << ',' << camPos.z
                          << ") camDir=(" << cameraDirection.x << ',' << cameraDirection.y << ',' << cameraDirection.z
                          << ") rayDir=(" << rayWorld.x << ',' << rayWorld.y << ',' << rayWorld.z
                          << ") world=(" << hit.worldPosition.x << ',' << hit.worldPosition.y << ',' << hit.worldPosition.z
                          << ") faceNormal=(" << hit.faceNormal.x << ',' << hit.faceNormal.y << ',' << hit.faceNormal.z
                          << ") face=" << hit.faceIndex << " uv=(" << hit.uv.x << ',' << hit.uv.y
                          << ") chart=(" << chart.x << ',' << chart.y << ") N=" << resolution
                          << " col=" << col << " row=" << row << " sizeSource=" << sizeSource << '\n';
                lastLoggedFace = hit.faceIndex;
                lastLoggedResolution = resolution;
                lastLoggedCol = col;
                lastLoggedRow = row;
            }
            // D1.2.11 TEMP DIAGNOSTICS END.
        }
        m_lastRayHit = hit;
    } else {
        m_lastRayHit.hit = false;
    }

    return allowCameraControl;
}
