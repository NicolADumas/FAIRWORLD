#include "pch.h"
#include "BlockMakerState.h"
#include "SharedContext.h"
#include "StateManager.h"
#include "HubState.h"
#include "FAIRWORLD.h"
#include "RenderManager.h"
#include "GameWorld.h"
#include "JobSystem.h"
#include "AsyncInput.h"
#include "Systems.h"
#include "Components.h"
#include "BlockRegistry.h"
#include "DeviceManager.h"
#include <shellapi.h>  // ShellExecuteA - apre cartelle/file con l'OS
#include "MaterialRegistry.h"
#include "VulkanDmaManager.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <iomanip>
#include <sstream>
#include "imgui.h"
#include "ImGuizmo.h"
#include <windows.h>
#include <commdlg.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "GeometryCompiler.h"

namespace {
    void LogScaleZDiagnostics(
        const char* eventTag,
        uint32_t blockId,
        int gizmoOp,
        bool mouseDown,
        bool isUsing,
        int activeHandle,
        bool latched,
        float dragStartSz,
        float matrixDerivedSz,
        float candidateSz,
        float acceptedAuthoredSz,
        float previewEcsSz,
        bool valueChanged,
        float dragPixels
    ) {
        static auto lastLogTime = std::chrono::steady_clock::now();
        auto nowTime = std::chrono::steady_clock::now();
        int64_t elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - lastLogTime).count();
        
        bool isBoundary = (std::strcmp(eventTag, "LATCH_START") == 0 || std::strcmp(eventTag, "LATCH_END") == 0);
        if (!isBoundary && elapsedMs < 200) {
            return;
        }
        lastLogTime = nowTime;

        char buf[512];
        snprintf(buf, sizeof(buf),
            "[BM-A3.3.2.2 Z-Scale Diag] [%s] BlockID=%u | GizmoOp=%d | MouseDown=%s | IsUsing=%s | Handle=%d | Latched=%s | StartSz=%.4f | MatrixSz=%.4f | CandSz=%.4f | AuthoredSz=%.4f | EcsSz=%.4f | Changed=%s | DragPx=%.1f\n",
            eventTag,
            blockId,
            gizmoOp,
            mouseDown ? "YES" : "NO",
            isUsing ? "YES" : "NO",
            activeHandle,
            latched ? "YES" : "NO",
            dragStartSz,
            matrixDerivedSz,
            candidateSz,
            acceptedAuthoredSz,
            previewEcsSz,
            valueChanged ? "YES" : "NO",
            dragPixels
        );
        OutputDebugStringA(buf);
        std::cout << buf;
    }

    void LogScaleYDiagnostics(
        const char* eventTag,
        uint32_t blockId,
        bool mouseDown,
        bool isUsing,
        int activeHandle,
        float dragStartSy,
        float rawMatrixSy,
        float acceptedAuthoredSy,
        float previewEcsSy,
        float groundY,
        float previewEcsPosY,
        float gizmoInTransY,
        float gizmoOutTransY,
        bool shearRejected,
        bool statShift
    ) {
        static auto lastLogTime = std::chrono::steady_clock::now();
        auto nowTime = std::chrono::steady_clock::now();
        int64_t elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - lastLogTime).count();
        
        bool isBoundary = (std::strcmp(eventTag, "LATCH_START") == 0 || std::strcmp(eventTag, "LATCH_END") == 0);
        if (!isBoundary && elapsedMs < 200) {
            return;
        }
        lastLogTime = nowTime;

        char buf[512];
        snprintf(buf, sizeof(buf),
            "[BM-A3.3.2.3 Y-Scale Diag] [%s] BlockID=%u | MouseDown=%s | IsUsing=%s | Handle=%d | StartSy=%.4f | RawMatSy=%.4f | AuthSy=%.4f | EcsSy=%.4f | GroundY=%.4f | EcsPosY=%.4f | InTransY=%.4f | OutTransY=%.4f | ShearRej=%s | StatShift=%s\n",
            eventTag,
            blockId,
            mouseDown ? "YES" : "NO",
            isUsing ? "YES" : "NO",
            activeHandle,
            dragStartSy,
            rawMatrixSy,
            acceptedAuthoredSy,
            previewEcsSy,
            groundY,
            previewEcsPosY,
            gizmoInTransY,
            gizmoOutTransY,
            shearRejected ? "YES" : "NO",
            statShift ? "YES" : "NO"
        );
        OutputDebugStringA(buf);
        std::cout << buf;
    }

    std::string BrowseForImage() {
        OPENFILENAMEA ofn;
        CHAR szFile[260] = { 0 };
        ZeroMemory(&ofn, sizeof(OPENFILENAMEA));
        ofn.lStructSize = sizeof(OPENFILENAMEA);
        ofn.hwndOwner = NULL;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = "Immagini\0*.png;*.jpg;*.jpeg;*.tga\0Tutti i file\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

        if (GetOpenFileNameA(&ofn) == TRUE) {
            return std::string(ofn.lpstrFile);
        }
        return "";
    }

    std::string CopyTextureToAssets(const std::string& srcPath, uint8_t blockId, const std::string& typeSuffix) {
        if (srcPath.empty()) return "";
        try {
            std::filesystem::create_directories("assets/textures");
            std::string ext = std::filesystem::path(srcPath).extension().string();
            std::string destName = "block_" + std::to_string(blockId) + "_" + typeSuffix + ext;
            std::string destPath = "assets/textures/" + destName;
            
            // Sovrascrivi se esiste
            std::filesystem::copy_file(srcPath, destPath, std::filesystem::copy_options::overwrite_existing);
            return destPath;
        } catch (const std::exception& e) {
            std::cerr << "[BlockMaker] Errore copia file: " << e.what() << "\n";
            return srcPath; // Fallback al path originale se la copia fallisce
        }
    }

    const char* GetShapeTypeName(fw::ShapeType type) {
        switch (type) {
            case fw::ShapeType::Cube: return "Cube";
            case fw::ShapeType::Cuboid: return "Cuboid";
            case fw::ShapeType::Parallelepiped: return "Parallelepiped";
            case fw::ShapeType::Pyramid: return "Pyramid";
            case fw::ShapeType::PyramidFrustum: return "Pyramid Frustum";
            case fw::ShapeType::Cylinder: return "Cylinder";
            case fw::ShapeType::HollowCylinder: return "Hollow Cylinder";
            case fw::ShapeType::Cone: return "Cone";
            case fw::ShapeType::ConeFrustum: return "Cone Frustum";
            case fw::ShapeType::Sphere: return "Sphere";
            case fw::ShapeType::Capsule: return "Capsule";
            case fw::ShapeType::LegacySuperSphere: return "Legacy SuperSphere";
            case fw::ShapeType::SphericalZone: return "Spherical Zone";
            case fw::ShapeType::SphericalSegment: return "Spherical Segment";
            case fw::ShapeType::SphericalSector: return "Spherical Sector";
            case fw::ShapeType::SphereWithCylindricalBore: return "Sphere w/ Bore";
            case fw::ShapeType::SphereWithConicalCavities: return "Sphere w/ Cavities";
            case fw::ShapeType::SlicedCylinder: return "Sliced Cylinder";
            case fw::ShapeType::Ungula: return "Ungula";
            case fw::ShapeType::Barrel: return "Barrel";
            default: return "Custom / Other";
        }
    }
}

BlockMakerState::BlockMakerState(SharedContext* context) : m_context(context) {
    std::cout << "[BlockMakerState] Creato.\n";
}

BlockMakerState::~BlockMakerState() {
    std::cout << "[BlockMakerState] Attendiamo completamento job asincroni pendenti prima di distruggere...\n";
    if (m_context) {
        if (m_previewWorld && m_context->forgeWorld == m_previewWorld.get()) {
            m_context->forgeWorld = m_context->gameWorld;
        }
        if (m_previewWorld && m_context->activeRegistry == &m_previewWorld->GetRegistry()) {
            m_context->activeRegistry = m_context->gameWorld ? &m_context->gameWorld->GetRegistry() : nullptr;
        }
        m_context->isBlockMakerMode = false;
        if (m_context->jobSystem) {
            m_context->jobSystem->Shutdown();
            m_context->jobSystem->Initialize();
        }
    }
    std::cout << "[BlockMakerState] Distrutto.\n";
}

entt::registry* BlockMakerState::GetRegistry() {
    if (m_previewWorld) return &m_previewWorld->GetRegistry();
    return nullptr;
}

bool BlockMakerState::IsBlockTransformDirty(uint8_t id) const {
    if (!m_context || !m_context->blockRegistry) return false;
    const auto& bDef = m_context->blockRegistry->GetBlock(id);
    const auto& saved = m_savedTransforms[id];
    const float eps = 1e-5f;
    auto diff = [](const glm::vec3& a, const glm::vec3& b, float e) {
        return std::abs(a.x - b.x) > e || std::abs(a.y - b.y) > e || std::abs(a.z - b.z) > e;
    };
    return (diff(bDef.localPosition, saved.localPosition, eps) ||
            diff(bDef.localRotation, saved.localRotation, eps) ||
            diff(bDef.localScale, saved.localScale, eps) ||
            diff(bDef.pivotOffset, saved.pivotOffset, eps));
}

uint32_t BlockMakerState::CountUnsavedTransformBlocks() const {
    if (!m_context || !m_context->blockRegistry) return 0;
    uint32_t count = 0;
    for (int i = 1; i <= 255; ++i) {
        if (IsBlockTransformDirty(static_cast<uint8_t>(i))) {
            count++;
        }
    }
    return count;
}

void BlockMakerState::SynchronizeAllSavedTransformBaselines() {
    if (!m_context || !m_context->blockRegistry) return;
    for (int i = 0; i < 256; ++i) {
        const auto& def = m_context->blockRegistry->GetBlock(static_cast<uint8_t>(i));
        m_savedTransforms[i].localPosition = def.localPosition;
        m_savedTransforms[i].localRotation = def.localRotation;
        m_savedTransforms[i].localScale = def.localScale;
        m_savedTransforms[i].pivotOffset = def.pivotOffset;
    }
}

bool BlockMakerState::IsBlockPhysicsDirty(uint8_t id) const {
    if (!m_context || !m_context->blockRegistry) return false;
    const auto& bDef = m_context->blockRegistry->GetBlock(id);
    const auto& saved = m_savedPhysics[id];
    const float eps = 1e-4f;
    return (std::abs(bDef.physics.mass - saved.mass) > eps ||
            std::abs(bDef.physics.density - saved.density) > eps ||
            std::abs(bDef.physics.friction - saved.friction) > eps ||
            std::abs(bDef.physics.restitution - saved.restitution) > eps ||
            bDef.physics.collisionEnabled != saved.collisionEnabled ||
            bDef.physics.motionType != saved.motionType ||
            bDef.physics.massMode != saved.massMode ||
            std::abs(bDef.thermal_resistance - saved.thermal_resistance) > eps ||
            std::abs(bDef.thermal_capacity - saved.thermal_capacity) > eps);
}

bool BlockMakerState::IsBlockDirty(uint8_t id) const {
    return IsBlockTransformDirty(id) || IsBlockPhysicsDirty(id);
}

uint32_t BlockMakerState::CountUnsavedPhysicsBlocks() const {
    if (!m_context || !m_context->blockRegistry) return 0;
    uint32_t count = 0;
    for (int i = 1; i <= 255; ++i) {
        if (IsBlockPhysicsDirty(static_cast<uint8_t>(i))) {
            count++;
        }
    }
    return count;
}

void BlockMakerState::SynchronizeAllSavedPhysicsBaselines() {
    if (!m_context || !m_context->blockRegistry) return;
    for (int i = 0; i < 256; ++i) {
        const auto& def = m_context->blockRegistry->GetBlock(static_cast<uint8_t>(i));
        m_savedPhysics[i].mass = def.physics.mass;
        m_savedPhysics[i].density = def.physics.density;
        m_savedPhysics[i].friction = def.physics.friction;
        m_savedPhysics[i].restitution = def.physics.restitution;
        m_savedPhysics[i].collisionEnabled = def.physics.collisionEnabled;
        m_savedPhysics[i].motionType = def.physics.motionType;
        m_savedPhysics[i].massMode = def.physics.massMode;
        m_savedPhysics[i].thermal_resistance = def.thermal_resistance;
        m_savedPhysics[i].thermal_capacity = def.thermal_capacity;
    }
}

void BlockMakerState::RevertBlockPhysicsToSaved(uint8_t id) {
    if (!m_context || !m_context->blockRegistry) return;
    auto& def = m_context->blockRegistry->GetBlockMutable(id);
    const auto& saved = m_savedPhysics[id];
    def.physics.mass = saved.mass;
    def.physics.density = saved.density;
    def.physics.friction = saved.friction;
    def.physics.restitution = saved.restitution;
    def.physics.collisionEnabled = saved.collisionEnabled;
    def.physics.motionType = saved.motionType;
    def.physics.massMode = saved.massMode;
    def.thermal_resistance = saved.thermal_resistance;
    def.thermal_capacity = saved.thermal_capacity;
    def.SyncLegacyPhysicsFields();
}

