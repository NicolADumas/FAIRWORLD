#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entt.hpp>

namespace fw {

    struct AstronomyData {
        glm::vec3 sunDirection;
        glm::vec3 moonDirection;
        float sunElevation;
        float moonElevation;
        
        // Colori e luce calcolati fisicamente
        glm::vec3 skyColor;
        glm::vec3 ambientLight;
        float directionalLightIntensity;
        
        // Informazioni orarie locali
        float sunriseHour;
        float sunsetHour;
        float dayLengthHours;
    };

    class AstronomySystem {
    public:
        // Esegue il calcolo astronomico per il pianeta centrale nel GameWorld
        static void Update(entt::registry& registry, float dt);
    };

}
