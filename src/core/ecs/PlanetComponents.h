#pragma once
#include <glm/glm.hpp>
#include <string>

namespace fw {

// Componente che caratterizza le entità come corpi planetari fisici
struct PlanetComponent {
    float radius = 50.0f;            // Raggio del pianeta in metri
    float surfaceGravity = 9.81f;    // Gravità superficiale (m/s^2)
    float axialTilt = 23.5f;         // Inclinazione dell'asse di rotazione in gradi
    float rotationSpeed = 1.0f;      // Velocità angolare di rotazione sul proprio asse (rad/s)
    float currentRotationAngle = 0.0f; // Angolo attuale di rotazione (rad)
};

// Componente che caratterizza i corpi nello spazio profondo e il moto rivolutivo in un Sistema Solare
struct SolarSystemOrbitComponent {
    glm::vec3 centerOfMass = glm::vec3(0.0f); // Centro dell'orbita (es. il Sole o il pianeta madre per le lune)
    float orbitRadius = 500.0f;               // Raggio dell'orbita (in metri o unità spaziali)
    float angularSpeed = 0.1f;                // Velocità angolare lungo l'orbita (rad/s)
    float currentAngle = 0.0f;                // Angolo di rivoluzione attuale (rad)
    float inclination = 0.0f;                 // Inclinazione del piano orbitale in gradi
};

// Tag Component di EnTT per identificare esplicitamente i Chunk provvisori degli editor 
// Esenta dal culling di lontananza o dall'unload su disco durante il lookdev
struct ChunkPreviewTag {
    bool doNotCull = true;
    bool doNotSaveToDisk = true;
};

} // namespace fw