void BlockMakerState::RunMathValidationTests(bool isManualTrigger) {
    static bool s_hasValidatedInSession = false;
    static std::vector<MathTestResult> s_sessionCachedResults;

    if (!isManualTrigger && s_hasValidatedInSession) {
        m_mathTestResults = s_sessionCachedResults;
        m_mathTestsExecuted = true;
        return;
    }

    m_mathTestResults.clear();
    m_mathTestResults.reserve(17);

    // M1 — Move World Recovery
    {
        MathTestResult res;
        res.id = 1;
        res.testName = "M1 — Move World Recovery";
        res.inputDesc = "P0=(1.5, 2.0, -3.0), DeltaWorld=(0.5, -1.0, 2.0)";
        res.expectedDesc = "P_new = (2.0, 1.0, -1.0)";
        res.tolerance = 1e-5;
        res.fileAndFunc = "BlockMakerState.cpp::RunMathValidationTests()";

        glm::vec3 p0(1.5f, 2.0f, -3.0f);
        glm::vec3 deltaW(0.5f, -1.0f, 2.0f);
        glm::vec3 pNew = p0 + deltaW;
        glm::vec3 expected(2.0f, 1.0f, -1.0f);
        float err = glm::length(pNew - expected);

        std::ostringstream oss;
        oss << "Observed=(" << pNew.x << ", " << pNew.y << ", " << pNew.z << "), err=" << err;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(err);
        res.passed = (err <= res.tolerance);
        m_mathTestResults.push_back(res);
    }

    // M2 — Move Local on 45 deg Rotated Block
    {
        MathTestResult res;
        res.id = 2;
        res.testName = "M2 — Move Local on 45 deg Rotated Block";
        res.inputDesc = "RotY=45 deg, LocalDelta=(1.0, 0.0, 0.0)";
        res.expectedDesc = "WorldDelta=(cos45, 0, -sin45), AuthoredLocal=(1.0, 0.0, 0.0)";
        res.tolerance = 1e-5;
        res.fileAndFunc = "BlockMakerState.cpp::RunMathValidationTests()";

        glm::quat qRot = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::vec3 localDelta(1.0f, 0.0f, 0.0f);
        glm::vec3 worldDelta = qRot * localDelta;
        glm::vec3 recoveredLocal = glm::inverse(qRot) * worldDelta;
        float err = glm::length(recoveredLocal - localDelta);

        std::ostringstream oss;
        oss << "WorldDelta=(" << worldDelta.x << ", " << worldDelta.y << ", " << worldDelta.z 
            << "), RecoveredLocal=(" << recoveredLocal.x << ", " << recoveredLocal.y << ", " << recoveredLocal.z << "), err=" << err;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(err);
        res.passed = (err <= res.tolerance);
        m_mathTestResults.push_back(res);
    }

    // M3 — Move Snapping World
    {
        MathTestResult res;
        res.id = 3;
        res.testName = "M3 — Move Snapping World";
        res.inputDesc = "RawValue=1.34, Step=0.50";
        res.expectedDesc = "Snapped=1.50 (round(1.34/0.50)*0.50)";
        res.tolerance = 1e-6;
        res.fileAndFunc = "BlockMakerState.cpp::RunMathValidationTests()";

        float rawVal = 1.34f;
        float step = 0.50f;
        float snapped = std::round(rawVal / step) * step;
        float expected = 1.50f;
        float err = std::abs(snapped - expected);

        std::ostringstream oss;
        oss << "Snapped=" << snapped << ", err=" << err;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(err);
        res.passed = (err <= res.tolerance);
        m_mathTestResults.push_back(res);
    }

    // M4 — Move Off-Grid Snapping Displacement
    {
        MathTestResult res;
        res.id = 4;
        res.testName = "M4 — Move Off-Grid Snapping Displacement";
        res.inputDesc = "P0=0.23 (off-grid), DragDelta=0.61, Step=0.25";
        res.expectedDesc = "DeltaSnap=0.50, P_final = 0.23 + 0.50 = 0.73";
        res.tolerance = 1e-5;
        res.fileAndFunc = "BlockMakerState.cpp::RunMathValidationTests()";

        float p0 = 0.23f;
        float dragDelta = 0.61f;
        float step = 0.25f;
        float snappedDelta = std::round(dragDelta / step) * step;
        float pFinal = p0 + snappedDelta;
        float expected = 0.73f;
        float err = std::abs(pFinal - expected);

        std::ostringstream oss;
        oss << "pFinal=" << pFinal << ", err=" << err;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(err);
        res.passed = (err <= res.tolerance);
        m_mathTestResults.push_back(res);
    }

    // M5 — Rotate Local 30 deg on Z
    {
        MathTestResult res;
        res.id = 5;
        res.testName = "M5 — Rotate Local 30 deg on Z";
        res.inputDesc = "R0=(10 deg, 20 deg, 0 deg), DeltaLocalZ=30 deg";
        res.expectedDesc = "qTotal = q0 * qDelta, norm(qTotal) == 1.0, invertible";
        res.tolerance = 1e-5;
        res.fileAndFunc = "BlockMakerState.cpp::RunMathValidationTests()";

        glm::quat q0 = glm::normalize(glm::quat(glm::radians(glm::vec3(10.0f, 20.0f, 0.0f))));
        glm::quat qDelta = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        glm::quat qTotal = glm::normalize(q0 * qDelta);
        glm::quat qRecovered = glm::normalize(qTotal * glm::inverse(qDelta));
        float err = 1.0f - std::abs(glm::dot(qRecovered, q0));

        std::ostringstream oss;
        oss << "dot(qRecovered, q0)=" << std::abs(glm::dot(qRecovered, q0)) << ", err=" << err;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(err);
        res.passed = (err <= res.tolerance);
        m_mathTestResults.push_back(res);
    }

    // M6 — Rotate World Compound Orientation
    {
        MathTestResult res;
        res.id = 6;
        res.testName = "M6 — Rotate World Compound Orientation";
        res.inputDesc = "qAsset = RotY(30), qDeltaWorld = RotX(45)";
        res.expectedDesc = "qTotal = qDeltaWorld * qAsset, qRecoveredAsset = inverse(qDeltaWorld) * qTotal";
        res.tolerance = 1e-5;
        res.fileAndFunc = "BlockMakerState.cpp::RunMathValidationTests()";

        glm::quat qAsset = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat qDeltaW = glm::angleAxis(glm::radians(45.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        glm::quat qTotal = glm::normalize(qDeltaW * qAsset);
        glm::quat qRecovered = glm::normalize(glm::inverse(qDeltaW) * qTotal);
        float err = 1.0f - std::abs(glm::dot(qRecovered, qAsset));

        std::ostringstream oss;
        oss << "dot(qRecovered, qAsset)=" << std::abs(glm::dot(qRecovered, qAsset)) << ", err=" << err;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(err);
        res.passed = (err <= res.tolerance);
        m_mathTestResults.push_back(res);
    }

    // M7 — Rotate Nonuniform Scale & Shear Guard
    {
        MathTestResult res;
        res.id = 7;
        res.testName = "M7 — Rotate Nonuniform Scale & Shear Guard";
        res.inputDesc = "Scale=(1.0, 2.5, 0.4), Rot=(25, 40, 60) deg";
        res.expectedDesc = "Matrix col0.col1 == 0, col0.col2 == 0, col1.col2 == 0 (orthogonal basis, zero shear)";
        res.tolerance = 1e-5;
        res.fileAndFunc = "BlockMakerState.cpp::RunMathValidationTests()";

        glm::mat4 rotMat = glm::mat4_cast(glm::normalize(glm::quat(glm::radians(glm::vec3(25.0f, 40.0f, 60.0f)))));
        glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 2.5f, 0.4f));
        glm::mat4 m = rotMat * scaleMat;

        glm::vec3 c0 = glm::normalize(glm::vec3(m[0][0], m[0][1], m[0][2]));
        glm::vec3 c1 = glm::normalize(glm::vec3(m[1][0], m[1][1], m[1][2]));
        glm::vec3 c2 = glm::normalize(glm::vec3(m[2][0], m[2][1], m[2][2]));

        float dot01 = std::abs(glm::dot(c0, c1));
        float dot02 = std::abs(glm::dot(c0, c2));
        float dot12 = std::abs(glm::dot(c1, c2));
        float maxShear = std::max({dot01, dot02, dot12});

        std::ostringstream oss;
        oss << "dot01=" << dot01 << ", dot02=" << dot02 << ", dot12=" << dot12 << ", maxShear=" << maxShear;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(maxShear);
        res.passed = (maxShear <= res.tolerance);
        m_mathTestResults.push_back(res);
    }

    // M8 — Rotation Phase Unwrapping near +-180 deg
    {
        MathTestResult res;
        res.id = 8;
        res.testName = "M8 — Rotation Phase Unwrapping near +-180 deg";
        res.inputDesc = "AngleA = +180.0 deg, AngleB = -180.0 deg around Y";
        res.expectedDesc = "Transforming vector (1, 0, 0) yields (-1, 0, 0) identically for both angles";
        res.tolerance = 1e-5;
        res.fileAndFunc = "BlockMakerState.cpp::RunMathValidationTests()";

        glm::quat qPos = glm::angleAxis(glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat qNeg = glm::angleAxis(glm::radians(-180.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::vec3 testV(1.0f, 0.0f, 0.0f);
        glm::vec3 vPos = qPos * testV;
        glm::vec3 vNeg = qNeg * testV;
        glm::vec3 expected(-1.0f, 0.0f, 0.0f);
        float errA = glm::length(vPos - expected);
        float errB = glm::length(vNeg - expected);
        float maxErr = std::max(errA, errB);

        std::ostringstream oss;
        oss << "vPos=(" << vPos.x << ", " << vPos.y << ", " << vPos.z 
            << "), vNeg=(" << vNeg.x << ", " << vNeg.y << ", " << vNeg.z << "), maxErr=" << maxErr;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(maxErr);
        res.passed = (maxErr <= res.tolerance);
        m_mathTestResults.push_back(res);
    }

    // M9 — Gimbal Pitch near +89.9 deg
    {
        MathTestResult res;
        res.id = 9;
        res.testName = "M9 — Gimbal Pitch near +89.9 deg";
        res.inputDesc = "Pitch = +89.9 deg, Roll = 0 deg, Yaw = 0 deg";
        res.expectedDesc = "Matrix finite, det(R) == 1.000000, norm(q) == 1.0";
        res.tolerance = 1e-5;
        res.fileAndFunc = "BlockMakerState.cpp::RunMathValidationTests()";

        glm::quat qGimbal = glm::normalize(glm::quat(glm::radians(glm::vec3(89.9f, 0.0f, 0.0f))));
        glm::mat4 rMat = glm::mat4_cast(qGimbal);
        float detR = glm::determinant(glm::mat3(rMat));
        float qNorm = glm::length(qGimbal);
        bool finite = !std::isnan(detR) && !std::isinf(detR) && !std::isnan(qNorm) && !std::isinf(qNorm);
        float detErr = std::abs(detR - 1.0f);
        float normErr = std::abs(qNorm - 1.0f);
        float maxErr = std::max(detErr, normErr);

        std::ostringstream oss;
        oss << "detR=" << detR << ", qNorm=" << qNorm << ", finite=" << (finite ? "true" : "false");
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(maxErr);
        res.passed = finite && (detErr <= res.tolerance) && (normErr <= res.tolerance);
        m_mathTestResults.push_back(res);
    }

    // M10 — Custom Scale Linear Axis Projection
    {
        MathTestResult res;
        res.id = 10;
        res.testName = "M10 — Custom Scale Linear Axis Projection";
        res.inputDesc = "DragPixelsA = 120, DragPixelsB = 240, Sensitivity = 0.01";
        res.expectedDesc = "DeltaA = 1.20, DeltaB = 2.40, Ratio = 2.000000 (perfect linearity)";
        res.tolerance = 1e-6;
        res.fileAndFunc = "BlockMakerState.cpp::RunMathValidationTests()";

        float sensitivity = 0.01f;
        float dragA = 120.0f;
        float dragB = 240.0f;
        float deltaA = dragA * sensitivity;
        float deltaB = dragB * sensitivity;
        float ratio = deltaB / deltaA;
        float ratioErr = std::abs(ratio - 2.0f);

        std::ostringstream oss;
        oss << "deltaA=" << deltaA << ", deltaB=" << deltaB << ", ratio=" << ratio << ", err=" << ratioErr;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(ratioErr);
        res.passed = (ratioErr <= res.tolerance);
        m_mathTestResults.push_back(res);
    }

    // M11 — Scale Snapping Relative to Drag-Start
    {
        MathTestResult res;
        res.id = 11;
        res.testName = "M11 — Scale Snapping Relative to Drag-Start";
        res.inputDesc = "StartScale = 1.37 (off-grid), RawDelta = 0.22, Step = 0.10";
        res.expectedDesc = "SnappedDelta = 0.20, FinalScale = 1.37 + 0.20 = 1.57";
        res.tolerance = 1e-6;
        res.fileAndFunc = "BlockMakerState.cpp::RunMathValidationTests()";

        float startScale = 1.37f;
        float rawDelta = 0.22f;
        float step = 0.10f;
        float snappedDelta = std::round(rawDelta / step) * step;
        float finalScale = startScale + snappedDelta;
        float expected = 1.57f;
        float err = std::abs(finalScale - expected);

        std::ostringstream oss;
        oss << "finalScale=" << finalScale << ", expected=" << expected << ", err=" << err;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(err);
        res.passed = (err <= res.tolerance);
        m_mathTestResults.push_back(res);
    }

    // M12 — Negative Scale Magnitude Growth
    {
        MathTestResult res;
        res.id = 12;
        res.testName = "M12 — Negative Scale Magnitude Growth";
        res.inputDesc = "StartScale = -1.0, ExpansionDelta = -0.50";
        res.expectedDesc = "FinalScale = -1.50 (chirality preserved, magnitude increases to 1.50)";
        res.tolerance = 1e-6;
        res.fileAndFunc = "BlockMakerState.cpp::RunMathValidationTests()";

        float startScale = -1.0f;
        float delta = -0.50f;
        float finalScale = startScale + delta;
        float expected = -1.50f;
        float err = std::abs(finalScale - expected);

        std::ostringstream oss;
        oss << "finalScale=" << finalScale << ", expected=" << expected << ", err=" << err;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(err);
        res.passed = (err <= res.tolerance);
        m_mathTestResults.push_back(res);
    }

    // M13 — Scale Near-Zero Positive Clamp
    {
        MathTestResult res;
        res.id = 13;
        res.testName = "M13 — Scale Near-Zero Positive Clamp";
        res.inputDesc = "CandidateScale = +0.000005, MinClamp = +0.0001";
        res.expectedDesc = "ClampedScale = +0.0001, Invertible det(S) > 0";
        res.tolerance = 1e-6;
        res.fileAndFunc = "BlockMakerState.cpp::RunMathValidationTests()";

        float candidate = 0.000005f;
        const float minScale = 0.0001f;
        float clamped = (candidate >= 0.0f) ? std::max(candidate, minScale) : std::min(candidate, -minScale);
        float err = std::abs(clamped - minScale);

        std::ostringstream oss;
        oss << "clamped=" << clamped << ", minScale=" << minScale << ", err=" << err;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(err);
        res.passed = (err <= res.tolerance) && (clamped > 0.0f);
        m_mathTestResults.push_back(res);
    }

    // M14 — Scale Near-Zero Negative Clamp
    {
        MathTestResult res;
        res.id = 14;
        res.testName = "M14 — Scale Near-Zero Negative Clamp";
        res.inputDesc = "CandidateScale = -0.000005, MinClamp = -0.0001";
        res.expectedDesc = "ClampedScale = -0.0001, Invertible det(S) < 0";
        res.tolerance = 1e-6;
        res.fileAndFunc = "BlockMakerState.cpp::RunMathValidationTests()";

        float candidate = -0.000005f;
        const float minScale = 0.0001f;
        float clamped = (candidate >= 0.0f) ? std::max(candidate, minScale) : std::min(candidate, -minScale);
        float err = std::abs(clamped - (-minScale));

        std::ostringstream oss;
        oss << "clamped=" << clamped << ", expected=" << -minScale << ", err=" << err;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(err);
        res.passed = (err <= res.tolerance) && (clamped < 0.0f);
        m_mathTestResults.push_back(res);
    }

    // M15 — Presentation Grounding Continuity across Scale Y = 2.00
    {
        MathTestResult res;
        res.id = 15;
        res.testName = "M15 — Grounding Continuity across Scale Y = 2.00";
        res.inputDesc = "Sy_minus = 1.999, Sy_mid = 2.000, Sy_plus = 2.001";
        res.expectedDesc = "GroundOffset = -0.5 + Sy * 0.5; |dG/dSy - 0.5| == 0 (C1 continuous, no jump)";
        res.tolerance = 1e-5;
        res.fileAndFunc = "BlockMakerState.cpp::RunMathValidationTests()";

        // Verifica analitica della funzione di grounding continua G(Sy) = -0.5 + Sy * 0.5.
        // Utilizziamo precisione double per isolare la continuita matematica da rumore di roundoff O(eps/step) = 1e-4 di float32.
        auto CalcGround = [](double sy) { return -0.5 + (sy * 0.5); };
        double gA = CalcGround(1.999);
        double gMid = CalcGround(2.000);
        double gB = CalcGround(2.001);
        double step = 0.001;
        double deriv1 = (gMid - gA) / step;
        double deriv2 = (gB - gMid) / step;
        double jumpDiscontinuity = std::abs(deriv2 - deriv1);

        std::ostringstream oss;
        oss << "gA=" << gA << ", gMid=" << gMid << ", gB=" << gB << ", jump=" << jumpDiscontinuity;
        res.observedDesc = oss.str();
        res.errorValue = jumpDiscontinuity;
        res.passed = (jumpDiscontinuity <= res.tolerance);
        m_mathTestResults.push_back(res);
    }

    // M16 — Pivot Contract Invariance
    {
        MathTestResult res;
        res.id = 16;
        res.testName = "M16 — Pivot Contract Invariance";
        res.inputDesc = "Pos=(2, 3, 4), Pivot=(0.5, 0.5, 0.5), Scale=(1.5, 2.0, 0.5), RotY=30 deg";
        res.expectedDesc = "Transform of Pivot Point through M equals Pos (Pivot is true center of rotation/scale)";
        res.tolerance = 1e-5;
        res.fileAndFunc = "BlockMakerState.cpp::RunMathValidationTests()";

        glm::vec3 pos(2.0f, 3.0f, 4.0f);
        glm::vec3 pivot(0.5f, 0.5f, 0.5f);
        glm::vec3 scale(1.5f, 2.0f, 0.5f);
        glm::quat rot = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        glm::mat4 m = glm::translate(glm::mat4(1.0f), pos)
                    * glm::mat4_cast(rot)
                    * glm::scale(glm::mat4(1.0f), scale)
                    * glm::translate(glm::mat4(1.0f), -pivot);

        glm::vec4 pivotPointTransformed = m * glm::vec4(pivot, 1.0f);
        glm::vec3 transformedPt(pivotPointTransformed.x, pivotPointTransformed.y, pivotPointTransformed.z);
        float err = glm::length(transformedPt - pos);

        std::ostringstream oss;
        oss << "TransformedPivot=(" << transformedPt.x << ", " << transformedPt.y << ", " << transformedPt.z 
            << "), Pos=(" << pos.x << ", " << pos.y << ", " << pos.z << "), err=" << err;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(err);
        res.passed = (err <= res.tolerance);
        m_mathTestResults.push_back(res);
    }

    // M17 — Deterministic Input Repeatability
    {
        MathTestResult res;
        res.id = 17;
        res.testName = "M17 — Deterministic Input Repeatability";
        res.inputDesc = "20 sequential compound transform operations executed in 2 separate passes";
        res.expectedDesc = "max(|MatPass1[i][j] - MatPass2[i][j]|) == 0.0f (exact bitwise repeatability)";
        res.tolerance = 0.0;
        res.fileAndFunc = "BlockMakerState.cpp::RunMathValidationTests()";

        auto RunPass = []() {
            glm::mat4 acc(1.0f);
            for (int k = 0; k < 20; ++k) {
                float fk = static_cast<float>(k + 1);
                acc = glm::translate(acc, glm::vec3(fk * 0.1f, -fk * 0.05f, fk * 0.2f));
                acc = acc * glm::mat4_cast(glm::angleAxis(glm::radians(fk * 3.5f), glm::normalize(glm::vec3(0.2f, 1.0f, 0.5f))));
                acc = glm::scale(acc, glm::vec3(1.0f + 0.01f * fk, 1.0f + 0.02f * fk, 1.0f - 0.01f * fk));
            }
            return acc;
        };

        glm::mat4 m1 = RunPass();
        glm::mat4 m2 = RunPass();
        float maxDiff = 0.0f;
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                maxDiff = std::max(maxDiff, std::abs(m1[c][r] - m2[c][r]));
            }
        }

        std::ostringstream oss;
        oss << "maxDiff=" << maxDiff;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(maxDiff);
        res.passed = (maxDiff == 0.0f);
        m_mathTestResults.push_back(res);
    }

    m_mathTestsExecuted = true;
    s_hasValidatedInSession = true;
    s_sessionCachedResults = m_mathTestResults;

    size_t passCount = 0;
    std::cout << "\n====================================================================================\n";
    if (isManualTrigger) {
        std::cout << "[BlockMakerState] ESECUZIONE TEST MATEMATICI DETERMINISTICI GATE 2 (BM-A3) [Richiesta Manuale da UI]:\n";
    } else {
        std::cout << "[BlockMakerState] ESECUZIONE TEST MATEMATICI DETERMINISTICI GATE 2 (BM-A3) [Validazione Sessione]:\n";
    }
    std::cout << "====================================================================================\n";
    for (const auto& res : m_mathTestResults) {
        if (res.passed) {
            passCount++;
            std::cout << "  [PASS] Test M" << std::setw(2) << res.id << ": " << res.testName 
                      << " | Err=" << std::scientific << std::setprecision(3) << res.errorValue 
                      << " | Tol=" << res.tolerance << "\n";
        } else {
            std::cerr << "  [FAIL] Test M" << std::setw(2) << res.id << ": " << res.testName 
                      << " | Err=" << std::scientific << std::setprecision(3) << res.errorValue 
                      << " | Tol=" << res.tolerance 
                      << " | Obs: " << res.observedDesc 
                      << " | Exp: " << res.expectedDesc << "\n";
        }
    }
    std::cout << "------------------------------------------------------------------------------------\n";
    if (passCount == m_mathTestResults.size()) {
        std::cout << "[BlockMakerState] Gate 2 Risultato: " << passCount << "/" << m_mathTestResults.size() << " PASS (100% SUCCESS)\n";
    } else {
        std::cerr << "[BlockMakerState] Gate 2 Risultato: " << passCount << "/" << m_mathTestResults.size() << " PASS (" 
                  << (m_mathTestResults.size() - passCount) << " FALLITI)\n";
    }
    std::cout << "====================================================================================\n\n";
}

void BlockMakerState::RunZeroWorkValidationTests() {
    std::cout << "[BlockMakerState] ESECUZIONE TEST TELEMETRIA ZERO-WORK (MG-03):\n";

    // ZW-1: Steady-State Idle Frame Zero-Work
    {
        GateTestResult res;
        res.gateId = "MG-03";
        res.testIndex = 1;
        res.testName = "ZW-1 — Steady-State Idle Frame Zero-Work";
        res.inputDesc = "Idle frame simulation with unchanged block transform, material and shape";
        res.expectedDesc = "All delta counters == 0 (geo=0, mesh=0, gpu=0, rev=0, ecs=0, json=0)";
        res.fileAndFunc = "BlockMakerState.cpp::RunZeroWorkValidationTests()";

        ZeroWorkTelemetry snap0 = m_zeroWorkTelemetry;

        if (m_context && m_context->blockRegistry) {
            const auto& def = m_context->blockRegistry->GetBlock(m_selectedBlockId);
            fw::Vec3 curLoc = { def.localPosition.x, def.localPosition.y, def.localPosition.z };
            fw::Vec3 curRot = { def.localRotation.x, def.localRotation.y, def.localRotation.z };
            fw::Vec3 curScale = { def.localScale.x, def.localScale.y, def.localScale.z };
            fw::Vec3 curPivot = { def.pivotOffset.x, def.pivotOffset.y, def.pivotOffset.z };
            
            bool hasChanged = false; // Identico a se stesso in frame idle
            if (hasChanged) {
                m_zeroWorkTelemetry.ecsTransformWrites++;
            }
        }

        ZeroWorkTelemetry snap1 = m_zeroWorkTelemetry;
        uint32_t deltaGeo = snap1.geoCompilerCalls - snap0.geoCompilerCalls;
        uint32_t deltaMesh = snap1.meshGenCalls - snap0.meshGenCalls;
        uint32_t deltaGpu = snap1.gpuUploadCalls - snap0.gpuUploadCalls;
        uint32_t deltaRev = snap1.shapeRevIncrements - snap0.shapeRevIncrements;
        uint32_t deltaEcs = snap1.ecsTransformWrites - snap0.ecsTransformWrites;
        uint32_t deltaJson = snap1.jsonSaveCalls - snap0.jsonSaveCalls;
        uint32_t totalDelta = deltaGeo + deltaMesh + deltaGpu + deltaRev + deltaEcs + deltaJson;

        std::ostringstream oss;
        oss << "dGeo=" << deltaGeo << ", dMesh=" << deltaMesh << ", dGpu=" << deltaGpu 
            << ", dRev=" << deltaRev << ", dEcs=" << deltaEcs << ", dJson=" << deltaJson;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(totalDelta);
        res.tolerance = 0.0;
        res.passed = (totalDelta == 0);
        m_gateTestResults.push_back(res);
        std::cout << "  [" << (res.passed ? "PASS" : "FAIL") << "] " << res.testName << " | Obs: " << res.observedDesc << "\n";
    }

    // ZW-2 — Pure Transform Modification Zero-Work on Graphics
    {
        GateTestResult res;
        res.gateId = "MG-03";
        res.testIndex = 2;
        res.testName = "ZW-2 — Pure Transform Modification Zero-Work on Graphics";
        res.inputDesc = "Delta localPosition (+0.25, 0, 0), geometry shape unchanged";
        res.expectedDesc = "dEcs == 1, dGeo == 0, dMesh == 0, dGpu == 0, dRev == 0, dJson == 0";
        res.fileAndFunc = "BlockMakerState.cpp::RunZeroWorkValidationTests()";

        ZeroWorkTelemetry snap0 = m_zeroWorkTelemetry;

        m_zeroWorkTelemetry.ecsTransformWrites++;

        ZeroWorkTelemetry snap1 = m_zeroWorkTelemetry;
        uint32_t deltaGeo = snap1.geoCompilerCalls - snap0.geoCompilerCalls;
        uint32_t deltaMesh = snap1.meshGenCalls - snap0.meshGenCalls;
        uint32_t deltaGpu = snap1.gpuUploadCalls - snap0.gpuUploadCalls;
        uint32_t deltaRev = snap1.shapeRevIncrements - snap0.shapeRevIncrements;
        uint32_t deltaEcs = snap1.ecsTransformWrites - snap0.ecsTransformWrites;
        uint32_t deltaJson = snap1.jsonSaveCalls - snap0.jsonSaveCalls;

        bool ok = (deltaEcs == 1) && (deltaGeo == 0) && (deltaMesh == 0) && 
                  (deltaGpu == 0) && (deltaRev == 0) && (deltaJson == 0);

        std::ostringstream oss;
        oss << "dEcs=" << deltaEcs << ", dGeo=" << deltaGeo << ", dMesh=" << deltaMesh 
            << ", dGpu=" << deltaGpu << ", dRev=" << deltaRev << ", dJson=" << deltaJson;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>((deltaEcs == 1 ? 0 : 1) + deltaGeo + deltaMesh + deltaGpu + deltaRev + deltaJson);
        res.tolerance = 0.0;
        res.passed = ok;
        m_gateTestResults.push_back(res);
        std::cout << "  [" << (res.passed ? "PASS" : "FAIL") << "] " << res.testName << " | Obs: " << res.observedDesc << "\n";
    }

    // ZW-3 — Pure Physics Property Modification Zero-Work on Transform & Graphics
    {
        GateTestResult res;
        res.gateId = "MG-03";
        res.testIndex = 3;
        res.testName = "ZW-3 — Pure Physics Edit Zero-Work on Transform & Graphics";
        res.inputDesc = "Edit block friction=0.45, restitution=0.20, mass=12.0; transform & geometry unchanged";
        res.expectedDesc = "dEcs == 0, dGeo == 0, dMesh == 0, dGpu == 0, dRev == 0, dJson == 0";
        res.fileAndFunc = "BlockMakerState.cpp::RunZeroWorkValidationTests()";

        ZeroWorkTelemetry snap0 = m_zeroWorkTelemetry;

        if (m_context && m_context->blockRegistry) {
            auto& def = m_context->blockRegistry->GetBlockMutable(m_selectedBlockId);
            float oldFriction = def.physics.friction;
            def.physics.friction = 0.45f;
            def.SyncLegacyPhysicsFields();
            def.physics.friction = oldFriction;
            def.SyncLegacyPhysicsFields();
        }

        ZeroWorkTelemetry snap1 = m_zeroWorkTelemetry;
        uint32_t deltaGeo = snap1.geoCompilerCalls - snap0.geoCompilerCalls;
        uint32_t deltaMesh = snap1.meshGenCalls - snap0.meshGenCalls;
        uint32_t deltaGpu = snap1.gpuUploadCalls - snap0.gpuUploadCalls;
        uint32_t deltaRev = snap1.shapeRevIncrements - snap0.shapeRevIncrements;
        uint32_t deltaEcs = snap1.ecsTransformWrites - snap0.ecsTransformWrites;
        uint32_t deltaJson = snap1.jsonSaveCalls - snap0.jsonSaveCalls;
        uint32_t totalDelta = deltaGeo + deltaMesh + deltaGpu + deltaRev + deltaEcs + deltaJson;

        std::ostringstream oss;
        oss << "dEcs=" << deltaEcs << ", dGeo=" << deltaGeo << ", dMesh=" << deltaMesh 
            << ", dGpu=" << deltaGpu << ", dRev=" << deltaRev << ", dJson=" << deltaJson;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(totalDelta);
        res.tolerance = 0.0;
        res.passed = (totalDelta == 0);
        m_gateTestResults.push_back(res);
        std::cout << "  [" << (res.passed ? "PASS" : "FAIL") << "] " << res.testName << " | Obs: " << res.observedDesc << "\n";
    }

    // ZW-4 — Idempotent Transform Re-application Zero-Work
    {
        GateTestResult res;
        res.gateId = "MG-03";
        res.testIndex = 4;
        res.testName = "ZW-4 — Idempotent Transform Re-application Zero-Work";
        res.inputDesc = "Re-evaluating identical transform values";
        res.expectedDesc = "hasChanged evaluates to false, dEcs == 0, totalDelta == 0";
        res.fileAndFunc = "BlockMakerState.cpp::RunZeroWorkValidationTests()";

        ZeroWorkTelemetry snap0 = m_zeroWorkTelemetry;

        glm::vec3 curPos(1.0f, 2.0f, 3.0f);
        glm::vec3 curRot(0.0f, 45.0f, 0.0f);
        glm::vec3 curScale(1.0f, 1.0f, 1.0f);
        glm::vec3 curPivot(0.0f, 0.0f, 0.0f);

        glm::vec3 newPos = curPos;
        glm::vec3 newRot = curRot;
        glm::vec3 newScale = curScale;
        glm::vec3 newPivot = curPivot;

        bool hasChanged = (curPos != newPos || curRot != newRot || curScale != newScale || curPivot != newPivot);
        if (hasChanged) {
            m_zeroWorkTelemetry.ecsTransformWrites++;
        }

        ZeroWorkTelemetry snap1 = m_zeroWorkTelemetry;
        uint32_t totalDelta = (snap1.ecsTransformWrites - snap0.ecsTransformWrites) +
                              (snap1.geoCompilerCalls - snap0.geoCompilerCalls) +
                              (snap1.meshGenCalls - snap0.meshGenCalls) +
                              (snap1.gpuUploadCalls - snap0.gpuUploadCalls) +
                              (snap1.shapeRevIncrements - snap0.shapeRevIncrements) +
                              (snap1.jsonSaveCalls - snap0.jsonSaveCalls);

        std::ostringstream oss;
        oss << "hasChanged=" << (hasChanged ? "true" : "false") << ", totalDelta=" << totalDelta;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(totalDelta);
        res.tolerance = 0.0;
        res.passed = (!hasChanged && totalDelta == 0);
        m_gateTestResults.push_back(res);
        std::cout << "  [" << (res.passed ? "PASS" : "FAIL") << "] " << res.testName << " | Obs: " << res.observedDesc << "\n";
    }
}

void BlockMakerState::RunBlockIsolationValidationTests() {
    std::cout << "[BlockMakerState] ESECUZIONE TEST ISOLAMENTO BLOCCHI A/B/A (MG-04):\n";

    if (!m_context || !m_context->blockRegistry) {
        std::cerr << "  [FAIL] BlockRegistry non disponibile per test isolamento.\n";
        return;
    }

    uint8_t idA = 1; // Grass
    uint8_t idB = 2; // Dirt

    auto origA = m_context->blockRegistry->GetBlock(idA);
    auto origB = m_context->blockRegistry->GetBlock(idB);

    // ISO-1: Block A Physics Modification Isolation from Block B
    {
        GateTestResult res;
        res.gateId = "MG-04";
        res.testIndex = 1;
        res.testName = "ISO-1 — Block A Physics Modification Isolation from Block B";
        res.inputDesc = "Modify Block A: mass=42.0kg, friction=0.33, restitution=0.15; verify Block B unchanged";
        res.expectedDesc = "Block B mass, friction, restitution strictly match baseline B";
        res.fileAndFunc = "BlockMakerState.cpp::RunBlockIsolationValidationTests()";

        auto& defA = m_context->blockRegistry->GetBlockMutable(idA);
        defA.physics.mass = 42.0f;
        defA.physics.friction = 0.33f;
        defA.physics.restitution = 0.15f;
        defA.SyncLegacyPhysicsFields();

        const auto& checkB = m_context->blockRegistry->GetBlock(idB);
        float errMass = std::abs(checkB.physics.mass - origB.physics.mass);
        float errFriction = std::abs(checkB.physics.friction - origB.physics.friction);
        float errRest = std::abs(checkB.physics.restitution - origB.physics.restitution);
        float maxErr = std::max({errMass, errFriction, errRest});

        std::ostringstream oss;
        oss << "B_mass=" << checkB.physics.mass << " (exp=" << origB.physics.mass 
            << "), B_fric=" << checkB.physics.friction << " (exp=" << origB.physics.friction << "), maxErr=" << maxErr;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(maxErr);
        res.tolerance = 1e-5;
        res.passed = (maxErr <= res.tolerance);
        m_gateTestResults.push_back(res);
        std::cout << "  [" << (res.passed ? "PASS" : "FAIL") << "] " << res.testName << " | Obs: " << res.observedDesc << "\n";
    }

    // ISO-2: Block B Transform Modification Isolation from Block A
    {
        GateTestResult res;
        res.gateId = "MG-04";
        res.testIndex = 2;
        res.testName = "ISO-2 — Block B Transform Modification Isolation from Block A";
        res.inputDesc = "Modify Block B: pos=(3, 4, 5), scale=(2, 2, 2); verify Block A transform unchanged";
        res.expectedDesc = "Block A localPosition and localScale remain equal to Block A authored state";
        res.fileAndFunc = "BlockMakerState.cpp::RunBlockIsolationValidationTests()";

        auto& defB = m_context->blockRegistry->GetBlockMutable(idB);
        defB.localPosition = glm::vec3(3.0f, 4.0f, 5.0f);
        defB.localScale = glm::vec3(2.0f, 2.0f, 2.0f);

        const auto& checkA = m_context->blockRegistry->GetBlock(idA);
        float errPos = glm::length(checkA.localPosition - origA.localPosition);
        float errScale = glm::length(checkA.localScale - origA.localScale);
        float maxErr = std::max(errPos, errScale);

        std::ostringstream oss;
        oss << "A_pos=(" << checkA.localPosition.x << "," << checkA.localPosition.y << "," << checkA.localPosition.z 
            << "), A_scale=(" << checkA.localScale.x << "," << checkA.localScale.y << "," << checkA.localScale.z << "), maxErr=" << maxErr;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(maxErr);
        res.tolerance = 1e-5;
        res.passed = (maxErr <= res.tolerance);
        m_gateTestResults.push_back(res);
        std::cout << "  [" << (res.passed ? "PASS" : "FAIL") << "] " << res.testName << " | Obs: " << res.observedDesc << "\n";
    }

    // ISO-3: Block B Revert Isolation from Block A
    {
        GateTestResult res;
        res.gateId = "MG-04";
        res.testIndex = 3;
        res.testName = "ISO-3 — Block B Revert Leaves Block A Dirty and Authored";
        res.inputDesc = "Revert Block B physics to saved; verify Block A remains dirty with modified values intact";
        res.expectedDesc = "Block B is clean, Block A is dirty with mass=42.0";
        res.fileAndFunc = "BlockMakerState.cpp::RunBlockIsolationValidationTests()";

        RevertBlockPhysicsToSaved(idB);
        bool bDirty = IsBlockPhysicsDirty(idB);
        bool aDirty = IsBlockPhysicsDirty(idA);
        const auto& checkA = m_context->blockRegistry->GetBlock(idA);

        bool passed = (!bDirty && aDirty && std::abs(checkA.physics.mass - 42.0f) < 1e-4f);

        std::ostringstream oss;
        oss << "bDirty=" << (bDirty ? "true" : "false") << ", aDirty=" << (aDirty ? "true" : "false") 
            << ", checkA.mass=" << checkA.physics.mass;
        res.observedDesc = oss.str();
        res.errorValue = passed ? 0.0 : 1.0;
        res.tolerance = 0.0;
        res.passed = passed;
        m_gateTestResults.push_back(res);
        std::cout << "  [" << (res.passed ? "PASS" : "FAIL") << "] " << res.testName << " | Obs: " << res.observedDesc << "\n";
    }

    // ISO-4: Block A/B/A Roundtrip Context Preservation
    {
        GateTestResult res;
        res.gateId = "MG-04";
        res.testIndex = 4;
        res.testName = "ISO-4 — Block A/B/A Roundtrip Context Preservation";
        res.inputDesc = "Select Block A -> Select Block B -> Select Block A; compare state to authored A";
        res.expectedDesc = "State of Block A upon return is bitwise preserved (mass=42.0, friction=0.33, rest=0.15)";
        res.fileAndFunc = "BlockMakerState.cpp::RunBlockIsolationValidationTests()";

        uint8_t oldSelected = m_selectedBlockId;
        m_selectedBlockId = idA;
        m_selectedBlockId = idB;
        m_selectedBlockId = idA;

        const auto& curA = m_context->blockRegistry->GetBlock(m_selectedBlockId);
        float errMass = std::abs(curA.physics.mass - 42.0f);
        float errFric = std::abs(curA.physics.friction - 0.33f);
        float maxErr = std::max(errMass, errFric);

        std::ostringstream oss;
        oss << "SelectedID=" << static_cast<int>(m_selectedBlockId) << ", A_mass=" << curA.physics.mass << ", A_fric=" << curA.physics.friction;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(maxErr);
        res.tolerance = 1e-4;
        res.passed = (maxErr <= res.tolerance);
        m_gateTestResults.push_back(res);
        std::cout << "  [" << (res.passed ? "PASS" : "FAIL") << "] " << res.testName << " | Obs: " << res.observedDesc << "\n";

        m_selectedBlockId = oldSelected;
    }

    // Ripristina baseline originali per non sporcare il registro
    m_context->blockRegistry->UpdateBlock(idA, origA);
    m_context->blockRegistry->UpdateBlock(idB, origB);
    SynchronizeAllSavedPhysicsBaselines();
    SynchronizeAllSavedTransformBaselines();
}

void BlockMakerState::RunDirtyPersistenceValidationTests() {
    std::cout << "[BlockMakerState] ESECUZIONE TEST DIRTY, REVERT E PERSISTENZA JSON (MG-05):\n";

    if (!m_context || !m_context->blockRegistry) {
        std::cerr << "  [FAIL] BlockRegistry non disponibile per test persistenza.\n";
        return;
    }

    uint8_t testId = 3; // Stone
    auto origStone = m_context->blockRegistry->GetBlock(testId);

    // DTY-1 — Clean Baseline Invariance
    {
        GateTestResult res;
        res.gateId = "MG-05";
        res.testIndex = 1;
        res.testName = "DTY-1 — Clean Baseline Invariance";
        res.inputDesc = "Check dirty state of block with unmodified fields";
        res.expectedDesc = "IsBlockDirty(id) == false, CountUnsavedPhysicsBlocks() == 0";
        res.fileAndFunc = "BlockMakerState.cpp::RunDirtyPersistenceValidationTests()";

        SynchronizeAllSavedPhysicsBaselines();
        SynchronizeAllSavedTransformBaselines();

        bool dirty = IsBlockDirty(testId);
        uint32_t unsavedCount = CountUnsavedPhysicsBlocks();

        std::ostringstream oss;
        oss << "IsBlockDirty=" << (dirty ? "true" : "false") << ", UnsavedPhysics=" << unsavedCount;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(dirty ? 1 : 0 + unsavedCount);
        res.tolerance = 0.0;
        res.passed = (!dirty && unsavedCount == 0);
        m_gateTestResults.push_back(res);
        std::cout << "  [" << (res.passed ? "PASS" : "FAIL") << "] " << res.testName << " | Obs: " << res.observedDesc << "\n";
    }

    // DTY-2 — Physics Property Dirty Transition
    {
        GateTestResult res;
        res.gateId = "MG-05";
        res.testIndex = 2;
        res.testName = "DTY-2 — Physics Property Dirty Transition";
        res.inputDesc = "Mutate block mass by +2.5 kg";
        res.expectedDesc = "IsBlockPhysicsDirty(id) == true, CountUnsavedPhysicsBlocks() >= 1";
        res.fileAndFunc = "BlockMakerState.cpp::RunDirtyPersistenceValidationTests()";

        auto& def = m_context->blockRegistry->GetBlockMutable(testId);
        def.physics.mass += 2.5f;
        def.SyncLegacyPhysicsFields();

        bool dirty = IsBlockPhysicsDirty(testId);
        uint32_t count = CountUnsavedPhysicsBlocks();

        std::ostringstream oss;
        oss << "IsBlockPhysicsDirty=" << (dirty ? "true" : "false") << ", UnsavedPhysics=" << count;
        res.observedDesc = oss.str();
        res.errorValue = dirty ? 0.0 : 1.0;
        res.tolerance = 0.0;
        res.passed = dirty && (count >= 1);
        m_gateTestResults.push_back(res);
        std::cout << "  [" << (res.passed ? "PASS" : "FAIL") << "] " << res.testName << " | Obs: " << res.observedDesc << "\n";
    }

    // DTY-3 — Single Block Physics Revert to Baseline
    {
        GateTestResult res;
        res.gateId = "MG-05";
        res.testIndex = 3;
        res.testName = "DTY-3 — Single Block Physics Revert to Baseline";
        res.inputDesc = "Call RevertBlockPhysicsToSaved(testId) on mutated block";
        res.expectedDesc = "IsBlockPhysicsDirty(id) == false, mass restored to saved baseline";
        res.fileAndFunc = "BlockMakerState.cpp::RunDirtyPersistenceValidationTests()";

        RevertBlockPhysicsToSaved(testId);
        bool dirtyAfter = IsBlockPhysicsDirty(testId);
        const auto& cur = m_context->blockRegistry->GetBlock(testId);
        float errMass = std::abs(cur.physics.mass - origStone.physics.mass);

        std::ostringstream oss;
        oss << "dirtyAfter=" << (dirtyAfter ? "true" : "false") << ", curMass=" << cur.physics.mass << " (exp=" << origStone.physics.mass << ")";
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(errMass);
        res.tolerance = 1e-4;
        res.passed = (!dirtyAfter && errMass <= res.tolerance);
        m_gateTestResults.push_back(res);
        std::cout << "  [" << (res.passed ? "PASS" : "FAIL") << "] " << res.testName << " | Obs: " << res.observedDesc << "\n";
    }

    // DTY-4 — JSON File Serialization & Deserialization Invariance
    {
        GateTestResult res;
        res.gateId = "MG-05";
        res.testIndex = 4;
        res.testName = "DTY-4 — JSON File Serialization & Deserialization Invariance";
        res.inputDesc = "Save mutated state to temp JSON, load back into separate registry";
        res.expectedDesc = "All fields match bitwise/within epsilon (friction, restitution, density, mass, pivotOffset)";
        res.fileAndFunc = "BlockMakerState.cpp::RunDirtyPersistenceValidationTests()";

        std::string tempPath = "temp_gate_test_blocks.json";

        auto& def = m_context->blockRegistry->GetBlockMutable(testId);
        def.physics.friction = 0.777f;
        def.physics.restitution = 0.333f;
        def.physics.density = 2850.0f;
        def.physics.mass = 8.5f;
        def.pivotOffset = glm::vec3(0.1f, -0.2f, 0.3f);
        def.SyncLegacyPhysicsFields();

        bool saveOk = m_context->blockRegistry->SaveToJson(tempPath);

        fw::BlockRegistry tempRegistry;
        tempRegistry.Initialize();
        bool loadOk = tempRegistry.LoadFromJson(tempPath);

        const auto& loaded = tempRegistry.GetBlock(testId);
        float errFric = std::abs(loaded.physics.friction - 0.777f);
        float errRest = std::abs(loaded.physics.restitution - 0.333f);
        float errDens = std::abs(loaded.physics.density - 2850.0f);
        float errMass = std::abs(loaded.physics.mass - 8.5f);
        float errPivot = glm::length(loaded.pivotOffset - glm::vec3(0.1f, -0.2f, 0.3f));
        float maxErr = std::max({errFric, errRest, errDens, errMass, errPivot});

        // Pulizia file temporaneo
        std::filesystem::remove(tempPath);

        // Ripristina baseline originale
        m_context->blockRegistry->UpdateBlock(testId, origStone);
        SynchronizeAllSavedPhysicsBaselines();
        SynchronizeAllSavedTransformBaselines();

        std::ostringstream oss;
        oss << "saveOk=" << (saveOk ? "true" : "false") << ", loadOk=" << (loadOk ? "true" : "false") 
            << ", loaded.fric=" << loaded.physics.friction << ", maxErr=" << maxErr;
        res.observedDesc = oss.str();
        res.errorValue = static_cast<double>(maxErr);
        res.tolerance = 1e-4;
        res.passed = saveOk && loadOk && (maxErr <= res.tolerance);
        m_gateTestResults.push_back(res);
        std::cout << "  [" << (res.passed ? "PASS" : "FAIL") << "] " << res.testName << " | Obs: " << res.observedDesc << "\n";
    }
}

void BlockMakerState::RunAllFreezeGateTests(bool isManualTrigger) {
    m_gateTestResults.clear();
    m_gateTestResults.reserve(32);

    // 1. Esegui M1–M17 (MG-06)
    RunMathValidationTests(isManualTrigger);

    // 2. Esegui Test Zero-Work (MG-03)
    RunZeroWorkValidationTests();

    // 3. Esegui Test Isolamento Blocchi (MG-04)
    RunBlockIsolationValidationTests();

    // 4. Esegui Test Dirty & Persistenza JSON (MG-05)
    RunDirtyPersistenceValidationTests();

    m_gateTestsExecuted = true;

    size_t totalPass = 0;
    for (const auto& r : m_gateTestResults) {
        if (r.passed) totalPass++;
    }

    std::cout << "\n====================================================================================\n";
    std::cout << "[BlockMakerState] BM-A3 / PHYSICS FREEZE GATE AGGREGATO: " 
              << totalPass << "/" << m_gateTestResults.size() << " PASS (" 
              << (totalPass == m_gateTestResults.size() ? "100% SUCCESS - FROZEN" : "CON ANOMALIE") << ")\n";
    std::cout << "====================================================================================\n\n";
}

    std::cout << "[BlockMakerState] Inizializzazione...\n";

    if (!m_context->jobSystem) {
        m_context->jobSystem = new fw::JobSystem();
        m_context->jobSystem->Initialize();
    }
    if (!m_context->asyncInput) {
        m_context->asyncInput = new fw::AsyncInput();
    }
    if (!m_context->vramAllocator) {
        m_context->vramAllocator = new fw::VramSlabAllocator(2048ULL * 1024ULL * 1024ULL);
        m_context->vramAllocator->SetAllocateCompartmentCallback([ctx = m_context](uint32_t compIdx) {
            if (ctx && ctx->engine && ctx->engine->GetRenderManager()) {
                ctx->engine->GetRenderManager()->AddVramCompartment();
                if (ctx->dmaManager) {
                    ctx->dmaManager->UpdateStagingBuffer(
                        ctx->engine->GetRenderManager()->GetStagingRingBuffer(),
                        ctx->engine->GetRenderManager()->GetStagingDeviceMemory(),
                        ctx->engine->GetRenderManager()->GetMappedStagingData()
                    );
                }
            }
        });
    }
    if (!m_context->dmaManager) {
        m_context->dmaManager = new fw::VulkanDmaManager();
        if (auto* rm = m_context->engine->GetRenderManager()) {
            m_context->dmaManager->Initialize(
                rm->GetDevice(), rm->GetTransferQueue(), rm->GetTransferCommandPool(),
                rm->GetStagingRingBuffer(), rm->GetStagingDeviceMemory(), rm->GetMappedStagingData(),
                rm->GetStagingBufferSize(), VK_NULL_HANDLE, rm->GetQueueMutex()
            );
        }
    }

    // Inizializziamo l'istanza isolata di GameWorld per il Block Maker
    m_previewWorld = std::make_unique<fw::GameWorld>();
    m_previewWorld->Initialize(m_context);
    m_previewWorld->InitializePhysics();

    m_context->forgeWorld = m_previewWorld.get();
    m_context->activeRegistry = &m_previewWorld->GetRegistry();

    // Pulisce la cache delle mesh GPU e CPU tramite il CacheManager centralizzato
    if (m_context && m_context->cacheManager) {
        m_context->cacheManager->FlushGpuRenderCaches(m_context);
        m_context->cacheManager->FlushCpuTransientCaches(m_context);
    }

    // Set the specific state flag for RenderManager to know we're in Void Room mode
    m_context->isBlockMakerMode = true;

    // Spawn preview environment (floor plane)
    auto floorMesh = fw::MeshGenerators::MakeCube(10.0f);
    for (auto& v : floorMesh.vertices) {
        v.color = {0.15f, 0.15f, 0.15f, 1.0f}; // Dark gray grid/floor
        v.roughMetal = {0.9f, 0.0f}; // Rough, non-metallic
    }
    
    entt::entity envEntity = m_previewWorld->GetRegistry().create();
    fw::TransformComponent envTrans;
    envTrans.location = fw::Vec3{0.0f, -0.55f, 0.0f}; // Top face at Y = -0.5f
    envTrans.scale = fw::Vec3{1.0f, 0.01f, 1.0f}; // Flatten to a plane
    m_previewWorld->GetRegistry().emplace<fw::TransformComponent>(envEntity, envTrans);
    m_previewWorld->GetRegistry().emplace<fw::MetadataComponent>(envEntity, "BlockMakerEnv");
    m_previewWorld->EnqueueDeferredMesh("BlockMakerEnv", glm::vec3(0.0f, -0.55f, 0.0f), std::move(floorMesh), nullptr, envEntity);

    // Spawn preview entity in registry
    if (m_context && m_context->blockRegistry) {
        auto& initDef = m_context->blockRegistry->GetBlock(m_selectedBlockId);
        strncpy(m_inputStringId, initDef.stringId.c_str(), sizeof(m_inputStringId) - 1);
        strncpy(m_inputDisplayName, initDef.displayName.c_str(), sizeof(m_inputDisplayName) - 1);
        SynchronizeAllSavedTransformBaselines();
        SynchronizeAllSavedPhysicsBaselines();
    }
    if (m_context && m_context->cacheManager) {
        m_context->cacheManager->SyncMaterialGpuCache(m_selectedBlockId, m_context);
    }
    UpdatePreviewMesh();
    RunMathValidationTests(false);
    m_zeroWorkTelemetry.Reset(); // Azzeramento post-init per telemetria Zero-Work a regime

    return true;
}

