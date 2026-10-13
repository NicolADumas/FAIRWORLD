#pragma once

#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <optional>
#include <glm/glm.hpp>
#include "EditableMesh.h"
#include "MeshSelectionService.h"

namespace fw {

// =============================================================================
// BM-S7: REMESHING & MANIFOLD REPAIR CONTRACT
// =============================================================================

struct DegeneracyReport {
    std::vector<FaceID> zeroAreaFaces;
    std::vector<FaceID> duplicateVertexFaces;
    std::vector<EdgeID> zeroLengthEdges;
    std::vector<VertexID> isolatedVertices;
    std::vector<EdgeID> isolatedEdges;
    std::vector<VertexID> pinchedNonManifoldVertices;
    std::vector<std::vector<HalfEdgeID>> boundaryHoleLoops;

    bool HasIssues() const {
        return !zeroAreaFaces.empty() ||
               !duplicateVertexFaces.empty() ||
               !zeroLengthEdges.empty() ||
               !isolatedVertices.empty() ||
               !isolatedEdges.empty() ||
               !pinchedNonManifoldVertices.empty() ||
               !boundaryHoleLoops.empty();
    }
};

struct RepairOptions {
    float areaEpsilon = 1e-6f;
    float edgeLengthEpsilon = 1e-5f;
    bool removeDegenerateFaces = true;
    bool removeIsolatedElements = true;
    bool splitPinchedVertices = true;
    bool unifyFaceWindings = true;
    bool capHoles = false;
    uint32_t maxHoleEdges = 64;
    uint32_t defaultSurfaceId = 0;
    uint32_t defaultMaterialId = 0;
};

struct DissolveOptions {
    float maxCoplanarAngleDegrees = 1.0f; // Soglia angolare massima (gradi) per considerare facce complanari
    bool preserveSurfaceId = true;
    bool preserveMaterialId = true;
    bool dissolveCollinearVertices = true;
    float collinearAngleDegrees = 1.0f;
};

struct RemeshOptions {
    float targetEdgeLength = 0.25f;
    uint32_t iterations = 2;
    float relaxFactor = 0.25f;
    bool preserveBoundaries = true;
};

struct RepairResult {
    bool success = false;
    std::string message;
    bool modified = false;

    size_t degenerateFacesRemoved = 0;
    size_t isolatedVerticesRemoved = 0;
    size_t isolatedEdgesRemoved = 0;
    size_t pinchedVerticesSplit = 0;
    size_t flippedFacesCount = 0;
    size_t holesCapped = 0;
    size_t coplanarFacesDissolved = 0;

    uint64_t oldTopologyRev = 0;
    uint64_t newTopologyRev = 0;
    uint64_t oldGeometryRev = 0;
    uint64_t newGeometryRev = 0;

    MeshValidationReport reportBefore;
    MeshValidationReport reportAfter;
};

struct RemeshResult {
    bool success = false;
    std::string message;
    bool modified = false;

    size_t edgesSplit = 0;
    size_t edgesCollapsed = 0;
    size_t verticesRelaxed = 0;

    float minEdgeLength = 0.0f;
    float maxEdgeLength = 0.0f;
    float avgEdgeLength = 0.0f;

    uint64_t oldTopologyRev = 0;
    uint64_t newTopologyRev = 0;

    MeshValidationReport finalReport;
};

// =============================================================================
// MESH REPAIR SERVICE (BM-S7)
// =============================================================================
class MeshRepairService {
public:
    MeshRepairService() = default;
    ~MeshRepairService() = default;

    // --- 1. Rilevamento Degenerazioni & Diagnostica ---
    static DegeneracyReport DetectDegeneracies(const EditableMesh& mesh, 
                                              float areaEpsilon = 1e-6f, 
                                              float edgeLengthEpsilon = 1e-5f);

    // --- 2. Pulizia Facce Degeneri ed Elementi Isolati ---
    static RepairResult RemoveDegenerateFaces(EditableMesh& mesh, float areaEpsilon = 1e-6f);
    static RepairResult CleanupIsolatedElements(EditableMesh& mesh);

    // --- 3. Riparazione Non-Manifold (Pinched Vertices) ---
    static RepairResult SplitPinchedVertices(EditableMesh& mesh);

    // --- 4. Unificazione Orientamento Normali / Avvolgimento Facce ---
    static RepairResult UnifyNormalOrientations(EditableMesh& mesh);

    // --- 5. Chiusura Fori di Bordo (Hole Capping) ---
    static RepairResult CapHoles(EditableMesh& mesh, 
                                 uint32_t maxHoleEdges = 64, 
                                 uint32_t surfaceId = 0, 
                                 uint32_t materialId = 0);

    // --- 6. Decimazione Complanare / Limited Dissolve ---
    static RepairResult LimitedDissolve(EditableMesh& mesh, const DissolveOptions& options = DissolveOptions{});

    // --- 7. Pipeline Completa di Riparazione Manifold ---
    static RepairResult RepairAll(EditableMesh& mesh, const RepairOptions& options = RepairOptions{});

    // --- 8. Remeshing Deterministico Uniforme ---
    static RemeshResult UniformRemesh(EditableMesh& mesh, const RemeshOptions& options);

    // --- Suite di Validazione Deterministica BM-S7 (REP-1 .. REP-10) ---
    struct RepairTestResult {
        std::string testId;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        bool passed = false;
        std::string details;
    };
    static std::vector<RepairTestResult> RunRepairContractTests();
};

} // namespace fw
