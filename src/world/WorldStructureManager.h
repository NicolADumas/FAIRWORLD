#pragma once
#include <entt/entt.hpp>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>
#include "ForgeMath.h"

struct SharedContext;

namespace fw {

struct StructureBlock {
    int type;
    glm::vec4 color;
};

class WorldStructureManager {
public:
    WorldStructureManager() = default;
    ~WorldStructureManager() = default;

    bool SaveStructure(const std::string& name, const std::unordered_map<glm::ivec3, StructureBlock>& blocks, uint8_t placementMode = 0, int pivotX = 0, int pivotY = 0, int pivotZ = 0);
    entt::entity LoadStructureAsPrefab(entt::registry& registry, SharedContext* context, const std::string& filepath, const fw::Vec3& position);
    bool SaveStructureJSON(const std::string& name, const std::unordered_map<glm::ivec3, StructureBlock>& blocks, uint8_t placementMode = 0, int pivotX = 0, int pivotY = 0, int pivotZ = 0);
};

} // namespace fw
