#pragma once
#include "MobInstance.h"
#include <vector>
#include <entt/entt.hpp>

// Forward declaration di Player per evitare cicli di inclusione se necessario,
// anche se includerlo direttamente potrebbe andare bene. In questo caso è meglio la forward declaration per UpdateMobs.
#include "Player.h"
class World;

namespace fw {
    class DimensionsManager;
}

class MobManager {
public:
    std::vector<MobInstance> instances;

    void Spawn(const MobTemplate& tmpl, glm::vec3 pos);
    void DespawnAll();
    
    // Ritorna l'indice del mob colpito più vicino (-1 se nessuno)
    int Raycast(glm::vec3 rayOrigin, glm::vec3 rayDir, float maxDist = 3.5f);
    
    // Esegue il movimento e il combattimento di tutti i mob contro il player
    void UpdateMobs(float deltaTime, glm::vec3 playerPos, Player& player, AssetManager& assets, World& world);

    // Sistema di Spawning guidato dai Chunk (Fase 1.5)
    void UpdateSpawners(entt::registry& registry, const fw::DimensionsManager& dimManager, float worldPositionX, float worldPositionZ);
    void SpawnBoss(entt::registry& registry, int32_t cx, int32_t cz);
    void SpawnRegularPack(entt::registry& registry, int32_t cx, int32_t cz, uint16_t biomeID);
};
