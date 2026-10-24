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
#include "MaterialRegistry.h"
#include "blocks/factory/FactoryOrchestrator.h"
#include "blocks/sculpt/EditableMesh.h"
#include "blocks/sculpt/MeshSelectionService.h"
#include "blocks/sculpt/MeshModelingService.h"
#include "blocks/sculpt/MeshSculptService.h"
#include "blocks/sculpt/MeshTopologyService.h"
#include "blocks/sculpt/MeshRepairService.h"
#include "blocks/sculpt/MeshHistoryService.h"
#include "blocks/sculpt/MeshBridgeService.h"
#include "blocks/texture/TextureContractTests.h"
#include "blocks/texture/PaintContractTests.h"
#include "blocks/texture/TexturePaintingService.h"
#include "blocks/factory/PainterStationContractTests.h"
#include "blocks/factory/TextureBakingContractTests.h"
#include "blocks/factory/AssetPackagingContractTests.h"
#include "blocks/factory/TransactionIntegrityAuditTests.h"
#include "blocks/factory/SculptedMeshRuntimeBridgeTests.h"
#include "blocks/factory/AssetIntegrityRoundTripTests.h"
#include "blocks/factory/AssetIntegrityHardeningTests.h"
#include "blocks/factory/AssetDigestTrustTests.h"
#include "blocks/factory/AssetCrashRecoveryTests.h"
#include "blocks/factory/AssetCrashRecoveryHardeningTests.h"
#include "blocks/factory/AssetDependencyContractTests.h"
#include "blocks/factory/AssetSchemaMigrationTests.h"
#include "blocks/texture/SurfacePaintContractTests.h"
#include "blocks/texture/SurfacePaintingAdapter.h"
#include "blocks/uv/PrimitiveUVContractTests.h"
#include "blocks/uv/PrimitiveUVMapper.h"
#include "blocks/uv/UVEditorDocument.h"
#include "blocks/uv/UVEditorPanel.h"
#include "blocks/uv/TextureReprojectionService.h"
#include "blocks/factory/FactoryEditSessionContext.h"
#include "imgui.h"
struct SharedContext;

class BlockMakerState : public State {
public:
    explicit BlockMakerState(SharedContext* context);
    ~BlockMakerState() override;

    bool Init() override;
    void Update(float dt) override;
    void Render() override;

    entt::registry* GetRegistry() override;

    // --- BM-UI2.3-B1 Factory Edit Session Accessors ---
    fw::FactoryEditSessionContext& GetEditSessionContext() { return m_sessionContext; }
    const fw::FactoryEditSessionContext& GetEditSessionContext() const { return m_sessionContext; }
    fw::EditableMesh& GetActiveEditableMesh() { return m_activeEditableMesh; }
    const fw::EditableMesh& GetActiveEditableMesh() const { return m_activeEditableMesh; }

private:
    void DrawUI();
    void DrawGizmo();

    // --- BM-UI2: Workspace Shell Dock-Free State & Methods ---
    bool m_useWorkspaceLayout = true; // Modalità Layout Workspace Dock-Free (true) / Finestra Legacy (false)
    bool m_footerCollapsed = false;   // Stato del footer inferiore richiudibile
    float m_leftPanelWidth = 260.0f;  // Larghezza adattiva del pannello sinistro
    float m_rightPanelWidth = 380.0f; // Larghezza adattiva del pannello destro

    // --- BM-UI2.2: Unified Factory Inspector Context ---
    enum class InspectorContext {
        GeneralSettings = 0,
        FactoryStation = 1
    };
    InspectorContext m_inspectorContext = InspectorContext::GeneralSettings;

    void DrawWorkspaceShell();
    void DrawWorkspaceTopMenuBar(float& outTopBarHeight);
    void DrawWorkspaceToolbar(float topY, float& outToolbarHeight);
    void DrawWorkspaceLeftPanel(float topY, float bottomY, float panelWidth);
    void DrawWorkspaceRightInspector(float topY, float bottomY, float panelWidth);
    void DrawWorkspaceFooter(float& outFooterHeight);

