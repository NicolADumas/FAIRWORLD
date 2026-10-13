#pragma once

#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <optional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "EditableMesh.h"
#include "MeshSelectionService.h"
#include "MeshSculptService.h"

namespace fw {

// =============================================================================
// BM-S6: TOPOLOGY OPERATIONS & SYMMETRY CONTRACT
// =============================================================================

enum class MirrorPlane : uint8_t {
    X = 0, // Piano YZ (x = 0)
    Y = 1, // Piano XZ (y = 0)
    Z = 2  // Piano XY (z = 0)
};

struct MirrorParams {
    MirrorPlane plane = MirrorPlane::X;
    float weldThreshold = 0.005f;      // Distanza massima per fondere vertici di cucitura sulla linea di specchio
    bool removeNegativeSide = true;   // Elimina la geometria sul lato negativo prima di specchiare
    bool weldSeam = true;             // Fonda i vertici corrispondenti lungo il piano di simmetria
};

struct ProportionalEditParams {
    float radius = 1.0f;
    BrushFalloffType falloff = BrushFalloffType::Smooth;
    glm::vec3 translationDelta{0.0f};
    glm::quat rotationDelta{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scaleDelta{1.0f};
};

struct SubdivideParams {
    uint32_t iterations = 1;
    bool smooth = false; // Se true, applica smoothing laplaciano (Catmull-Clark); se false, midpoint subdivision
};

struct LoopCutParams {
    EdgeID startEdge = INVALID_EDGE_ID;
    float t = 0.5f; // Posizione normalizzata del taglio lungo lo spigolo (0 < t < 1)
};

struct TopologyOperationResult {
    bool success = false;
    std::string errorMessage;
    bool topologyChanged = false;
    bool geometryChanged = false;
    uint64_t oldTopologyRev = 0;
    uint64_t newTopologyRev = 0;
    uint64_t oldGeometryRev = 0;
    uint64_t newGeometryRev = 0;

    std::vector<VertexID> createdVertices;
    std::vector<EdgeID> createdEdges;
    std::vector<FaceID> createdFaces;
};

// =============================================================================
// MESH TOPOLOGY SERVICE (BM-S6)
// =============================================================================
class MeshTopologyService {
public:
    MeshTopologyService() = default;
    ~MeshTopologyService() = default;

    // --- 1. Mirroring & Simmetria Globale (BM-S6) ---
    static TopologyOperationResult MirrorAcrossPlane(EditableMesh& mesh, const MirrorParams& params);

    // --- 2. Proportional Editing (Soft Selection Transform) ---
    static TopologyOperationResult ApplyProportionalEdit(EditableMesh& mesh, 
                                                         const MeshSelectionService& selection, 
                                                         const ProportionalEditParams& params);

    // --- 3. Suddivisione Poligonale (Midpoint / Catmull-Clark) ---
    static TopologyOperationResult Subdivide(EditableMesh& mesh, const SubdivideParams& params);

    // --- 4. Edge Loop Cut & Ring Traversal ---
    static TopologyOperationResult LoopCut(EditableMesh& mesh, const LoopCutParams& params);

    // Trova l'anello di edge (Edge Ring) che attraversa quads consecutivi a partire da un EdgeID
    static std::vector<EdgeID> FindEdgeRing(const EditableMesh& mesh, EdgeID startEdge);

    // Trova il loop continuo di edge lungo una catena di spigoli consecutivi
    static std::vector<EdgeID> FindEdgeLoop(const EditableMesh& mesh, EdgeID startEdge);

    // --- Suite di Validazione Deterministica BM-S6 (TOP-1 .. TOP-10) ---
    struct TopologyTestResult {
        std::string testId;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        bool passed = false;
        std::string details;
    };
    static std::vector<TopologyTestResult> RunTopologyContractTests();
};

} // namespace fw
