#include "pch.h"
#include "PhysicsEngine.h"
#include "GameWorld.h"
#include "PlanetComponents.h"
#include "MapWorldGenerator.h"
#include "PlanetRadialMapping.h"
#include "PlanetaryCameraFrame.h"
#include "PlanetaryMovementMath.h"
#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <glm/gtx/quaternion.hpp>

void PhysicsEngine::StepSimulation(RigidBody& rb, float dt, const fw::GameWorld& world,
                                   const glm::vec3* preferredPlanetaryForward) {
    // 1. Azzera la forza netta del frame precedente
    rb.netForce = glm::vec3(0.0f);

    // 2. Controlla il tipo di blocco ai piedi e al centro
    // rb.position.y rappresenta i piedi del giocatore
    fw::BlockType feetBlock = world.GetBlock((int)floor(rb.position.x + 0.5f), (int)floor(rb.position.y + 0.1f), (int)floor(rb.position.z + 0.5f));
    fw::BlockType centerBlock = world.GetBlock((int)floor(rb.position.x + 0.5f), (int)floor(rb.position.y + (rb.height * 0.5f)), (int)floor(rb.position.z + 0.5f));
    
    bool isSpherical = false;
    auto planetView = const_cast<fw::GameWorld&>(world).GetRegistry().view<fw::PlanetGeometryComponent>();
    if (!planetView.empty()) {
        isSpherical = planetView.get<fw::PlanetGeometryComponent>(*planetView.begin()).isLogicalSphere;
    }

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
    ApplyDrag(rb, world);

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
    ResolveCollisions(rb, dt, world, preferredPlanetaryForward);
}

void PhysicsEngine::ApplyGravity(RigidBody& rb, const fw::GameWorld& world) {
    if (rb.isFlying) return; // Niente gravità in volo creativo

    glm::vec3 planetCenter(0.0f, 0.0f, 0.0f);
    float surfaceGravity = 9.81f; // G_EARTH
    fw::PlanetSize pSize = fw::PlanetSize::Medium;
    bool isSpherical = false;

    // Usiamo const_cast temporaneo perché GetRegistry() non è marcata const in GameWorld
    auto planetView = const_cast<fw::GameWorld&>(world).GetRegistry().view<fw::PlanetGeometryComponent>();
    if (!planetView.empty()) {
        auto entity = *planetView.begin();
        auto& planet = planetView.get<fw::PlanetGeometryComponent>(entity);
        surfaceGravity = 9.81f; // Valore base fisso per ora
        pSize = planet.planetSize;
        isSpherical = planet.isLogicalSphere;
    }

    if (isSpherical) {
        glm::vec3 dirToCenter = planetCenter - rb.position;
        float distSq = glm::dot(dirToCenter, dirToCenter);
        if (distSq > 0.001f) {
            float distance = std::sqrt(distSq);
            glm::vec3 normDir = dirToCenter / distance;
            
            float R = fw::PlanetMath::GetPlanetRadius(pSize);
            float r_clamped = std::max(distance, R * 0.1f);
            float ratio = R / r_clamped;
            float currentG = surfaceGravity * (ratio * ratio);

            glm::vec3 gravityAcceleration = normDir * currentG;
            rb.dbg_gravityAccel = gravityAcceleration;
            
            glm::vec3 gravityForce = gravityAcceleration * rb.mass;
            rb.netForce += gravityForce;
        }
    } else {
        glm::vec3 gravityForce = glm::vec3(0.0f, -surfaceGravity * rb.mass, 0.0f);
        rb.dbg_gravityAccel = glm::vec3(0.0f, -surfaceGravity, 0.0f);
        rb.netForce += gravityForce;
    }
}

