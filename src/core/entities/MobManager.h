#pragma once
#include "MobInstance.h"
#include <vector>

// Forward declaration di Player per evitare cicli di inclusione se necessario,
// anche se includerlo direttamente potrebbe andare bene. In questo caso è meglio la forward declaration per UpdateMobs.
#include "Player.h"
class World;

class MobManager {
public:
    std::vector<MobInstance> instances;

    void Spawn(const MobTemplate& tmpl, glm::vec3 pos);
    void DespawnAll();
    
    // Ritorna l'indice del mob colpito più vicino (-1 se nessuno)
    int Raycast(glm::vec3 rayOrigin, glm::vec3 rayDir, float maxDist = 3.5f);
    
    // Esegue il movimento e il combattimento di tutti i mob contro il player
    void UpdateMobs(float deltaTime, glm::vec3 playerPos, Player& player, AssetManager& assets, World& world);
};