    void DrawAssetBrowserContents();
    void DrawInspectorContents();
    void DrawLegacyFloatingWindowUI();
    void ReturnToHub();

    // --- BM-UI2.2-A: Modular UI Extraction for General Settings & Factory Tools ---
    void DrawGeneralTransformUI(fw::SimBlockDef& def);
    void DrawGeneralIdentityUI(fw::SimBlockDef& def);
    void DrawGeneralGraphicsUI(fw::PBRMaterialDef& mat, bool& isDirty);
    void DrawGeneralPhysicsUI(fw::SimBlockDef& def);

    void DrawFactoryShapeControls(fw::PBRMaterialDef& mat, bool& isDirty);
    void DrawFactorySculptControls();
    void DrawFactorySurfaceUVControls(fw::PBRMaterialDef& mat, bool& isDirty);
    void DrawFactoryTextureControls(fw::PBRMaterialDef& mat, bool& isDirty);
    void DrawDiagnosticsControls(const fw::SimBlockDef& def, const fw::PBRMaterialDef& mat);

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
    uint8_t m_lastCompiledBlockId = 0; // Tracciamento reattivo dell'ultimo blocco compilato per anteprima
    int m_lastGeometryLegacyType = -1;
    float m_lastGeometryLegacyN = -1.0f;
    bool m_lastGeometryWasParametric = false;

    // Diagnostica scrolling Inspector (BM-UI2.1)
    float m_inspectorScrollY = 0.0f;
    float m_inspectorScrollMaxY = 0.0f;
    float m_inspectorContentH = 0.0f;
    float m_inspectorVisibleH = 0.0f;
    bool m_inspectorHovered = false;
    
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

    // --- BM-A4 Asset Management State & UI Controls ---
    char m_blockFilter[64] = "";
    char m_newBlockStringId[64] = "";
    char m_newBlockDisplayName[64] = "";
    bool m_showCreateDialog = false;
    bool m_showDeleteConfirmDialog = false;
    uint8_t m_blockPendingDelete = 0;
    std::string m_assetOpFeedback;
    float m_assetOpFeedbackTimer = 0.0f;
    bool m_assetOpFeedbackIsError = false;

    void SetAssetFeedback(const std::string& msg, bool isError = false);
    void ExecuteCreateBlock(const std::string& stringId, const std::string& displayName);
    void ExecuteDuplicateBlock(uint8_t sourceId);
    void ExecuteDeleteBlock(uint8_t blockId);
    void ExecuteRenameBlock(uint8_t blockId, const std::string& newStringId, const std::string& newDisplayName);

    // --- BM-A4 Asset Management Validation (BM-A4.8) ---
    struct AssetTestResult {
        std::string testId;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        bool passed = false;
        std::string details;
    };
    std::vector<AssetTestResult> m_assetTestResults;
    bool m_assetTestsExecuted = false;
    void RunAssetManagementValidationTests(bool isManualTrigger = false);

    // --- BM-B Geometry System Validation (BM-B7: GM-1 .. GM-7) ---
    struct GeometryTestResult {
        std::string testId;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        bool passed = false;
        std::string details;
    };
    std::vector<GeometryTestResult> m_geometryTestResults;
    bool m_geometryTestsExecuted = false;
    void RunGeometryValidationTests(bool isManualTrigger = false);

    // --- BM-C UV & Surface System State & UI ---
    int m_selectedSurfaceIndex = 0;
    uint32_t m_lastMaterialRevision = 0;

    // --- BM-C UV & Surface Validation (BM-C8: UV-1 .. UV-7) ---
    struct UVTestResult {
        std::string testId;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        bool passed = false;
        std::string details;
    };
    std::vector<UVTestResult> m_uvTestResults;
    bool m_uvTestsExecuted = false;
    void RunUVValidationTests(bool isManualTrigger = false);