void PhysicsEngine::ApplyDrag(RigidBody& rb, const fw::GameWorld& world) {
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

    bool spherical = false;
    auto planetView = const_cast<fw::GameWorld&>(world).GetRegistry().view<fw::PlanetGeometryComponent>();
    if (!planetView.empty()) {
        const auto& geometry = planetView.get<fw::PlanetGeometryComponent>(*planetView.begin());
        spherical = geometry.isLogicalSphere;
    }

    if (spherical) {
        glm::vec3 up;
        if (fw::PlanetaryCameraFrame::Normalize(rb.position, up)) {
            rb.velocity = fw::PlanetaryMovementMath::ClampInwardRadialSpeed(rb.velocity, up, terminalVel);
        }
    } else if (rb.velocity.y < -terminalVel) {
        // Preserve the legacy Cartesian-world terminal speed rule.
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

void PhysicsEngine::ResolveCollisions(RigidBody& rb, float dt, const fw::GameWorld& world,
                                      const glm::vec3* preferredPlanetaryForward) {
    rb.isGrounded = false;
    rb.isAgainstWall = false;
    rb.dbg_voxelContacts = 0;
    rb.dbg_collisionProbeCount = 0;
    rb.dbg_collisionQueryNanoseconds = 0;
    rb.dbg_initialRecoveryProbeCount = 0;
    rb.dbg_contactRefinementProbeCount = 0;
    rb.dbg_airSamples = 0;
    rb.dbg_passableVoxelSamples = 0;
    rb.dbg_outOfBoundsSamples = 0;
    rb.dbg_terrainPendingSamples = 0;
    rb.dbg_terrainPending = false;
    rb.dbg_lastPlanetarySample = PlanetaryCollisionSampleStatus::None;
    rb.dbg_radialGroundContact = false;
    rb.dbg_tangentialSolidBlock = false;
    rb.dbg_initialPenetrationUnresolved = false;
    rb.dbg_groundVoxelCoord = glm::ivec3(-1);
    rb.dbg_groundContactNormal = glm::vec3(0.0f);
    
    bool isSpherical = false;
    fw::PlanetSize pSize = fw::PlanetSize::Medium;
    auto planetView = const_cast<fw::GameWorld&>(world).GetRegistry().view<fw::PlanetGeometryComponent>();
    if (!planetView.empty()) {
        auto& geom = planetView.get<fw::PlanetGeometryComponent>(*planetView.begin());
        pSize = geom.planetSize;
        isSpherical = geom.isLogicalSphere;
    }

    glm::vec3 voxPos = rb.position;
    glm::quat localRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 localVel = rb.velocity;
    float oldVelY = localVel.y;
    glm::vec3 preCollisionGlobalPos = rb.position;
    
    if (isSpherical) {
        ResolveSphericalCollisions(rb, dt, world, preferredPlanetaryForward);
        return;
    }

    // Helper per verificare se un blocco è solido
    auto isSolid = [&](int x, int y, int z) {
        rb.dbg_lastLookupCoord = glm::ivec3(x, y, z);
        if (y < 0 || y >= 128) { rb.dbg_lastLookupSolid = false; return false; }
        
        int flatX = x;
        int flatZ = z;
        fw::BlockType b = world.GetBlockFlat(flatX, y, flatZ);
        
        // Trattiamo OutOfBounds come solido sotto Y=25, oppure ovunque per evitare di cadere nel vuoto
        if (b == fw::BlockType::OutOfBounds) {
            bool sol = (y < 25);
            rb.dbg_lastLookupSolid = sol;
            if (sol) rb.dbg_voxelContacts++;
            return sol;
        }
        
        bool sol = (b != fw::BlockType::Air && b != fw::BlockType::Water && 
                b != fw::BlockType::Lava && b != fw::BlockType::StargatePortal);
        rb.dbg_lastLookupSolid = sol;
        if (sol) rb.dbg_voxelContacts++;
        return sol;
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

    rb.position = voxPos;

    // Ricalcolo velocità effettiva (Post-Collisione)
    if (dt > 0.0f) {
        glm::vec3 actualGlobalVelocity = (rb.position - preCollisionGlobalPos) / dt;
        if (moveX != dx) actualGlobalVelocity.x = -localVel.x * rb.restitution;
        if (moveY != dy) actualGlobalVelocity.y = -localVel.y * rb.restitution;
        if (moveZ != dz) actualGlobalVelocity.z = -localVel.z * rb.restitution;
        rb.velocity = actualGlobalVelocity;
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

void PhysicsEngine::ResolveSphericalCollisions(RigidBody& rb, float dt, const fw::GameWorld& world,
                                               const glm::vec3* preferredPlanetaryForward) {
    if (glm::length(rb.position) < 0.1f) return;
    
    glm::vec3 preCollisionGlobalPos = rb.position;
    glm::vec3 movement = rb.velocity * dt;
    float oldVelY = glm::dot(rb.velocity, glm::normalize(rb.position));

    glm::vec3 localUp;
    if (!fw::PlanetaryCameraFrame::Normalize(rb.position, localUp)) return;

    fw::PlanetaryCameraFrame::State collisionFrame{
        rb.planetaryCollisionFrameUp,
        rb.planetaryCollisionFrameForward,
        rb.planetaryCollisionFrameInitialized};
    bool frameReady = false;
    if (preferredPlanetaryForward) {
        glm::vec3 preferredTangent = fw::PlanetaryCameraFrame::ProjectToTangent(
            *preferredPlanetaryForward, localUp);
        frameReady = fw::PlanetaryCameraFrame::Normalize(preferredTangent, preferredTangent);
        if (frameReady) collisionFrame = {localUp, preferredTangent, true};
    }
    if (!frameReady) frameReady = fw::PlanetaryCameraFrame::TransportTo(collisionFrame, localUp);
    if (!frameReady) return;
    rb.planetaryCollisionFrameUp = collisionFrame.up;
    rb.planetaryCollisionFrameForward = collisionFrame.forward;
    rb.planetaryCollisionFrameInitialized = collisionFrame.initialized;

    fw::PlanetSize planetSize = fw::PlanetSize::Tiny;
    auto planetGeometryView = const_cast<fw::GameWorld&>(world).GetRegistry().view<fw::PlanetGeometryComponent>();
    if (!planetGeometryView.empty()) {
        planetSize = planetGeometryView.get<fw::PlanetGeometryComponent>(*planetGeometryView.begin()).planetSize;
    }

    glm::vec3 localFwd = collisionFrame.forward;
    glm::vec3 localRight;
    if (!fw::PlanetaryCameraFrame::Normalize(glm::cross(localFwd, localUp), localRight)) return;
    bool usedRadialStepCorrection = false;

    enum class CylinderBlockReason : uint8_t { Clear, SolidVoxel, OutOfBounds, TerrainPending, InvalidInput };
    enum class CollisionProbePhase : uint8_t { General, InitialRecovery, ContactRefinement };
    CollisionProbePhase probePhase = CollisionProbePhase::General;
    std::unordered_set<uint64_t> solidContacts;
    auto isCylinderBlocked = [&](const glm::vec3& cPos) -> CylinderBlockReason {
        float r = rb.radius;
        float h = rb.height;
        // RigidBody::position is the foot/base point. Probe it as well as the
        // interior heights so resting contact cannot settle 0.1 units inside
        // a solid voxel while the lowest legacy sample is still clear.
        float y_steps[] = { 0.0f, 0.1f, h * 0.5f, h - 0.1f };
        std::array<glm::vec2, 17> xz_offsets = {{
            {0, 0}, {r, 0}, {-r, 0}, {0, r}, {0, -r},
            {r*0.707f, r*0.707f}, {-r*0.707f, r*0.707f}, {r*0.707f, -r*0.707f}, {-r*0.707f, -r*0.707f}
        }};
        // The cardinal/diagonal perimeter probes leave angular gaps wide
        // enough for a voxel corner to intersect the player's cylinder.
        // Add eight deterministic half-angle probes at 95% radius.
        constexpr int perimeterSamples = 8;
        for (int sample = 0; sample < perimeterSamples; ++sample) {
            const float angle = 6.28318530718f *
                (static_cast<float>(sample) + 0.5f) / static_cast<float>(perimeterSamples);
            xz_offsets[9 + sample] = glm::vec2(r * 0.95f * std::cos(angle),
                                                r * 0.95f * std::sin(angle));
        }
        for (float y_off : y_steps) {
            for (auto& off : xz_offsets) {
                glm::vec3 samplePos = cPos + localUp * y_off + localRight * off.x + localFwd * off.y;
                if (rb.dbg_countCollisionProbes) ++rb.dbg_collisionProbeCount;
                const auto queryStart = rb.dbg_countCollisionProbes ? std::chrono::steady_clock::now()
                    : std::chrono::steady_clock::time_point{};
                const fw::WorldVoxelQueryResult query = world.QueryBlockCartesian(samplePos);
                if (rb.dbg_countCollisionProbes) {
                    rb.dbg_collisionQueryNanoseconds += static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - queryStart).count());
                    if (probePhase == CollisionProbePhase::InitialRecovery)
                        ++rb.dbg_initialRecoveryProbeCount;
                    else if (probePhase == CollisionProbePhase::ContactRefinement)
                        ++rb.dbg_contactRefinementProbeCount;
                }
                switch (query.status) {
                    case fw::WorldVoxelQueryStatus::SolidVoxel: {
                        rb.dbg_lastLookupCoord = query.voxelCoord;
                        rb.dbg_lastLookupSolid = true;
                        rb.dbg_lastPlanetarySample = PlanetaryCollisionSampleStatus::SolidVoxel;
                        const uint64_t key = (static_cast<uint64_t>(query.voxelCoord.x) << 18) |
                            (static_cast<uint64_t>(query.voxelCoord.y) << 11) |
                            static_cast<uint64_t>(query.voxelCoord.z);
                        if (solidContacts.insert(key).second) ++rb.dbg_voxelContacts;
                        return CylinderBlockReason::SolidVoxel;
                    }
                    case fw::WorldVoxelQueryStatus::Air:
                        ++rb.dbg_airSamples;
                        rb.dbg_lastLookupCoord = query.voxelCoord;
                        rb.dbg_lastLookupSolid = false;
                        rb.dbg_lastPlanetarySample = PlanetaryCollisionSampleStatus::Air;
                        break;
                    case fw::WorldVoxelQueryStatus::PassableVoxel:
                        ++rb.dbg_passableVoxelSamples;
                        rb.dbg_lastLookupCoord = query.voxelCoord;
                        rb.dbg_lastLookupSolid = false;
                        rb.dbg_lastPlanetarySample = PlanetaryCollisionSampleStatus::PassableVoxel;
                        break;
                    case fw::WorldVoxelQueryStatus::OutOfBounds:
                        ++rb.dbg_outOfBoundsSamples;
                        rb.dbg_lastLookupCoord = query.voxelCoord;
                        rb.dbg_lastLookupSolid = false;
                        rb.dbg_lastPlanetarySample = PlanetaryCollisionSampleStatus::OutOfBounds;
                        return CylinderBlockReason::OutOfBounds;
                    case fw::WorldVoxelQueryStatus::TerrainPending:
                        ++rb.dbg_terrainPendingSamples;
                        rb.dbg_terrainPending = true;
                        rb.dbg_lastLookupCoord = query.voxelCoord;
                        rb.dbg_lastLookupSolid = false;
                        rb.dbg_lastPlanetarySample = PlanetaryCollisionSampleStatus::TerrainPending;
                        return CylinderBlockReason::TerrainPending;
                    case fw::WorldVoxelQueryStatus::InvalidInput:
                        rb.dbg_lastPlanetarySample = PlanetaryCollisionSampleStatus::InvalidInput;
                        return CylinderBlockReason::InvalidInput;
                }
            }
        }
        return CylinderBlockReason::Clear;
    };

    auto tryMove = [&](const glm::vec3& stepVec, bool isY, CylinderBlockReason& blockedBy) -> bool {
        blockedBy = isCylinderBlocked(rb.position + stepVec);
        if (blockedBy == CylinderBlockReason::Clear) {
            rb.position += stepVec;
            return true;
        }
        if (!isY && rb.isGrounded && blockedBy == CylinderBlockReason::SolidVoxel) {
            float stepUp = 0.6f;
            if (isCylinderBlocked(rb.position + localUp * stepUp) == CylinderBlockReason::Clear) {
                const CylinderBlockReason stepMoveReason = isCylinderBlocked(rb.position + localUp * stepUp + stepVec);
                if (stepMoveReason == CylinderBlockReason::Clear) {
                    rb.position += localUp * stepUp + stepVec;
                    usedRadialStepCorrection = true;
                    for(float d = 0.1f; d <= stepUp; d += 0.1f) {
                        if (isCylinderBlocked(rb.position - localUp * 0.1f) != CylinderBlockReason::Clear) break;
                        rb.position -= localUp * 0.1f;
                    }
                    return true;
                }
            }
        }
        return false;
    };

    // Recover an initially embedded body to the nearest clear radial position.
    // Solid terrain is bounded by the configured radial voxel column; missing,
    // OOB, or invalid samples stop recovery rather than being treated as clear.
    bool usedInitialDepenetration = false;
    const glm::vec3 initialPosition = rb.position;
    probePhase = CollisionProbePhase::InitialRecovery;
    const CylinderBlockReason initialOverlap = isCylinderBlocked(initialPosition);
    if (initialOverlap == CylinderBlockReason::SolidVoxel) {
        const float guaranteedAirRadius = fw::PlanetMath::GetPlanetRadius(planetSize) +
            static_cast<float>(fw::ChunkDimensions::VoxelsY - fw::PlanetRadialMapping::ReferenceVoxelOffset) -
            0.5f + 0.1f;
        const float maximumDistance = std::max(0.0f, guaranteedAirRadius - glm::length(initialPosition));
        const int maximumSteps = static_cast<int>(std::ceil(maximumDistance / 0.1f)) + 1;
        float lastSolidDistance = 0.0f;
        bool recovered = false;
        for (int step = 1; step <= maximumSteps; ++step) {
            const float distance = std::min(maximumDistance, static_cast<float>(step) * 0.1f);
            const glm::vec3 candidate = initialPosition + localUp * distance;
            const CylinderBlockReason candidateState = isCylinderBlocked(candidate);
            if (candidateState == CylinderBlockReason::Clear) {
                float clearDistance = distance;
                float solidDistance = lastSolidDistance;
                for (int refinement = 0; refinement < 8; ++refinement) {
                    const float midpointDistance = (solidDistance + clearDistance) * 0.5f;
                    const CylinderBlockReason midpointState = isCylinderBlocked(
                        initialPosition + localUp * midpointDistance);
                    if (midpointState == CylinderBlockReason::Clear) {
                        clearDistance = midpointDistance;
                    } else if (midpointState == CylinderBlockReason::SolidVoxel) {
                        solidDistance = midpointDistance;
                    } else {
                        break;
                    }
                }
                rb.position = initialPosition + localUp * clearDistance;
                usedInitialDepenetration = true;
                recovered = true;
                break;
            }
            if (candidateState != CylinderBlockReason::SolidVoxel) break;
            lastSolidDistance = distance;
            if (distance >= maximumDistance) break;
        }
        rb.dbg_initialPenetrationUnresolved = !recovered;
    }
    probePhase = CollisionProbePhase::General;

    float dy = glm::dot(movement, localUp);
    float dx = glm::dot(movement, localRight);
    float dz = glm::dot(movement, localFwd);

    if (dy != 0.0f) {
        int steps = std::max(1, (int)std::ceil(std::abs(dy) / 0.1f));
        float inc = dy / steps;
        glm::vec3 stepVec = localUp * inc;
        for(int i = 0; i < steps; ++i) {
            CylinderBlockReason blockedBy = CylinderBlockReason::Clear;
            if (!tryMove(stepVec, true, blockedBy)) {
                if (dy < 0.0f && blockedBy == CylinderBlockReason::SolidVoxel) {
                    rb.dbg_radialGroundContact = true;
                    rb.dbg_groundVoxelCoord = rb.dbg_lastLookupCoord;
                    rb.dbg_groundContactNormal = localUp;
                    // The sweep advances in <= 0.1-unit increments. Without
                    // refining the last clear/first solid interval, the body
                    // can be left almost one step above the surface: Grounded
                    // is true for this tick, then drops on the next small
                    // gravity step. Find the voxel contact boundary along the
                    // same swept segment instead of applying a radial clamp.
                    glm::vec3 clearPosition = rb.position;
                    glm::vec3 solidPosition = rb.position + stepVec;
                    probePhase = CollisionProbePhase::ContactRefinement;
                    for (int refinement = 0; refinement < 8; ++refinement) {
                        const glm::vec3 midpoint = (clearPosition + solidPosition) * 0.5f;
                        if (isCylinderBlocked(midpoint) == CylinderBlockReason::Clear) {
                            clearPosition = midpoint;
                        } else {
                            solidPosition = midpoint;
                        }
                    }
                    probePhase = CollisionProbePhase::General;
                    rb.position = clearPosition;
                    rb.isGrounded = true;
                }
                break;
            }
        }
    }
    
    if (dx != 0.0f) {
        int steps = std::max(1, (int)std::ceil(std::abs(dx) / 0.1f));
        float inc = dx / steps;
        glm::vec3 stepVec = localRight * inc;
        for(int i = 0; i < steps; ++i) {
            CylinderBlockReason blockedBy = CylinderBlockReason::Clear;
            if (!tryMove(stepVec, false, blockedBy)) {
                if (blockedBy == CylinderBlockReason::SolidVoxel) {
                    rb.isAgainstWall = true;
                    rb.dbg_tangentialSolidBlock = true;
                }
                break;
            }
        }
    }
    
    if (dz != 0.0f) {
        int steps = std::max(1, (int)std::ceil(std::abs(dz) / 0.1f));
        float inc = dz / steps;
        glm::vec3 stepVec = localFwd * inc;
        for(int i = 0; i < steps; ++i) {
            CylinderBlockReason blockedBy = CylinderBlockReason::Clear;
            if (!tryMove(stepVec, false, blockedBy)) {
                if (blockedBy == CylinderBlockReason::SolidVoxel) {
                    rb.isAgainstWall = true;
                    rb.dbg_tangentialSolidBlock = true;
                }
                break;
            }
        }
    }

    if (dt > 0.0f) {
        const glm::vec3 displacementVelocity = (rb.position - preCollisionGlobalPos) / dt;
        rb.velocity = displacementVelocity;
        if (usedRadialStepCorrection || usedInitialDepenetration) {
            // Stepping over a voxel is a positional collision resolution, not
            // an upward impulse. Keep the incoming radial velocity (or cancel
            // an inward one when the same tick established Grounded) instead
            // of converting the step height into launch velocity.
            float desiredRadialVelocity = oldVelY;
            if (rb.isGrounded && desiredRadialVelocity < 0.0f) desiredRadialVelocity = 0.0f;
            const float displacementRadialVelocity = glm::dot(displacementVelocity, localUp);
            rb.velocity += localUp * (desiredRadialVelocity - displacementRadialVelocity);
        }
        if (rb.isGrounded) {
            // A downward SolidVoxel hit is a unilateral contact: retain
            // tangential motion, but remove velocity into the contacted voxel.
            const float inwardRadialVelocity = glm::dot(rb.velocity, localUp);
            if (inwardRadialVelocity < 0.0f) rb.velocity -= localUp * inwardRadialVelocity;
        }
    }

    if (rb.isGrounded && oldVelY < -7.7f) {
        float deltaV = std::abs(oldVelY - glm::dot(rb.velocity, localUp));
        float damage = ComputeFallDamage(deltaV, rb.mass);
        if (damage > 0.0f) {
            PhysicsEvent ev;
            ev.type = PhysicsEvent::Type::FallDamage;
            ev.value = damage;
            rb.pendingEvents.push_back(ev);
        }
    }
}
