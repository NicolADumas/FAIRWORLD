#include "pch.h"
#include "PhysicsEngine.h"
#include "GameWorld.h"
#include "PlanetComponents.h"
#include "MapWorldGenerator.h"
#include <algorithm>
#include <glm/gtx/quaternion.hpp>

void PhysicsEngine::StepSimulation(RigidBody& rb, float dt, const fw::GameWorld& world) {
    // 1. Azzera la forza netta del frame precedente
    rb.netForce = glm::vec3(0.0f);

    // 2. Controlla il tipo di blocco ai piedi e al centro
    // rb.position.y rappresenta i piedi del giocatore
    fw::BlockType feetBlock = world.GetBlock((int)floor(rb.position.x + 0.5f), (int)floor(rb.position.y + 0.1f), (int)floor(rb.position.z + 0.5f));
    fw::BlockType centerBlock = world.GetBlock((int)floor(rb.position.x + 0.5f), (int)floor(rb.position.y + (rb.height * 0.5f)), (int)floor(rb.position.z + 0.5f));
    
    bool isSpherical = false;
    auto planetView = const_cast<fw::GameWorld&>(world).GetRegistry().view<fw::PlanetGeometryComponent>();
    if (!planetView.empty()) isSpherical = true;

    // Se ci troviamo in un chunk non caricato e non siamo su un pianeta sferico di base, freeziamo la fisica!
    if (!isSpherical && (feetBlock == fw::BlockType::OutOfBounds || centerBlock == fw::BlockType::OutOfBounds)) {
        rb.velocity = glm::vec3(0.0f);
        return; // Mettiamo in pausa la caduta finché il chunk non appare
    }

    bool feetInWater = (feetBlock == fw::BlockType::Water);
    bool centerInWater = (centerBlock == fw::BlockType::Water);
    rb.isInWater = feetInWater || centerInWater;

    // 3. Applica forze continue (Gravità e Attrito - Dinamica Cap. 8/9)
    ApplyGravity(rb, world);
    ApplyDrag(rb);

    // [Spazio per galleggiamento Archimede se in acqua]
    if (rb.isInWater) {
        // Approssimiamo volume sommerso in base a quanti punti sono in acqua
        float submergedFraction = feetInWater ? (centerInWater ? 1.0f : 0.5f) : 0.0f;
        float V_sommerso = (rb.radius * 2) * (rb.radius * 2) * rb.height * submergedFraction;
        
        float buoyancyForce = RHO_WATER * V_sommerso * G_EARTH;
        rb.netForce.y += buoyancyForce; // Spinta verso l'alto
        
        // Aumenta l'attrito (Viscosità del fluido)
        // Drag quadratico proporzionale a v^2 per l'acqua
        float vMag = glm::length(rb.velocity);
        if (vMag > 0.0f) {
            rb.netForce -= glm::normalize(rb.velocity) * (vMag * vMag) * 5.0f; // Drag quadratico in acqua
        }
    }

    // 4. Integra il moto (Cinematica Cap. 7 - Verlet/Euler)
    Integrate(rb, dt);

    // 5. Risolvi collisioni spaziali e reazioni vincolari (Cap. 9)
    ResolveCollisions(rb, dt, world);
}

void PhysicsEngine::ApplyGravity(RigidBody& rb, const fw::GameWorld& world) {
    if (rb.isFlying) return; // Niente gravità in volo creativo

    glm::vec3 planetCenter(0.0f, 0.0f, 0.0f);
    float surfaceGravity = 9.81f; // G_EARTH
    float planetRadius = 50.0f;
    bool isSpherical = false;

    // Usiamo const_cast temporaneo perché GetRegistry() non è marcata const in GameWorld
    auto planetView = const_cast<fw::GameWorld&>(world).GetRegistry().view<fw::PlanetGeometryComponent>();
    if (!planetView.empty()) {
        auto entity = *planetView.begin();
        auto& planet = planetView.get<fw::PlanetGeometryComponent>(entity);
        surfaceGravity = 9.81f; // Valore base fisso per ora
        planetRadius = planet.planetRadius;
        isSpherical = planet.isLogicalSphere;
    }

    if (isSpherical) {
        glm::vec3 dirToCenter = planetCenter - rb.position;
        float distSq = glm::dot(dirToCenter, dirToCenter);
        if (distSq > 0.001f) {
            float distance = std::sqrt(distSq);
            glm::vec3 normDir = dirToCenter / distance;
            
            float r_clamped = std::max(distance, planetRadius * 0.1f);
            float ratio = planetRadius / r_clamped;
            float currentG = surfaceGravity * (ratio * ratio);
            
            glm::vec3 gravityForce = normDir * (currentG * rb.mass);
            rb.netForce += gravityForce;
        }
    } else {
        glm::vec3 gravityForce = glm::vec3(0.0f, -surfaceGravity * rb.mass, 0.0f);
        rb.netForce += gravityForce;
    }
}

