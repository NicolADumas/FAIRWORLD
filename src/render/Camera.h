#pragma once
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

class Camera {
public:
    // Attributi dell'entità
    glm::vec3 Position;
    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;

    // Angoli di Eulero (per guardarsi attorno)
    float Yaw;
    float Pitch;

    // Opzioni
    float MovementSpeed;
    float MouseSensitivity;

    Camera(glm::vec3 startPosition = glm::vec3(0.0f, 0.0f, 3.0f));

    // Physics
    float VelocityY = 0.0f;
    bool IsGrounded = false;
    bool IsSwimming = false;

    // Calcola la matrice di vista (il M.V.P.)
    glm::mat4 GetViewMatrix();

    // Input da Tastiera
    void ProcessKeyboard(char direction, float deltaTime);
    // Input da Mouse
    void ProcessMouseMovement(float xoffset, float yoffset);

private:
    void updateCameraVectors();
};