    // --- BM-FC Factory Core System & Orchestrator ---
    fw::FactoryOrchestrator m_factoryOrchestrator;
    std::string m_factoryOpFeedback;
    float m_factoryOpFeedbackTimer = 0.0f;
    bool m_factoryOpFeedbackIsError = false;

    // --- BM-FC Validation Suite (FC-1 .. FC-10) ---
    struct FactoryTestResult {
        std::string testId;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        bool passed = false;
        std::string details;
    };
    std::vector<FactoryTestResult> m_factoryTestResults;
    bool m_factoryTestsExecuted = false;
    void RunFactoryCoreValidationTests(bool isManualTrigger = false);

    // --- BM-S Sculpt & Modelling Validation Suite (SM-1 .. SM-10) ---
    struct SculptTestResult {
        std::string testId;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        bool passed = false;
        std::string details;
    };
    std::vector<SculptTestResult> m_sculptTestResults;
    bool m_sculptTestsExecuted = false;
    void RunSculptValidationTests(bool isManualTrigger = false);

    // --- BM-S3 Mesh Selection System & Controls ---
    fw::MeshSelectionService m_selectionService;
    fw::EditableMesh m_activeEditableMesh;
    bool m_meshSelectionEnabled = false;
    bool m_boxSelectActive = false;
    glm::vec2 m_boxSelectStart{0.0f};

    // --- BM-S3 Selection Validation Suite (SEL-1 .. SEL-10) ---
    struct SelectionTestResult {
        std::string testId;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        bool passed = false;
        std::string details;
    };
    std::vector<SelectionTestResult> m_selectionTestResults;
    bool m_selectionTestsExecuted = false;
    void RunSelectionValidationTests(bool isManualTrigger = false);

    void DrawMeshSelectionVisualFeedback();

    // --- BM-S4 Model Operations State & Parameters ---
    int m_modelingTool = 0; // 0 = None, 1 = Move, 2 = Rotate, 3 = Scale, 4 = Extrude, 5 = Inset, 6 = Bevel
    glm::vec3 m_modelMoveDelta{0.0f, 0.1f, 0.0f};
    float m_modelRotateAngle = 45.0f;
    int m_modelRotateAxis = 1; // 0 = X, 1 = Y, 2 = Z
    glm::vec3 m_modelScaleVec{1.2f, 1.2f, 1.2f};
    float m_modelExtrudeOffset = 0.2f;
    bool m_modelExtrudeRegion = true;
    float m_modelInsetAmount = 0.1f;
    float m_modelInsetDepth = 0.0f;
    float m_modelBevelWidth = 0.05f;
    std::optional<fw::EditableMesh> m_modelingStagedBackup;
    std::string m_modelingFeedbackMsg;
    float m_modelingFeedbackTimer = 0.0f;
    bool m_modelingFeedbackIsError = false;

    // --- BM-S4 Modeling Validation Suite (MOD-1 .. MOD-10) ---
    struct ModelingTestResult {
        std::string testId;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        bool passed = false;
        std::string details;
    };
    std::vector<ModelingTestResult> m_modelingTestResults;
    bool m_modelingTestsExecuted = false;
    void RunModelingValidationTests(bool isManualTrigger = false);
    void ApplyModelingOperation(fw::ModelingOperationType opType);

    // --- BM-S5 Sculpt Brush Engine State & Parameters ---
    fw::MeshSculptService m_sculptService;
    bool m_sculptModeActive = false;
    bool m_isSculptStroking = false;
    fw::StrokePoint m_lastSculptStrokePoint;
    bool m_sculptLiveGpuDirty = false;
    float m_sculptLiveGpuCooldown = 0.0f; // Coalescenza aggiornamenti GPU live (~30 FPS max in drag)

    // --- BM-S5 Sculpt Brush Validation Suite (SCL-1 .. SCL-12) ---
    struct SculptBrushTestResult {
        std::string testId;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        bool passed = false;
        std::string details;
    };
    std::vector<SculptBrushTestResult> m_sculptBrushTestResults;
    bool m_sculptBrushTestsExecuted = false;
    void RunSculptBrushValidationTests(bool isManualTrigger = false);
    void DrawSculptBrushVisualFeedback();

