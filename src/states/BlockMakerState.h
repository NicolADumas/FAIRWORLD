#pragma once
#include "State.h"
#include "entt/entt.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include "Systems.h"

#include "GameWorld.h"
#include "MeshGenerators.h"
struct SharedContext;

class BlockMakerState : public State {
public:
    explicit BlockMakerState(SharedContext* context);
    ~BlockMakerState() override;

    bool Init() override;
    void Update(float dt) override;
    void Render() override;

    entt::registry* GetRegistry() override;

private:
    void DrawUI();

    SharedContext* m_context;
    std::vector<std::unique_ptr<System>> m_systems;

    // --- Orbital Camera (Fixed on block) ---
    float m_orbitDistance = 5.0f;
    float m_orbitYaw = -90.0f;
    float m_orbitPitch = 30.0f;
    glm::vec3 m_orbitTarget = glm::vec3(0.0f, 0.0f, 0.0f);
    float m_cameraFov = 60.0f;

    // --- Block Rotation ---
    glm::vec3 m_blockEulerAngles = glm::vec3(0.0f); // Pitch (X), Yaw (Y), Roll (Z)
    bool m_autoRotateBlock = true;

    // --- Preview Block ---
    entt::entity m_previewBlockEntity = entt::null;
    glm::vec3 m_previewLightDir = glm::normalize(glm::vec3(0.5f, -1.0f, 0.5f));
    
    // --- Block Definition State ---
    uint8_t m_selectedBlockId = 1;
    char m_inputStringId[128] = "";
    char m_inputDisplayName[128] = "";
    int m_activeTab = 0; // 0 = Identity, 1 = Material, 2 = Physics

    // --- Physics Simulation (Bouncing) ---
    bool m_simulatePhysics = false;
    float m_simPosY = 0.0f;
    float m_simVelY = 0.0f;
    float m_simGravity = -15.0f; // gravity factor

    // --- PBR Texture Loading UI ---
    bool m_isCopying = false;
    float m_copyProgress = 0.0f;
    float m_saveMessageTimer = 0.0f;

    void UpdatePreviewMesh();
    void HandlePhysicsSimulation(float dt);

    // --- Parametric Shape controls ---
    int m_selectedShapeIndex = 0; // 0 = Standard Cube, 1 = SuperSphere (|x|^n + |y|^n + |z|^n = 1)
    float m_superSphereN = 2.0f;  // n parameter (0.6 = Star, 1.0 = Octahedron, 2.0 = Sphere, 4.0 = Rounded Cube, 10.0 = Near Cube)

    std::unique_ptr<fw::GameWorld> m_previewWorld;
};
