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

    std::expected<void, std::string> Init() override;
    void Update(float dt) override;
    void Render() override;
    
    entt::registry* GetRegistry() override { return &m_registry; }

    // --- DevMode Inventory ---
    bool m_showDevInventory = true; // Toggle visibilità inventario dev
    
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
