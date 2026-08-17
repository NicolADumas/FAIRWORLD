#pragma once
#include "State.h"
#include "SharedContext.h"
#include "Skeleton.h"
#include "Camera.h"
#include "GlobalAssetBrowser.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

namespace fw {
    class JoltPhysicsSystem;
}

namespace JPH {
    class Body;
    class Constraint;
}

class PhysicsLabState : public State {
public:
    PhysicsLabState(SharedContext* context);
    ~PhysicsLabState() override;

    bool Init() override;
    void Update(float dt) override;
    void Render() override;

private:
    SharedContext* m_context;
    fw::Skeleton m_skeleton;
    
    // Global Asset Browser
    fw::GlobalAssetBrowser m_assetBrowser;
    bool m_showAssetBrowser = false;
    

    
    // UI template utente
    char m_newTemplateName[128] = "";
    std::vector<std::string> m_customTemplates;
    void ScanTemplates();

    // Gestione Camera Orbitale
    glm::vec3 m_orbitTarget = glm::vec3(0, 0.0f, 0);
    float m_orbitDistance = 12.0f;
    float m_orbitYaw = 30.0f;
    float m_orbitPitch = 25.0f;
    float m_labFov = 60.0f;
    
    // BUG FIX #2: Due proiezioni separate.
    // m_projRenderer → con [1][1]*=-1 per Vulkan (passata all'UBO).
    // m_projEditor   → senza flip, per ImGuizmo, overlay 2D e raycast.
    glm::mat4 m_projEditor = glm::mat4(1.0f);
    
    // Selezione e animazione procedurale
    int m_selectedJointIndex = -1;
    bool m_simulateMode = false;
    bool m_previewAnimation = false;
    float m_animationTime = 0.0f;
    int m_animationPreset = 0;
    int m_renderMode = 1; // 0=Mesh, 1=Skeleton X-Ray, 2=Physics Debug, 3=Textured
    int m_gizmoMode = 0;  // 0=Off, 1=Translate, 2=Rotate

    // --- Metriche di Combattimento (Arena) ---
    struct DamageEvent {
        glm::vec3 position;
        float damage;
        float timer;
        float maxTime = 1.0f;
        glm::vec3 randomOffset;
    };
    std::vector<DamageEvent> m_damageEvents;
    float m_dpsAccumulator = 0.0f;
    float m_dpsTimer = 0.0f;
    float m_currentDPS = 0.0f;
    
    void SpawnDamageNumber(glm::vec3 worldPos, float damage);
    void DrawCombatMetrics();

    // Colori per giunto (color picker)
    std::unordered_map<int, glm::vec4> m_jointColors;

    // Funzioni di supporto
    void StartSimulation();
    void StopSimulation();
    
    fw::GameWorld* m_labWorld = nullptr;
    fw::JoltPhysicsSystem* m_joltSystem = nullptr;
    entt::entity m_playerEntity = entt::null;

    uint32_t m_floorBodyID = 0;
    std::vector<uint32_t> m_joltBodies;      // Track body IDs (uint32_t to avoid full JPH headers)
    std::vector<void*> m_joltConstraints;   // Track constraints (void* to avoid full JPH headers)
    
    // Metodi UI ImGui
    void DrawTimeline();
    void DrawViewportOverlay();   // Griglia + overlay ossa + assi XYZ
    void DrawSkeletonHierarchy();
    void DrawJointProperties(fw::JointData& joint);
    void DrawAngularLimitsGizmo(fw::JointData& joint, const glm::mat4& globalMat);
    void HandleArcPicking(fw::JointData& joint, const glm::mat4& globalMat, const glm::vec3& rayOrigin, const glm::vec3& rayDir);
    
    // Timeline state
    int m_currentFrame = 0;
    int m_maxFrames = 60;
    bool m_isPlaying = false;

    // Generatori Scheletro
    void GenerateBipedSkeleton();
    void GenerateCentipedeSkeleton(int segments);
    void GenerateSnakeSkeleton(int segments);
    void GenerateSpiderSkeleton();
    void GenerateGargoyleSkeleton();
    void GenerateQuadrupedSkeleton();
    void GenerateOctopusSkeleton();
    
    void SaveRig(const std::string& path);
    void LoadRig(const std::string& path);
    
    // Mondo
    fw::GameWorld* m_originalWorld = nullptr;
};
