#include "pch.h"
#include "PhysicsEngine.h"
#include "World.h"
#include <algorithm>

void PhysicsEngine::StepSimulation(RigidBody& rb, float dt, const World& world) {
    // 1. Azzera la forza netta del frame precedente
    rb.netForce = glm::vec3(0.0f);

    // 2. Controlla se è in acqua (Fluidodinamica Cap. 18)
    BlockType centerBlock = world.GetBlock((int)floor(rb.position.x), (int)floor(rb.position.y - rb.radius), (int)floor(rb.position.z));
    rb.isInWater = (centerBlock == BlockType::Water);

    // 3. Applica forze continue (Gravità e Attrito - Dinamica Cap. 8/9)
    ApplyGravity(rb, world);
    ApplyDrag(rb);

    // [Spazio per galleggiamento Archimede se in acqua]
    if (rb.isInWater) {
        // Approssimiamo volume sommerso
        float V_sommerso = (rb.radius * 2) * (rb.radius * 2) * (rb.height * 0.5f);
        float buoyancyForce = RHO_WATER * V_sommerso * G_EARTH;
        rb.netForce.y += buoyancyForce; // Spinta verso l'alto
        
        // Aumenta l'attrito (Viscosità del fluido)
        rb.netForce -= rb.velocity * 50.0f; 
    }

    // 4. Integra il moto (Cinematica Cap. 7 - Verlet/Euler)
    float oldVelY = rb.velocity.y; // Salva per danno da caduta
    Integrate(rb, dt);

    // 5. Risolvi collisioni spaziali e reazioni vincolari (Cap. 9)
    ResolveCollisions(rb, dt, world);

    // 6. Danno da caduta (Energia Cinetica persa improvvisamente)
    if (rb.isGrounded && oldVelY < -10.0f) {
        float deltaV = abs(oldVelY - rb.velocity.y);
        // Il Game Loop chiamerà ComputeFallDamage usando questo delta
        // ma per ora gestiamo le variabili.
    }
}

void PhysicsEngine::ApplyGravity(RigidBody& rb, const World& world) {
    // F_g = m * g (verso il basso) usando la gravità del pianeta
    float g = world.GetCurrentPlanet()->gravity;
    glm::vec3 gravityForce = glm::vec3(0.0f, -g * rb.mass, 0.0f);
    rb.netForce += gravityForce;
}

void PhysicsEngine::ApplyDrag(RigidBody& rb) {
    // F_d = -k * v (Resistenza aerodinamica semplificata lineare)
    glm::vec3 dragForce = -rb.drag * rb.velocity;
    rb.netForce += dragForce;
}

void PhysicsEngine::Integrate(RigidBody& rb, float dt) {
    // F = m * a  =>  a = F / m (Seconda legge di Newton)
    rb.acceleration = rb.netForce / rb.mass;

    // Integrazione simplettica (semplice Eulero in questo caso)
    rb.velocity += rb.acceleration * dt;
    // La posizione verrà aggiornata parzialmente in ResolveCollisions per gestire l'AABB
}

void PhysicsEngine::ResolveCollisions(RigidBody& rb, float dt, const World& world) {
    // Separiamo il movimento per assi per simulare lo "slittamento" sui muri
    
    // Funzione helper: Controlla se la bounding box centrata in testPos interseca blocchi solidi
    auto checkCollision = [&](glm::vec3 testPos) {
        float minX = testPos.x - rb.radius;
        float maxX = testPos.x + rb.radius;
        float minY = testPos.y - rb.eyeOffset;
        float maxY = testPos.y + (rb.height - rb.eyeOffset);
        float minZ = testPos.z - rb.radius;
        float maxZ = testPos.z + rb.radius;

        for (int x = (int)floor(minX); x <= (int)floor(maxX); x++) {
            for (int y = (int)floor(minY); y <= (int)floor(maxY); y++) {
                for (int z = (int)floor(minZ); z <= (int)floor(maxZ); z++) {
                    BlockType b = world.GetBlock(x, y, z);
                    // Consideriamo solido tutto ciò che non è aria o acqua/lava o portale
                    if (b != BlockType::Air && b != BlockType::Water && b != BlockType::Lava && b != BlockType::StargatePortal) {
                        return true;
                    }
                }
            }
        }
        return false;
    };

    // 1. Controlla i trigger (Stargate, Acqua, ecc.) in modo indipendente dalla collisione solida!
    {
        float minX = rb.position.x - rb.radius;
        float maxX = rb.position.x + rb.radius;
        float minY = rb.position.y - rb.eyeOffset;
        float maxY = rb.position.y + (rb.height - rb.eyeOffset);
        float minZ = rb.position.z - rb.radius;
        float maxZ = rb.position.z + rb.radius;
        
        for (int x = (int)floor(minX); x <= (int)floor(maxX); x++) {
            for (int y = (int)floor(minY); y <= (int)floor(maxY); y++) {
                for (int z = (int)floor(minZ); z <= (int)floor(maxZ); z++) {
                    if (world.GetBlock(x, y, z) == BlockType::StargatePortal) {
                        rb.touchedStargate = true;
                    }
                }
            }
        }
    }

    // La "next position" se non ci fossero ostacoli
    // Notare: Il movement input orizzontale (WASD) viene applicato esternamente manipolando la velocità
    
    // Asse X
    glm::vec3 nextPosX = rb.position;
    nextPosX.x += rb.velocity.x * dt; 
    
    // Asse Y
    glm::vec3 nextPosY = rb.position;
    nextPosY.y += rb.velocity.y * dt;

    // Asse Z
    glm::vec3 nextPosZ = rb.position;
    nextPosZ.z += rb.velocity.z * dt;

    // Risoluzione iterativa
    rb.isGrounded = false;

    // Y Axis (Reazione vincolare normale, pavimento/soffitto)
    glm::vec3 testPos = rb.position;
    testPos.y += rb.velocity.y * dt;
    if (!checkCollision(testPos)) {
        rb.position.y = testPos.y;
    } else {
        if (rb.velocity.y < 0.0f) {
            rb.isGrounded = true; // Ha toccato il suolo
        }
        // Urti anelastici (restitution = 0) azzerano la velocità sull'asse
        rb.velocity.y *= -rb.restitution;
    }

    // X Axis
    testPos = rb.position;
    testPos.x += rb.velocity.x * dt;
    if (!checkCollision(testPos)) {
        rb.position.x = testPos.x;
    } else {
        rb.velocity.x *= -rb.restitution;
    }

    // Z Axis
    testPos = rb.position;
    testPos.z += rb.velocity.z * dt;
    if (!checkCollision(testPos)) {
        rb.position.z = testPos.z;
    } else {
        rb.velocity.z *= -rb.restitution;
    }
}

float PhysicsEngine::ComputeFallDamage(float deltaV, float mass) {
    // E_cin_persa = 0.5 * m * (v_in^2 - v_fin^2)
    // Semplificando, se sbatti a velocità > 10 m/s:
    if (deltaV < 10.0f) return 0.0f;
    
    float kineticEnergyLost = 0.5f * mass * (deltaV * deltaV);
    // Scaliamo l'energia in "Punti Ferita"
    // Esempio: 70kg a 10m/s -> 3500 Joules = 5 danni
    float damage = kineticEnergyLost * 0.001f; 
    return damage;
}
