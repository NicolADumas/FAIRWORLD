#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <cstdint>

namespace fw {

struct SimBlockDef {
    // Identity
    uint8_t id = 0;
    std::string stringId = "fairworld:unknown";
    std::string displayName = "Unknown Block";

    // Physics
    bool isSolid = true;
    bool isTransparent = false;
    float mass = 1.0f;       // Determines falling speed / inertia impact
    float friction = 0.6f;   // Slipperiness (e.g. ice = 0.01)
    float bounciness = 0.0f; // Restitution (0 = no bounce, 1 = full bounce)

    // Thermodynamics
    float thermal_resistance = 1.0f; // Resistance to heat flow
    float thermal_capacity = 1.0f;   // Energy required to heat up
    float lightEmissionLevel = 0.0f; // Also part of simulation (light system)
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
