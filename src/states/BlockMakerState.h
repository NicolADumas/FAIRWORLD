#pragma once
#include "State.h"
#include "entt/entt.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <array>
#include "Systems.h"

#include "GameWorld.h"
#include "MeshGenerators.h"
#include "ShapeRegistry.h"
#include "BlockRegistry.h"
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
    void DrawGizmo();

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
    std::string m_lastCompileError;
    
    fw::MeshComponent m_cachedGeometryMesh;
    fw::ShapeRevision m_lastGeometryRevision = 0;
    fw::ShapeID m_lastGeometryShapeID = 0;
    int m_lastGeometryLegacyType = -1;
    float m_lastGeometryLegacyN = -1.0f;
    bool m_lastGeometryWasParametric = false;
    
    fw::ShapeParameters m_editorParams;
    fw::ShapeID m_editorShapeID = 0;
    std::string m_lastValidationError;
    
    bool m_isolateSharedShape = true;
    bool m_sharedEditConfirmed = false;
    
    // --- Block Definition State ---
    uint8_t m_selectedBlockId = 1;
    char m_inputStringId[128] = "";
    char m_inputDisplayName[128] = "";
    int m_activeTab = 0; // 0 = Transform, 1 = Identity, 2 = Graphics, 3 = Physics

    // --- Per-Block Transform Baseline Snapshots & Dirty Tracking (BM-A2.1) ---
    struct SavedTransformState {
        glm::vec3 localPosition{0.0f};
        glm::vec3 localRotation{0.0f};
        glm::vec3 localScale{1.0f};
        glm::vec3 pivotOffset{0.0f};
    };
    std::array<SavedTransformState, 256> m_savedTransforms{};

    // --- Per-Block Physics Baseline Snapshots & Dirty Tracking ---
    struct SavedPhysicsState {
        float mass = 1.0f;
        float density = 1000.0f;
        float friction = 0.6f;
        float restitution = 0.0f;
        bool collisionEnabled = true;
        fw::BlockMotionType motionType = fw::BlockMotionType::Static;
        fw::BlockMassMode massMode = fw::BlockMassMode::Manual;
        float thermal_resistance = 1.0f;
        float thermal_capacity = 1.0f;
    };
    std::array<SavedPhysicsState, 256> m_savedPhysics{};

    bool IsBlockTransformDirty(uint8_t id) const;
    bool IsBlockPhysicsDirty(uint8_t id) const;
    bool IsBlockDirty(uint8_t id) const;
    uint32_t CountUnsavedTransformBlocks() const;
    uint32_t CountUnsavedPhysicsBlocks() const;
    void SynchronizeAllSavedTransformBaselines();
    void SynchronizeAllSavedPhysicsBaselines();
    void RevertBlockPhysicsToSaved(uint8_t id);

    // --- Physics Simulation (Bouncing) ---
    bool m_simulatePhysics = false;
    float m_simPosY = 0.0f;
    float m_simVelY = 0.0f;
    float m_simGravity = -15.0f; // gravity factor

    // --- PBR Texture Loading UI ---
    bool m_isCopying = false;
    float m_copyProgress = 0.0f;
    float m_saveMessageTimer = 0.0f;

    // --- Viewport Transform Gizmo (BM-A3.1, BM-A3.2, BM-A3.3, BM-A3.4.1 & BM-A3.4.2) ---
    bool m_gizmoEnabled = true;
    int m_gizmoOperation = 0; // 0 = Move (Translate), 1 = Rotate, 2 = Scale
    int m_gizmoMode = 1;      // 0 = Local, 1 = World (BM-A3.4.1 Coordinate Space Selector, default 1 = World)
    bool m_isGizmoInteracting = false;
    glm::mat4 m_projNoFlip{1.0f};

    // --- Transform Snapping Controls (BM-A3.4.2) ---
    bool m_snapEnabled = false;
    float m_snapMove = 0.5f;     // in block/world units
    float m_snapRotate = 15.0f;  // in degrees
    float m_snapScale = 0.1f;    // dimensionless scale increment

    // --- Scale Axis Stable Drag Ownership & Diagnosis (BM-A3.3.2.2 & BM-A3.3.2.3) ---
    int m_scaleDragLatchedAxis = -1; // -1 = None, 0 = X, 1 = Y, 2 = Z
    uint32_t m_scaleLatchedBlockId = 0;
    bool m_scaleZDragLatched = false;
    bool m_isDraggingScaleZ = false;
    float m_scaleDragStartVal = 1.0f;
    glm::vec3 m_scaleDragStartScaleVec{1.0f};
    glm::vec2 m_scaleDragStartMouse{0.0f};
    glm::vec2 m_scaleProjectedAxis{0.0f, -1.0f};
    float m_scaleProjectedHandleLength = 100.0f;
    float m_scaleDragStartGroundY = 0.0f;
    float m_scaleLastInputTransY = 0.0f;

    void UpdatePreviewMesh();
    void HandlePhysicsSimulation(float dt);

    // --- Parametric Shape controls ---
    int m_selectedShapeIndex = 0; // 0 = Standard Cube, 1 = SuperSphere (|x|^n + |y|^n + |z|^n = 1)
    float m_superSphereN = 2.0f;  // n parameter (0.6 = Star, 1.0 = Octahedron, 2.0 = Sphere, 4.0 = Rounded Cube, 10.0 = Near Cube)

    // --- BM-A3 Final Freeze Gate Telemetry & Math Verification ---
    struct ZeroWorkTelemetry {
        uint32_t geoCompilerCalls = 0;
        uint32_t meshGenCalls = 0;
        uint32_t gpuUploadCalls = 0;
        uint32_t shapeRevIncrements = 0;
        uint32_t ecsTransformWrites = 0;
        uint32_t jsonSaveCalls = 0;
        void Reset() {
            geoCompilerCalls = 0;
            meshGenCalls = 0;
            gpuUploadCalls = 0;
            shapeRevIncrements = 0;
            ecsTransformWrites = 0;
            jsonSaveCalls = 0;
        }
    } m_zeroWorkTelemetry;

    struct MathTestResult {
        int id = 0;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        double errorValue = 0.0;
        double tolerance = 0.0;
        bool passed = false;
        std::string fileAndFunc;
    };
    std::vector<MathTestResult> m_mathTestResults;
    bool m_mathTestsExecuted = false;

    // --- BM-A3 Micro-Gate Test Harness (MG-03, MG-04, MG-05, MG-06, MG-07) ---
    struct GateTestResult {
        std::string gateId;
        int testIndex = 0;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        double errorValue = 0.0;
        double tolerance = 0.0;
        bool passed = false;
        std::string fileAndFunc;
    };
    std::vector<GateTestResult> m_gateTestResults;
    bool m_gateTestsExecuted = false;

    void RunZeroWorkValidationTests();
    void RunBlockIsolationValidationTests();
    void RunDirtyPersistenceValidationTests();
    void RunAllFreezeGateTests(bool isManualTrigger = false);

    void RunMathValidationTests(bool isManualTrigger = false);

    std::unique_ptr<fw::GameWorld> m_previewWorld;
};
