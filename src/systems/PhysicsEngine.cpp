#include "pch.h"
#include "PhysicsEngine.h"
#include "GameWorld.h"
#include "PlanetComponents.h"
#include "MapWorldGenerator.h"
#include <algorithm>

void PhysicsEngine::StepSimulation(RigidBody& rb, float dt, const fw::GameWorld& world) {
    // 1. Azzera la forza netta del frame precedente
    rb.netForce = glm::vec3(0.0f);

    // 2. Controlla il tipo di blocco ai piedi e al centro
    // rb.position.y rappresenta i piedi del giocatore
    fw::BlockType feetBlock = world.GetBlock((int)floor(rb.position.x + 0.5f), (int)floor(rb.position.y + 0.1f), (int)floor(rb.position.z + 0.5f));
    fw::BlockType centerBlock = world.GetBlock((int)floor(rb.position.x + 0.5f), (int)floor(rb.position.y + (rb.height * 0.5f)), (int)floor(rb.position.z + 0.5f));
    
    // Se ci troviamo in un chunk non caricato, freeziamo la fisica!
    if (feetBlock == fw::BlockType::OutOfBounds || centerBlock == fw::BlockType::OutOfBounds) {
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

    glm::quat chunkRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 chunkPos = glm::vec3(0.0f);
    int cx = 0, cz = 0;

    if (isSpherical) {
        fw::MapWorldGenerator::GetChunkCoordFromPosition(planetRadius, rb.position, cx, cz);
        if (fw::MapWorldGenerator::GetSphericalChunkTransform(planetRadius, cx, cz, chunkPos, chunkRot)) {
            // Trasforma in Spazio Locale del Chunk per operare con la gravità (0, -1, 0)
            rb.position = glm::inverse(chunkRot) * (rb.position - chunkPos);
            rb.velocity = glm::inverse(chunkRot) * rb.velocity;
        } else {
            isSpherical = false; 
        }
    }

    // Helper per verificare se un blocco è solido
    auto isSolid = [&](int x, int y, int z) {
        if (y < 0 || y >= 128) return false;
        
        // Mappa le coordinate locali (x,y,z) del chunk alle coordinate flat globali di GameWorld
        int flatX = x;
        int flatZ = z;
        if (isSpherical) {
            flatX = (cx * 16) + x;
            flatZ = (cz * 16) + z;
        }

        fw::BlockType b = world.GetBlock(flatX, y, flatZ);
        if (b == fw::BlockType::OutOfBounds) return true; // Blocca il player in aria se il chunk sta caricando!
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
            rb.position.x - rb.radius + eps,
            rb.position.y + eps,
            rb.position.z - rb.radius + eps,
            rb.position.x + rb.radius - eps,
            rb.position.y + rb.height - eps,
            rb.position.z + rb.radius - eps
        };
    };

    // DEPENETRATION SOLVER
    // Controlla se siamo già dentro un blocco solido e ci spinge fuori dalla via più breve
    bool anyPenetration = true;
    int maxIters = 4;
    while (anyPenetration && maxIters-- > 0) {
        anyPenetration = false;
        AABB box = getAABB();
        // Margine per non triggerare la depenetration solo sfiorando i blocchi
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
                        
                        // Calcola le distanze di spinta per uscire dal blocco (6 facce)
                        float pushUp    = (y + 1.0f) - box.minY;
                        float pushDown  = box.maxY - y;
                        float pushRight = (x + 1.0f) - box.minX;
                        float pushLeft  = box.maxX - x;
                        float pushFront = (z + 1.0f) - box.minZ;
                        float pushBack  = box.maxZ - z;

                        // Trova la minima distanza per uscire
                        float minPush = pushUp;
                        if (pushDown < minPush) minPush = pushDown;
                        if (pushRight < minPush) minPush = pushRight;
                        if (pushLeft < minPush) minPush = pushLeft;
                        if (pushFront < minPush) minPush = pushFront;
                        if (pushBack < minPush) minPush = pushBack;
                        
                        // Spinge fuori! (con un piccolo margine)
                        if (minPush == pushUp) rb.position.y += pushUp + eps;
                        else if (minPush == pushDown) rb.position.y -= pushDown + eps;
                        else if (minPush == pushRight) rb.position.x += pushRight + eps;
                        else if (minPush == pushLeft) rb.position.x -= pushLeft + eps;
                        else if (minPush == pushFront) rb.position.z += pushFront + eps;
                        else if (minPush == pushBack) rb.position.z -= pushBack + eps;
                        
                        break; // esce dal loop z e riparte con la nuova AABB
                    }
                }
                if (anyPenetration) break; // esce dal loop y
            }
            if (anyPenetration) break; // esce dal loop x
        }
    }

    // VOXEL AABB SWEEP: Movimento perfetto asse per asse
    AABB box = getAABB(); // Ricalcola dopo depenetration
    
    float dx = rb.velocity.x * dt;
    float dy = rb.velocity.y * dt;
    float dz = rb.velocity.z * dt;
    
    float moveX = dx;
    float moveY = dy;
    float moveZ = dz;
    
    bool hasSteppedUp = false;

    float oldVelY = rb.velocity.y;
    glm::vec3 preCollisionPos = rb.position;

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
        rb.position.y += moveY;

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

        // --- STEP-UP AUTOMATICO X E WALL CLIMB ---
        if (moveX != dx) {
            rb.isAgainstWall = true;
        }

        if (moveX != dx && rb.isGrounded) {
            const float MAX_STEP_HEIGHT = 0.6f;
            
            // Sweep Y upwards per vedere quanto possiamo salire
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
                // Testa sweep X nella nuova posizione Y sollevata
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
                
                // Se la nuova posizione ci fa avanzare di più, applichiamo lo step-up
                if (abs(liftedMoveX) > abs(moveX) + 0.001f) {
                    moveX = liftedMoveX;
                    
                    // Snap down per trovare l'esatta altezza del gradino
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
                    rb.position.y += finalLift;
                    hasSteppedUp = true;
                }
            }
        }
        // ----------------------------

        box.minX += moveX;
        box.maxX += moveX;
        rb.position.x += moveX;
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

        // --- STEP-UP AUTOMATICO Z E WALL CLIMB ---
        if (moveZ != dz) {
            rb.isAgainstWall = true;
        }

        if (moveZ != dz && rb.isGrounded && !hasSteppedUp) {
            const float MAX_STEP_HEIGHT = 0.6f;
            
            // Sweep Y upwards
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
                // Testa sweep Z nella nuova posizione Y sollevata
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
                    
                    // Snap down
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
                    rb.position.y += finalLift;
                }
            }
        }
        // ----------------------------

        box.minZ += moveZ;
        box.maxZ += moveZ;
        rb.position.z += moveZ;
    }

    // Ricalcolo velocità effettiva (Post-Collisione)
    if (dt > 0.0f) {
        glm::vec3 actualVelocity = (rb.position - preCollisionPos) / dt;
        
        // Applica restitution (rimbalzo) se ci siamo fermati su un asse
        if (moveX != dx) actualVelocity.x = -rb.velocity.x * rb.restitution;
        if (moveY != dy) actualVelocity.y = -rb.velocity.y * rb.restitution;
        if (moveZ != dz) actualVelocity.z = -rb.velocity.z * rb.restitution;
        
        rb.velocity = actualVelocity;
    }

    // Danno da caduta (Energia Cinetica persa improvvisamente)
    // Soglia: 7.7 m/s corrisponde a ~3m di caduta (v = sqrt(2*g*h))
    // Cadute più basse di ~3m non causano danni
    if (rb.isGrounded && oldVelY < -7.7f) {
        float deltaV = abs(oldVelY - rb.velocity.y);
        float damage = ComputeFallDamage(deltaV, rb.mass);
        if (damage > 0.0f) {
            PhysicsEvent ev;
            ev.type = PhysicsEvent::Type::FallDamage;
            ev.value = damage;
            rb.pendingEvents.push_back(ev);
        }
    }

    // --- Ritorno a Spazio Globale ---
    if (isSpherical) {
        rb.position = chunkPos + (chunkRot * rb.position);
        rb.velocity = chunkRot * rb.velocity;
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