    // --- BM-S6 Topology Operations Validation Suite (TOP-1 .. TOP-10) ---
    struct TopologyTestResult {
        std::string testId;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        bool passed = false;
        std::string details;
    };
    std::vector<TopologyTestResult> m_topologyTestResults;
    bool m_topologyTestsExecuted = false;
    void RunTopologyValidationTests(bool isManualTrigger = false);

    // --- BM-S6 Subdivision State & Complexity Safety API (BM-S2.2 / BM-S2.3) ---
    int m_subdivideIterations = 1;
    bool m_subdivideSmooth = false;
    static constexpr uint64_t GPU_VRAM_BUCKET_MAX_BYTES = 4194304ULL; // 4 MB (VramSlabAllocator max bucket)
    static constexpr uint64_t GPU_VRAM_MAX_VERTICES = 65536ULL;        // 4 MB / 64 bytes
    static constexpr uint64_t GPU_VRAM_MAX_TRIANGLES = 21845ULL;       // 65536 / 3
    static constexpr uint64_t MAX_SUBDIVISION_FACE_BUDGET = 10922ULL;   // 21845 / 2
    static uint64_t EstimateSubdivisionFaceCount(const fw::EditableMesh& mesh, int iterations);
    static uint64_t EstimateSubdivisionTriangleCount(const fw::EditableMesh& mesh, int iterations);
    static uint64_t ComputeRenderedTriangleCount(const fw::EditableMesh& mesh);
    void ApplySubdivideOperation();

    // --- BM-S7 Remeshing & Manifold Repair Validation Suite (REP-1 .. REP-10) ---
    struct RepairTestResult {
        std::string testId;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        bool passed = false;
        std::string details;
    };
    std::vector<RepairTestResult> m_repairTestResults;
    bool m_repairTestsExecuted = false;
    void RunRepairValidationTests(bool isManualTrigger = false);

    // --- BM-S8 History & Undo/Redo Engine ---
    fw::MeshHistoryService m_meshHistoryService;
    void ExecuteMeshUndo();
    void ExecuteMeshRedo();

    struct HistoryTestResult {
        std::string testId;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        bool passed = false;
        std::string details;
    };
    std::vector<HistoryTestResult> m_historyTestResults;
    bool m_historyTestsExecuted = false;
    void RunHistoryValidationTests(bool isManualTrigger = false);

    // --- BM-S9 GeometryCompiler Bridge Validation Suite (BRG-1 .. BRG-10) ---
    std::vector<fw::BridgeTestResult> m_bridgeTestResults;
    bool m_bridgeTestsExecuted = false;
    void RunBridgeValidationTests(bool isManualTrigger = false);
    fw::BridgePreviewCache m_bridgePreviewCache;

    // --- BM-TX2 Texture Engine & PBR Importer Validation Suite (TX-1 .. TX-10) ---
    std::vector<fw::TextureTestResult> m_textureTestResults;
    bool m_textureTestsExecuted = false;
    void RunTextureValidationTests(bool isManualTrigger = false);

    // --- BM-TX3 Painting & Layer Engine Validation Suite (PT-1 .. PT-10) ---
    std::vector<fw::PaintTestResult> m_paintTestResults;
    bool m_paintTestsExecuted = false;
    void RunPaintValidationTests(bool isManualTrigger = false);
    fw::TexturePaintingService m_paintingService;

    // --- BM-TX4 3D Surface Brush & Painting Suite (BP-1 .. BP-15) ---
    std::vector<fw::SurfacePaintTestResult> m_surfacePaintTestResults;
    bool m_surfacePaintTestsExecuted = false;
    void RunSurfacePaintValidationTests(bool isManualTrigger = false);
    fw::SurfacePaintingAdapter m_surfacePaintingAdapter;

