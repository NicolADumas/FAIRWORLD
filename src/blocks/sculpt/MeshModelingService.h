#pragma once

#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <optional>
#include <limits>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "EditableMesh.h"
#include "MeshSelectionService.h"

namespace fw {

// =============================================================================
// BM-S4: OPERAZIONI DI MODELLAZIONE & EDITING TOPOLOGICO
// =============================================================================

enum class ModelingOperationType : uint8_t {
    Move = 0,
    Rotate = 1,
    Scale = 2,
    ExtrudeFace = 3,
    ExtrudeRegion = 4,
    ExtrudeEdge = 5,
    InsetFace = 6,
    BevelEdge = 7
};

enum class TransformPivot : uint8_t {
    SelectionCentroid = 0,
    ActiveElement = 1,
    ExplicitPoint = 2,
    AssetOrigin = 3
};

// --- Parametri Trasformazione ---
struct TransformParams {
    glm::vec3 delta{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    glm::vec3 explicitPivot{0.0f};
    TransformPivot pivotMode = TransformPivot::SelectionCentroid;
    glm::bvec3 axisConstraint{true, true, true};
};

// --- Parametri Estrusione ---
struct ExtrudeParams {
    float offset = 0.2f;
    glm::vec3 customDirection{0.0f};
    bool useFaceNormal = true;
    bool createIndividualFaces = false; // Se false e facce adiacenti, esegue Region Extrude
    std::optional<uint32_t> overrideSurfaceId;
    std::optional<uint32_t> overrideMaterialId;
};

// --- Parametri Inset ---
struct InsetParams {
    float insetAmount = 0.1f;
    float depthOffset = 0.0f;
    bool clampToCentroid = true;
    std::optional<uint32_t> overrideSurfaceId;
    std::optional<uint32_t> overrideMaterialId;
};

// --- Parametri Bevel ---
struct BevelParams {
    float width = 0.05f;
    int segments = 1;
    float clamp = 0.5f;
    bool linearProfile = true;
};

// --- Struttura Risultato Unificata & Dati Undo per BM-S8 ---
struct ModelingOperationResult {
    bool success = false;
    ModelingOperationType opType = ModelingOperationType::Move;
    std::string errorMessage;
    std::vector<std::string> diagnostics;

    // Elementi coinvolti
    std::vector<VertexID> modifiedVertices;
    std::vector<VertexID> createdVertices;
    std::vector<VertexID> removedVertices;

    std::vector<EdgeID> modifiedEdges;
    std::vector<EdgeID> createdEdges;
    std::vector<EdgeID> removedEdges;

    std::vector<FaceID> modifiedFaces;
    std::vector<FaceID> createdFaces;
    std::vector<FaceID> removedFaces;

    // Tracciamento Revisioni
    bool topologyChanged = false;
    bool geometryChanged = false;
    bool attributesChanged = false;
    uint64_t oldTopologyRev = 0;
    uint64_t newTopologyRev = 0;
    uint64_t oldGeometryRev = 0;
    uint64_t newGeometryRev = 0;
    uint64_t oldAttributeRev = 0;
    uint64_t newAttributeRev = 0;

    // Informazioni delta per Undo/Redo geometrico
    struct VertexPositionDelta {
        VertexID id;
        glm::vec3 oldPos{0.0f};
        glm::vec3 newPos{0.0f};
    };
    std::vector<VertexPositionDelta> positionDeltas;

    // Snapshot serializzato per Undo/Redo topologico (BM-S8)
    std::string topologicalUndoSnapshot;
};

// =============================================================================
// MESH MODELING SERVICE (BM-S4)
// Servizio atomico e deterministico indipendente da UI, ImGui e Vulkan
// =============================================================================
class MeshModelingService {
public:
    MeshModelingService() = default;
    ~MeshModelingService() = default;

    // --- 1. Operazioni di Trasformazione Geometrica (BM-S4.2) ---
    static ModelingOperationResult Move(EditableMesh& mesh, 
                                        const std::unordered_set<VertexID>& vertices, 
                                        const glm::vec3& delta, 
                                        const glm::bvec3& axisConstraint = {true, true, true});

    static ModelingOperationResult Rotate(EditableMesh& mesh, 
                                          const std::unordered_set<VertexID>& vertices, 
                                          const glm::vec3& pivot, 
                                          const glm::quat& rotation, 
                                          const glm::bvec3& axisConstraint = {true, true, true});