void PhysicsEngine::ApplyDrag(RigidBody& rb) {
    // F_d = -k * v (Resistenza aerodinamica semplificata lineare)
    glm::vec3 dragForce = -rb.drag * rb.velocity;
    rb.netForce += dragForce;
    
    // Velocità terminale: in caduta libera, F_g = F_drag =>  m*g = k*v_term
    // Quindi v_term ∝ massa: corpi più pesanti cadono più veloce.
    // Formula: v_term = BASE_TERMINAL * sqrt(rb.mass / BASE_MASS)
    // Baseline: 70kg → 55 m/s (~198 km/h, velocità terminale umana reale)
    //           50kg → ~46 m/s  |  100kg → ~65 m/s  |  140kg → ~77 m/s
    constexpr float BASE_TERMINAL = 55.0f;  // [m/s] a 70 kg
    constexpr float BASE_MASS     = 70.0f;  // [kg]  massa di riferimento
    const float massRatio = (rb.mass > 0.0f) ? rb.mass / BASE_MASS : 1.0f;
    const float terminalVel = BASE_TERMINAL * sqrtf(massRatio);

    if (rb.velocity.y < -terminalVel) {
        rb.velocity.y = -terminalVel;
    }
}

void PhysicsEngine::Integrate(RigidBody& rb, float dt) {
    // F = m * a  =>  a = F / m (Seconda legge di Newton)
    rb.acceleration = rb.netForce / rb.mass;

    // Integrazione simplettica (semplice Eulero in questo caso)
    rb.velocity += rb.acceleration * dt;
    // La posizione verrà aggiornata parzialmente in ResolveCollisions per gestire l'AABB
}

