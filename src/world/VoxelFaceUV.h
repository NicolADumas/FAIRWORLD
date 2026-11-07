#pragma once

#include <glm/glm.hpp>
#include "core/utils/ForgeMath.h"

namespace fw {

// Maps a voxel-face corner in local [-0.5, 0.5] coordinates to repeatable
// [0, 1] UVs. U/V are chosen per outward normal so every face is oriented
// deterministically and adjacent faces do not share accidental projections.
inline fw::Vec2 VoxelFaceUV(const glm::vec3& corner, const glm::vec3& normal) {
    glm::vec3 uAxis;
    glm::vec3 vAxis;
    if (normal.y > 0.5f) { uAxis = {1, 0, 0}; vAxis = {0, 0, 1}; }
    else if (normal.y < -0.5f) { uAxis = {1, 0, 0}; vAxis = {0, 0, -1}; }
    else if (normal.x < -0.5f) { uAxis = {0, 0, 1}; vAxis = {0, 1, 0}; }
    else if (normal.x > 0.5f) { uAxis = {0, 0, -1}; vAxis = {0, 1, 0}; }
    else if (normal.z > 0.5f) { uAxis = {1, 0, 0}; vAxis = {0, 1, 0}; }
    else { uAxis = {-1, 0, 0}; vAxis = {0, 1, 0}; }

    return {
        corner.x * uAxis.x + corner.y * uAxis.y + corner.z * uAxis.z + 0.5f,
        corner.x * vAxis.x + corner.y * vAxis.y + corner.z * vAxis.z + 0.5f
    };
}

} // namespace fw
