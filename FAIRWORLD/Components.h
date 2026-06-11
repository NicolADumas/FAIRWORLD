#pragma once
#include <string>
#include <glm/glm.hpp>
#include "PhysicsEngine.h"

// Componente di base per la posizione nello spazio 3D
struct TransformComponent {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float pitch = 0.0f;
    float yaw = -90.0f;
    float roll = 0.0f;
};

// Componente per la telecamera (Data-Driven)
struct CameraComponent {
    bool isMain = true;
    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    
    // Vettori di direzione
    glm::vec3 front = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    
    // Configurazioni input
    float movementSpeed = 5.0f;
    float mouseSensitivity = 0.1f;
};

// Componente per identificare un'entità con un nome leggibile
struct NameComponent {
    std::string name;
};

// Componente che indica che l'entità è un giocatore controllabile
struct PlayerControllerComponent {
    float walkSpeed = 5.0f;
    float runSpeed = 8.0f;
    float jumpForce = 7.0f;
};

// Componente per la simulazione fisica
struct RigidBodyComponent {
    RigidBody body;
};
