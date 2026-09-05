#pragma once

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <limits>
#include <cmath>

struct SharedContext;

namespace fw {

class GameWorld;

enum class RaycastMode {
    Auto,
    Voxel,
    Sphere,
    Physics
};

enum class RaycastHitType {
    None,
    Voxel,
    Sphere,
    Physics
};

struct RaycastHit {
    bool hit = false;
    float distance = 0.0f;
    glm::vec3 worldPosition{0.0f};
    glm::vec3 faceNormal{0.0f};
    entt::entity targetEntity{entt::null};
    
    RaycastHitType type = RaycastHitType::None;

    // Voxel specific
    glm::ivec3 voxelPosition{0};

    // Sphere specific
    int faceIndex = -1;
    glm::vec2 uv{0.0f};
};

struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
};

struct RaycastQuery {
    Ray ray;
    RaycastMode mode = RaycastMode::Auto;
    float maxDistance = std::numeric_limits<float>::max();
    entt::entity ignoreEntity = entt::null;
    uint32_t layerMask = 0xFFFFFFFF;
};

class RaycastSystem {
public:
    static RaycastHit Cast(SharedContext* context, const RaycastQuery& query);

private:
    static RaycastHit CastVoxel(SharedContext* context, const RaycastQuery& query);
    static RaycastHit CastSphere(SharedContext* context, const RaycastQuery& query);
    static RaycastHit CastPhysics(SharedContext* context, const RaycastQuery& query);

    // Helper for Voxel DDA
    static bool IntersectAABB(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& aabbMin, const glm::vec3& aabbMax, float& tMin, float& tMax);
};

} // namespace fw
