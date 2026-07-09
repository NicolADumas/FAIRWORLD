#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <cstdint>

namespace fw {

struct BlockDefinition {
    // Identity
    uint8_t id = 0;
    std::string stringId = "fairworld:unknown";
    std::string displayName = "Unknown Block";

    // PBR Material (replacing ForgeMaterial)
    glm::vec3 baseColor = glm::vec3(1.0f);
    float metallic = 0.0f;
    float roughness = 1.0f;
    float emissiveStrength = 0.0f;
    glm::vec3 emissiveColor = glm::vec3(0.0f);
    float ior = 1.45f;
    float clearcoat = 0.0f;
    float clearcoatRoughness = 0.0f;
    float transmission = 0.0f;
    float normalIntensity = 1.0f;
    float alphaCutoff = 0.5f;
    float aoStrength = 1.0f;
    float textureIndex = -1.0f;

    // Physics
    bool isSolid = true;
    bool isTransparent = false;
    float mass = 1.0f;       // Determines falling speed / inertia impact
    float friction = 0.6f;   // Slipperiness (e.g. ice = 0.01)
    float bounciness = 0.0f; // Restitution (0 = no bounce, 1 = full bounce)

    // Gameplay
    int lightEmissionLevel = 0;
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
    void UpdateBlock(uint8_t id, const BlockDefinition& def);

    const BlockDefinition& GetBlock(uint8_t id) const;
    const BlockDefinition& GetBlock(const std::string& stringId) const;
    
    BlockDefinition& GetBlockMutable(uint8_t id);

    const std::vector<BlockDefinition>& GetAllBlocks() const { return m_blocks; }

private:
    void RegisterDefaultBlocks();

    std::vector<BlockDefinition> m_blocks;
    std::unordered_map<std::string, uint8_t> m_stringToIdMap;
    
    // Fallback block if an invalid ID is requested
    BlockDefinition m_fallbackBlock;
};

} // namespace fw