void PhysicsEngine::ResolveCollisions(RigidBody& rb, float dt, const fw::GameWorld& world) {
    rb.isGrounded = false;
    rb.isAgainstWall = false;
    
    bool isSpherical = false;
    float planetRadius = 50.0f;
    auto planetView = const_cast<fw::GameWorld&>(world).GetRegistry().view<fw::PlanetGeometryComponent>();
    if (!planetView.empty()) {
        planetRadius = planetView.get<fw::PlanetGeometryComponent>(*planetView.begin()).planetRadius;
        isSpherical = true;
    }

    glm::vec3 voxPos = rb.position;
    glm::quat localRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 localVel = rb.velocity;
    glm::vec3 preCollisionGlobalPos = rb.position;
    
    glm::mat4 planetGlobalMatrix = glm::mat4(1.0f);
    glm::mat4 invPlanetMatrix = glm::mat4(1.0f);
    if (isSpherical && const_cast<fw::GameWorld&>(world).GetRegistry().all_of<fw::TransformComponent>(*planetView.begin())) {
        auto& pt = const_cast<fw::GameWorld&>(world).GetRegistry().get<fw::TransformComponent>(*planetView.begin());
        fw::Mat4 ptMat = pt.computeGlobalMatrix(const_cast<fw::GameWorld&>(world).GetRegistry());
        planetGlobalMatrix = glm::transpose(*reinterpret_cast<glm::mat4*>(&ptMat));
        invPlanetMatrix = glm::inverse(planetGlobalMatrix);
    }
    
    float oldVelY = localVel.y;

    if (isSpherical) {
        // Mappa la posizione globale in locale al pianeta, poi nello spazio continuo dei voxel piatti
        glm::vec3 localPos = glm::vec3(invPlanetMatrix * glm::vec4(rb.position, 1.0f));
        fw::MapWorldGenerator::WorldToVoxelCoord(planetRadius, localPos, voxPos.x, voxPos.y, voxPos.z);
        
        // Calcola una rotazione locale approssimata basata sulla normale per la velocità
        glm::vec3 normal = glm::normalize(localPos);
        localRot = glm::rotation(glm::vec3(0, 1, 0), normal);
        localVel = glm::inverse(localRot) * glm::vec3(invPlanetMatrix * glm::vec4(rb.velocity, 0.0f));
    }

    // Helper per verificare se un blocco è solido
    auto isSolid = [&](int x, int y, int z) {
        if (y < 0 || y >= 128) return false;
        
        int flatX = x;
        int flatZ = z;

        fw::BlockType b;
        if (isSpherical) {
            b = world.GetBlockFlat(flatX, y, flatZ);
        } else {
            b = world.GetBlock(flatX, y, flatZ);
        }
        
        if (b == fw::BlockType::OutOfBounds) {
            if (isSpherical) {
                return (y < 25); // Il pianeta matematico è solido sotto Y=25 (superficie base)
            } else {
                return true; // Mondo piatto: blocca in aria se non caricato
            }
        }
        
        return (b != fw::BlockType::Air && b != fw::BlockType::Water && 
                b != fw::BlockType::Lava && b != fw::BlockType::StargatePortal);
    };

    // Stargate Trigger check
    {
        float epsilon = 0.01f;
        int minX = (int)floor(rb.position.x - rb.radius + epsilon);
        int maxX = (int)floor(rb.position.x + rb.radius - epsilon);
        int minY = (int)floor(rb.position.y + epsilon);
        int maxY = (int)floor(rb.position.y + rb.height - epsilon);
        int minZ = (int)floor(rb.position.z - rb.radius + epsilon);
        int maxZ = (int)floor(rb.position.z + rb.radius - epsilon);

        for (int x = minX; x <= maxX; x++) {
            for (int y = minY; y <= maxY; y++) {
                for (int z = minZ; z <= maxZ; z++) {
                    if (world.GetBlock(x, y, z) == fw::BlockType::StargatePortal) {
                        rb.touchedStargate = true;
                    }
                }
            }
        }
    }

    struct AABB {
        float minX, minY, minZ;
        float maxX, maxY, maxZ;
    };

    float eps = 0.01f;
    auto getAABB = [&]() -> AABB {
        return {
            voxPos.x - rb.radius + eps,
            voxPos.y + eps,
            voxPos.z - rb.radius + eps,
            voxPos.x + rb.radius - eps,
            voxPos.y + rb.height - eps,
            voxPos.z + rb.radius - eps
        };
    };

    // DEPENETRATION SOLVER
    bool anyPenetration = true;
    int maxIters = 4;
    while (anyPenetration && maxIters-- > 0) {
        anyPenetration = false;
        AABB box = getAABB();
        float margin = 0.05f;
        int minX = (int)floor(box.minX + margin);
        int maxX = (int)floor(box.maxX - margin);
        int minY = (int)floor(box.minY + margin);
        int maxY = (int)floor(box.maxY - margin);
        int minZ = (int)floor(box.minZ + margin);
        int maxZ = (int)floor(box.maxZ - margin);
        
        for (int x = minX; x <= maxX; x++) {
            for (int y = minY; y <= maxY; y++) {
                for (int z = minZ; z <= maxZ; z++) {
                    if (isSolid(x, y, z)) {
                        anyPenetration = true;
                        
                        float pushUp    = (y + 1.0f) - box.minY;
                        float pushDown  = box.maxY - y;
                        float pushRight = (x + 1.0f) - box.minX;
                        float pushLeft  = box.maxX - x;
                        float pushFront = (z + 1.0f) - box.minZ;
                        float pushBack  = box.maxZ - z;

                        float minPush = pushUp;
                        if (pushDown < minPush) minPush = pushDown;
                        if (pushRight < minPush) minPush = pushRight;
                        if (pushLeft < minPush) minPush = pushLeft;
                        if (pushFront < minPush) minPush = pushFront;
                        if (pushBack < minPush) minPush = pushBack;
                        
                        if (minPush == pushUp) { voxPos.y += pushUp + eps; localVel.y = std::max(localVel.y, 0.0f); rb.isGrounded = true; }
                        else if (minPush == pushDown) { voxPos.y -= pushDown + eps; localVel.y = std::min(localVel.y, 0.0f); }
                        else if (minPush == pushRight) { voxPos.x += pushRight + eps; localVel.x = 0.0f; rb.isAgainstWall = true; }
                        else if (minPush == pushLeft) { voxPos.x -= pushLeft + eps; localVel.x = 0.0f; rb.isAgainstWall = true; }
                        else if (minPush == pushFront) { voxPos.z += pushFront + eps; localVel.z = 0.0f; rb.isAgainstWall = true; }
                        else if (minPush == pushBack) { voxPos.z -= pushBack + eps; localVel.z = 0.0f; rb.isAgainstWall = true; }
                        
                        break; 
                    }
                }
                if (anyPenetration) break; 
            }
            if (anyPenetration) break; 
        }
    }

    AABB box = getAABB(); 
    
    float dx = localVel.x * dt;
    float dy = localVel.y * dt;
    float dz = localVel.z * dt;
    
    float moveX = dx;
    float moveY = dy;
    float moveZ = dz;
    
    bool hasSteppedUp = false;

    // 1. Spostamento Y (Gravità / Salto)
    if (dy != 0.0f) {
        int minX = (int)floor(box.minX + 0.01f);
        int maxX = (int)floor(box.maxX - 0.01f);
        int minZ = (int)floor(box.minZ + 0.01f);
        int maxZ = (int)floor(box.maxZ - 0.01f);

        if (dy > 0.0f) {
            int minY = (int)floor(box.maxY);
            int testMaxY = (int)floor(box.maxY + dy);
            for (int x = minX; x <= maxX; x++) {
                for (int z = minZ; z <= maxZ; z++) {
                    for (int y = minY; y <= testMaxY; y++) {
                        if (isSolid(x, y, z)) {
                            float allowed = (float)y - box.maxY;
                            if (allowed < moveY) moveY = allowed;
                        }
                    }
                }
            }
        } else {
            int maxY = (int)floor(box.minY);
            int testMinY = (int)floor(box.minY + dy);
            for (int x = minX; x <= maxX; x++) {
                for (int z = minZ; z <= maxZ; z++) {
                    for (int y = maxY; y >= testMinY; y--) {
                        if (isSolid(x, y, z)) {
                            float allowed = (float)(y + 1) - box.minY;
                            if (allowed > moveY) moveY = allowed;
                        }
                    }
                }
            }
        }
        
        box.minY += moveY;
        box.maxY += moveY;
        voxPos.y += moveY;

        if (moveY != dy) {
            if (dy < 0.0f) rb.isGrounded = true;
        }
    }

    // 2. Spostamento X
    if (dx != 0.0f) {
        int minY = (int)floor(box.minY + 0.01f);
        int maxY = (int)floor(box.maxY - 0.01f);
        int minZ = (int)floor(box.minZ + 0.01f);
        int maxZ = (int)floor(box.maxZ - 0.01f);

        if (dx > 0.0f) {
            int minX = (int)floor(box.maxX);
            int testMaxX = (int)floor(box.maxX + dx);
            for (int y = minY; y <= maxY; y++) {
                for (int z = minZ; z <= maxZ; z++) {
                    for (int x = minX; x <= testMaxX; x++) {
                        if (isSolid(x, y, z)) {
                            float allowed = (float)x - box.maxX;
                            if (allowed < moveX) moveX = allowed;
                        }
                    }
                }
            }
        } else {
            int maxX = (int)floor(box.minX);
            int testMinX = (int)floor(box.minX + dx);
            for (int y = minY; y <= maxY; y++) {
                for (int z = minZ; z <= maxZ; z++) {
                    for (int x = maxX; x >= testMinX; x--) {
                        if (isSolid(x, y, z)) {
                            float allowed = (float)(x + 1) - box.minX;
                            if (allowed > moveX) moveX = allowed;
                        }
                    }
                }
            }
        }

        if (moveX != dx) {
            rb.isAgainstWall = true;
        }

        if (moveX != dx && rb.isGrounded) {
            const float MAX_STEP_HEIGHT = 0.6f;
            float stepUpAmount = MAX_STEP_HEIGHT;
            int minY_Y = (int)floor(box.maxY);
            int testMaxY_Y = (int)floor(box.maxY + stepUpAmount);
            for (int x = (int)floor(box.minX); x <= (int)floor(box.maxX); x++) {
                for (int z = (int)floor(box.minZ); z <= (int)floor(box.maxZ); z++) {
                    for (int y = minY_Y; y <= testMaxY_Y; y++) {
                        if (isSolid(x, y, z)) {
                            float allowed = (float)y - box.maxY;
                            if (allowed < stepUpAmount) stepUpAmount = allowed;
                        }
                    }
                }
            }
            
            if (stepUpAmount > 0.01f) {
                AABB liftedBox = box;
                liftedBox.minY += stepUpAmount;
                liftedBox.maxY += stepUpAmount;
                float liftedMoveX = dx;
                int lMinY = (int)floor(liftedBox.minY);
                int lMaxY = (int)floor(liftedBox.maxY);
                
                if (dx > 0.0f) {
                    int minX = (int)floor(liftedBox.maxX);
                    int testMaxX = (int)floor(liftedBox.maxX + dx);
                    for (int y = lMinY; y <= lMaxY; y++) {
                        for (int z = minZ; z <= maxZ; z++) {
                            for (int x = minX; x <= testMaxX; x++) {
                                if (isSolid(x, y, z)) {
                                    float allowed = (float)x - liftedBox.maxX;
                                    if (allowed < liftedMoveX) liftedMoveX = allowed;
                                }
                            }
                        }
                    }
                } else {
                    int maxX = (int)floor(liftedBox.minX);
                    int testMinX = (int)floor(liftedBox.minX + dx);
                    for (int y = lMinY; y <= lMaxY; y++) {
                        for (int z = minZ; z <= maxZ; z++) {
                            for (int x = maxX; x >= testMinX; x--) {
                                if (isSolid(x, y, z)) {
                                    float allowed = (float)(x + 1) - liftedBox.minX;
                                    if (allowed > liftedMoveX) liftedMoveX = allowed;
                                }
                            }
                        }
                    }
                }
                
                if (abs(liftedMoveX) > abs(moveX) + 0.001f) {
                    moveX = liftedMoveX;
                    float moveDown = -stepUpAmount;
                    int dMinX = (int)floor(liftedBox.minX);
                    int dMaxX = (int)floor(liftedBox.maxX);
                    int dMinZ = (int)floor(liftedBox.minZ);
                    int dMaxZ = (int)floor(liftedBox.maxZ);
                    int dMaxY = (int)floor(liftedBox.minY);
                    int testMinY = (int)floor(liftedBox.minY + moveDown);
                    
                    for (int x = dMinX; x <= dMaxX; x++) {
                        for (int z = dMinZ; z <= dMaxZ; z++) {
                            for (int y = dMaxY; y >= testMinY; y--) {
                                if (isSolid(x, y, z)) {
                                    float allowed = (float)(y + 1) - liftedBox.minY;
                                    if (allowed > moveDown) moveDown = allowed;
                                }
                            }
                        }
                    }
                    float finalLift = stepUpAmount + moveDown;
                    moveY += finalLift;
                    box.minY += finalLift;
                    box.maxY += finalLift;
                    voxPos.y += finalLift;
                    hasSteppedUp = true;
                }
            }
        }
        box.minX += moveX;
        box.maxX += moveX;
        voxPos.x += moveX;
    }

    // 3. Spostamento Z
    if (dz != 0.0f) {
        int minX = (int)floor(box.minX + 0.01f);
        int maxX = (int)floor(box.maxX - 0.01f);
        int minY = (int)floor(box.minY + 0.01f);
        int maxY = (int)floor(box.maxY - 0.01f);

        if (dz > 0.0f) {
            int minZ = (int)floor(box.maxZ);
            int testMaxZ = (int)floor(box.maxZ + dz);
            for (int y = minY; y <= maxY; y++) {
                for (int x = minX; x <= maxX; x++) {
                    for (int z = minZ; z <= testMaxZ; z++) {
                        if (isSolid(x, y, z)) {
                            float allowed = (float)z - box.maxZ;
                            if (allowed < moveZ) moveZ = allowed;
                        }
                    }
                }
            }
        } else {
            int maxZ = (int)floor(box.minZ);
            int testMinZ = (int)floor(box.minZ + dz);
            for (int y = minY; y <= maxY; y++) {
                for (int x = minX; x <= maxX; x++) {
                    for (int z = maxZ; z >= testMinZ; z--) {
                        if (isSolid(x, y, z)) {
                            float allowed = (float)(z + 1) - box.minZ;
                            if (allowed > moveZ) moveZ = allowed;
                        }
                    }
                }
            }
        }

        if (moveZ != dz) {
            rb.isAgainstWall = true;
        }

        if (moveZ != dz && rb.isGrounded && !hasSteppedUp) {
            const float MAX_STEP_HEIGHT = 0.6f;
            float stepUpAmount = MAX_STEP_HEIGHT;
            int minY_Y = (int)floor(box.maxY);
            int testMaxY_Y = (int)floor(box.maxY + stepUpAmount);
            for (int x = (int)floor(box.minX); x <= (int)floor(box.maxX); x++) {
                for (int z = (int)floor(box.minZ); z <= (int)floor(box.maxZ); z++) {
                    for (int y = minY_Y; y <= testMaxY_Y; y++) {
                        if (isSolid(x, y, z)) {
                            float allowed = (float)y - box.maxY;
                            if (allowed < stepUpAmount) stepUpAmount = allowed;
                        }
                    }
                }
            }
            
            if (stepUpAmount > 0.01f) {
                AABB liftedBox = box;
                liftedBox.minY += stepUpAmount;
                liftedBox.maxY += stepUpAmount;
                float liftedMoveZ = dz;
                int lMinY = (int)floor(liftedBox.minY);
                int lMaxY = (int)floor(liftedBox.maxY);
                
                if (dz > 0.0f) {
                    int minZ = (int)floor(liftedBox.maxZ);
                    int testMaxZ = (int)floor(liftedBox.maxZ + dz);
                    for (int y = lMinY; y <= lMaxY; y++) {
                        for (int x = minX; x <= maxX; x++) {
                            for (int z = minZ; z <= testMaxZ; z++) {
                                if (isSolid(x, y, z)) {
                                    float allowed = (float)z - liftedBox.maxZ;
                                    if (allowed < liftedMoveZ) liftedMoveZ = allowed;
                                }
                            }
                        }
                    }
                } else {
                    int maxZ = (int)floor(liftedBox.minZ);
                    int testMinZ = (int)floor(liftedBox.minZ + dz);
                    for (int y = lMinY; y <= lMaxY; y++) {
                        for (int x = minX; x <= maxX; x++) {
                            for (int z = maxZ; z >= testMinZ; z--) {
                                if (isSolid(x, y, z)) {
                                    float allowed = (float)(z + 1) - liftedBox.minZ;
                                    if (allowed > liftedMoveZ) liftedMoveZ = allowed;
                                }
                            }
                        }
                    }
                }
                
                if (abs(liftedMoveZ) > abs(moveZ) + 0.001f) {
                    moveZ = liftedMoveZ;
                    float moveDown = -stepUpAmount;
                    int dMinX = (int)floor(liftedBox.minX);
                    int dMaxX = (int)floor(liftedBox.maxX);
                    int dMinZ = (int)floor(liftedBox.minZ);
                    int dMaxZ = (int)floor(liftedBox.maxZ);
                    int dMaxY = (int)floor(liftedBox.minY);
                    int testMinY = (int)floor(liftedBox.minY + moveDown);
                    
                    for (int x = dMinX; x <= dMaxX; x++) {
                        for (int z = dMinZ; z <= dMaxZ; z++) {
                            for (int y = dMaxY; y >= testMinY; y--) {
                                if (isSolid(x, y, z)) {
                                    float allowed = (float)(y + 1) - liftedBox.minY;
                                    if (allowed > moveDown) moveDown = allowed;
                                }
                            }
                        }
                    }
                    float finalLift = stepUpAmount + moveDown;
                    moveY += finalLift;
                    box.minY += finalLift;
                    box.maxY += finalLift;
                    voxPos.y += finalLift;
                }
            }
        }
        box.minZ += moveZ;
        box.maxZ += moveZ;
        voxPos.z += moveZ;
    }

    // Ritorna in coordinate globali
    if (isSpherical) {
        int gcx = (int)std::floor(voxPos.x / 16.0f);
        int gcz = (int)std::floor(voxPos.z / 16.0f);
        float local_x = voxPos.x - (gcx * 16.0f);
        float local_z = voxPos.z - (gcz * 16.0f);
        glm::vec3 localSpherePos;
        fw::MapWorldGenerator::GetTrueSphericalPosition(planetRadius, gcx, gcz, local_x, voxPos.y, local_z, localSpherePos);
        rb.position = glm::vec3(planetGlobalMatrix * glm::vec4(localSpherePos, 1.0f));
    } else {
        rb.position = voxPos;
    }

    // Ricalcolo velocità effettiva (Post-Collisione)
    if (dt > 0.0f) {
        glm::vec3 actualGlobalVelocity = (rb.position - preCollisionGlobalPos) / dt;
        glm::vec3 actualLocalVelocity = actualGlobalVelocity;
        
        if (isSpherical) {
            glm::vec3 localCurrentPos = glm::vec3(invPlanetMatrix * glm::vec4(rb.position, 1.0f));
            glm::vec3 newNormal = glm::normalize(localCurrentPos);
            glm::quat newLocalRot = glm::rotation(glm::vec3(0, 1, 0), newNormal);
            actualLocalVelocity = glm::inverse(newLocalRot) * glm::vec3(invPlanetMatrix * glm::vec4(actualGlobalVelocity, 0.0f));
        }
        
        if (moveX != dx) actualLocalVelocity.x = -localVel.x * rb.restitution;
        if (moveY != dy) actualLocalVelocity.y = -localVel.y * rb.restitution;
        if (moveZ != dz) actualLocalVelocity.z = -localVel.z * rb.restitution;
        
        localVel = actualLocalVelocity;
    }

    if (isSpherical) {
        glm::vec3 localCurrentPos = glm::vec3(invPlanetMatrix * glm::vec4(rb.position, 1.0f));
        glm::vec3 finalNormal = glm::normalize(localCurrentPos);
        glm::quat finalRot = glm::rotation(glm::vec3(0, 1, 0), finalNormal);
        glm::vec3 localPlanetVel = finalRot * localVel;
        rb.velocity = glm::vec3(planetGlobalMatrix * glm::vec4(localPlanetVel, 0.0f));
    } else {
        rb.velocity = localVel;
    }

    // Danno da caduta (Energia Cinetica persa improvvisamente)
    // Cadute più basse di ~3m non causano danni
    if (rb.isGrounded && oldVelY < -7.7f) {
        float deltaV = std::abs(oldVelY - localVel.y);
        float damage = ComputeFallDamage(deltaV, rb.mass);
        if (damage > 0.0f) {
            PhysicsEvent ev;
            ev.type = PhysicsEvent::Type::FallDamage;
            ev.value = damage;
            rb.pendingEvents.push_back(ev);
        }
    }
}