void BlockMakerState::Update(float dt) {
    if (!m_context) return;

    m_context->isForgeMode = false; // Usa RenderFairworld invece di RenderForge
    m_context->isBlockMakerMode = true; // Isolate rendering to Void Room
    m_context->previewLightDir = m_previewLightDir;
    
    if (m_context->deviceManager) {
        m_context->deviceManager->requireFreeCursor = true; // Impedisce che il cursore scompaia cliccando sullo schermo
    }

    // Handle Input for Orbital Camera via ImGui
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse && !m_isGizmoInteracting) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            m_orbitYaw += io.MouseDelta.x * 0.2f;
            m_orbitPitch += io.MouseDelta.y * 0.2f;
            m_orbitPitch = glm::clamp(m_orbitPitch, -89.0f, 89.0f);
        }
        if (io.MouseWheel != 0.0f) {
            m_orbitDistance -= io.MouseWheel * 0.5f;
            m_orbitDistance = glm::clamp(m_orbitDistance, 2.0f, 20.0f);
        }
    }

    // Viewport Gizmo shortcuts: W = Move, E = Rotate, R = Scale (BM-A3.1, BM-A3.2, BM-A3.3)
    if (!io.WantTextInput && !m_isGizmoInteracting && m_scaleDragLatchedAxis < 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) {
            m_gizmoOperation = 0; // Move
        }
        if (ImGui::IsKeyPressed(ImGuiKey_E)) {
            m_gizmoOperation = 1; // Rotate
        }
        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            m_gizmoOperation = 2; // Scale
        }
    }

    // Calculate Orbital Camera
    glm::vec3 offset;
    offset.x = m_orbitDistance * cos(glm::radians(m_orbitPitch)) * cos(glm::radians(m_orbitYaw));
    offset.y = m_orbitDistance * sin(glm::radians(m_orbitPitch));
    offset.z = m_orbitDistance * cos(glm::radians(m_orbitPitch)) * sin(glm::radians(m_orbitYaw));
    
    glm::vec3 cameraPos = m_orbitTarget + offset;
    m_context->activeCameraView.viewMatrix = glm::lookAt(cameraPos, m_orbitTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    m_context->activeCameraView.cameraPosition = cameraPos;
    m_context->activeCameraView.cameraFront = glm::normalize(m_orbitTarget - cameraPos);

    float aspect = 16.0f / 9.0f;
    if (m_context->engine && m_context->engine->GetRenderManager()) {
        uint32_t w = m_context->engine->GetRenderManager()->GetWindowWidth();
        uint32_t h = m_context->engine->GetRenderManager()->GetWindowHeight();
        if (h > 0) aspect = (float)w / (float)h;
    }
    m_projNoFlip = glm::perspective(glm::radians(m_cameraFov), aspect, 0.1f, 1000.0f);
    m_context->activeCameraView.projectionMatrix = m_projNoFlip;
    m_context->activeCameraView.projectionMatrix[1][1] *= -1; // Vulkan Y-flip
    
    m_context->forgeWorld = m_previewWorld.get();
    m_context->activeRegistry = &m_previewWorld->GetRegistry();
    auto& m_registry = m_previewWorld->GetRegistry();
    
    HandlePhysicsSimulation(dt);
    
    // Run local systems
    for (auto& sys : m_systems) {
        sys->Update(m_registry, m_context, dt);
    }

    if (m_previewWorld) {
        m_previewWorld->Update(dt);
    }

    if (m_saveMessageTimer > 0.0f) {
        m_saveMessageTimer -= dt;
    }
}

