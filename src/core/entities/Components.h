#pragma once
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "PhysicsEngine.h"

// =============================================================================
// TransformComponent
// Posizione + Rotazione (Quaternione) per la Physics/Logic e per l'Interpolazione.
// prev_* vengono scritti all'inizio di ogni tick fisico (in Update) e letti
// dal Render per il LERP/SLERP tra un tick e l'altro.
// =============================================================================
struct TransformComponent {
    // --- Stato Corrente (scritto dalla Fisica a 60 Hz) ---
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // Identità

    // --- Stato Precedente (snapshot preso all'inizio del tick) ---
    float prev_x = 0.0f;
    float prev_y = 0.0f;
    float prev_z = 0.0f;
    glm::quat prev_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    // --- Constructors ---
    TransformComponent() = default;
    TransformComponent(float px, float py, float pz)
        : x(px), y(py), z(pz), prev_x(px), prev_y(py), prev_z(pz) {}
};

// =============================================================================
// MassPropertiesComponent
// Contiene le proprietà fisiche calcolate proceduralmente per un'entità.
// =============================================================================
struct MassPropertiesComponent {
    float mass = 0.0f;
    glm::vec3 centerOfMass = glm::vec3(0.0f);
    glm::mat3 inertiaTensor = glm::mat3(0.0f);
};

// =============================================================================
// CameraComponent
// I vettori direzionali derivati dalla rotazione vengono aggiornati ogni tick.
// pitch/yaw sono mantenuti qui (in float) solo come accumulatori per l'input
// grezzo del mouse (FPS-style), prima della conversione in quaternione.
// =============================================================================
struct CameraComponent {
    bool  isMain = true;
    float fov       = 45.0f;
    float nearPlane = 0.1f;
    float farPlane  = 1000.0f;

    // Accumulatori angolari per input FPS (in gradi)
    // Pitch è clampato a [-89, 89] per evitare gimbal lock a poli
    float pitch = 0.0f;
    float yaw   = -90.0f;

    // Vettori direzionali derivati (aggiornati ogni tick dal pitch/yaw)
    glm::vec3 front   = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 up      = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right   = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

    // Parametri input
    float movementSpeed   = 5.0f;
    float mouseSensitivity = 0.1f;
};

// Componente per identificare un'entità con un nome leggibile
struct NameComponent {
    std::string name;
};



// Componente che indica che l'entità è un giocatore controllabile
struct PlayerControllerComponent {
    float walkSpeed = 5.0f;
    float runSpeed  = 8.0f;
    float jumpForce = 5.5f;
    float velocityY = 0.0f;
    bool isGrounded = false;
};

// Componente per la simulazione fisica
struct RigidBodyComponent {
    RigidBody body;
};

// =============================================================================
// COMBAT COMPONENTS
// =============================================================================

struct EquippedWeaponComponent {
    std::string weaponId = "sword";
    float baseDamage = 10.0f;
    float reach = 2.0f; // Lunghezza della lama per Jolt ShapeCast / Raycast
};

enum class CombatState {
    IDLE,
    CHARGING,
    SWINGING,
    PARRYING
};

struct CombatStateComponent {
    CombatState state = CombatState::IDLE;
    float chargeTimer = 0.0f;
    glm::vec3 attackDirection = glm::vec3(0.0f, 0.0f, -1.0f);
    bool isPosterior = false;
    
    // --- Eventi di comunicazione col Physics Engine ---
    bool hasPendingSweep = false;
    float sweepDamage = 0.0f;
    glm::vec3 sweepOrigin = glm::vec3(0.0f);
    glm::vec3 sweepDirection = glm::vec3(0.0f);
    float sweepReach = 0.0f;
};