float PhysicsEngine::ComputeFallDamage(float deltaV, float mass) {
    // E_cin_persa = 0.5 * m * v^2  (v_finale ~= 0 dopo l'impatto)
    // Soglia biologica: impatti < 7.7 m/s (h < 3m) sono assorbiti dalle ginocchia
    if (deltaV < 7.7f) return 0.0f;

    float kineticEnergyLost = 0.5f * mass * (deltaV * deltaV);

    // Scala J -> HP: calibrata su un giocatore da 70kg
    //   caduta  3m  (~7.7 m/s)  =>  E ~2075 J  => ~8  HP
    //   caduta 10m  (~14 m/s)   =>  E ~6860 J  => ~27 HP
    //   caduta 20m  (~19.8 m/s) =>  E ~13720 J => ~55 HP
    //   velocità terminale 55m/s => danni letali garantiti
    float damage = kineticEnergyLost * 0.004f;
    return damage;
}

// Funzione helper per controllare l'intersezione tra due scatole in movimento (Swept AABB)
bool SweptAABB(const AABB& b1, const glm::vec3& v, const AABB& b2, float& outFraction, glm::vec3& outNormal) {
    float txEntry, tyEntry, tzEntry;
    float txExit, tyExit, tzExit;

    if (v.x == 0.0f) {
        txEntry = -std::numeric_limits<float>::infinity();
        txExit = std::numeric_limits<float>::infinity();
    } else {
        txEntry = (v.x > 0) ? (b2.min.x - b1.max.x) / v.x : (b2.max.x - b1.min.x) / v.x;
        txExit  = (v.x > 0) ? (b2.max.x - b1.min.x) / v.x : (b2.min.x - b1.max.x) / v.x;
    }

    if (v.y == 0.0f) {
        tyEntry = -std::numeric_limits<float>::infinity();
        tyExit = std::numeric_limits<float>::infinity();
    } else {
        tyEntry = (v.y > 0) ? (b2.min.y - b1.max.y) / v.y : (b2.max.y - b1.min.y) / v.y;
        tyExit  = (v.y > 0) ? (b2.max.y - b1.min.y) / v.y : (b2.min.y - b1.max.y) / v.y;
    }

    if (v.z == 0.0f) {
        tzEntry = -std::numeric_limits<float>::infinity();
        tzExit = std::numeric_limits<float>::infinity();
    } else {
        tzEntry = (v.z > 0) ? (b2.min.z - b1.max.z) / v.z : (b2.max.z - b1.min.z) / v.z;
        tzExit  = (v.z > 0) ? (b2.max.z - b1.min.z) / v.z : (b2.min.z - b1.max.z) / v.z;
    }

    float entryTime = std::max({txEntry, tyEntry, tzEntry});
    float exitTime = std::min({txExit, tyExit, tzExit});

    if (entryTime > exitTime || (txEntry < 0.0f && tyEntry < 0.0f && tzEntry < 0.0f) || entryTime < -0.01f || entryTime > 1.0f) {
        return false;
    }

    // Clamp per sicurezza per evitare rimbalzi inversi
    if (entryTime < 0.0f) entryTime = 0.0f;

    outFraction = entryTime;

    if (entryTime == txEntry) {
        outNormal = glm::vec3((v.x > 0.0f) ? -1.0f : 1.0f, 0.0f, 0.0f);
    } else if (entryTime == tyEntry) {
        outNormal = glm::vec3(0.0f, (v.y > 0.0f) ? -1.0f : 1.0f, 0.0f);
    } else {
        outNormal = glm::vec3(0.0f, 0.0f, (v.z > 0.0f) ? -1.0f : 1.0f);
    }

    return true;
}

