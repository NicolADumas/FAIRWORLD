#include "RaycastSystem.h"
#include "GameWorld.h"
#include "ForgeComponents.h"
#include <algorithm>

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

VoxelHit RaycastSystem::Cast(GameWorld& world, const glm::vec3& rayOrigin, const glm::vec3& rayDirection, float maxDistance) {
    VoxelHit bestHit;
    bestHit.distance = maxDistance;

    auto& registry = world.GetRegistry();
    auto chunkView = registry.view<VoxelChunkComponent, TransformComponent>();

    for (auto chunkEnt : chunkView) {
        auto& transform = chunkView.get<TransformComponent>(chunkEnt);
        auto& chunkData = chunkView.get<VoxelChunkComponent>(chunkEnt);

        // OTTIMIZZAZIONE: Scarta i chunk visibilmente troppo lontani prima di fare calcoli matriciali!
        // (16x128x16 = diagonale max di ~130 blocchi)
        glm::vec3 chunkLoc(transform.location.x, transform.location.y, transform.location.z);
        if (glm::distance(rayOrigin, chunkLoc) > maxDistance + 150.0f) {
            continue;
        }

        // Transform global ray to chunk's local space
        fw::Mat4 cGlobal = transform.computeGlobalMatrix(registry);
        glm::mat4 chunkGlobalMatrix = glm::transpose(*reinterpret_cast<glm::mat4*>(&cGlobal));
        glm::mat4 invTransform = glm::inverse(chunkGlobalMatrix);

        glm::vec3 localOrigin = glm::vec3(invTransform * glm::vec4(rayOrigin, 1.0f));
        glm::vec3 localDir = glm::normalize(glm::vec3(invTransform * glm::vec4(rayDirection, 0.0f)));

        // Voxel procedural meshes are centered on integers. Block 0,0,0 spans [-0.5, 0.5].
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

        // Setup Amanatides & Woo DDA inside the local grid
        // Offset by +0.5 so that voxel boundaries align with integers (0.0 to 1.0 for block 0)
        glm::vec3 ddaOrigin = localOrigin + glm::vec3(0.5f, 0.5f, 0.5f);
        
        glm::ivec3 currentPos(std::floor(ddaOrigin.x), std::floor(ddaOrigin.y), std::floor(ddaOrigin.z));
        
        // Clamp to ensure we don't start outside due to float precision
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

        // Traverse chunk locally
        while (currentDist < bestHit.distance) {
            if (currentPos.x < 0 || currentPos.x >= CHUNK_SIZE ||
                currentPos.y < 0 || currentPos.y >= CHUNK_HEIGHT ||
                currentPos.z < 0 || currentPos.z >= CHUNK_SIZE) {
                break; // Exited chunk bounds
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
            bestHit.chunkID = chunkEnt;
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

} // namespace fw
