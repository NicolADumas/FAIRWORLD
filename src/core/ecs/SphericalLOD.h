#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include "entt/entt.hpp"

class AssetManager;

namespace fw {

class JobSystem;
class GameWorld;
class ForgeWorld;
struct PlanetMap;

struct ChunkNode {
    glm::vec3 centerPos;
    float boundsRadius;
    int lodLevel; // 0 = Massima risoluzione, N = Radice
    bool isSplit = false;
    
    // Cube-sphere face patch
    glm::vec3 p00, p10, p01, p11;
    
    std::unique_ptr<ChunkNode> children[4];
    
    // Entità generata nel GameWorld
    entt::entity targetEntity = entt::null;
    
    // Indica se c'è un job pendente per questo nodo
    bool isGenerating = false;

    ChunkNode(glm::vec3 c, float r, int lod, glm::vec3 p00, glm::vec3 p10, glm::vec3 p01, glm::vec3 p11) 
        : centerPos(c), boundsRadius(r), lodLevel(lod), p00(p00), p10(p10), p01(p01), p11(p11) {}
};

class SphericalLODSystem {
private:
    const float DISTANCE_MULTIPLIER = 2.0f;
    float m_planetRadius = 50.0f;

    float GetThresholdForLOD(int lodLevel, float chunkRadius) {
        return chunkRadius * DISTANCE_MULTIPLIER * (lodLevel + 1);
    }

    void SplitNode(ChunkNode& node, GameWorld* world, JobSystem* jobs, AssetManager* assets, const PlanetMap* planetInfo);
    void MergeNode(ChunkNode& node, GameWorld* world);
    void RequestMeshGeneration(ChunkNode* node, GameWorld* world, JobSystem* jobs, AssetManager* assets, const PlanetMap* planetInfo);

public:
    void SetPlanetRadius(float radius) { m_planetRadius = radius; }
    void UpdateLODTree(ChunkNode& node, const glm::vec3& playerPos, GameWorld* world, JobSystem* jobs, AssetManager* assets, const PlanetMap* planetInfo);
};

} // namespace fw
