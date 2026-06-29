#pragma once
#include "State.h"
#include "entt/entt.hpp"
#include <vector>
#include <memory>
#include "Systems.h"

struct SharedContext;
namespace fw {
    class ForgeWorld;
}

class ForgeState : public State {
public:
    explicit ForgeState(SharedContext* context);
    ~ForgeState() override;

    std::expected<void, std::string> Init() override;
    void Update(float dt) override;
    void Render() override;
    
    entt::registry* GetRegistry() override { return &m_registry; }

private:
    SharedContext* m_context;
    entt::registry m_registry;
    std::vector<std::unique_ptr<System>> m_systems;

    // --- Orbital Camera ---
    float m_orbitDistance = 24.0f;
    float m_orbitYaw = -90.0f;
    float m_orbitPitch = 30.0f;
    glm::vec3 m_orbitTarget = glm::vec3(8.0f, 8.0f, 8.0f);

    // --- Editor Tools ---
    int m_selectedTool = 1; // 0: Select, 1: Place, 2: Erase
    int m_selectedColorIndex = 1; // Default to Palette Index 1
    bool m_isMouseOverUI = false;

    // --- CAD-Style Cursor (X, Y, Z) ---
    int m_cursorX = 8;
    int m_cursorY = 0;
    int m_cursorZ = 8;
};
