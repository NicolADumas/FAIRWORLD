#pragma once
#include "State.h"
#include "entt/entt.hpp"
#include <vector>
#include <memory>
#include "Systems.h"

struct SharedContext;

class PlayState : public State {
public:
    explicit PlayState(SharedContext* context);
    ~PlayState() override;

    bool Init() override;
    void Update(float dt) override;
    void Render() override;
    
    entt::registry* GetRegistry() override { return &m_registry; }

    // --- DevMode Inventory ---
    bool m_showDevInventory = true; // Toggle visibilità inventario dev
    
    // --- Global Asset Browser ---
    bool m_showAssetBrowser = false;
    struct RigAsset {
        std::string name;
        std::string path;
    };
    std::vector<RigAsset> m_availableRigs;
    void RefreshAvailableRigs();
    
    bool m_isPlacingRig = false;
    std::string m_rigToPlace = "";
    glm::vec3 m_ghostPos = {0,0,0};
    void SpawnRig(const std::string& rigPath, const glm::vec3& position);
    
    struct DevStructure {
        std::string name;
        uint8_t mode;
    };
    std::vector<DevStructure> m_availableStructures;
    void RefreshAvailableStructures();

private:
    SharedContext* m_context;
    entt::registry m_registry;
    std::vector<std::unique_ptr<System>> m_systems;
};
