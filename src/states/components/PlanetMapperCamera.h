#pragma once
#include <glm/glm.hpp>
#include "SharedContext.h"
#include "RaycastSystem.h"

class PlanetMapperCamera {
public:
    PlanetMapperCamera();
    ~PlanetMapperCamera() = default;

    void Init(float initialDistance, float initialPitch, float initialYaw);

    // Updates camera position based on input and returns whether camera took control of mouse
    bool Update(float dt, SharedContext* context, uint32_t windowWidth, uint32_t windowHeight);

    // Getters for UI overlay
    float GetOrbitDistance() const { return m_orbitDistance; }
    void SetOrbitDistance(float d) { m_orbitDistance = d; }
    float GetOrbitYaw() const { return m_orbitYaw; }
    float GetOrbitPitch() const { return m_orbitPitch; }
    const fw::RaycastHit& GetLastRayHit() const { return m_lastRayHit; }

    void SetTarget(const glm::vec3& target) { m_orbitTarget = target; }

private:
    float m_orbitDistance = 250.0f;
    float m_orbitYaw = 45.0f;
    float m_orbitPitch = 30.0f;
    glm::vec3 m_orbitTarget = glm::vec3(0.0f);

    fw::RaycastHit m_lastRayHit;
};
