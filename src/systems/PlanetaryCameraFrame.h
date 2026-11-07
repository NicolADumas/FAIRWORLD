#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace fw::PlanetaryCameraFrame {

struct State {
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    // Yaw-zero heading, parallel-transported as the player changes surface normal.
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    bool initialized = false;
};

struct Basis {
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
};

inline bool IsFinite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

inline bool Normalize(const glm::vec3& value, glm::vec3& result) {
    const float lengthSquared = glm::dot(value, value);
    if (!IsFinite(value) || !std::isfinite(lengthSquared) || lengthSquared < 1.0e-12f) return false;
    result = value * glm::inversesqrt(lengthSquared);
    return IsFinite(result);
}

inline glm::vec3 ProjectToTangent(const glm::vec3& value, const glm::vec3& unitUp) {
    return value - unitUp * glm::dot(value, unitUp);
}

inline State MakeInitial(const glm::vec3& normal) {
    State state;
    if (!Normalize(normal, state.up)) return state;

    // Preserve the established global-north heading away from the poles. At a
    // pole choose +X as a deterministic reference axis instead of a degenerating
    // cross product.
    const glm::vec3 reference = std::abs(glm::dot(state.up, glm::vec3(0.0f, 1.0f, 0.0f))) < 0.99f
        ? glm::vec3(0.0f, 1.0f, 0.0f)
        : glm::vec3(1.0f, 0.0f, 0.0f);
    if (!Normalize(glm::cross(state.up, reference), state.forward)) return State{};
    state.initialized = true;
    return state;
}

inline bool TransportTo(State& state, const glm::vec3& normal) {
    glm::vec3 nextUp;
    if (!Normalize(normal, nextUp)) return false;
    if (!state.initialized) {
        state = MakeInitial(nextUp);
        return state.initialized;
    }

    glm::vec3 oldUp, oldForward;
    if (!Normalize(state.up, oldUp) ||
        !Normalize(ProjectToTangent(state.forward, oldUp), oldForward)) {
        state = MakeInitial(nextUp);
        return state.initialized;
    }

    const float cosine = std::clamp(glm::dot(oldUp, nextUp), -1.0f, 1.0f);
    const glm::vec3 crossNormals = glm::cross(oldUp, nextUp);
    const float sine = glm::length(crossNormals);
    glm::vec3 transportedForward = oldForward;

    if (sine > 1.0e-6f) {
        const glm::vec3 axis = crossNormals / sine;
        const float angle = std::atan2(sine, cosine);
        transportedForward = glm::angleAxis(angle, axis) * oldForward;
    } else if (cosine < 0.0f) {
        // The antiparallel case has no unique shortest-arc axis. Choose the
        // existing heading as the deterministic half-turn axis, preserving
        // forward instead of introducing an arbitrary 180-degree heading flip.
        transportedForward = glm::angleAxis(glm::pi<float>(), oldForward) * oldForward;
    }

    transportedForward = ProjectToTangent(transportedForward, nextUp);
    if (!Normalize(transportedForward, transportedForward)) {
        State fallback = MakeInitial(nextUp);
        if (!fallback.initialized) return false;
        state = fallback;
        return true;
    }

    state.up = nextUp;
    state.forward = transportedForward;
    state.initialized = true;
    return true;
}

inline bool ApplyLookDelta(float& yaw, float& pitch, float deltaYaw, float deltaPitch, float sensitivity) {
    if (!std::isfinite(yaw) || !std::isfinite(pitch) || !std::isfinite(deltaYaw) ||
        !std::isfinite(deltaPitch) || !std::isfinite(sensitivity)) return false;
    yaw = std::remainder(yaw + deltaYaw * sensitivity, 360.0f);
    pitch = std::clamp(pitch - deltaPitch * sensitivity, -89.0f, 89.0f);
    return std::isfinite(yaw) && std::isfinite(pitch);
}

inline bool BuildBasis(const State& state, float yaw, float pitch, Basis& result) {
    glm::vec3 unitUp, referenceForward;
    if (!state.initialized || !Normalize(state.up, unitUp) ||
        !Normalize(ProjectToTangent(state.forward, unitUp), referenceForward) ||
        !std::isfinite(yaw) || !std::isfinite(pitch)) return false;

    const glm::quat yawRotation = glm::angleAxis(glm::radians(-yaw), unitUp);
    glm::vec3 front = yawRotation * referenceForward;
    glm::vec3 right;
    if (!Normalize(glm::cross(front, unitUp), right)) return false;

    const glm::quat pitchRotation = glm::angleAxis(glm::radians(std::clamp(pitch, -89.0f, 89.0f)), right);
    if (!Normalize(pitchRotation * front, front)) return false;
    glm::vec3 up;
    if (!Normalize(glm::cross(right, front), up) || !Normalize(glm::cross(front, up), right)) return false;

    result = {front, right, up};
    return true;
}

} // namespace fw::PlanetaryCameraFrame
