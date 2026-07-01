#pragma once
#include "State.h"
#include "SharedContext.h"
#include "Skeleton.h"
#include "Camera.h"
#include <string>
#include <vector>

enum class DevStructureType {
    Voxel,
    Mesh
};

struct DevStructure {
    std::string name;
    DevStructureType type;
};

class PhysicsLabState : public State {
public:
    PhysicsLabState(SharedContext* context);
    ~PhysicsLabState() override = default;

    bool Init() override;
    void Update(float dt) override;
    void Render() override;

private:
    SharedContext* m_context;
    fw::Skeleton m_skeleton;
    
    // UI e logica per i Voxel
    std::vector<DevStructure> m_devStructures;
    void RefreshDevStructures();
    
    // UI template utente
    char m_newTemplateName[128] = "";
    std::vector<std::string> m_customTemplates;
    void ScanTemplates();

    // Gestione Camera Orbitale
    glm::vec3 m_orbitTarget = glm::vec3(0, 1.0f, 0);
    float m_orbitDistance = 10.0f;
    float m_orbitYaw = 0.0f;
    float m_orbitPitch = 20.0f;
    float m_labFov = 60.0f;
    
    // Selezione e animazione procedurale
    int m_selectedJointIndex = -1;
    bool m_simulateMode = false;
    bool m_previewAnimation = false;
    float m_animationTime = 0.0f;
    int m_animationPreset = 0; // 0=Centopiedi, 1=Bipede
    int m_renderMode = 1; // 0=Mesh, 1=Skeleton, 2=Physics, 3=Texture
    int m_gizmoMode = 0; // 0=Nessuno, 1=Traduci, 2=Ruota

    // Funzioni di supporto
    void StartSimulation();
    void StopSimulation();
    void DrawSkeletonHierarchy();
    void DrawJointProperties(fw::JointData& joint);
    void DrawAngularLimitsGizmo(fw::JointData& joint, const glm::mat4& globalMat);
    void HandleArcPicking(fw::JointData& joint, const glm::mat4& globalMat, const glm::vec3& rayOrigin, const glm::vec3& rayDir);
    void GenerateBipedSkeleton();
    void GenerateCentipedeSkeleton(int segments);
    void GenerateSnakeSkeleton(int segments);
    void GenerateSpiderSkeleton();
    
    void SaveRig(const std::string& path);
    void LoadRig(const std::string& path);
    
    // Mondo
    fw::ForgeWorld* m_originalWorld = nullptr;
    fw::ForgeWorld* m_labWorld = nullptr;
};
