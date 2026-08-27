#include "AstronomySystem.h"
#include "../components/PlanetComponents.h"
#include <cmath>
#include <algorithm>
#include <iostream>

namespace fw {

    void AstronomySystem::Update(entt::registry& registry, float dt) {
        auto view = registry.view<PlanetTag, PlanetTimeComponent, PlanetAstronomyComponent, PlanetEnvironmentComponent>();
        
        for (auto entity : view) {
            auto& timeData = view.get<PlanetTimeComponent>(entity);
            auto& astroData = view.get<PlanetAstronomyComponent>(entity);
            auto& envData = view.get<PlanetEnvironmentComponent>(entity);

            // Costanti e conversioni in radianti
            float tiltRad = glm::radians((float)astroData.axialTilt);
            
            // Assume 45 degrees latitude for default global rendering calculations (until camera/player is added)
            float latRad = glm::radians(45.0f); 
            
            // Frazione dell'anno [0.0, 1.0]
            float yearProgress = fmod((float)timeData.dayOfYear, (float)astroData.yearLength) / (float)astroData.yearLength;
            timeData.yearPhase = yearProgress;
            
            // Posizione orbitale (0 = Equinozio di Primavera)
            float orbitalAngle = yearProgress * glm::two_pi<float>();

            // 1. Direzione GLOBALE del sole
            glm::vec3 sunGlobal = glm::vec3(cos(orbitalAngle), 0.0f, -sin(orbitalAngle));
            
            // 2. Inclinazione Assiale del Pianeta
            glm::mat4 mTilt = glm::rotate(glm::mat4(1.0f), tiltRad, glm::vec3(1.0f, 0.0f, 0.0f));
            
            // 3. Rotazione Giornaliera
            float dailyAngle = (timeData.timeOfDay - 12.0f) * (glm::two_pi<float>() / 24.0f);
            glm::mat4 mRot = glm::rotate(glm::mat4(1.0f), -dailyAngle + glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
            
            // 4. Trasformazione alla Latitudine dell'osservatore
            glm::mat4 mLat = glm::rotate(glm::mat4(1.0f), latRad, glm::vec3(1.0f, 0.0f, 0.0f));
            
            glm::mat4 planetToLocal = mLat; 
            glm::mat4 globalToPlanet = mRot * mTilt;
            glm::mat4 globalToLocal = planetToLocal * globalToPlanet;
            
            // Mappiamo gli assi locali del motore (Y = UP)
            glm::vec3 sunLocal = glm::vec3(globalToLocal * glm::vec4(sunGlobal, 0.0f));
            envData.sunDirection = glm::normalize(sunLocal);
            
            // --- CALCOLO LUNA ---
            // Sincronizzato al mese lunare
            float moonPhase = fmod(timeData.absoluteTime / 24.0, astroData.moonOrbitalPeriod) / astroData.moonOrbitalPeriod;
            envData.moonPhase = moonPhase;
            
            float moonOrbitalAngle = orbitalAngle + (moonPhase * glm::two_pi<float>());
            glm::mat4 mMoonTilt = glm::rotate(glm::mat4(1.0f), glm::radians(5.14f), glm::vec3(0.0f, 0.0f, 1.0f));
            glm::vec3 moonGlobal = glm::vec3(mMoonTilt * glm::vec4(cos(moonOrbitalAngle), 0.0f, -sin(moonOrbitalAngle), 0.0f));
            
            envData.moonDirection = glm::normalize(glm::vec3(globalToLocal * glm::vec4(moonGlobal, 0.0f)));

            // --- INTENSITA LUCE / DECLINAZIONE ---
            envData.solarDeclination = tiltRad * sin(orbitalAngle);
            envData.dayFactor = glm::clamp((envData.sunDirection.y + 0.1f) / 0.2f, 0.0f, 1.0f);
        }
    }

}
