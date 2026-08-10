#include "pch.h"
#include "PlanetSystems.h"
#include "ForgeComponents.h"
#include <cmath>

namespace fw {

void PlanetOrbitSystem::Update(entt::registry& registry, float dt) {
    // 1. Aggiorniamo le rivoluzioni orbitali nel Sistema Solare (Orbite di Keplero)
    auto orbitView = registry.view<SolarSystemOrbitComponent, TransformComponent>();
    for (auto entity : orbitView) {
        auto& orbit = orbitView.get<SolarSystemOrbitComponent>(entity);
        auto& trans = orbitView.get<TransformComponent>(entity);

        // Terza legge di Keplero: w = sqrt(G * M / r^3)
        // Calcolo della velocità angolare in base a distanza e massa del corpo attrattore
        const float G_GAME = 6.674e-11f;
        float r = orbit.orbitRadius;
        if (r > 0.001f) {
            float r3 = r * r * r;
            // Calcoliamo la velocità e applichiamo un moltiplicatore per rendere le orbite visibili e giocabili
            orbit.angularSpeed = std::sqrt((G_GAME * orbit.centralMass) / r3) * 1.0f; // Moltiplicatore di time-lapse
        } else {
            orbit.angularSpeed = 0.0f;
        }

        orbit.currentAngle += orbit.angularSpeed * dt;
        if (orbit.currentAngle > 6.283185307f) {
            orbit.currentAngle -= 6.283185307f;
        }

        float x = orbit.centerOfMass.x + orbit.orbitRadius * std::cos(orbit.currentAngle);
        float z = orbit.centerOfMass.z + orbit.orbitRadius * std::sin(orbit.currentAngle);
        float y = orbit.centerOfMass.y + orbit.orbitRadius * std::sin(orbit.currentAngle) * std::sin(orbit.inclination * 0.01745329252f);

        trans.location.x = x;
        trans.location.y = y;
        trans.location.z = z;
    }

    // 2. Aggiorniamo la rotazione planetaria sul proprio asse
    auto planetView = registry.view<PlanetComponent, TransformComponent>();
    for (auto entity : planetView) {
        auto& planet = planetView.get<PlanetComponent>(entity);
        auto& trans = planetView.get<TransformComponent>(entity);

        planet.currentRotationAngle += planet.rotationSpeed * dt;
        if (planet.currentRotationAngle > 6.283185307f) {
            planet.currentRotationAngle -= 6.283185307f;
        }

        // Calcoliamo la rotazione con inclinazione assiale sul piano Y
        Vec3 axis{ std::sin(planet.axialTilt * 0.01745329252f), std::cos(planet.axialTilt * 0.01745329252f), 0.0f };
        trans.rotation = Quat::angleAxis(planet.currentRotationAngle, axis);
    }
}

} // namespace fw
