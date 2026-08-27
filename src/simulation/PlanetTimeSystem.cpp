#include "PlanetTimeSystem.h"
#include "../components/PlanetComponents.h"

namespace fw {

    void PlanetTimeSystem::Update(entt::registry& registry, float dt) {
        auto view = registry.view<PlanetTag, PlanetTimeComponent>();
        
        for (auto entity : view) {
            auto& timeData = view.get<PlanetTimeComponent>(entity);
            
            // 24 ore / 3600 secondi = 1 ora di gioco in tempo reale (come da TimeManager)
            float inGameHoursPerRealSecond = 24.0f / 3600.0f; 
            double timeDelta = dt * inGameHoursPerRealSecond * timeData.timeScale;
            
            timeData.absoluteTime += timeDelta;
            timeData.timeOfDay += timeDelta;
            
            if (timeData.timeOfDay >= 24.0) {
                timeData.timeOfDay -= 24.0;
                timeData.dayOfYear += 1.0;
            }
        }
    }

}
