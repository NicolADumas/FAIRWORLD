#pragma once
#include "GlobalAssetBrowser.h"
#include "Player.h"
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

    Player m_player;
    glm::vec3 m_ghostPos = {0.0f, 0.0f, 0.0f};

    // --- Global Asset Browser ---
    fw::GlobalAssetBrowser m_assetBrowser;
    bool m_showAssetBrowser = false;
    bool m_isPlacingRig = false;
    std::string m_rigToPlace = "";

    void RefreshAvailableRigs();
    void SpawnRig(const std::string& rigPath, const glm::vec3& position);

private:
    SharedContext* m_context;
    entt::registry m_registry;
    std::vector<std::unique_ptr<System>> m_systems;
};
