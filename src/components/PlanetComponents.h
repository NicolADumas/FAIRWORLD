#pragma once
#include <glm/glm.hpp>
#include <entt/entt.hpp>

namespace fw {

    // Identificatore univoco per l'Entità Pianeta
    struct PlanetTag {};

    // 1. Stato/Input Temporale puro
    struct PlanetTimeComponent {
        double absoluteTime = 0.0;     // Tempo totale simulato (ore)
        double dayOfYear = 1.0;        // Giorno dell'anno corrente (es. 1.0 a 365.0)
        double yearPhase = 0.0;        // Frazione dell'anno (0.0 a 1.0, 0 = Equinozio di Primavera)
        double timeScale = 1.0;        // Moltiplicatore di scorrimento del tempo
        double timeOfDay = 8.0;        // Ora del giorno locale (0.0 a 24.0)
    };

    // 2. Parametri Fisici Inalterabili dell'Astronomia
    struct PlanetAstronomyComponent {
        double axialTilt = 23.44;           // Inclinazione dell'asse di rotazione (gradi)
        double yearLength = 365.0;          // Durata di un anno in giorni
        double orbitalEccentricity = 0.0;   // Eccentricità (per futura implementazione)
        double moonOrbitalPeriod = 27.3;    // Giorni per una fase lunare
    };

    // 3. Geometria Globale
    struct PlanetGeometryComponent {
        double planetRadius = 50.0;
        bool isLogicalSphere = true;
        // In futuro: mappatura facce, LOD globale, coordinate sferiche del giocatore
    };

    // 4. Stato Derivato (Output della Simulazione Astronomica)
    struct PlanetEnvironmentComponent {
        glm::vec3 sunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 moonDirection = glm::vec3(0.0f, -1.0f, 0.0f);
        
        double solarDeclination = 0.0; // Inclinazione stagionale del sole (-axialTilt a +axialTilt)
        double moonPhase = 0.5;        // 0 = Nuova, 0.5 = Piena
        
        float dayFactor = 1.0f;        // Intensità della luce (0 = Notte, 1 = Giorno)
    };

    // 5. Atmosfera Fisicamente Plausibile
    struct PlanetAtmosphereComponent {
        float rayleighScattering = 0.0025f;
        float mieScattering = 0.0010f;
        float atmosphericDensity = 1.0f;
        float visibility = 10000.0f;
        glm::vec3 skyBaseColor = glm::vec3(0.2f, 0.4f, 0.8f);
    };

    // 6. Sistema Climatico e Vento Globale
    struct PlanetClimateComponent {
        float globalTemperature = 15.0f; // Celsius
        float globalHumidity = 0.5f;     // 0.0 a 1.0
        float globalPressure = 1013.25f; // hPa
        
        glm::vec3 globalWindDirection = glm::vec3(1.0f, 0.0f, 0.0f);
        float globalWindSpeed = 5.0f;    // m/s
        
        float cloudDensity = 0.2f;
        float precipitationLevel = 0.0f;
    };

    // 7. Oceani e Idrologia Dinamica
    struct PlanetOceanComponent {
        float baseSeaLevel = 32.0f;
        float tideAmplitude = 2.0f;      // Variazione di marea influenzata dalla luna
        glm::vec3 oceanCurrent = glm::vec3(0.0f, 0.0f, 1.0f);
        float waterTemperature = 12.0f;
    };

    // 8. Geologia e Tettonica
    struct PlanetGeologyComponent {
        float tectonicActivity = 0.1f;   // 0 = Morto, 1 = Attivo
        float erosionRate = 0.05f;       // Fattore di erosione temporale
        float volcanicActivity = 0.0f;
    };

    // 9. Ecologia e Biosfera
    struct PlanetEcologyComponent {
        float floraDensity = 0.8f;
        float faunaDensity = 0.5f;
        float seasonalGrowthPhase = 1.0f; // Legato alla declinazione solare
    };

}
