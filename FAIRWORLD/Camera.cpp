#include "pch.h"
#include "Camera.h"
#include <algorithm>

Camera::Camera(glm::vec3 startPosition)
    : Position(startPosition), WorldUp(glm::vec3(0.0f, 1.0f, 0.0f)), 
      Yaw(-90.0f), Pitch(0.0f), MovementSpeed(5.0f), MouseSensitivity(0.2f) {
    updateCameraVectors();
}

glm::mat4 Camera::GetViewMatrix() {
    // La funzione magica: "Sono qui, guardo lì, e questo è l'alto"
    return glm::lookAt(Position, Position + Front, Up);
}

void Camera::ProcessKeyboard(char direction, float deltaTime) {
    float velocity = MovementSpeed * deltaTime;

    // FIX VOLO: proiettiamo Front e Right sul piano orizzontale (Y=0)
    // così guardare in su/giù non fa salire/scendere mentre ci si muove con WASD.
    // Il volo verticale rimane controllato SOLO da Q (scendi) ed E (sali).
    glm::vec3 flatFront = glm::normalize(glm::vec3(Front.x, 0.0f, Front.z));
    glm::vec3 flatRight = glm::normalize(glm::vec3(Right.x, 0.0f, Right.z));

    if (direction == 'W') Position += flatFront * velocity;
    if (direction == 'S') Position -= flatFront * velocity;
    if (direction == 'A') Position -= flatRight * velocity;
    if (direction == 'D') Position += flatRight * velocity;
    if (direction == 'Q') Position.y -= velocity; // Scendi
    if (direction == 'E') Position.y += velocity; // Sali
}

void Camera::ProcessMouseMovement(float xoffset, float yoffset) {
    xoffset *= MouseSensitivity;
    yoffset *= MouseSensitivity;

    Yaw += xoffset;
    Pitch += yoffset;

    // Blocchiamo il collo per non fare capriole all'indietro (sindrome dell'esorcista)
    if (Pitch > 89.0f) Pitch = 89.0f;
    if (Pitch < -89.0f) Pitch = -89.0f;

    updateCameraVectors();
}

void Camera::updateCameraVectors() {
    // Calcoliamo il nuovo vettore direzionale usando la trigonometria
    glm::vec3 front;
    front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    front.y = sin(glm::radians(Pitch));
    front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    Front = glm::normalize(front);
    
    // Ricalcoliamo Destra e Alto tramite il Prodotto Vettoriale (Cross Product)
    Right = glm::normalize(glm::cross(Front, WorldUp));
    Up    = glm::normalize(glm::cross(Right, Front));
}
