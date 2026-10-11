#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <cstdint>

namespace fw {

enum class BlockMotionType : uint8_t {
    Static = 0,    // Immobile in simulation (terrain, standard voxel blocks)
    Dynamic = 1,   // Affected by forces, gravity, collisions
    Kinematic = 2  // Controlled programmatically / keyframed, infinite mass in collisions
};

enum class BlockMassMode : uint8_t {
    Manual = 0,        // Mass authored directly by user in kg
    DensityDerived = 1 // Mass calculated from volume and material density
};

struct BlockPhysicsDef {
    float mass = 1.0f;               // [kg] Authored or effective mass
    float density = 1000.0f;         // [kg/m^3] Density (water=1000, wood~600, stone~2500, iron~7870)
    float friction = 0.6f;          // [0.0 - 1.0] Surface friction coefficient
    float restitution = 0.0f;       // [0.0 - 1.0] Coefficient of restitution / bounciness
    bool collisionEnabled = true;   // Whether collision resolution is enabled
    BlockMotionType motionType = BlockMotionType::Static;
    BlockMassMode massMode = BlockMassMode::Manual;

    // Derived mass calculation based on bounding volume
    float CalculateDerivedMass(const glm::vec3& scale) const {
        float volume = std::abs(scale.x * scale.y * scale.z);
        if (volume < 1e-4f) volume = 1e-4f;
        return density * volume;
    }

    // Single source of truth for effective runtime mass
    float GetEffectiveMass(const glm::vec3& scale) const {
        if (massMode == BlockMassMode::DensityDerived) {
            return CalculateDerivedMass(scale);
        }
        return (mass > 0.0f) ? mass : 0.001f;
    }
};

struct SimBlockDef {
    // Identity
    uint8_t id = 0;
    std::string stringId = "fairworld:unknown";
    std::string displayName = "Unknown Block";

    // Physics Contract (Per-Asset Independent Definition)
    BlockPhysicsDef physics;

    // Backward-compatibility mirrors (kept synchronized with `physics`)
    bool isSolid = true;
    bool isTransparent = false;
    float mass = 1.0f;       // Legacy mirror of physics.mass
    float friction = 0.6f;   // Legacy mirror of physics.friction
    float bounciness = 0.0f; // Legacy mirror of physics.restitution

    // Synchronizes legacy mirrors from physics definition
    void SyncLegacyPhysicsFields() {
        isSolid = physics.collisionEnabled;
        mass = physics.GetEffectiveMass(localScale);
        friction = physics.friction;
        bounciness = physics.restitution;
    }

    // Synchronizes physics definition from legacy mirrors
    void SyncFromLegacyPhysicsFields() {
        physics.collisionEnabled = isSolid;
        physics.mass = mass;
        physics.friction = friction;
        physics.restitution = bounciness;
    }

    // Thermodynamics
    float thermal_resistance = 1.0f; // Resistance to heat flow
    float thermal_capacity = 1.0f;   // Energy required to heat up
    float lightEmissionLevel = 0.0f; // Also part of simulation (light system)

    // Asset-Local Transform & Pivot (BM-A1)
    glm::vec3 localPosition{0.0f, 0.0f, 0.0f}; // Asset-local position offset
    glm::vec3 localRotation{0.0f, 0.0f, 0.0f}; // Asset-local orientation (Euler angles in degrees)
    glm::vec3 localScale{1.0f, 1.0f, 1.0f};    // Asset-local scale
    glm::vec3 pivotOffset{0.0f, 0.0f, 0.0f};  // Asset-local pivot offset relative to shape canonical center
};

class BlockRegistry {
public:
    BlockRegistry();
    ~BlockRegistry() = default;

    void Initialize();
    bool LoadFromJson(const std::string& filepath);
    bool SaveToJson(const std::string& filepath);

    // Creates a new block definition and returns its ID
    uint8_t CreateNewBlock(const std::string& stringId, const std::string& displayName);
    
    // Updates an existing definition
    void UpdateBlock(uint8_t id, const SimBlockDef& def);

    const SimBlockDef& GetBlock(uint8_t id) const;
    const SimBlockDef& GetBlock(const std::string& stringId) const;
    
    SimBlockDef& GetBlockMutable(uint8_t id);

    const std::vector<SimBlockDef>& GetAllBlocks() const { return m_blocks; }

private:
    void RegisterDefaultBlocks();

    std::vector<SimBlockDef> m_blocks;
    std::unordered_map<std::string, uint8_t> m_stringToIdMap;
    
    // Fallback block if an invalid ID is requested
    SimBlockDef m_fallbackBlock;
};

} // namespace fw
