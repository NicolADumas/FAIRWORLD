#include "pch.h"
#include "RaycastSystem.h"
#include "SharedContext.h"
#include "GameWorld.h"
#include "ForgeComponents.h"
#include "FAIRWORLD.h"
#include "CubeSphereMapping.h"
#include "WorldProjectManager.h"
#include <algorithm>
#include <iostream>

namespace fw {

bool RaycastSystem::IntersectAABB(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& aabbMin, const glm::vec3& aabbMax, float& tMin, float& tMax) {
    glm::vec3 invDir = 1.0f / rayDir;
    glm::vec3 t0 = (aabbMin - rayOrigin) * invDir;
    glm::vec3 t1 = (aabbMax - rayOrigin) * invDir;

    glm::vec3 tMinVec = glm::min(t0, t1);
    glm::vec3 tMaxVec = glm::max(t0, t1);

    tMin = std::max(std::max(tMinVec.x, tMinVec.y), tMinVec.z);
    tMax = std::min(std::min(tMaxVec.x, tMaxVec.y), tMaxVec.z);

    return tMax >= tMin && tMax >= 0.0f;
}

RaycastHit RaycastSystem::Cast(SharedContext* context, const RaycastQuery& query) {
    if (!context) return RaycastHit{};

    RaycastMode mode = query.mode;
    if (mode == RaycastMode::Auto) {
        if (context->engine) {
            GameMode engineMode = context->engine->GetGameMode();
            if (engineMode == GameMode::Play || engineMode == GameMode::ChunkEditor) {
                mode = RaycastMode::Voxel;
            } else if (engineMode == GameMode::PlanetMapper) {
                mode = RaycastMode::Sphere;
            } else {
                mode = RaycastMode::Physics;
            }
        } else {
            mode = RaycastMode::Voxel; // Fallback
        }
    }

    switch (mode) {
        case RaycastMode::Voxel:   return CastVoxel(context, query);
        case RaycastMode::Sphere:  return CastSphere(context, query);
        case RaycastMode::Physics: return CastPhysics(context, query);
        default:                   return RaycastHit{};
    }
}

RaycastHit RaycastSystem::CastVoxel(SharedContext* context, const RaycastQuery& query) {
    RaycastHit bestHit;
    bestHit.distance = query.maxDistance;
    bestHit.type = RaycastHitType::Voxel;

    if (!context->forgeWorld) return bestHit;

    auto& registry = context->forgeWorld->GetRegistry();
    auto chunkView = registry.view<VoxelChunkComponent, TransformComponent>();

    for (auto chunkEnt : chunkView) {
        if (chunkEnt == query.ignoreEntity) continue;
        
        auto& transform = chunkView.get<TransformComponent>(chunkEnt);
        auto& chunkData = chunkView.get<VoxelChunkComponent>(chunkEnt);

        glm::vec3 chunkLoc(transform.location.x, transform.location.y, transform.location.z);
        if (glm::distance(query.ray.origin, chunkLoc) > query.maxDistance + 150.0f) {
            continue;
        }

        fw::Mat4 cGlobal = transform.computeGlobalMatrix(registry);
        glm::mat4 chunkGlobalMatrix = glm::transpose(*reinterpret_cast<glm::mat4*>(&cGlobal));
        glm::mat4 invTransform = glm::inverse(chunkGlobalMatrix);

        glm::vec3 localOrigin = glm::vec3(invTransform * glm::vec4(query.ray.origin, 1.0f));
        glm::vec3 localDir = glm::normalize(glm::vec3(invTransform * glm::vec4(query.ray.direction, 0.0f)));

        glm::vec3 aabbMin(-0.5f, -0.5f, -0.5f);
        glm::vec3 aabbMax(CHUNK_SIZE - 0.5f, CHUNK_HEIGHT - 0.5f, CHUNK_SIZE - 0.5f);

        float tMin, tMax;
        if (!IntersectAABB(localOrigin, localDir, aabbMin, aabbMax, tMin, tMax)) {
            continue;
        }
        
        float tIntersect = std::max(0.0f, tMin);
        if (tIntersect > bestHit.distance) {
            continue;
        }

        glm::vec3 ddaOrigin = localOrigin + glm::vec3(0.5f, 0.5f, 0.5f);
        
        glm::ivec3 currentPos(std::floor(ddaOrigin.x), std::floor(ddaOrigin.y), std::floor(ddaOrigin.z));
        
        currentPos.x = std::clamp(currentPos.x, 0, CHUNK_SIZE - 1);
        currentPos.y = std::clamp(currentPos.y, 0, CHUNK_HEIGHT - 1);
        currentPos.z = std::clamp(currentPos.z, 0, CHUNK_SIZE - 1);

        glm::ivec3 step(
            (localDir.x > 0) ? 1 : ((localDir.x < 0) ? -1 : 0),
            (localDir.y > 0) ? 1 : ((localDir.y < 0) ? -1 : 0),
            (localDir.z > 0) ? 1 : ((localDir.z < 0) ? -1 : 0)
        );

        glm::vec3 tDelta(
            (localDir.x != 0) ? std::abs(1.0f / localDir.x) : std::numeric_limits<float>::max(),
            (localDir.y != 0) ? std::abs(1.0f / localDir.y) : std::numeric_limits<float>::max(),
            (localDir.z != 0) ? std::abs(1.0f / localDir.z) : std::numeric_limits<float>::max()
        );

        glm::vec3 tMaxAxis(
            (localDir.x > 0) ? (currentPos.x + 1.0f - ddaOrigin.x) * tDelta.x : (localDir.x < 0 ? (ddaOrigin.x - currentPos.x) * tDelta.x : std::numeric_limits<float>::max()),
            (localDir.y > 0) ? (currentPos.y + 1.0f - ddaOrigin.y) * tDelta.y : (localDir.y < 0 ? (ddaOrigin.y - currentPos.y) * tDelta.y : std::numeric_limits<float>::max()),
            (localDir.z > 0) ? (currentPos.z + 1.0f - ddaOrigin.z) * tDelta.z : (localDir.z < 0 ? (ddaOrigin.z - currentPos.z) * tDelta.z : std::numeric_limits<float>::max())
        );

        float currentDist = 0.0f;
        glm::ivec3 lastPos = currentPos;
        bool found = false;

        while (currentDist < bestHit.distance) {
            if (currentPos.x < 0 || currentPos.x >= CHUNK_SIZE ||
                currentPos.y < 0 || currentPos.y >= CHUNK_HEIGHT ||
                currentPos.z < 0 || currentPos.z >= CHUNK_SIZE) {
                break;
            }

            if (currentPos.x >= 0 && currentPos.x < CHUNK_SIZE &&
                currentPos.y >= 0 && currentPos.y < CHUNK_HEIGHT &&
                currentPos.z >= 0 && currentPos.z < CHUNK_SIZE) {
                
                BlockType bType = static_cast<BlockType>(chunkData.blocks[currentPos.x][currentPos.y][currentPos.z]);
                if (bType != BlockType::Air && bType != BlockType::Water && bType != BlockType::OutOfBounds) {
                    found = true;
                    break;
                }
            }

            lastPos = currentPos;

            if (tMaxAxis.x < tMaxAxis.y) {
                if (tMaxAxis.x < tMaxAxis.z) {
                    currentPos.x += step.x;
                    currentDist = tMaxAxis.x;
                    tMaxAxis.x += tDelta.x;
                } else {
                    currentPos.z += step.z;
                    currentDist = tMaxAxis.z;
                    tMaxAxis.z += tDelta.z;
                }
            } else {
                if (tMaxAxis.y < tMaxAxis.z) {
                    currentPos.y += step.y;
                    currentDist = tMaxAxis.y;
                    tMaxAxis.y += tDelta.y;
                } else {
                    currentPos.z += step.z;
                    currentDist = tMaxAxis.z;
                    tMaxAxis.z += tDelta.z;
                }
            }
        }

        if (found && currentDist < bestHit.distance) {
            bestHit.hit = true;
            bestHit.distance = currentDist;
            bestHit.targetEntity = chunkEnt;
            bestHit.voxelPosition = glm::ivec3(chunkData.cx * CHUNK_SIZE + currentPos.x, currentPos.y, chunkData.cz * CHUNK_SIZE + currentPos.z);
            
            glm::ivec3 normal = lastPos - currentPos;
            if (normal == glm::ivec3(0,0,0)) {
                normal = glm::ivec3(0,1,0); 
            }
            bestHit.faceNormal = normal;
        }
    }

    return bestHit;
}

RaycastHit RaycastSystem::CastSphere(SharedContext* context, const RaycastQuery& query) {
    RaycastHit result;
    result.type = RaycastHitType::Sphere;
    
    // Fallback if not injected properly, though usually it should be set via PlanetMapper context
    glm::vec3 sphereCenter{0.0f};
    float sphereRadius = 50.0f; // Default R
    if (context && context->projectManager) {
        const auto& doc = context->projectManager->GetDocument();
        // Just grab the first planet for now
        if (!doc.planets.empty()) {
            sphereRadius = doc.planets[0].planetRadius;
        }
    }
    
    glm::vec3 oc = query.ray.origin - sphereCenter;
    float b = glm::dot(oc, query.ray.direction);
    float c = glm::dot(oc, oc) - sphereRadius * sphereRadius;
    float h = b * b - c;
    
    if (h < 0.0f) return result; 
    
    h = glm::sqrt(h);
    float t = -b - h; 
    
    if (t < 0.0f || t > query.maxDistance) return result; 
    
    result.hit = true;
    result.distance = t;
    result.worldPosition = query.ray.origin + query.ray.direction * t;
    result.faceNormal = glm::normalize(result.worldPosition - sphereCenter);
    
    CubeSphereMapping::DirectionToFaceUV(result.faceNormal, result.faceIndex, result.uv);
    
    return result;
}

RaycastHit RaycastSystem::CastPhysics(SharedContext* context, const RaycastQuery& query) {
    RaycastHit result;
    result.type = RaycastHitType::Physics;
    // Jolt physics raycast integration placeholder
    return result;
}

} // namespace fw
