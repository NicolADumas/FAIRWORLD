#pragma once
#include <glm/glm.hpp>

// Forward declaration
namespace fw { class ForgeWorld; }

struct RigidBody {
    glm::vec3 position     = glm::vec3(0.0f); // r(t)
    glm::vec3 velocity     = glm::vec3(0.0f); // v(t)
    glm::vec3 acceleration = glm::vec3(0.0f); // a(t)
    glm::vec3 netForce     = glm::vec3(0.0f); // ΣF(t)
    
    float mass        = 70.0f; // [kg] default (es. peso player)
    float restitution = 0.0f;  // Elasticità dell'urto (0 = perfettamente anelastico)
    float drag        = 0.5f;  // Coefficiente di attrito viscoso dell'aria

    bool isGrounded   = false;
    bool isInWater    = false;
    bool touchedStargate = false;
    
    // Dimensioni AABB (Assiale-Aligned Bounding Box) per le collisioni
    float radius = 0.3f;
    float height = 1.8f;
    float eyeOffset = 1.6f;
};

class PhysicsEngine {
public:
    static constexpr float G_EARTH   = 9.81f;    // [m/s^2]
    static constexpr float RHO_WATER = 1000.0f;  // [kg/m^3]

    // Integra il moto e risolve collisioni nel timestep dt
    void StepSimulation(RigidBody& rb, float dt, const fw::ForgeWorld& world);

    // Calcola il danno da caduta basato sulla variazione istantanea di velocità all'impatto (Cap. 12/13)
    float ComputeFallDamage(float deltaV, float mass);

private:
    // Applica forza peso: F = m * g (Cap. 9)
    void ApplyGravity(RigidBody& rb, const fw::ForgeWorld& world);

    // Applica attrito viscoso: F = -k * v (Cap. 9)
    void ApplyDrag(RigidBody& rb);

    // Risolve collisioni AABB contro i blocchi Voxel (Cap. 9 - reazioni vincolari normali)
    void ResolveCollisions(RigidBody& rb, float dt, const fw::ForgeWorld& world);

    // Integrazione Numerica (Metodo di Eulero/Verlet) (Cap. 7)
    void Integrate(RigidBody& rb, float dt);
};
