#pragma once

#include "core/utils/PlanetMath.h"
#include <glm/glm.hpp>

namespace fw {

struct PlanetarySpawnPlan {
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    glm::vec3 temporaryBodyPosition{0.0f};
    float heightOffset = 0.11f;
    bool pending = true;
};

// Until voxel terrain is queryable, keep the player outside the planet and
// freeze player simulation. The actual body position is resolved from voxels.
inline PlanetarySpawnPlan MakePlanetaryFallbackSpawn(PlanetSize size, float safeAltitude = 150.0f) {
    PlanetarySpawnPlan plan;
    const float radius = PlanetMath::GetPlanetRadius(size);
    plan.temporaryBodyPosition = plan.direction * (radius + safeAltitude);
    return plan;
}

} // namespace fw