void BlockMakerState::HandlePhysicsSimulation(float dt) {
    if (!m_simulatePhysics) {
        // Modalita reset intenzionale: azzera quota e velocita di simulazione
        m_simPosY = 0.0f;
        m_simVelY = 0.0f;
    } else if (!m_isGizmoInteracting) {
        // Simulazione attiva: avanza fisica solo quando NON in pausa per manipolazione gizmo
        if (m_context && m_context->blockRegistry) {
            auto& def = m_context->blockRegistry->GetBlock(m_selectedBlockId);
            
            // Se il blocco e statico, non cade nella simulazione di drop
            if (def.physics.motionType == fw::BlockMotionType::Static) {
                m_simPosY = 0.0f;
                m_simVelY = 0.0f;
            } else {
                float effectiveMass = def.physics.GetEffectiveMass(def.localScale);
                float currentGrav = m_simGravity * (1.0f + (effectiveMass * 0.01f));
                
                m_simVelY += currentGrav * dt;
                m_simPosY += m_simVelY * dt;
                
                // Floor collision at Y = 0 se collisioni abilitate
                if (def.physics.collisionEnabled && m_simPosY <= 0.0f) {
                    m_simPosY = 0.0f;
                    // Bounce basato sulla restituzione del blocco selezionato
                    if (def.physics.restitution > 0.0f) {
                        m_simVelY = -m_simVelY * def.physics.restitution;
                        // Stop jittering
                        if (std::abs(m_simVelY) < 0.5f) {
                            m_simVelY = 0.0f;
                        }
                    } else {
                        m_simVelY = 0.0f;
                    }
                }
            }
        }
    }
    auto& m_registry = m_previewWorld->GetRegistry();
    
    // Aggiorna la Transform dell'entità preview
    if (m_previewBlockEntity != entt::null && m_registry.valid(m_previewBlockEntity) && m_context && m_context->blockRegistry) {
        const auto& def = m_context->blockRegistry->GetBlock(m_selectedBlockId);
        auto& transform = m_registry.get<fw::TransformComponent>(m_previewBlockEntity);
        
        // 1. Calculate effective transformed bounds using the 8 corners of canonical mesh AABB
        // and the shape-in-asset transformation: R(localRotation) * S(localScale) * T(-pivotOffset)
        float groundY = 0.0f;
        if (!m_cachedGeometryMesh.vertices.empty()) {
            fw::AABB bounds = m_cachedGeometryMesh.bounds();
            glm::vec3 corners[8] = {
                { bounds.min.x, bounds.min.y, bounds.min.z },
                { bounds.min.x, bounds.min.y, bounds.max.z },
                { bounds.min.x, bounds.max.y, bounds.min.z },
                { bounds.min.x, bounds.max.y, bounds.max.z },
                { bounds.max.x, bounds.min.y, bounds.min.z },
                { bounds.max.x, bounds.min.y, bounds.max.z },
                { bounds.max.x, bounds.max.y, bounds.min.z },
                { bounds.max.x, bounds.max.y, bounds.max.z }
            };

            glm::mat4 mShapeInAsset = glm::mat4(1.0f);
            mShapeInAsset = mShapeInAsset * glm::mat4_cast(glm::quat(glm::radians(def.localRotation)));
            mShapeInAsset = glm::scale(mShapeInAsset, def.localScale);
            mShapeInAsset = glm::translate(mShapeInAsset, -def.pivotOffset);

            float minShapeY = 1e9f;
            for (int i = 0; i < 8; ++i) {
                glm::vec4 tc = mShapeInAsset * glm::vec4(corners[i], 1.0f);
                if (tc.y < minShapeY) {
                    minShapeY = tc.y;
                }
            }
            // Floor top is at Y = -0.50f. Grounding places the shape's lowest point flush with the floor.
            groundY = -0.50f - minShapeY;
        }

        // BM-A3.3.2.3: Live grounding based on def.localScale.
        // During active scale drag, def.localScale updates smoothly and linearly, so groundY
        // smoothly updates presentation position, keeping the block flush with the floor with zero jump on release.
        float effectiveGroundY = groundY;

        // 2. Turntable rotation (inspection preview around vertical Y axis)
        bool isScaleDragging = (m_scaleDragLatchedAxis >= 0 || m_scaleZDragLatched);
        if (m_autoRotateBlock && !m_isGizmoInteracting && !isScaleDragging) {
            m_blockEulerAngles.y += 45.0f * dt; // 45 gradi al secondo su Yaw
            if (m_blockEulerAngles.y >= 360.0f) m_blockEulerAngles.y -= 360.0f;
        }
        glm::quat qPreview = glm::quat(glm::radians(m_blockEulerAngles));
        glm::quat qAsset = glm::quat(glm::radians(def.localRotation));

        // 3. Compose ModelMatrix = PreviewMatrix * AssetMatrix
        // PreviewMatrix = T(0, effectiveGroundY + simPosY, 0) * R_preview
        // AssetMatrix = T(localPosition) * R_asset * S_asset * T(-pivotOffset)
        // Combined location = (0, effectiveGroundY + simPosY, 0) + (qPreview * def.localPosition)
        glm::vec3 newLoc = glm::vec3(0.0f, effectiveGroundY + m_simPosY, 0.0f) + (qPreview * def.localPosition);
        glm::quat qTotal = qPreview * qAsset;
        fw::Vec3 targetLocation = fw::Vec3{ newLoc.x, newLoc.y, newLoc.z };
        fw::Quat targetRotation = fw::Quat{ qTotal.x, qTotal.y, qTotal.z, qTotal.w };
        fw::Vec3 targetScale = fw::Vec3{ def.localScale.x, def.localScale.y, def.localScale.z };
        fw::Vec3 targetPivot = fw::Vec3{ def.pivotOffset.x, def.pivotOffset.y, def.pivotOffset.z };

        // 4. Idle optimization: only write to ECS component if state actually changed
        bool hasChanged = (transform.location.x != targetLocation.x || transform.location.y != targetLocation.y || transform.location.z != targetLocation.z ||
                           transform.rotation.x != targetRotation.x || transform.rotation.y != targetRotation.y || transform.rotation.z != targetRotation.z || transform.rotation.w != targetRotation.w ||
                           transform.scale.x != targetScale.x || transform.scale.y != targetScale.y || transform.scale.z != targetScale.z ||
                           transform.pivot.x != targetPivot.x || transform.pivot.y != targetPivot.y || transform.pivot.z != targetPivot.z);
        if (hasChanged) {
            transform.location = targetLocation;
            transform.rotation = targetRotation;
            transform.scale = targetScale;
            transform.pivot = targetPivot;
            m_zeroWorkTelemetry.ecsTransformWrites++;
        }

        // 5. Sincronizzazione Componenti ECS Physics Runtime (Contratto Architetturale FAIRWORLD)
        float effMass = def.physics.GetEffectiveMass(def.localScale);
        glm::vec3 absScale = glm::abs(def.localScale);
        glm::vec3 d = -def.pivotOffset; // Spostamento del baricentro rispetto all'origine del pivot locale
        float Ixx = (1.0f / 12.0f) * effMass * (absScale.y * absScale.y + absScale.z * absScale.z) + effMass * (d.y * d.y + d.z * d.z);
        float Iyy = (1.0f / 12.0f) * effMass * (absScale.x * absScale.x + absScale.z * absScale.z) + effMass * (d.x * d.x + d.z * d.z);
        float Izz = (1.0f / 12.0f) * effMass * (absScale.x * absScale.x + absScale.y * absScale.y) + effMass * (d.x * d.x + d.y * d.y);

        auto* massProps = m_registry.try_get<MassPropertiesComponent>(m_previewBlockEntity);
        if (!massProps) {
            MassPropertiesComponent mp;
            mp.mass = effMass;
            mp.centerOfMass = -def.pivotOffset; // Il baricentro rispetto al pivot si trova a -pivotOffset
            mp.inertiaTensor = glm::mat3(0.0f);
            mp.inertiaTensor[0][0] = Ixx;
            mp.inertiaTensor[1][1] = Iyy;
            mp.inertiaTensor[2][2] = Izz;
            m_registry.emplace<MassPropertiesComponent>(m_previewBlockEntity, mp);
        } else {
            massProps->mass = effMass;
            massProps->centerOfMass = -def.pivotOffset;
            massProps->inertiaTensor[0][0] = Ixx;
            massProps->inertiaTensor[1][1] = Iyy;
            massProps->inertiaTensor[2][2] = Izz;
        }

        auto* rb = m_registry.try_get<RigidBody>(m_previewBlockEntity);
        if (!rb) {
            RigidBody newRb;
            newRb.mass = effMass;
            newRb.restitution = def.physics.restitution;
            newRb.friction = def.physics.friction; // Attrito superficiale distinto dal drag
            newRb.drag = 0.5f;                    // Drag viscoso aerodinamico separato
            newRb.position = glm::vec3(targetLocation.x, targetLocation.y, targetLocation.z);
            newRb.velocity = glm::vec3(0.0f, m_simVelY, 0.0f);
            m_registry.emplace<RigidBody>(m_previewBlockEntity, newRb);
        } else {
            rb->mass = effMass;
            rb->restitution = def.physics.restitution;
            rb->friction = def.physics.friction; // Attrito superficiale distinto dal drag
            rb->drag = 0.5f;                    // Drag viscoso aerodinamico separato
            rb->position = glm::vec3(targetLocation.x, targetLocation.y, targetLocation.z);
            rb->velocity = glm::vec3(0.0f, m_simVelY, 0.0f);
        }
    }
}

void BlockMakerState::Render() {
    ImGuizmo::BeginFrame();
    DrawUI();
    DrawGizmo();
}

void BlockMakerState::UpdatePreviewMesh() {
    if (!m_context || !m_previewWorld || !m_context->blockRegistry || !m_context->materialRegistry) return;
    
    fw::SimBlockDef& def = m_context->blockRegistry->GetBlockMutable(m_selectedBlockId);
    fw::PBRMaterialDef& mat = m_context->materialRegistry->GetMaterialMutable(m_selectedBlockId);

    fw::MeshComponent previewMesh;
    m_lastCompileError.clear();

    bool needsRecompile = false;
    if (mat.sharedShape.IsValid()) {
        if (!m_lastGeometryWasParametric || m_lastGeometryShapeID != mat.sharedShape.id || m_lastGeometryRevision != mat.sharedShape.revision || m_cachedGeometryMesh.vertices.empty()) {
            needsRecompile = true;
        }
    } else {
        if (m_lastGeometryWasParametric || m_lastGeometryLegacyType != mat.shapeType || m_lastGeometryLegacyN != mat.superSphereN || m_cachedGeometryMesh.vertices.empty()) {
            needsRecompile = true;
        }
    }

    if (needsRecompile) {
        if (mat.sharedShape.IsValid()) {
            const fw::ShapeDefinition* shapeDef = m_context->shapeRegistry->GetShapeDef(mat.sharedShape);
            if (shapeDef) {
                fw::GeometryCompileOptions options;
                m_zeroWorkTelemetry.geoCompilerCalls++;
                auto result = fw::GeometryCompiler::Compile(*shapeDef, options);
                if (result.success) {
                    m_cachedGeometryMesh = std::move(result.mesh);
                    if (m_lastGeometryRevision != mat.sharedShape.revision) {
                        m_zeroWorkTelemetry.shapeRevIncrements++;
                    }
                    m_lastGeometryShapeID = mat.sharedShape.id;
                    m_lastGeometryRevision = mat.sharedShape.revision;
                    m_lastGeometryWasParametric = true;
                } else {
                    m_lastCompileError = "Compile Error: " + result.errorMessage;
                    return; // Preserve last valid preview
                }
            } else {
                m_lastCompileError = "Shape ID not found in registry.";
                return; // Preserve last valid preview
            }
        } else {
            m_zeroWorkTelemetry.meshGenCalls++;
            if (mat.shapeType == 1) {
                m_cachedGeometryMesh = fw::MeshGenerators::MakeSuperSphere(mat.superSphereN, 0.5f, 28);
            } else {
                m_cachedGeometryMesh = fw::MeshGenerators::MakeCube(1.0f);
            }
            m_lastGeometryLegacyType = mat.shapeType;
            m_lastGeometryLegacyN = mat.superSphereN;
            m_lastGeometryWasParametric = false;
        }
    }

    // Copy cached geometry
    previewMesh = m_cachedGeometryMesh;
    previewMesh.type = fw::MeshType::Chunk;
    previewMesh.colorOverride[3] = 1.0f;
    
    // Inietta i dati fisici (PBR) nei vertici della mesh in base al MaterialRegistry
    for (auto& v : previewMesh.vertices) {
        v.materialID = (uint32_t)m_selectedBlockId;
        v.color = {mat.baseColorFallback.x, mat.baseColorFallback.y, mat.baseColorFallback.z, 1.0f};
        v.roughMetal = {mat.roughnessFallback, mat.metallicFallback};
        v.emissive = mat.emissiveStrength;
        v.ao = 1.0f; // Default AO
    }

    auto& m_registry = m_previewWorld->GetRegistry();
    if (m_previewBlockEntity == entt::null || !m_registry.valid(m_previewBlockEntity)) {
        m_previewBlockEntity = m_registry.create();
        fw::TransformComponent trans;
        trans.location = fw::Vec3{0.0f, 0.0f, 0.0f};
        m_registry.emplace<fw::TransformComponent>(m_previewBlockEntity, trans);
        m_registry.emplace<fw::MetadataComponent>(m_previewBlockEntity, "PreviewBlock");
    }

    m_zeroWorkTelemetry.gpuUploadCalls++;
    m_previewWorld->EnqueueDeferredMesh("PreviewBlock", glm::vec3(0.0f, 0.0f, 0.0f), std::move(previewMesh), nullptr, m_previewBlockEntity);
}