    // --- BM-F2A Painter Station Contract Suite (PNT-01 .. PNT-08) ---
    std::vector<fw::PainterStationTestResult> m_painterStationTestResults;
    bool m_painterStationTestsExecuted = false;
    void RunPainterStationValidationTests(bool isManualTrigger = false);

    // --- BM-F2B Texture Atlas Baking Suite (TXB-01 .. TXB-10) ---
    std::vector<fw::TextureBakingTestResult> m_textureBakingTestResults;
    bool m_textureBakingTestsExecuted = false;
    void RunTextureBakingValidationTests(bool isManualTrigger = false);

    // --- BM-F2C Asset Packaging & Registry Binding Suite (PKG-01 .. PKG-10) ---
    std::vector<fw::AssetPackagingTestResult> m_assetPackagingTestResults;
    bool m_assetPackagingTestsExecuted = false;
    void RunAssetPackagingValidationTests(bool isManualTrigger = false);

    // --- BM-F2C.1 Transaction Integrity & Persistence Audit Suite (TRX-01 .. TRX-10) ---
    std::vector<fw::TransactionAuditTestResult> m_transactionAuditTestResults;
    bool m_transactionAuditTestsExecuted = false;
    void RunTransactionAuditValidationTests(bool isManualTrigger = false);

    // --- BM-F3 Sculpted Mesh Runtime Bridge Suite (SBR-01 .. SBR-10) ---
    std::vector<fw::SculptedBridgeTestResult> m_sculptedBridgeTestResults;
    bool m_sculptedBridgeTestsExecuted = false;
    void RunSculptedMeshBridgeValidationTests(bool isManualTrigger = false);

    // --- BM-F4 Asset Integrity & End-to-End Round-Trip Suite (INT-01 .. INT-10) ---
    std::vector<fw::AssetIntegrityTestResult> m_assetIntegrityTestResults;
    bool m_assetIntegrityTestsExecuted = false;
    void RunAssetIntegrityValidationTests(bool isManualTrigger = false);

    // --- BM-F4.1 Asset Integrity Hardening Gate Suite (SEC-01 .. SEC-10) ---
    std::vector<fw::AssetHardeningTestResult> m_assetHardeningTestResults;
    bool m_assetHardeningTestsExecuted = false;
    void RunAssetIntegrityHardeningValidationTests(bool isManualTrigger = false);

    // --- BM-F4.2 Digest Verification, Trust Policy & Zero-Work Safety Suite (DGV-01 .. DGV-10) ---
    std::vector<fw::AssetDigestTestResult> m_assetDigestTestResults;
    bool m_assetDigestTestsExecuted = false;
    void RunAssetDigestTrustValidationTests(bool isManualTrigger = false);

    // --- BM-F4.3 Transactional Asset Publication & Crash Recovery Suite (CRH-01 .. CRH-10) ---
    std::vector<fw::AssetCrashRecoveryTestResult> m_assetCrashRecoveryTestResults;
    bool m_assetCrashRecoveryTestsExecuted = false;
    void RunAssetCrashRecoveryValidationTests(bool isManualTrigger = false);

    // --- BM-F4.3.1 Crash Recovery Hardening & Commit Semantics Suite (CRH-H01 .. CRH-H08) ---
    std::vector<fw::CrashRecoveryHardeningTestResult> m_crashRecoveryHardeningTestResults;
    bool m_crashRecoveryHardeningTestsExecuted = false;
    void RunCrashRecoveryHardeningValidationTests(bool isManualTrigger = false);

    // --- BM-F4.4 Asset Dependency Graph & Referential Integrity Suite (DEP-01 .. DEP-10) ---
    std::vector<fw::AssetDependencyTestResult> m_assetDependencyTestResults;
    bool m_assetDependencyTestsExecuted = false;
    void RunAssetDependencyValidationTests(bool isManualTrigger = false);

    // --- BM-F4.5 Asset Schema Migration & Backward Compatibility Suite (MIG-01 .. MIG-10) ---
    std::vector<fw::AssetSchemaMigrationTestResult> m_assetSchemaMigrationTestResults;
    bool m_assetSchemaMigrationTestsExecuted = false;
    void RunAssetSchemaMigrationValidationTests(bool isManualTrigger = false);