    static ModelingOperationResult Scale(EditableMesh& mesh, 
                                         const std::unordered_set<VertexID>& vertices, 
                                         const glm::vec3& pivot, 
                                         const glm::vec3& scale, 
                                         const glm::bvec3& axisConstraint = {true, true, true});

    // Helper di trasformazione che risolvono automaticamente la selezione (Vertex, Edge, Face)
    static ModelingOperationResult MoveSelection(EditableMesh& mesh, 
                                                 const MeshSelectionService& selection, 
                                                 const glm::vec3& delta, 
                                                 const glm::bvec3& axisConstraint = {true, true, true});

    static ModelingOperationResult RotateSelection(EditableMesh& mesh, 
                                                   const MeshSelectionService& selection, 
                                                   const glm::quat& rotation, 
                                                   const glm::bvec3& axisConstraint = {true, true, true},
                                                   TransformPivot pivotMode = TransformPivot::SelectionCentroid,
                                                   const glm::vec3& customPivot = glm::vec3(0.0f));

    static ModelingOperationResult ScaleSelection(EditableMesh& mesh, 
                                                  const MeshSelectionService& selection, 
                                                  const glm::vec3& scale, 
                                                  const glm::bvec3& axisConstraint = {true, true, true},
                                                  TransformPivot pivotMode = TransformPivot::SelectionCentroid,
                                                  const glm::vec3& customPivot = glm::vec3(0.0f));

    // --- 2. Operazioni di Estrusione (BM-S4.3) ---
    static ModelingOperationResult ExtrudeFace(EditableMesh& mesh, FaceID faceId, const ExtrudeParams& params);
    static ModelingOperationResult ExtrudeFaces(EditableMesh& mesh, const std::vector<FaceID>& faceIds, const ExtrudeParams& params);
    static ModelingOperationResult ExtrudeRegion(EditableMesh& mesh, const std::vector<FaceID>& faceIds, const ExtrudeParams& params);
    static ModelingOperationResult ExtrudeBoundaryEdge(EditableMesh& mesh, EdgeID edgeId, const ExtrudeParams& params);
    static ModelingOperationResult ExtrudeBoundaryEdges(EditableMesh& mesh, const std::vector<EdgeID>& edgeIds, const ExtrudeParams& params);
    static ModelingOperationResult ExtrudeSelection(EditableMesh& mesh, const MeshSelectionService& selection, const ExtrudeParams& params);

    // --- 3. Operazioni di Face Inset (BM-S4.4) ---
    static ModelingOperationResult InsetFace(EditableMesh& mesh, FaceID faceId, const InsetParams& params);
    static ModelingOperationResult InsetFaces(EditableMesh& mesh, const std::vector<FaceID>& faceIds, const InsetParams& params);
    static ModelingOperationResult InsetSelection(EditableMesh& mesh, const MeshSelectionService& selection, const InsetParams& params);

    // --- 4. Operazioni di Edge Bevel (BM-S4.5) ---
    static ModelingOperationResult BevelEdge(EditableMesh& mesh, EdgeID edgeId, const BevelParams& params);
    static ModelingOperationResult BevelEdges(EditableMesh& mesh, const std::vector<EdgeID>& edgeIds, const BevelParams& params);
    static ModelingOperationResult BevelSelection(EditableMesh& mesh, const MeshSelectionService& selection, const BevelParams& params);

    // --- 5. Query Geometriche e Helper Deterministici ---
    static std::unordered_set<VertexID> ResolveSelectionVertices(const EditableMesh& mesh, const MeshSelectionService& selection);
    static glm::vec3 CalculateSelectionCentroid(const EditableMesh& mesh, const MeshSelectionService& selection);
    static glm::vec3 CalculateVerticesCentroid(const EditableMesh& mesh, const std::unordered_set<VertexID>& vertices);
    static glm::vec3 CalculateFaceCentroid(const EditableMesh& mesh, FaceID faceId);
    static glm::vec3 CalculateFaceNormal(const EditableMesh& mesh, FaceID faceId);
    static glm::vec3 CalculateRegionAverageNormal(const EditableMesh& mesh, const std::vector<FaceID>& faces);
    static bool IsFaceConvex(const EditableMesh& mesh, FaceID faceId);

    // --- 6. Suite di Validazione Deterministica BM-S4 (MOD-1 .. MOD-10) ---
    struct ModelingTestResult {
        std::string testId;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        bool passed = false;
        std::string details;
    };
    static std::vector<ModelingTestResult> RunModelingContractTests();
};

} // namespace fw