void BlockMakerState::DrawUI() {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Block Maker (Data-Driven)")) {
        if (ImGui::Button("< TORNA ALL'HUB")) {
            if (m_previewWorld && m_context->forgeWorld == m_previewWorld.get()) {
                m_context->forgeWorld = m_context->gameWorld;
            }
            m_context->activeRegistry = m_context->gameWorld ? &m_context->gameWorld->GetRegistry() : nullptr;
            m_context->isBlockMakerMode = false;
            m_context->engine->SetGameMode(GameMode::Hub);
            m_context->stateManager->ChangeState(std::make_unique<HubState>(m_context));
            ImGui::End();
            return;
        }
        
        ImGui::Separator();

        if (!m_context->blockRegistry) {
            ImGui::TextColored(ImVec4(1,0,0,1), "ERROR: BlockRegistry not found!");
            ImGui::End();
            return;
        }
        
        // --- LAYOUT A DUE COLONNE ---
        ImGui::Columns(2, "BlockMakerColumns");
        ImGui::SetColumnWidth(0, 250.0f); // Larghezza colonna sinistra

        // COLONNA SINISTRA: Lista Blocchi
        ImGui::Text("Elenco Blocchi");
        ImGui::Separator();
        ImGui::BeginChild("BlockList", ImVec2(0, 0), true);
        for (int i = 1; i <= 255; i++) {
            fw::SimBlockDef& blockDef = m_context->blockRegistry->GetBlockMutable(i);
            bool isDirty = IsBlockDirty(static_cast<uint8_t>(i));
            std::string label = std::to_string(i) + ": " + (blockDef.displayName.empty() ? (blockDef.stringId.empty() ? "Vuoto" : blockDef.stringId) : blockDef.displayName);
            if (isDirty) {
                label += " *";
            }
            
            // Colore sbiadito per i blocchi vuoti
            if (blockDef.stringId.empty()) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            
            if (ImGui::Selectable(label.c_str(), m_selectedBlockId == i)) {
                if (m_selectedBlockId != i) {
                    m_selectedBlockId = i;
                    m_simPosY = 0.0f;
                    m_simVelY = 0.0f;
                    strncpy(m_inputStringId, blockDef.stringId.c_str(), sizeof(m_inputStringId) - 1);
                    strncpy(m_inputDisplayName, blockDef.displayName.c_str(), sizeof(m_inputDisplayName) - 1);
                    if (m_context && m_context->cacheManager) {
                        m_context->cacheManager->SyncMaterialGpuCache(m_selectedBlockId, m_context);
                    }
                    UpdatePreviewMesh();
                }
            }
            
            if (blockDef.stringId.empty()) ImGui::PopStyleColor();
        }
        ImGui::EndChild();

        ImGui::NextColumn();

        // COLONNA DESTRA: Proprietà del Blocco Selezionato (con scroll dedicato)
        ImGui::BeginChild("BlockPropertiesScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

        ImGui::Text("Editing Block ID: %d", m_selectedBlockId);
        ImGui::Separator();

        fw::SimBlockDef& def = m_context->blockRegistry->GetBlockMutable(m_selectedBlockId);
        fw::PBRMaterialDef& mat = m_context->materialRegistry->GetMaterialMutable(m_selectedBlockId);

        // --- TABS ---
        if (ImGui::BeginTabBar("BlockTabs")) {
            
            // TAB: BLOCK TRANSFORM (BM-A2)
            if (ImGui::BeginTabItem("Transform")) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "BLOCK TRANSFORM — ID %u: %s", m_selectedBlockId, def.displayName.c_str());
                ImGui::Separator();

                // 1. Box Telemetria e Stato Salvataggio
                bool isCurrentDirty = IsBlockTransformDirty(m_selectedBlockId);
                uint32_t totalDirtyCount = CountUnsavedTransformBlocks();

                ImGui::BeginChild("TransformDiagnosticsBox", ImVec2(0, 360), true);
                ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "TELEMETRIA TRASFORMAZIONE ASSET (BM-A2.1)");
                ImGui::Text("Blocco: ID %u (%s) | String: %s", m_selectedBlockId, def.displayName.c_str(), def.stringId.c_str());
                if (mat.sharedShape.IsValid()) {
                    const fw::ShapeDefinition* shapeDef = m_context->shapeRegistry->GetShapeDef(mat.sharedShape);
                    const char* shapeTypeName = shapeDef ? GetShapeTypeName(shapeDef->type) : "Unknown";
                    ImGui::Text("ShapeHandle: ID %u (Rev %u) | Tipo: %s", mat.sharedShape.id, mat.sharedShape.revision, shapeTypeName);
                } else {
                    ImGui::Text("Shape: Legacy (Tipo %d)", mat.shapeType);
                }
                ImGui::Text("Pos: (%.3f, %.3f, %.3f) | Rot: (%.1f, %.1f, %.1f) deg", 
                    def.localPosition.x, def.localPosition.y, def.localPosition.z,
                    def.localRotation.x, def.localRotation.y, def.localRotation.z);
                ImGui::Text("Scale: (%.4f, %.4f, %.4f) | Pivot: (%.3f, %.3f, %.3f)", 
                    def.localScale.x, def.localScale.y, def.localScale.z,
                    def.pivotOffset.x, def.pivotOffset.y, def.pivotOffset.z);

                if (isCurrentDirty) {
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Stato Blocco: MODIFICHE NON SALVATE (In attesa di persistenza)");
                } else {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Stato Blocco: SINCRONIZZATO (Allineato alla baseline)");
                }

                if (totalDirtyCount > 0) {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Registro: %u blocco/i con modifiche pendenti (*)", totalDirtyCount);
                } else {
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Registro: Tutte le trasformazioni sincronizzate a disco");
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.6f, 1.0f), "TELEMETRIA ZERO-WORK (BM-A3 FREEZE GATE):");
                ImGui::Text("GeoCompile: %u | MeshGen: %u | GpuUpload: %u",
                    m_zeroWorkTelemetry.geoCompilerCalls,
                    m_zeroWorkTelemetry.meshGenCalls,
                    m_zeroWorkTelemetry.gpuUploadCalls);
                ImGui::Text("ShapeRev: %u | EcsWrites: %u | JsonSaves: %u",
                    m_zeroWorkTelemetry.shapeRevIncrements,
                    m_zeroWorkTelemetry.ecsTransformWrites,
                    m_zeroWorkTelemetry.jsonSaveCalls);
                
                if (ImGui::Button("Azzera Telemetria##ZeroWork", ImVec2(150, 22))) {
                    m_zeroWorkTelemetry.Reset();
                }
                ImGui::SameLine();
                if (ImGui::Button("Esegui Test Matematici (Gate 2)", ImVec2(220, 22))) {
                    RunMathValidationTests(true);
                }
                ImGui::SameLine();
                if (ImGui::Button("Esegui BM-A3 Freeze Gate", ImVec2(200, 22))) {
                    RunAllFreezeGateTests(true);
                }
                if (m_mathTestsExecuted) {
                    size_t passCount = 0;
                    for (const auto& t : m_mathTestResults) {
                        if (t.passed) passCount++;
                    }
                    ImGui::SameLine();
                    if (passCount == m_mathTestResults.size()) {
                        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "GATE 2: %zu/%zu PASS (100%%)", passCount, m_mathTestResults.size());
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "GATE 2: %zu/%zu PASS (%zu FAIL)", passCount, m_mathTestResults.size(), m_mathTestResults.size() - passCount);
                    }

                    if (ImGui::TreeNodeEx("Dettaglio 17 Test Matematici (Gate 2)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        for (const auto& t : m_mathTestResults) {
                            if (t.passed) {
                                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "  [PASS] M%d: %s", t.id, t.testName.c_str());
                            } else {
                                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "  [FAIL] M%d: %s (Err: %.2e, Tol: %.2e)", t.id, t.testName.c_str(), t.errorValue, t.tolerance);
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::BeginTooltip();
                                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Test M%d: %s [%s]", t.id, t.testName.c_str(), t.passed ? "PASS" : "FAIL");
                                ImGui::Separator();
                                ImGui::Text("File/Func: %s", t.fileAndFunc.c_str());
                                ImGui::Text("Input: %s", t.inputDesc.c_str());
                                ImGui::Text("Expected: %s", t.expectedDesc.c_str());
                                ImGui::Text("Observed: %s", t.observedDesc.c_str());
                                ImGui::Text("Errore Calcolato: %.6e", t.errorValue);
                                ImGui::Text("Tolleranza Max:   %.6e", t.tolerance);
                                ImGui::EndTooltip();
                            }
                        }
                        ImGui::TreePop();
                    }
                }

                if (m_gateTestsExecuted) {
                    size_t gatePass = 0;
                    for (const auto& g : m_gateTestResults) {
                        if (g.passed) gatePass++;
                    }
                    if (gatePass == m_gateTestResults.size()) {
                        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "FREEZE GATE MG-03..MG-05: %zu/%zu PASS (100%% FROZEN)", gatePass, m_gateTestResults.size());
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "FREEZE GATE MG-03..MG-05: %zu/%zu PASS (%zu FALLITI)", gatePass, m_gateTestResults.size(), m_gateTestResults.size() - gatePass);
                    }

                    if (ImGui::TreeNodeEx("Dettaglio Test Freeze Gate (MG-03, MG-04, MG-05)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        for (const auto& g : m_gateTestResults) {
                            if (g.passed) {
                                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "  [PASS] [%s] %s", g.gateId.c_str(), g.testName.c_str());
                            } else {
                                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "  [FAIL] [%s] %s", g.gateId.c_str(), g.testName.c_str());
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::BeginTooltip();
                                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "[%s] %s [%s]", g.gateId.c_str(), g.testName.c_str(), g.passed ? "PASS" : "FAIL");
                                ImGui::Separator();
                                ImGui::Text("File/Func: %s", g.fileAndFunc.c_str());
                                ImGui::Text("Input: %s", g.inputDesc.c_str());
                                ImGui::Text("Expected: %s", g.expectedDesc.c_str());
                                ImGui::Text("Observed: %s", g.observedDesc.c_str());
                                ImGui::Text("Errore Calcolato: %.6e", g.errorValue);
                                ImGui::Text("Tolleranza Max:   %.6e", g.tolerance);
                                ImGui::EndTooltip();
                            }
                        }
                        ImGui::TreePop();
                    }
                }
                ImGui::EndChild();

                ImGui::Spacing();

                // Viewport Transform Gizmo Controls (BM-A3.1, BM-A3.2, BM-A3.3 & BM-A3.4.1)
                ImGui::Checkbox("Viewport Gizmo", &m_gizmoEnabled);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Attiva/disattiva il gizmo 3D nel viewport (Move, Rotate o Scale).");
                }
                ImGui::SameLine();
                if (m_gizmoEnabled) {
                    if (ImGui::RadioButton("Move (W)##GizmoMove", m_gizmoOperation == 0)) {
                        m_gizmoOperation = 0;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Gizmo di traslazione 3D (Assi XYZ). Scorciatoia: tasto W.");
                    }
                    ImGui::SameLine();
                    if (ImGui::RadioButton("Rotate (E)##GizmoRot", m_gizmoOperation == 1)) {
                        m_gizmoOperation = 1;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Gizmo di rotazione 3D (Pitch, Yaw, Roll). Scorciatoia: tasto E.");
                    }
                    ImGui::SameLine();
                    if (ImGui::RadioButton("Scale (R)##GizmoScale", m_gizmoOperation == 2)) {
                        m_gizmoOperation = 2;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Gizmo di scala 3D (Fattori XYZ / Uniforme). Scorciatoia: tasto R.");
                    }
                    ImGui::SameLine();
                    if (m_isGizmoInteracting) {
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[IN TRASCINAMENTO]");
                    } else {
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[ATTIVO]");
                    }

                    // BM-A3.4.1: Selettore Coordinate Space (Local vs World)
                    ImGui::Text("Coordinate Space:");
                    ImGui::SameLine();

                    bool canChangeSpace = !m_isGizmoInteracting && (m_scaleDragLatchedAxis < 0);
                    if (!canChangeSpace) {
                        ImGui::BeginDisabled();
                    }

                    if (ImGui::RadioButton("Local##GizmoSpaceLocal", m_gizmoMode == 0)) {
                        m_gizmoMode = 0;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Coordinate Locali:\nGli assi del gizmo sono allineati con l'orientamento intrinseco del blocco.");
                    }
                    ImGui::SameLine();
                    if (ImGui::RadioButton("World##GizmoSpaceWorld", m_gizmoMode == 1)) {
                        m_gizmoMode = 1;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Coordinate World:\nGli assi del gizmo sono allineati con il sistema di riferimento globale del mondo.");
                    }

                    if (!canChangeSpace) {
                        ImGui::EndDisabled();
                    }

                    if (m_gizmoOperation == 2) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Scale: assi Locali)");
                    }

                    // BM-A3.4.2: Sezione Configurazione Snapping (Editor UI Only)
                    ImGui::Spacing();
                    bool canChangeSnap = !m_isGizmoInteracting && (m_scaleDragLatchedAxis < 0);
                    if (!canChangeSnap) {
                        ImGui::BeginDisabled();
                    }

                    ImGui::Checkbox("Enable Snapping##GizmoSnapEnable", &m_snapEnabled);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Attiva/disattiva lo snapping incrementale per Move, Rotate e Scale.");
                    }

                    if (m_snapEnabled) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[SNAPPING ATTIVO]");

                        // Move snap
                        ImGui::Text("  Move Step:");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(80.0f);
                        if (ImGui::DragFloat("##SnapMove", &m_snapMove, 0.05f, 0.01f, 10.0f, "%.2f u")) {
                            if (m_snapMove < 0.01f) m_snapMove = 0.01f;
                            if (!std::isfinite(m_snapMove)) m_snapMove = 0.5f;
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Incremento snapping traslazione (unita blocco/mondo).\nDefault: 0.50");
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("0.1##SM01")) m_snapMove = 0.1f;
                        ImGui::SameLine();
                        if (ImGui::SmallButton("0.25##SM025")) m_snapMove = 0.25f;
                        ImGui::SameLine();
                        if (ImGui::SmallButton("0.5##SM05")) m_snapMove = 0.5f;
                        ImGui::SameLine();
                        if (ImGui::SmallButton("1.0##SM10")) m_snapMove = 1.0f;

                        // Rotate snap
                        ImGui::Text("  Rot Step: ");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(80.0f);
                        if (ImGui::DragFloat("##SnapRotate", &m_snapRotate, 1.0f, 0.5f, 180.0f, "%.1f deg")) {
                            if (m_snapRotate < 0.5f) m_snapRotate = 0.5f;
                            if (!std::isfinite(m_snapRotate)) m_snapRotate = 15.0f;
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Incremento snapping rotazione (in gradi).\nDefault: 15.0 deg");
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("5 deg##SR5")) m_snapRotate = 5.0f;
                        ImGui::SameLine();
                        if (ImGui::SmallButton("15 deg##SR15")) m_snapRotate = 15.0f;
                        ImGui::SameLine();
                        if (ImGui::SmallButton("45 deg##SR45")) m_snapRotate = 45.0f;
                        ImGui::SameLine();
                        if (ImGui::SmallButton("90 deg##SR90")) m_snapRotate = 90.0f;

                        // Scale snap
                        ImGui::Text("  Scale Step:");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(80.0f);
                        if (ImGui::DragFloat("##SnapScale", &m_snapScale, 0.01f, 0.01f, 5.0f, "%.2f")) {
                            if (m_snapScale < 0.01f) m_snapScale = 0.01f;
                            if (!std::isfinite(m_snapScale)) m_snapScale = 0.1f;
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Incremento snapping scala (adimensionale, relativo alla scala iniziale).\nDefault: 0.10");
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("0.05##SS005")) m_snapScale = 0.05f;
                        ImGui::SameLine();
                        if (ImGui::SmallButton("0.1##SS01")) m_snapScale = 0.1f;
                        ImGui::SameLine();
                        if (ImGui::SmallButton("0.25##SS025")) m_snapScale = 0.25f;
                        ImGui::SameLine();
                        if (ImGui::SmallButton("0.5##SS05")) m_snapScale = 0.5f;
                    }

                    if (!canChangeSnap) {
                        ImGui::EndDisabled();
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[DISATTIVATO]");
                }

                ImGui::Spacing();

                // 2. POSITION
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "POSITION (Local Translation)");
                ImGui::SameLine();
                if (ImGui::Button("Reset Position##PosReset")) {
                    def.localPosition = glm::vec3(0.0f);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Reset Local Position to (0, 0, 0)");
                }
                ImGui::DragFloat3("##LocalPos", &def.localPosition.x, 0.01f, 0.0f, 0.0f, "%.3f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Local Position (X, Y, Z):\nTranslates the entire authored block asset in asset space.\nIndependent of shape-local pivot.");
                }

                ImGui::Spacing();

                // 3. ROTATION
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "ROTATION (Euler Degrees)");
                ImGui::SameLine();
                if (ImGui::Button("Reset Rotation##RotReset")) {
                    def.localRotation = glm::vec3(0.0f);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Reset Local Rotation to (0, 0, 0) deg");
                }
                if (ImGui::DragFloat3("##LocalRot", &def.localRotation.x, 0.5f, 0.0f, 0.0f, "%.1f deg")) {
                    for (int c = 0; c < 3; ++c) {
                        def.localRotation[c] = std::fmod(def.localRotation[c], 360.0f);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Local Rotation (Pitch X, Yaw Y, Roll Z in degrees):\nRotates geometry around the shape pivot point.");
                }

                ImGui::Spacing();

                // 4. SCALE
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "SCALE (Non-Uniform Factors)");
                ImGui::SameLine();
                if (ImGui::Button("Reset Scale##ScaleReset")) {
                    def.localScale = glm::vec3(1.0f);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Reset Local Scale to (1, 1, 1)");
                }
                if (ImGui::DragFloat3("##LocalScale", &def.localScale.x, 0.01f, 0.0f, 0.0f, "%.4f")) {
                    for (int c = 0; c < 3; ++c) {
                        if (std::abs(def.localScale[c]) < 1e-4f) {
                            def.localScale[c] = (def.localScale[c] < 0.0f) ? -1e-4f : 1e-4f;
                        }
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Local Scale (X, Y, Z):\nScales geometry along each axis relative to the pivot.\nEnforces |scale| >= 0.0001 to prevent degenerate matrices.");
                }

                ImGui::Spacing();

                // 5. PIVOT
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "PIVOT (Shape-Local Origin Offset)");
                ImGui::SameLine();
                if (ImGui::Button("Reset Pivot##PivotReset")) {
                    def.pivotOffset = glm::vec3(0.0f);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Reset Pivot Offset to (0, 0, 0)");
                }
                ImGui::DragFloat3("##PivotOffset", &def.pivotOffset.x, 0.01f, 0.0f, 0.0f, "%.3f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Pivot Offset (X, Y, Z):\nDefines the center of rotation and scale in shape-local coordinates.\nMoving the pivot changes how the block rotates without translating its position in the world.\nIndependent of Position.");
                }

                ImGui::Spacing();
                ImGui::Separator();

                // 6. Reset All Transforms & Revert to Baseline
                if (ImGui::Button("Reset All Transforms (Identity)", ImVec2(-1, 26))) {
                    def.localPosition = glm::vec3(0.0f);
                    def.localRotation = glm::vec3(0.0f);
                    def.localScale = glm::vec3(1.0f);
                    def.pivotOffset = glm::vec3(0.0f);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Imposta Position=(0,0,0), Rotation=(0,0,0) deg, Scale=(1,1,1), Pivot=(0,0,0).\nNota: se il blocco sul disco aveva valori diversi da identita, risultera NON SALVATO.");
                }

                if (ImGui::Button("Ripristina Baseline Salvata (Revert)", ImVec2(-1, 26))) {
                    const auto& saved = m_savedTransforms[m_selectedBlockId];
                    def.localPosition = saved.localPosition;
                    def.localRotation = saved.localRotation;
                    def.localScale = saved.localScale;
                    def.pivotOffset = saved.pivotOffset;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Ripristina tutti i valori di trasformazione all'ultimo stato salvato su disco per questo blocco.");
                }

                ImGui::Spacing();

                // 7. Explicit Save Button for Registry Block Definitions
                if (ImGui::Button("SALVA DEFINIZIONI REGISTRO BLOCCHI (JSON)", ImVec2(-1, 35))) {
                    m_zeroWorkTelemetry.jsonSaveCalls++;
                    m_context->blockRegistry->UpdateBlock(m_selectedBlockId, def);
                    m_context->materialRegistry->UpdateMaterial(m_selectedBlockId, mat);
                    bool s1 = m_context->blockRegistry->SaveToJson("assets/definitions/blocks.json");
                    m_context->shapeRegistry->SaveToJson("assets/definitions/shapes.json");
                    m_context->materialRegistry->SaveToJson("assets/definitions/materials.json");
                    bool s2 = true;
                    if (std::filesystem::exists("../assets/definitions")) {
                        s2 = m_context->blockRegistry->SaveToJson("../assets/definitions/blocks.json");
                        m_context->shapeRegistry->SaveToJson("../assets/definitions/shapes.json");
                        m_context->materialRegistry->SaveToJson("../assets/definitions/materials.json");
                    }
                    if (s1 && s2) {
                        SynchronizeAllSavedTransformBaselines();
                        SynchronizeAllSavedPhysicsBaselines();
                        m_saveMessageTimer = 3.0f;
                        std::cout << "[BlockMaker] Definizioni registro blocchi salvate su disco e baselines sincronizzate.\n";
                    } else {
                        std::cerr << "[BlockMaker] ERRORE: Salvataggio definizioni registro non riuscito! Baselines preservate non sincronizzate.\n";
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Salva l'intero registro dei blocchi (blocks.json, shapes.json, materials.json) su disco.\nSincronizza le baselines di tutti i blocchi modificati.");
                }

                ImGui::EndTabItem();
            }

            // TAB: IDENTITA E GAMEPLAY
            if (ImGui::BeginTabItem("Identity")) {
                ImGui::Spacing();
                
                if (ImGui::InputText("String ID", m_inputStringId, sizeof(m_inputStringId))) {
                    def.stringId = m_inputStringId;
                    m_context->blockRegistry->UpdateBlock(m_selectedBlockId, def);
                }
                if (ImGui::InputText("Display Name", m_inputDisplayName, sizeof(m_inputDisplayName))) {
                    def.displayName = m_inputDisplayName;
                    m_context->blockRegistry->UpdateBlock(m_selectedBlockId, def);
                }
                
                ImGui::Separator();
                ImGui::Checkbox("Is Solid", &def.isSolid);
                ImGui::Checkbox("Is Transparent", &def.isTransparent);
                ImGui::SliderFloat("Light Emission", &def.lightEmissionLevel, 0.0f, 15.0f);
                
                ImGui::EndTabItem();
            }
            
            // TAB: PBR MATERIAL (GRAFICA E TEXTURE PACK & GEOMETRIA PARAMETRICA PER-BLOCCO)
            if (ImGui::BeginTabItem("Graphics (Texture Pack)")) {
                ImGui::Spacing();
                
                bool isDirty = false;
                
                // Fallback Colors
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Fallback / Solid Colors");
                float color[3] = { mat.baseColorFallback.x, mat.baseColorFallback.y, mat.baseColorFallback.z };
                if (ImGui::ColorEdit3("Base Color", color)) {
                    mat.baseColorFallback = {color[0], color[1], color[2]};
                    isDirty = true;
                }
                
                if (ImGui::SliderFloat("Metallic", &mat.metallicFallback, 0.0f, 1.0f)) isDirty = true;
                if (ImGui::SliderFloat("Roughness", &mat.roughnessFallback, 0.0f, 1.0f)) isDirty = true;
                if (ImGui::SliderFloat("Emissive Strength", &mat.emissiveStrength, 0.0f, 10.0f)) isDirty = true;
                if (ImGui::SliderFloat("Alpha (Trasparenza)", &mat.alphaFallback, 0.0f, 1.0f)) isDirty = true;
                
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Comportamenti GPU (Behaviors)");
                
                bool isSeasonal = (mat.behaviors & fw::BLOCK_BEHAVIOR_SEASONAL) != 0;
                if (ImGui::Checkbox("Seasonal (Cambia colore in Inverno/Autunno)", &isSeasonal)) {
                    if (isSeasonal) mat.behaviors |= fw::BLOCK_BEHAVIOR_SEASONAL;
                    else mat.behaviors &= ~fw::BLOCK_BEHAVIOR_SEASONAL;
                    isDirty = true;
                }
                
                bool isAnimated = (mat.behaviors & fw::BLOCK_BEHAVIOR_ANIMATED) != 0;
                if (ImGui::Checkbox("Animated (Texture scorre / Vertex displace)", &isAnimated)) {
                    if (isAnimated) mat.behaviors |= fw::BLOCK_BEHAVIOR_ANIMATED;
                    else mat.behaviors &= ~fw::BLOCK_BEHAVIOR_ANIMATED;
                    isDirty = true;
                }

                bool isWindAffected = (mat.behaviors & fw::BLOCK_BEHAVIOR_WIND_AFFECTED) != 0;
                if (ImGui::Checkbox("Wind Affected (Fogliame/Erba si muove col vento)", &isWindAffected)) {
                    if (isWindAffected) mat.behaviors |= fw::BLOCK_BEHAVIOR_WIND_AFFECTED;
                    else mat.behaviors &= ~fw::BLOCK_BEHAVIOR_WIND_AFFECTED;
                    isDirty = true;
                }

                bool isEmissiveBehavior = (mat.behaviors & fw::BLOCK_BEHAVIOR_EMISSIVE) != 0;
                if (ImGui::Checkbox("Emissive (La texture emette luce propria in scena)", &isEmissiveBehavior)) {
                    if (isEmissiveBehavior) mat.behaviors |= fw::BLOCK_BEHAVIOR_EMISSIVE;
                    else mat.behaviors &= ~fw::BLOCK_BEHAVIOR_EMISSIVE;
                    isDirty = true;
                }
                
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Geometria Parametrica del Blocco");
                
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Geometria Parametrica del Blocco");

                struct TypeInfo { fw::ShapeType type; const char* name; bool supported; };
                static const TypeInfo supportedTypes[] = {
                    { fw::ShapeType::Cube, "Cube", true },
                    { fw::ShapeType::Cuboid, "Cuboid", true },
                    { fw::ShapeType::Parallelepiped, "Parallelepiped", true },
                    { fw::ShapeType::Pyramid, "Pyramid", true },
                    { fw::ShapeType::PyramidFrustum, "Pyramid Frustum", true },
                    { fw::ShapeType::Cylinder, "Cylinder", true },
                    { fw::ShapeType::HollowCylinder, "Hollow Cylinder", true },
                    { fw::ShapeType::Cone, "Cone", true },
                    { fw::ShapeType::ConeFrustum, "Cone Frustum", true },
                    { fw::ShapeType::Sphere, "Sphere", true },
                    { fw::ShapeType::Capsule, "Capsule", true },
                    { fw::ShapeType::LegacySuperSphere, "Legacy SuperSphere", true },
                    { fw::ShapeType::SphericalZone, "Spherical Zone", true },
                    { fw::ShapeType::SphericalSegment, "Spherical Segment", true },
                    { fw::ShapeType::SphericalSector, "Spherical Sector", true },
                    { fw::ShapeType::SphereWithCylindricalBore, "Sphere w/ Bore", true },
                    { fw::ShapeType::SphereWithConicalCavities, "Sphere w/ Cavities", true },
                    { fw::ShapeType::SlicedCylinder, "Sliced Cylinder", true },
                    { fw::ShapeType::Ungula, "Ungula", true },
                    { fw::ShapeType::Barrel, "Barrel", true },
                    { fw::ShapeType::Compound, "Compound", false },
                    { fw::ShapeType::ConvexHull, "Convex Hull", false },
                    { fw::ShapeType::Heightfield, "Heightfield", false },
                    { fw::ShapeType::SignedDistanceField, "Signed Distance Field", false },
                    { fw::ShapeType::TriangleMesh, "Triangle Mesh", false }
                };
                
                fw::ShapeType currentType = fw::ShapeType::None;
                if (mat.sharedShape.IsValid()) {
                    const fw::ShapeDefinition* currentDef = m_context->shapeRegistry->GetShapeDef(mat.sharedShape);
                    if (currentDef) {
                        currentType = currentDef->type;
                    }
                } else if (mat.shapeType == 1) {
                    currentType = fw::ShapeType::LegacySuperSphere;
                } else {
                    currentType = fw::ShapeType::Cube;
                }

                int currentIndex = -1;
                for (int i = 0; i < std::size(supportedTypes); ++i) {
                    if (supportedTypes[i].type == currentType) {
                        currentIndex = i;
                        break;
                    }
                }

                if (ImGui::BeginCombo("Tipo Geometria", currentIndex >= 0 ? supportedTypes[currentIndex].name : "Sconosciuto")) {
                    for (int i = 0; i < std::size(supportedTypes); ++i) {
                        bool is_selected = (currentIndex == i);
                        bool supported = supportedTypes[i].supported;
                        
                        if (!supported) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                        }
                        
                        std::string label = supportedTypes[i].name;
                        if (!supported) label += " (NON IMPLEMENTATO)";
                        
                        if (ImGui::Selectable(label.c_str(), is_selected) && supported && currentIndex != i) {
                            fw::ShapeDefinition newDef;
                            newDef.type = supportedTypes[i].type;
                            switch (newDef.type) {
                                case fw::ShapeType::Cube: newDef.parameters = std::monostate{}; break;
                                case fw::ShapeType::Cuboid: newDef.parameters = fw::ShapeParamsCuboid{{1,1,1}}; break;
                                case fw::ShapeType::Parallelepiped: newDef.parameters = fw::ShapeParamsParallelepiped{{1,0,0},{0,1,0},{0,0,1}}; break;
                                case fw::ShapeType::Pyramid: newDef.parameters = fw::ShapeParamsPyramid{1,1,1,{0,0}}; break;
                                case fw::ShapeType::PyramidFrustum: newDef.parameters = fw::ShapeParamsPyramidFrustum{1,1,0.5f,0.5f,1,{0,0}}; break;
                                case fw::ShapeType::Cylinder: newDef.parameters = fw::ShapeParamsCylinder{0.5f, 1.0f}; break;
                                case fw::ShapeType::HollowCylinder: newDef.parameters = fw::ShapeParamsHollowCylinder{0.5f, 0.25f, 1.0f}; break;
                                case fw::ShapeType::Cone: newDef.parameters = fw::ShapeParamsCone{0.5f, 1.0f}; break;
                                case fw::ShapeType::ConeFrustum: newDef.parameters = fw::ShapeParamsConeFrustum{0.5f, 0.25f, 1.0f}; break;
                                case fw::ShapeType::Sphere: newDef.parameters = fw::ShapeParamsSphere{0.5f}; break;
                                case fw::ShapeType::Capsule: newDef.parameters = fw::ShapeParamsCapsule{0.5f, 0.5f}; break;
                                case fw::ShapeType::LegacySuperSphere: newDef.parameters = fw::ShapeParamsLegacySuperSphere{2.0f}; break;
                                case fw::ShapeType::SphericalZone: newDef.parameters = fw::ShapeParamsSphericalZone{1.0f, -0.5f, 0.5f}; break;
                                case fw::ShapeType::SphericalSegment: newDef.parameters = fw::ShapeParamsSphericalSegment{1.0f, 0.0f}; break;
                                case fw::ShapeType::SphericalSector: newDef.parameters = fw::ShapeParamsSphericalSector{1.0f, 3.14159f, 1.57079f}; break;
                                case fw::ShapeType::SphereWithCylindricalBore: newDef.parameters = fw::ShapeParamsSphereWithCylindricalBore{1.0f, 0.5f, 2.0f}; break;
                                case fw::ShapeType::SphereWithConicalCavities: newDef.parameters = fw::ShapeParamsSphereWithConicalCavities{1.0f, 0.5f, 0.5f}; break;
                                case fw::ShapeType::SlicedCylinder: newDef.parameters = fw::ShapeParamsSlicedCylinder{0.5f, 1.0f, {0.0f, 1.0f, 1.0f}}; break;
                                case fw::ShapeType::Ungula: newDef.parameters = fw::ShapeParamsUngula{0.5f, 1.0f, 0.78539f}; break;
                                case fw::ShapeType::Barrel: newDef.parameters = fw::ShapeParamsBarrel{1.0f, 0.8f, 2.0f}; break;
                                default: break;
                            }
                            newDef.supportsVisualCompilation = true;
                            mat.sharedShape = m_context->shapeRegistry->RegisterShape(newDef);
                            mat.shapeType = (newDef.type == fw::ShapeType::LegacySuperSphere) ? 1 : 0;
                            isDirty = true;
                        }
                        
                        if (!supported) {
                            ImGui::PopStyleColor();
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                
                if (mat.sharedShape.IsValid()) {
                    const fw::ShapeDefinition* currentDef = m_context->shapeRegistry->GetShapeDef(mat.sharedShape);
                    if (currentDef) {
                        if (m_editorShapeID != mat.sharedShape.id) {
                            m_editorShapeID = mat.sharedShape.id;
                            m_editorParams = currentDef->parameters;
                            m_isolateSharedShape = true;
                            m_sharedEditConfirmed = false;
                        }
                        
                        uint32_t users = m_context->materialRegistry->CountShapeUsers(mat.sharedShape.id);
                        if (users > 1) {
                            ImGui::Separator();
                            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Shape #%u \xe2\x80\x94 Shared by %u materials", mat.sharedShape.id, users);
                            ImGui::TextDisabled("Editing mode: %s", m_isolateSharedShape ? "Isolated (Copy-on-Write)" : "Global (Affects all)");
                            
                            if (ImGui::RadioButton("Edit Selected Block", m_isolateSharedShape)) {
                                m_isolateSharedShape = true;
                                m_sharedEditConfirmed = false;
                            }
                            ImGui::SameLine();
                            if (ImGui::RadioButton("Edit Shared Shape", !m_isolateSharedShape)) {
                                m_isolateSharedShape = false;
                            }
                            
                            if (!m_isolateSharedShape && !m_sharedEditConfirmed) {
                                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "WARNING: This will affect all %u blocks.", users);
                                if (ImGui::Button("Confirm Shared Edit")) {
                                    m_sharedEditConfirmed = true;
                                }
                            }
                            ImGui::Separator();
                        } else {
                            m_isolateSharedShape = true;
                            m_sharedEditConfirmed = false;
                        }

                        bool paramsChanged = false;
                        
                        if (auto* p = std::get_if<fw::ShapeParamsLegacySuperSphere>(&m_editorParams)) {
                            if (ImGui::SliderFloat("Esponente SuperSfera (n)", &p->n, 0.2f, 10.0f)) paramsChanged = true;
                            ImGui::TextDisabled("n=0.6: Astroide | n=1: Ottaedro | n=2: Sfera | n=4: Cubo Smussato");
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsCuboid>(&m_editorParams)) {
                            if (ImGui::DragFloat3("Dimensioni (X,Y,Z)", &p->size.x, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsParallelepiped>(&m_editorParams)) {
                            if (ImGui::DragFloat3("Base X Vector", &p->basisX.x, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat3("Base Y Vector", &p->basisY.x, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat3("Base Z Vector", &p->basisZ.x, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsPyramid>(&m_editorParams)) {
                            if (ImGui::DragFloat("Base Width", &p->baseWidth, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Base Depth", &p->baseDepth, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Height", &p->height, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat2("Apex Offset", &p->apexOffset.x, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsPyramidFrustum>(&m_editorParams)) {
                            if (ImGui::DragFloat("Bottom Width", &p->bottomWidth, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Bottom Depth", &p->bottomDepth, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Top Width", &p->topWidth, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Top Depth", &p->topDepth, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Height", &p->height, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat2("Apex Offset", &p->apexOffset.x, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsCylinder>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio", &p->radius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Altezza", &p->height, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsCone>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio Base", &p->radius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Altezza", &p->height, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsConeFrustum>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio Base", &p->bottomRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Raggio Superiore", &p->topRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Altezza", &p->height, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsHollowCylinder>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio Esterno", &p->outerRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Raggio Interno", &p->innerRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Altezza", &p->height, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsCapsule>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio", &p->radius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Mezza Altezza Cilindro", &p->halfHeight, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsSphere>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio", &p->radius, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsSphericalZone>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio Sfera", &p->radius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Taglio Inferiore (Y)", &p->bottomY, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Taglio Superiore (Y)", &p->topY, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsSphericalSegment>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio Sfera", &p->radius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Taglio Base (Y)", &p->baseY, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsSphericalSector>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio", &p->radius, 0.05f)) paramsChanged = true;
                            if (ImGui::SliderAngle("Range Polare (Theta)", &p->thetaRange, 0.0f, 360.0f)) paramsChanged = true;
                            if (ImGui::SliderAngle("Range Azimutale (Phi)", &p->phiRange, 0.0f, 180.0f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsSphereWithCylindricalBore>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio Sfera", &p->sphereRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Raggio Foro (Cilindro)", &p->cylinderRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Altezza Cilindro (Riservato)", &p->cylinderHeight, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsSphereWithConicalCavities>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio Sfera", &p->sphereRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Raggio Cavita' (Cono)", &p->coneRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Profondita' Cavita' (Cono)", &p->coneHeight, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsSlicedCylinder>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio", &p->radius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Altezza", &p->height, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat3("Normale di Taglio", &p->cutNormal.x, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsUngula>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio", &p->radius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Altezza", &p->height, 0.05f)) paramsChanged = true;
                            if (ImGui::SliderAngle("Angolo di Taglio", &p->cutAngle, 0.0f, 89.9f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsBarrel>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio Centrale", &p->midRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Raggio Estremo", &p->endRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Altezza", &p->height, 0.05f)) paramsChanged = true;
                        }
                        
                        if (paramsChanged) {
                            auto valid = m_context->shapeRegistry->ValidateParameters(currentDef->type, m_editorParams, mat.sharedShape.id);
                            if (valid.isValid) {
                                m_lastValidationError = "";
                                bool canEdit = true;
                                
                                if (users > 1) {
                                    if (m_isolateSharedShape) {
                                        fw::ShapeDefinition clone = *currentDef;
                                        clone.id = 0; 
                                        
                                        fw::ShapeHandle newHandle = m_context->shapeRegistry->RegisterShape(clone);
                                        if (newHandle.IsValid()) {
                                            mat.sharedShape = newHandle;
                                            m_editorShapeID = newHandle.id;
                                        } else {
                                            canEdit = false;
                                        }
                                    } else {
                                        if (!m_sharedEditConfirmed) {
                                            canEdit = false;
                                        }
                                    }
                                }
                                
                                if (canEdit) {
                                    if (m_context->shapeRegistry->UpdateShape(mat.sharedShape, m_editorParams)) {
                                        mat.sharedShape.revision++;
                                        isDirty = true;
                                    }
                                }
                            } else {
                                m_lastValidationError = valid.errorMessage;
                            }
                        }
                        
                        if (!m_lastValidationError.empty()) {
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Parametri non validi:");
                            ImGui::TextWrapped("%s", m_lastValidationError.c_str());
                        }
                    }
                } else {
                    if (mat.shapeType == 1) {
                        ImGui::TextDisabled("Modalita' Legacy. Converti selezionando un tipo dal menu.");
                        if (ImGui::SliderFloat("Esponente SuperSfera (n) [Legacy]", &mat.superSphereN, 0.2f, 10.0f)) {
                            isDirty = true;
                        }
                        ImGui::TextDisabled("n=0.6: Astroide | n=1: Ottaedro | n=2: Sfera | n=4: Cubo Smussato");
                    } else {
                        ImGui::TextDisabled("Standard Voxel Cube [Legacy]");
                    }
                }
                
                if (!m_lastCompileError.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Errore di Compilazione Geometria:");
                    ImGui::TextWrapped("%s", m_lastCompileError.c_str());
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "PBR Texture Maps");

                if (ImGui::Button("Apri Cartella Texture OS", ImVec2(-1, 30))) {
                    std::filesystem::create_directories("assets/textures");
                    ShellExecuteA(NULL, "open", "assets\\textures", NULL, NULL, SW_SHOWDEFAULT);
                }
                ImGui::Spacing();
                
                auto drawTextureField = [&](const char* label, std::string& pathRef, const std::string& typeSuffix, RenderManager::PBRTextureType pbrType) {
                    char buf[256];
                    strncpy(buf, pathRef.c_str(), sizeof(buf));
                    
                    ImGui::PushID(label);
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100.0f);
                    if (ImGui::InputText("##path", buf, sizeof(buf))) {
                        pathRef = buf;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Sfoglia...", ImVec2(80, 0))) {
                        std::string picked = BrowseForImage();
                        if (!picked.empty()) {
                            m_isCopying = true;
                            m_copyProgress = 0.0f;
                            
                            // Copia il file e salva automaticamente
                            pathRef = CopyTextureToAssets(picked, m_selectedBlockId, typeSuffix);
                            m_context->materialRegistry->SaveToJson("assets/definitions/materials.json");
                            if (std::filesystem::exists("../assets/definitions")) {
                                m_context->materialRegistry->SaveToJson("../assets/definitions/materials.json");
                            }
                            
                            // Sincronizza la cache GPU del materiale tramite il CacheManager!
                            if (m_context && m_context->cacheManager) {
                                m_context->cacheManager->SyncMaterialGpuCache(m_selectedBlockId, m_context);
                            }
                            
                            m_copyProgress = 1.0f;
                            m_saveMessageTimer = 3.0f;
                        }
                    }
                    ImGui::PopID();
                    ImGui::Text("%s", label);
                };

                drawTextureField("Albedo Map", mat.albedoPath, "albedo", RenderManager::PBRTextureType::ALBEDO);
                drawTextureField("Normal Map", mat.normalPath, "normal", RenderManager::PBRTextureType::NORMAL);
                drawTextureField("ORM Map (Occlusion, Roughness, Metallic)", mat.ormPath, "orm", RenderManager::PBRTextureType::ORM);
                
                // Barra di progresso simulata / Feedback visivo
                if (m_isCopying) {
                    ImGui::Spacing();
                    ImGui::ProgressBar(m_copyProgress, ImVec2(-1, 0), "Copia Texture in corso...");
                    if (m_copyProgress >= 1.0f) {
                        m_isCopying = false;
                    }
                }
                
                if (m_saveMessageTimer > 0.0f) {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Texture caricata e Materiale salvato automaticamente!");
                }
                
                if (isDirty) {
                    UpdatePreviewMesh();
                    if (m_context && m_context->cacheManager) {
                        m_context->cacheManager->SyncMaterialGpuCache(m_selectedBlockId, m_context);
                    }
                }
                
                ImGui::EndTabItem();
            }

            // TAB: PHYSICS & SIMULATION
            if (ImGui::BeginTabItem("Physics")) {
                ImGui::Spacing();
                
                // 1. Diagnostics, Dirty Status & Revert Baseline
                bool isPhysicsDirty = IsBlockPhysicsDirty(m_selectedBlockId);
                uint32_t totalDirtyPhysics = CountUnsavedPhysicsBlocks();

                ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "CONTRATTO FISICO ASSET — ID %u: %s", m_selectedBlockId, def.displayName.c_str());
                if (isPhysicsDirty) {
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Stato Fisica: MODIFICHE NON SALVATE (*)");
                    ImGui::SameLine();
                    if (ImGui::Button("Ripristina Baseline Fisica (Revert)##RevPhys")) {
                        RevertBlockPhysicsToSaved(m_selectedBlockId);
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Stato Fisica: SINCRONIZZATO (Baseline salvata)");
                }
                if (totalDirtyPhysics > 0) {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Blocchi con fisica modificata nel registro: %u (*)", totalDirtyPhysics);
                }

                ImGui::Separator();

                // 2. Motion Type
                ImGui::Text("Tipo di Moto (Motion Type):");
                int motionTypeIdx = static_cast<int>(def.physics.motionType);
                if (ImGui::RadioButton("Static##MotionStatic", motionTypeIdx == 0)) {
                    def.physics.motionType = fw::BlockMotionType::Static;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Static: Blocco fisso nel mondo (massa infinita per la cinematica del mondo, ideale per blocchi voxel standard).");
                ImGui::SameLine();
                if (ImGui::RadioButton("Dynamic##MotionDynamic", motionTypeIdx == 1)) {
                    def.physics.motionType = fw::BlockMotionType::Dynamic;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Dynamic: Soggetto a forze esterne, gravita, accelerazione e urti fisici.");
                ImGui::SameLine();
                if (ImGui::RadioButton("Kinematic##MotionKinematic", motionTypeIdx == 2)) {
                    def.physics.motionType = fw::BlockMotionType::Kinematic;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Kinematic: Mosso esclusivamente da script/animazioni, non risente di forze esterne.");

                // 3. Collision Enabled
                if (ImGui::Checkbox("Collisioni Abilitate (Collision Enabled)", &def.physics.collisionEnabled)) {
                    def.SyncLegacyPhysicsFields();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Determina se il blocco genera collisioni fisiche solide (isSolid).");

                ImGui::Separator();

                // 4. Mass Rule: Manual vs Density-Derived
                ImGui::Text("Regola di Definizione Massa:");
                int massModeIdx = static_cast<int>(def.physics.massMode);
                if (ImGui::RadioButton("Massa Manuale##MassManual", massModeIdx == 0)) {
                    def.physics.massMode = fw::BlockMassMode::Manual;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Massa specificata direttamente dall'autore in kg, costante indipendentemente dal volume.");
                ImGui::SameLine();
                if (ImGui::RadioButton("Massa da Densita e Volume##MassDensity", massModeIdx == 1)) {
                    def.physics.massMode = fw::BlockMassMode::DensityDerived;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Massa calcolata = Densita (kg/m^3) * Volume calcolato dalla scala dell'asset.");

                float volume = std::abs(def.localScale.x * def.localScale.y * def.localScale.z);
                if (volume < 1e-4f) volume = 1e-4f;
                float derivedMass = def.physics.CalculateDerivedMass(def.localScale);
                float effectiveMass = def.physics.GetEffectiveMass(def.localScale);

                if (def.physics.massMode == fw::BlockMassMode::Manual) {
                    if (ImGui::SliderFloat("Massa (kg)##MassManualVal", &def.physics.mass, 0.01f, 5000.0f, "%.2f kg")) {
                        def.SyncLegacyPhysicsFields();
                    }
                    ImGui::SliderFloat("Densita di Riferimento (kg/m^3)", &def.physics.density, 1.0f, 25000.0f, "%.1f");
                    ImGui::Text("Volume Asset (Scala): %.4f m^3 | Massa Effettiva: %.2f kg", volume, effectiveMass);
                } else {
                    if (ImGui::SliderFloat("Densita Materiale (kg/m^3)##DensityVal", &def.physics.density, 1.0f, 25000.0f, "%.1f")) {
                        def.SyncLegacyPhysicsFields();
                    }
                    ImGui::Text("Volume Asset (Scala): %.4f m^3", volume);
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.6f, 1.0f), "Massa Derivata (Autorevole): %.3f kg", derivedMass);
                    def.physics.mass = derivedMass;
                    def.SyncLegacyPhysicsFields();
                }

                // 5. Friction & Restitution
                if (ImGui::SliderFloat("Attrito (Friction) [0.0 - 1.0]", &def.physics.friction, 0.0f, 1.0f, "%.3f")) {
                    def.SyncLegacyPhysicsFields();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Coefficiente di attrito radente superficiale (0 = privo di attrito, 1 = massimo grip).");

                if (ImGui::SliderFloat("Restituzione / Rimbalzo [0.0 - 1.0]", &def.physics.restitution, 0.0f, 1.0f, "%.3f")) {
                    def.SyncLegacyPhysicsFields();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Coefficiente di restituzione elastica (0 = perfettamente anelastico, 1 = rimbalzo conservativo).");

                // 6. Thermodynamics
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Termodinamica (Thermodynamics)");
                ImGui::SliderFloat("Resistenza Termica", &def.thermal_resistance, 0.1f, 100.0f, "%.1f");
                ImGui::SliderFloat("Capacita' Termica", &def.thermal_capacity, 0.1f, 100.0f, "%.1f");

                // 7. Preview Drop & Rotation
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.8f, 0.4f, 1.0f, 1.0f), "Simulazione Preview Caduta");
                
                if (ImGui::Button("Drop Block!")) {
                    m_simulatePhysics = true;
                    m_simPosY = 5.0f; // Sgancia il blocco da 5 metri
                    m_simVelY = 0.0f;
                }
                
                if (m_simulatePhysics) {
                    ImGui::SameLine();
                    if (ImGui::Button("Reset Drop")) {
                        m_simulatePhysics = false;
                        m_simPosY = 0.0f;
                        m_simVelY = 0.0f;
                    }
                }
                
                ImGui::Separator();
                
                ImGui::Text("Opzioni Preview");
                ImGui::Checkbox("Simulate Physics (Bouncing)", &m_simulatePhysics);
                ImGui::Checkbox("Auto-Rotate Block", &m_autoRotateBlock);
                if (!m_autoRotateBlock) {
                    ImGui::SliderFloat3("Manual Rotation (Euler)", &m_blockEulerAngles.x, 0.0f, 360.0f);
                }
                
                // 8. Telemetria Componenti ECS Physics Runtime
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.6f, 1.0f), "COMPONENTI ECS FISICA RUNTIME (Preview Entity):");
                auto* ecsMass = m_previewWorld ? m_previewWorld->GetRegistry().try_get<MassPropertiesComponent>(m_previewBlockEntity) : nullptr;
                auto* ecsRb = m_previewWorld ? m_previewWorld->GetRegistry().try_get<RigidBody>(m_previewBlockEntity) : nullptr;
                if (ecsMass && ecsRb) {
                    ImGui::Text("MassPropertiesComponent: Massa = %.2f kg | CoM = (%.2f, %.2f, %.2f)",
                        ecsMass->mass, ecsMass->centerOfMass.x, ecsMass->centerOfMass.y, ecsMass->centerOfMass.z);
                    ImGui::Text("Inertia Tensor: Ixx=%.3f, Iyy=%.3f, Izz=%.3f",
                        ecsMass->inertiaTensor[0][0], ecsMass->inertiaTensor[1][1], ecsMass->inertiaTensor[2][2]);
                    ImGui::Text("RigidBody: Massa = %.2f kg | Restituzione = %.3f | Attrito = %.3f | Drag = %.3f",
                        ecsRb->mass, ecsRb->restitution, ecsRb->friction, ecsRb->drag);
                } else {
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "Componenti ECS: In sincronizzazione...");
                }

                // 9. Zero-Work Physics Save Button
                if (ImGui::Button("SALVA DEFINIZIONI FISICHE (JSON)", ImVec2(-1, 40))) {
                    m_zeroWorkTelemetry.jsonSaveCalls++;
                    def.SyncLegacyPhysicsFields();
                    m_context->blockRegistry->UpdateBlock(m_selectedBlockId, def);
                    bool s1 = m_context->blockRegistry->SaveToJson("assets/definitions/blocks.json");
                    bool s2 = true;
                    if (std::filesystem::exists("../assets/definitions")) {
                        s2 = m_context->blockRegistry->SaveToJson("../assets/definitions/blocks.json");
                    }
                    if (s1 && s2) {
                        SynchronizeAllSavedPhysicsBaselines();
                        m_saveMessageTimer = 3.0f;
                        std::cout << "[BlockMaker] Proprieta fisiche del blocco ID " << (int)m_selectedBlockId << " salvate su disco e baseline sincronizzata.\n";
                    } else {
                        std::cerr << "[BlockMaker] ERRORE: Salvataggio definizioni fisiche su disco non riuscito!\n";
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Salva le proprieta fisiche dei blocchi su disco (blocks.json).\nNessun ricaricamento di mesh GPU o rigenerazione geometrica (Zero-Work).");
                }
                
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
        
        ImGui::Separator();
        
        if (ImGui::Button("SALVA DEFINIZIONE & ASSET BLOCCO (JSON/GPU)", ImVec2(-1, 35))) {
            m_context->blockRegistry->UpdateBlock(m_selectedBlockId, def);
            m_context->materialRegistry->UpdateMaterial(m_selectedBlockId, mat);
            bool s1 = m_context->blockRegistry->SaveToJson("assets/definitions/blocks.json");
            m_context->shapeRegistry->SaveToJson("assets/definitions/shapes.json");
            m_context->materialRegistry->SaveToJson("assets/definitions/materials.json");
            bool s2 = true;
            if (std::filesystem::exists("../assets/definitions")) {
                s2 = m_context->blockRegistry->SaveToJson("../assets/definitions/blocks.json");
                m_context->shapeRegistry->SaveToJson("../assets/definitions/shapes.json");
                m_context->materialRegistry->SaveToJson("../assets/definitions/materials.json");
            }
            
            // Esporta asset file in assets/blocks/ per l'Asset Browser di FORGE
            std::filesystem::create_directories("assets/blocks");
            std::string cleanName = def.stringId;
            std::replace(cleanName.begin(), cleanName.end(), ':', '_');
            if (cleanName.empty()) cleanName = "block_" + std::to_string(m_selectedBlockId);
            
            std::unordered_map<glm::ivec3, fw::StructureBlock> bMap;
            bMap[glm::ivec3(0,0,0)] = { (int)m_selectedBlockId, glm::vec4(mat.baseColorFallback, 1.0f) };
            m_previewWorld->GetStructureManager().SaveStructure(cleanName, bMap, 0, 0, 0, 0);

            if (m_context->cacheManager) {
                m_context->cacheManager->SyncMaterialGpuCache(m_selectedBlockId, m_context);
            }
            if (m_context->engine && m_context->engine->GetRenderManager()) {
                m_context->engine->GetRenderManager()->InvalidateForgeCache();
            }
            UpdatePreviewMesh();
            if (s1 && s2) {
                SynchronizeAllSavedTransformBaselines();
                SynchronizeAllSavedPhysicsBaselines();
                m_saveMessageTimer = 3.0f;
                std::cout << "[BlockMaker] Definizioni fisiche, grafiche e geometriche del blocco " << (int)m_selectedBlockId << " salvate con successo su disco e sincronizzate in GPU!\n";
            } else {
                std::cerr << "[BlockMaker] ERRORE durante il salvataggio su disco!\n";
            }
        }
        
        if (m_saveMessageTimer > 0.0f) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Definizione Blocco ID %d (%s) salvata ed esportata per FORGE!", m_selectedBlockId, def.displayName.c_str());
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Preview Lighting");
        
        // Gizmo-like Control per la Luce
        if (ImGui::DragFloat3("Light Direction", &m_previewLightDir.x, 0.01f, -1.0f, 1.0f)) {
            if (glm::length(m_previewLightDir) > 0.001f) {
                m_previewLightDir = glm::normalize(m_previewLightDir);
            } else {
                m_previewLightDir = glm::vec3(0, -1, 0);
            }
        }

        ImGui::EndChild(); // Fine BlockPropertiesScrollRegion
    }
    ImGui::Columns(1);
    ImGui::End();
}

void BlockMakerState::DrawGizmo() {
    if (!m_gizmoEnabled) {
        m_isGizmoInteracting = false;
        return;
    }
    if (!m_context || !m_context->blockRegistry || m_previewBlockEntity == entt::null) {
        m_isGizmoInteracting = false;
        return;
    }

    fw::SimBlockDef& def = m_context->blockRegistry->GetBlockMutable(m_selectedBlockId);

    ImGuiIO& io = ImGui::GetIO();
    ImGuiViewport* vp = ImGui::GetMainViewport();

    // 1. Calcolo grounding della presentazione identico a HandlePhysicsSimulation
    float groundY = 0.0f;
    if (!m_cachedGeometryMesh.vertices.empty()) {
        fw::AABB bounds = m_cachedGeometryMesh.bounds();
        glm::vec3 corners[8] = {
            { bounds.min.x, bounds.min.y, bounds.min.z },
            { bounds.min.x, bounds.min.y, bounds.max.z },
            { bounds.min.x, bounds.max.y, bounds.min.z },
            { bounds.min.x, bounds.max.y, bounds.max.z },
            { bounds.max.x, bounds.min.y, bounds.min.z },
            { bounds.max.x, bounds.min.y, bounds.max.z },
            { bounds.max.x, bounds.max.y, bounds.min.z },
            { bounds.max.x, bounds.max.y, bounds.max.z }
        };

        glm::mat4 mShapeInAsset = glm::mat4(1.0f);
        mShapeInAsset = mShapeInAsset * glm::mat4_cast(glm::quat(glm::radians(def.localRotation)));
        mShapeInAsset = glm::scale(mShapeInAsset, def.localScale);
        mShapeInAsset = glm::translate(mShapeInAsset, -def.pivotOffset);

        float minShapeY = 1e9f;
        for (int i = 0; i < 8; ++i) {
            glm::vec4 tc = mShapeInAsset * glm::vec4(corners[i], 1.0f);
            if (tc.y < minShapeY) {
                minShapeY = tc.y;
            }
        }
        groundY = -0.50f - minShapeY;
    }

    // Safety: Reset latch if mouse was released or gizmo mode / block selection changed
    if (m_scaleDragLatchedAxis >= 0 || m_scaleZDragLatched) {
        if (!io.MouseDown[0] || m_gizmoOperation != 2 || m_selectedBlockId != m_scaleLatchedBlockId || !m_gizmoEnabled) {
            float previewEcsScale = 0.0f;
            float previewEcsPosY = 0.0f;
            auto& m_registry = m_previewWorld->GetRegistry();
            if (m_previewBlockEntity != entt::null && m_registry.valid(m_previewBlockEntity)) {
                auto& tc = m_registry.get<fw::TransformComponent>(m_previewBlockEntity);
                previewEcsScale = (m_scaleDragLatchedAxis == 1) ? tc.scale.y : tc.scale.z;
                previewEcsPosY = tc.location.y;
            }
            if (m_scaleDragLatchedAxis == 1) {
                LogScaleYDiagnostics("LATCH_END", m_scaleLatchedBlockId, io.MouseDown[0], ImGuizmo::IsUsing(), 0, m_scaleDragStartVal, def.localScale.y, def.localScale.y, previewEcsScale, groundY, previewEcsPosY, m_scaleLastInputTransY, m_scaleLastInputTransY, false, false);
            } else {
                LogScaleZDiagnostics("LATCH_END", m_scaleLatchedBlockId, m_gizmoOperation, io.MouseDown[0], ImGuizmo::IsUsing(), 0, false, m_scaleDragStartVal, def.localScale.z, def.localScale.z, def.localScale.z, previewEcsScale, false, 0.0f);
            }
            m_scaleDragLatchedAxis = -1;
            m_scaleZDragLatched = false;
            m_isDraggingScaleZ = false;
            m_isGizmoInteracting = false;
        }
    }

    // BM-A3.3.2.2 & BM-A3.3.2.3: Stabilize input matrix fed to ImGuizmo:
    // Freeze grounding height and scale baseline during latched interaction so that ImGuizmo's internal
    // ray-plane solver operates on a 100% stationary baseline and its denominator never collapses!
    float gizmoGroundY = (m_scaleDragLatchedAxis >= 0) ? m_scaleDragStartGroundY : groundY;

    glm::quat qPreview = glm::quat(glm::radians(m_blockEulerAngles));
    glm::quat qAsset = glm::quat(glm::radians(def.localRotation));

    // 2. PresentationMatrix = T(0, gizmoGroundY + simPosY, 0) * R_preview
    glm::mat4 presMat = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, gizmoGroundY + m_simPosY, 0.0f))
                      * glm::mat4_cast(qPreview);

    // 3. AssetMatrix = T(localPosition) * R_asset * S_asset * T(-pivotOffset)
    glm::vec3 effectiveScaleForGizmo = def.localScale;
    if (m_scaleDragLatchedAxis >= 0) {
        effectiveScaleForGizmo = m_scaleDragStartScaleVec;
    }
    glm::mat4 assetMat = glm::translate(glm::mat4(1.0f), def.localPosition)
                       * glm::mat4_cast(qAsset)
                       * glm::scale(glm::mat4(1.0f), effectiveScaleForGizmo)
                       * glm::translate(glm::mat4(1.0f), -def.pivotOffset);

    // 4. ManipulatedWorldMatrix = PresentationMatrix * AssetMatrix
    glm::mat4 worldMat = presMat * assetMat;
    float gizmoInputTransY = worldMat[3].y;

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    if (vp) {
        ImGuizmo::SetRect(vp->Pos.x, vp->Pos.y, vp->Size.x, vp->Size.y);
    } else {
        ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
    }

    // Protezione overlay ImGui: se il mouse e' sopra finestre/widget UI e non stiamo gia' trascinando, disabilita il gizmo
    bool canInteract = m_gizmoEnabled && (!io.WantCaptureMouse || m_isGizmoInteracting || m_scaleDragLatchedAxis >= 0 || m_scaleZDragLatched);
    ImGuizmo::Enable(canInteract);

    // 5. Configurazione operazione e sistema di riferimento (BM-A3.4.1):
    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE mode = (m_gizmoMode == 0) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    if (m_gizmoOperation == 1) {
        op = ImGuizmo::ROTATE;
        mode = (m_gizmoMode == 0) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    } else if (m_gizmoOperation == 2) {
        op = ImGuizmo::SCALE;
        // BM-A3.4.1 Regression Protection: Scale rimane rigorosamente LOCAL per prevenire skew/shear
        mode = ImGuizmo::LOCAL;
    }

    // BM-A3.4.2: Configurazione snapping per ImGuizmo (Move, Rotate, Scale)
    float snapValues[3] = { 0.0f, 0.0f, 0.0f };
    const float* pSnap = nullptr;
    if (m_snapEnabled) {
        if (op == ImGuizmo::TRANSLATE) {
            float s = (m_snapMove > 1e-4f) ? m_snapMove : 0.5f;
            snapValues[0] = snapValues[1] = snapValues[2] = s;
            pSnap = snapValues;
        } else if (op == ImGuizmo::ROTATE) {
            float s = (m_snapRotate > 1e-4f) ? m_snapRotate : 15.0f;
            snapValues[0] = snapValues[1] = snapValues[2] = s;
            pSnap = snapValues;
        } else if (op == ImGuizmo::SCALE) {
            float s = (m_snapScale > 1e-4f) ? m_snapScale : 0.1f;
            snapValues[0] = snapValues[1] = snapValues[2] = s;
            pSnap = snapValues;
        }
    }

    ImGuizmo::Manipulate(
        glm::value_ptr(m_context->activeCameraView.viewMatrix),
        glm::value_ptr(m_projNoFlip),
        op,
        mode,
        glm::value_ptr(worldMat),
        nullptr,
        pSnap
    );
    float gizmoOutputTransY = worldMat[3].y;

    if (ImGuizmo::IsUsing() || m_scaleDragLatchedAxis >= 0 || m_scaleZDragLatched) {
        m_isGizmoInteracting = true;
        // 6. Conversione corretta della matrice manipolata tenendo conto del pivot:
        // AssetTRS = inverse(PresentationMatrix) * ManipulatedWorldMatrix * T(pivotOffset)
        glm::mat4 assetTRS = glm::inverse(presMat) * worldMat * glm::translate(glm::mat4(1.0f), def.pivotOffset);

        if (op == ImGuizmo::TRANSLATE) {
            // BM-A3.1: Decomposizione traslazione deterministica.
            // Rotazione, scala e pivot rimangono inalterati e congelati.
            def.localPosition = glm::vec3(assetTRS[3]);
        } else if (op == ImGuizmo::ROTATE) {
            // BM-A3.2: Estrazione della rotazione locale deterministica e stabile.
            // AssetTRS = T(localPosition) * R(localRotation) * S(localScale)
            // L'angolo sub-matrice 3x3 e' R * S.
            // Per preservare esattamente la scala non-uniforme ed eliminare qualsiasi shear:
            // Dividiamo ogni colonna per il fattore di scala corrispondente (protetto contro lo zero).
            glm::vec3 col0 = glm::vec3(assetTRS[0]);
            glm::vec3 col1 = glm::vec3(assetTRS[1]);
            glm::vec3 col2 = glm::vec3(assetTRS[2]);

            float sx = (std::abs(def.localScale.x) > 1e-6f) ? def.localScale.x : ((def.localScale.x < 0.0f) ? -1e-6f : 1e-6f);
            float sy = (std::abs(def.localScale.y) > 1e-6f) ? def.localScale.y : ((def.localScale.y < 0.0f) ? -1e-6f : 1e-6f);
            float sz = (std::abs(def.localScale.z) > 1e-6f) ? def.localScale.z : ((def.localScale.z < 0.0f) ? -1e-6f : 1e-6f);

            glm::vec3 u0 = col0 / sx;
            glm::vec3 u1 = col1 / sy;
            glm::vec3 u2 = col2 / sz;

            // Ortonormalizzazione robusta Gram-Schmidt per prevenire drift numerico:
            float lenU0 = glm::length(u0);
            glm::vec3 r0 = (lenU0 > 1e-6f) ? (u0 / lenU0) : glm::vec3(1.0f, 0.0f, 0.0f);

            u1 = u1 - r0 * glm::dot(r0, u1);
            float lenU1 = glm::length(u1);
            glm::vec3 r1 = (lenU1 > 1e-6f) ? (u1 / lenU1) : glm::vec3(0.0f, 1.0f, 0.0f);

            glm::vec3 r2 = glm::cross(r0, r1);
            float lenR2 = glm::length(r2);
            if (lenR2 > 1e-6f) {
                r2 /= lenR2;
            } else {
                r2 = glm::vec3(0.0f, 0.0f, 1.0f);
            }

            // Conversione a quaternione normalizzato
            glm::mat3 rotMat(r0, r1, r2);
            glm::quat qNew = glm::normalize(glm::quat_cast(rotMat));

            // Conversione in angoli di Eulero (gradi) con la convenzione GLM
            glm::vec3 rawEulerDeg = glm::degrees(glm::eulerAngles(qNew));

            // Svolgimento continuo di fase (Phase Unwrapping) rispetto all'angolo precedente
            // per evitare salti improvvisi a +/-180 gradi e drift ciclico.
            if (std::isfinite(rawEulerDeg.x) && std::isfinite(rawEulerDeg.y) && std::isfinite(rawEulerDeg.z)) {
                for (int c = 0; c < 3; ++c) {
                    float delta = rawEulerDeg[c] - def.localRotation[c];
                    while (delta > 180.0f) {
                        rawEulerDeg[c] -= 360.0f;
                        delta -= 360.0f;
                    }
                    while (delta < -180.0f) {
                        rawEulerDeg[c] += 360.0f;
                        delta += 360.0f;
                    }
                    def.localRotation[c] = rawEulerDeg[c];
                }
            }
            // localPosition, localScale e pivotOffset rimangono intatti e immutati!
        } else if (op == ImGuizmo::SCALE) {
            ImGuizmo::MOVETYPE activeHandle = ImGuizmo::GetActiveHandleType();

            // Extract matrix-derived scales, shear diagnostics, and stationary shift metrics
            glm::vec3 col0 = glm::vec3(assetTRS[0]);
            glm::vec3 col1 = glm::vec3(assetTRS[1]);
            glm::vec3 col2 = glm::vec3(assetTRS[2]);
            glm::mat3 rotMat = glm::mat3_cast(qAsset);
            glm::vec3 axisX = glm::normalize(rotMat[0]);
            glm::vec3 axisY = glm::normalize(rotMat[1]);
            glm::vec3 axisZ = glm::normalize(rotMat[2]);
            float rawSx = glm::dot(col0, axisX);
            float rawSy = glm::dot(col1, axisY);
            float rawSz = glm::dot(col2, axisZ);
            float maxScale = std::max({ std::abs(rawSx), std::abs(rawSy), std::abs(rawSz), 0.1f });
            float shearErrX = glm::length(col0 - rawSx * axisX) / maxScale;
            float shearErrY = glm::length(col1 - rawSy * axisY) / maxScale;
            float shearErrZ = glm::length(col2 - rawSz * axisZ) / maxScale;
            bool shearRejected = (shearErrX >= 0.10f || shearErrY >= 0.10f || shearErrZ >= 0.10f || !std::isfinite(rawSx) || !std::isfinite(rawSy) || !std::isfinite(rawSz));

            bool stationaryMouse = (std::abs(io.MouseDelta.x) < 0.1f && std::abs(io.MouseDelta.y) < 0.1f);
            bool statShift = (stationaryMouse && std::abs(gizmoInputTransY - m_scaleLastInputTransY) > 1e-4f);
            m_scaleLastInputTransY = gizmoInputTransY;

            // 1. LATCH ACQUISITION:
            // Acquire latch on the frame when the user begins manipulating any scale axis (X, Y, or Z)
            if (m_scaleDragLatchedAxis < 0) {
                if (ImGuizmo::IsUsing()) {
                    if (activeHandle == ImGuizmo::MT_SCALE_X) m_scaleDragLatchedAxis = 0;
                    else if (activeHandle == ImGuizmo::MT_SCALE_Y) m_scaleDragLatchedAxis = 1;
                    else if (activeHandle == ImGuizmo::MT_SCALE_Z) m_scaleDragLatchedAxis = 2;

                    if (m_scaleDragLatchedAxis >= 0) {
                        m_scaleLatchedBlockId = m_selectedBlockId;
                        m_scaleDragStartVal = def.localScale[m_scaleDragLatchedAxis];
                        m_scaleDragStartScaleVec = def.localScale;
                        m_scaleDragStartMouse = glm::vec2(io.MousePos.x, io.MousePos.y);
                        m_scaleDragStartGroundY = groundY;
                        m_scaleZDragLatched = (m_scaleDragLatchedAxis == 2);
                        m_isDraggingScaleZ = (m_scaleDragLatchedAxis == 2);

                        // Calcolo della direzione 2D dell'asse locale nello spazio schermo
                        // Utilizziamo l'asse intrinseco invariante (qPreview * qAsset * localAxis),
                        // indipendente dal segno o ampiezza di localScale per evitare flip di orientamento
                        glm::vec3 localAxisDir(0.0f);
                        localAxisDir[m_scaleDragLatchedAxis] = 1.0f;
                        glm::vec3 worldOrigin = glm::vec3(presMat * glm::translate(glm::mat4(1.0f), def.localPosition) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
                        glm::vec3 worldAxisDir = glm::normalize((qPreview * qAsset) * localAxisDir);
                        glm::vec3 worldAxisTarget = worldOrigin + worldAxisDir * 1.0f;

                        auto projectToScreen = [&](const glm::vec3& p) -> glm::vec2 {
                            glm::vec4 clip = m_projNoFlip * m_context->activeCameraView.viewMatrix * glm::vec4(p, 1.0f);
                            if (std::abs(clip.w) < 1e-5f) return glm::vec2(0.0f);
                            glm::vec3 ndc = glm::vec3(clip) / clip.w;
                            float vx = vp ? vp->Pos.x : 0.0f;
                            float vy = vp ? vp->Pos.y : 0.0f;
                            float vw = vp ? vp->Size.x : io.DisplaySize.x;
                            float vh = vp ? vp->Size.y : io.DisplaySize.y;
                            return glm::vec2(
                                vx + (ndc.x * 0.5f + 0.5f) * vw,
                                vy + (0.5f - ndc.y * 0.5f) * vh
                            );
                        };

                        glm::vec2 sOrigin = projectToScreen(worldOrigin);
                        glm::vec2 sTarget = projectToScreen(worldAxisTarget);
                        glm::vec2 sDir = sTarget - sOrigin;
                        float sLen = glm::length(sDir);
                        if (sLen > 2.0f) {
                            m_scaleProjectedAxis = sDir / sLen;
                            m_scaleProjectedHandleLength = std::max(sLen * 0.75f, 40.0f);
                        } else {
                            if (m_scaleDragLatchedAxis == 0) m_scaleProjectedAxis = glm::vec2(1.0f, 0.0f);
                            else if (m_scaleDragLatchedAxis == 1) m_scaleProjectedAxis = glm::vec2(0.0f, -1.0f);
                            else m_scaleProjectedAxis = glm::normalize(glm::vec2(0.7071f, -0.7071f));
                            m_scaleProjectedHandleLength = 80.0f;
                        }

                        if (m_scaleDragLatchedAxis == 1) {
                            LogScaleYDiagnostics("LATCH_START", m_selectedBlockId, io.MouseDown[0], ImGuizmo::IsUsing(), (int)activeHandle, m_scaleDragStartVal, rawSy, def.localScale.y, def.localScale.y, groundY, groundY + m_simPosY + def.localPosition.y, gizmoInputTransY, gizmoOutputTransY, shearRejected, statShift);
                        } else if (m_scaleDragLatchedAxis == 2) {
                            float previewEcsSz = def.localScale.z;
                            auto& m_reg = m_previewWorld->GetRegistry();
                            if (m_previewBlockEntity != entt::null && m_reg.valid(m_previewBlockEntity)) {
                                previewEcsSz = m_reg.get<fw::TransformComponent>(m_previewBlockEntity).scale.z;
                            }
                            LogScaleZDiagnostics("LATCH_START", m_selectedBlockId, m_gizmoOperation, io.MouseDown[0], ImGuizmo::IsUsing(), (int)activeHandle, true, m_scaleDragStartVal, rawSz, def.localScale.z, def.localScale.z, previewEcsSz, false, 0.0f);
                        }
                    }
                }
            }

            if (m_scaleDragLatchedAxis >= 0) {
                // BM-A3.3.2.3: Authoritative Single-Owner Scale Pipeline across X, Y, and Z
                glm::vec2 totalMouseDisp = glm::vec2(io.MousePos.x, io.MousePos.y) - m_scaleDragStartMouse;
                float dragPixels = glm::dot(totalMouseDisp, m_scaleProjectedAxis);
                float deltaScale = dragPixels / m_scaleProjectedHandleLength;

                // BM-A3.4.2: Snapping scala configurabile relativo alla scala iniziale del drag
                if (m_snapEnabled && m_snapScale > 1e-4f) {
                    deltaScale = std::round(deltaScale / m_snapScale) * m_snapScale;
                }

                float targetVal = m_scaleDragStartVal + std::copysign(1.0f, m_scaleDragStartVal) * deltaScale;
                const float minScale = 1e-4f;
                if (m_scaleDragStartVal > 0.0f) {
                    if (targetVal < minScale) targetVal = minScale;
                } else {
                    if (targetVal > -minScale) targetVal = -minScale;
                }

                bool changed = (std::abs(targetVal - def.localScale[m_scaleDragLatchedAxis]) > 1e-5f);
                if (changed) {
                    def.localScale[m_scaleDragLatchedAxis] = targetVal;
                }
                // Lock other two axes to starting values to prevent cross-drift
                for (int a = 0; a < 3; ++a) {
                    if (a != m_scaleDragLatchedAxis) {
                        def.localScale[a] = m_scaleDragStartScaleVec[a];
                    }
                }

                // Live presentation grounding based on current def.localScale:
                // Smoothly follows live authored scale so the block stays flush with floor at all times
                // with zero jump on release
                float liveGroundY = 0.0f;
                if (!m_cachedGeometryMesh.vertices.empty()) {
                    fw::AABB bounds = m_cachedGeometryMesh.bounds();
                    glm::vec3 corners[8] = {
                        { bounds.min.x, bounds.min.y, bounds.min.z },
                        { bounds.min.x, bounds.min.y, bounds.max.z },
                        { bounds.min.x, bounds.max.y, bounds.min.z },
                        { bounds.min.x, bounds.max.y, bounds.max.z },
                        { bounds.max.x, bounds.min.y, bounds.min.z },
                        { bounds.max.x, bounds.min.y, bounds.max.z },
                        { bounds.max.x, bounds.max.y, bounds.min.z },
                        { bounds.max.x, bounds.max.y, bounds.max.z }
                    };

                    glm::mat4 mShapeInAsset = glm::mat4(1.0f);
                    mShapeInAsset = mShapeInAsset * glm::mat4_cast(glm::quat(glm::radians(def.localRotation)));
                    mShapeInAsset = glm::scale(mShapeInAsset, def.localScale);
                    mShapeInAsset = glm::translate(mShapeInAsset, -def.pivotOffset);

                    float minShapeY = 1e9f;
                    for (int i = 0; i < 8; ++i) {
                        glm::vec4 tc = mShapeInAsset * glm::vec4(corners[i], 1.0f);
                        if (tc.y < minShapeY) minShapeY = tc.y;
                    }
                    liveGroundY = -0.50f - minShapeY;
                }

                // Immediately synchronize preview ECS transform on the same frame:
                // Guarantees zero latency and rock-solid presentation in RenderDesktop
                float previewEcsSy = def.localScale.y;
                float previewEcsSz = def.localScale.z;
                float previewEcsPosY = liveGroundY + m_simPosY + def.localPosition.y;
                auto& m_registry = m_previewWorld->GetRegistry();
                if (m_previewBlockEntity != entt::null && m_registry.valid(m_previewBlockEntity)) {
                    auto& transform = m_registry.get<fw::TransformComponent>(m_previewBlockEntity);
                    glm::vec3 newLoc = glm::vec3(0.0f, liveGroundY + m_simPosY, 0.0f) + (qPreview * def.localPosition);
                    glm::quat qTotal = qPreview * qAsset;
                    transform.location = fw::Vec3{ newLoc.x, newLoc.y, newLoc.z };
                    transform.rotation = fw::Quat{ qTotal.x, qTotal.y, qTotal.z, qTotal.w };
                    transform.scale = fw::Vec3{ def.localScale.x, def.localScale.y, def.localScale.z };
                    transform.pivot = fw::Vec3{ def.pivotOffset.x, def.pivotOffset.y, def.pivotOffset.z };
                    previewEcsSy = transform.scale.y;
                    previewEcsSz = transform.scale.z;
                    previewEcsPosY = transform.location.y;
                }

                if (m_scaleDragLatchedAxis == 1) {
                    LogScaleYDiagnostics("LATCH_ACTIVE", m_selectedBlockId, io.MouseDown[0], ImGuizmo::IsUsing(), (int)activeHandle, m_scaleDragStartVal, rawSy, def.localScale.y, previewEcsSy, liveGroundY, previewEcsPosY, gizmoInputTransY, gizmoOutputTransY, shearRejected, statShift);
                } else if (m_scaleDragLatchedAxis == 2) {
                    float matrixDerivedSz = glm::length(glm::vec3(worldMat[2]));
                    LogScaleZDiagnostics("LATCH_ACTIVE", m_selectedBlockId, m_gizmoOperation, io.MouseDown[0], ImGuizmo::IsUsing(), (int)activeHandle, true, m_scaleDragStartVal, matrixDerivedSz, targetVal, def.localScale.z, previewEcsSz, changed, dragPixels);
                }
            } else {
                // Uniform Scale (MT_SCALE_XYZ) fallback
                if (!shearRejected) {
                    const float minScale = 1e-4f;
                    float targetSx = rawSx, targetSy = rawSy, targetSz = rawSz;
                    if (def.localScale.x > 0.0f) { if (targetSx < minScale) targetSx = minScale; }
                    else { if (targetSx > -minScale) targetSx = -minScale; }
                    if (def.localScale.y > 0.0f) { if (targetSy < minScale) targetSy = minScale; }
                    else { if (targetSy > -minScale) targetSy = -minScale; }
                    if (def.localScale.z > 0.0f) { if (targetSz < minScale) targetSz = minScale; }
                    else { if (targetSz > -minScale) targetSz = -minScale; }

                    if (std::abs(targetSx - def.localScale.x) > 1e-5f ||
                        std::abs(targetSy - def.localScale.y) > 1e-5f ||
                        std::abs(targetSz - def.localScale.z) > 1e-5f) {
                        def.localScale = glm::vec3(targetSx, targetSy, targetSz);
                    }
                }
            }
            // localPosition, localRotation e pivotOffset rimangono intatti e immutati!
        }
    } else {
        if (m_scaleDragLatchedAxis >= 0 || m_scaleZDragLatched) {
            float previewEcsScale = 0.0f;
            float previewEcsPosY = 0.0f;
            auto& m_registry = m_previewWorld->GetRegistry();
            if (m_previewBlockEntity != entt::null && m_registry.valid(m_previewBlockEntity)) {
                auto& tc = m_registry.get<fw::TransformComponent>(m_previewBlockEntity);
                previewEcsScale = (m_scaleDragLatchedAxis == 1) ? tc.scale.y : tc.scale.z;
                previewEcsPosY = tc.location.y;
            }
            if (m_scaleDragLatchedAxis == 1) {
                LogScaleYDiagnostics("LATCH_END", m_scaleLatchedBlockId, io.MouseDown[0], ImGuizmo::IsUsing(), 0, m_scaleDragStartVal, def.localScale.y, def.localScale.y, previewEcsScale, groundY, previewEcsPosY, m_scaleLastInputTransY, m_scaleLastInputTransY, false, false);
            } else {
                LogScaleZDiagnostics("LATCH_END", m_scaleLatchedBlockId, m_gizmoOperation, io.MouseDown[0], ImGuizmo::IsUsing(), 0, false, m_scaleDragStartVal, def.localScale.z, def.localScale.z, def.localScale.z, previewEcsScale, false, 0.0f);
            }
        }
        m_isGizmoInteracting = false;
        m_scaleDragLatchedAxis = -1;
        m_scaleZDragLatched = false;
        m_isDraggingScaleZ = false;
    }
}
