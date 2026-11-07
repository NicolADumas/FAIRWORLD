#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace fw::PlanetaryMovementMath {

inline glm::vec2 NormalizePlanarInput(const glm::vec2& input) {
    if (!std::isfinite(input.x) || !std::isfinite(input.y)) return glm::vec2(0.0f);
    const float lengthSquared = glm::dot(input, input);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0f) return input;
    return input * glm::inversesqrt(lengthSquared);
}

inline glm::vec3 DesiredTangentVelocity(const glm::vec3& forward, const glm::vec3& right,
                                        const glm::vec2& input, float speed) {
    if (!std::isfinite(speed)) return glm::vec3(0.0f);
    const glm::vec2 normalizedInput = NormalizePlanarInput(input);
    return (forward * normalizedInput.y + right * normalizedInput.x) * speed;
}

inline bool SplitRadialVelocity(const glm::vec3& velocity, const glm::vec3& up,
                                float& radialSpeed, glm::vec3& tangentVelocity) {
    const float upLengthSquared = glm::dot(up, up);
    if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z) ||
        !std::isfinite(upLengthSquared) || upLengthSquared < 1.0e-12f) return false;
    const glm::vec3 unitUp = up * glm::inversesqrt(upLengthSquared);
    radialSpeed = glm::dot(velocity, unitUp);
    tangentVelocity = velocity - unitUp * radialSpeed;
    return std::isfinite(radialSpeed) && std::isfinite(tangentVelocity.x) &&
           std::isfinite(tangentVelocity.y) && std::isfinite(tangentVelocity.z);
}

inline glm::vec3 ClampInwardRadialSpeed(const glm::vec3& velocity, const glm::vec3& up,
                                        float maxInwardSpeed) {
    float radialSpeed = 0.0f;
    glm::vec3 tangentVelocity(0.0f);
    if (!SplitRadialVelocity(velocity, up, radialSpeed, tangentVelocity) ||
        !std::isfinite(maxInwardSpeed) || maxInwardSpeed < 0.0f ||
        radialSpeed >= -maxInwardSpeed) return velocity;
    const glm::vec3 unitUp = glm::normalize(up);
    return tangentVelocity - unitUp * maxInwardSpeed;
}

} // namespace fw::PlanetaryMovementMath