bool PhysicsEngine::SweepTest(const AABB& playerBounds, const glm::vec3& movement, RaycastHit& outHit, const fw::GameWorld& world) {
    outHit.hit = false;
    outHit.fraction = 1.0f;
    
    AABB expandedBounds;
    expandedBounds.min = playerBounds.min + glm::min(glm::vec3(0.0f), movement);
    expandedBounds.max = playerBounds.max + glm::max(glm::vec3(0.0f), movement);

    int minX = static_cast<int>(std::floor(expandedBounds.min.x));
    int maxX = static_cast<int>(std::ceil(expandedBounds.max.x));
    int minY = static_cast<int>(std::floor(expandedBounds.min.y));
    int maxY = static_cast<int>(std::ceil(expandedBounds.max.y));
    int minZ = static_cast<int>(std::floor(expandedBounds.min.z));
    int maxZ = static_cast<int>(std::ceil(expandedBounds.max.z));

    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                fw::BlockType b = world.GetBlock(x, y, z);
                if (b == fw::BlockType::Air || b == fw::BlockType::Water || b == fw::BlockType::Lava || b == fw::BlockType::StargatePortal) {
                    continue; 
                }

                AABB voxelBounds;
                voxelBounds.min = glm::vec3(x, y, z);
                voxelBounds.max = glm::vec3(x + 1, y + 1, z + 1);

                float hitFraction;
                glm::vec3 hitNormal;

                if (SweptAABB(playerBounds, movement, voxelBounds, hitFraction, hitNormal)) {
                    if (hitFraction < outHit.fraction) {
                        outHit.hit = true;
                        outHit.fraction = hitFraction;
                        outHit.normal = hitNormal;
                    }
                }
            }
        }
    }

    return outHit.hit;
}
