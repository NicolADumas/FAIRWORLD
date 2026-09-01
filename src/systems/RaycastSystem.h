#pragma once

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <limits>
#include <cmath>

namespace fw {

class GameWorld;

struct VoxelHit {
    entt::entity chunkID = entt::null;
    glm::ivec3 voxelPosition{0, 0, 0};
    glm::ivec3 faceNormal{0, 0, 0};
    
    float distance = -1.0f;
    glm::vec3 worldPosition{0.0f, 0.0f, 0.0f};
    bool hit = false;
};

class RaycastSystem {
public:
    // Esegue un raycast ibrido (Globale -> Locale Chunk -> DDA)
    static VoxelHit Cast(GameWorld& world, const glm::vec3& rayOrigin, const glm::vec3& rayDirection, float maxDistance);

private:
    // Funzione helper per intersecare un raggio con un AABB 3D
    static bool IntersectAABB(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& aabbMin, const glm::vec3& aabbMax, float& tMin, float& tMax);
};

} // namespace fw