    // --- BM-UV2 Universal UV Contract & Primitive Mapping Suite (UVP-1 .. UVP-15) ---
    std::vector<fw::PrimitiveUVTestResult> m_primitiveUVTestResults;
    bool m_primitiveUVTestsExecuted = false;
    void RunPrimitiveUVValidationTests(bool isManualTrigger = false);
    fw::PrimitiveUVMapper m_primitiveUVMapper;

    // --- BM-PAINTER-RECOVERY-AND-STUDIO-MG01B: Unified UV & Painter Studio ---
    float m_studioSplitterRatio = 0.5f;
    bool m_studioSplitterDragging = false;
    bool IsStudioActive() const;
    void DrawStudioCenterCanvas(float leftX, float topY, float width, float height);
    void DrawStudioTopToolbar();
    void DrawStudioRightInspector();

    // --- BM-UV8 2D UV Editor State & UI ---
    fw::UVEditorPanel m_uvEditorPanel;
    std::unique_ptr<fw::UVEditorDocument> m_uvEditorDocument;
    std::vector<fw::UVEditorPanel::IslandDrawOutline> m_cachedUVOutlines;
    uint64_t m_cachedUVOutlineTopologyRev = 0;
    uint64_t m_cachedUVOutlineAttrRev = 0;
    uint64_t m_cachedUVOutlineAtlasRev = 0;
    uint64_t m_cachedUVOutlineSelectionRev = 0;
    bool m_uvCanvasPaintMode = false;
    fw::CanvasPaintMode m_canvasPaintMode = fw::CanvasPaintMode::SurfacePaint;
    std::string m_studioPaintNotification;
    float m_studioPaintNotificationTimer = 0.0f;
    void DrawUVEditorStationUI();
    void PrintUVStudioDiagnostic();
    void AuditCubeUVPipeline();

    // --- MG03-FIX04 Pipeline Status & UV Remap ---
    enum class UVPipelineStatus : uint8_t {
        NO_TOPOLOGY = 0,
        NO_UV_LAYOUT,
        INVALID_UV_LAYOUT,
        NO_ISLANDS,
        NO_OUTLINES,
        DOCUMENT_STALE,
        READY
    };
    struct UVPipelineDiagnostic {
        UVPipelineStatus status = UVPipelineStatus::READY;
        std::string statusReason;
        std::string shapeName;
        uint8_t shapeType = 0;
        bool hasCompiledTopology = false;
        bool hasUVLayout = false;
        bool isUVLayoutValid = false;
        bool isDocumentInSync = false;
        size_t faceCount = 0;
        size_t triangleCount = 0;
        size_t islandCount = 0;
        size_t outlineCount = 0;
        bool isLegacyFallbackActive = false;
        const char* geometrySource = "Unknown";
    };
    UVPipelineDiagnostic EvaluateUVPipelineDiagnostic() const;
    void RemapActivePrimitiveUV();

    // --- BM-UV8 3D Painting Viewport Input State ---
    bool m_is3DPaintStroking = false;
    fw::PaintBrush m_paintBrush;
    fw::PaintChannel m_activePaintChannel = fw::PaintChannel::BaseColor;
    void Handle3DPaintInput();
    void ComputeMouseRay(const ImVec2& mousePos, glm::vec3& outOrigin, glm::vec3& outDir);

    // --- BM-UI2.3-B1 Factory Edit Session Lifecycle & Data Ownership ---
    fw::FactoryEditSessionContext m_sessionContext;
    void FlushCurrentDraft();
    void LoadFactoryDraft(uint8_t blockId);
    void SynchronizeDraftToPreview();
    void CommitDraftToRegistry();
    void RevertCurrentDraftToSaved();
    void UpdateSessionContext();

    std::unique_ptr<fw::GameWorld> m_previewWorld;
};
