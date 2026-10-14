#pragma once

#include <vector>
#include <string>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cmath>
#include <glm/glm.hpp>
#include "json.hpp"
#include "ForgeComponents.h"

namespace fw {

// =============================================================================
// BM-S2: GENERATIONAL STABLE IDENTIFIERS
// Previene riferimenti dangling a elementi eliminati o riutilizzati
// =============================================================================

template<typename Tag>
struct ElementID {
    uint32_t index = 0xFFFFFFFF;
    uint32_t generation = 0;

    constexpr bool IsValid() const noexcept {
        return index != 0xFFFFFFFF && generation != 0;
    }

    constexpr bool operator==(const ElementID& other) const noexcept {
        return index == other.index && generation == other.generation;
    }

    constexpr bool operator!=(const ElementID& other) const noexcept {
        return !(*this == other);
    }

    constexpr bool operator<(const ElementID& other) const noexcept {
        if (index != other.index) return index < other.index;
        return generation < other.generation;
    }

    uint64_t ToPacked() const noexcept {
        return (static_cast<uint64_t>(generation) << 32) | static_cast<uint64_t>(index);
    }

    static ElementID FromPacked(uint64_t packed) noexcept {
        ElementID id;
        id.index = static_cast<uint32_t>(packed & 0xFFFFFFFFULL);
        id.generation = static_cast<uint32_t>((packed >> 32) & 0xFFFFFFFFULL);
        return id;
    }
};

template<typename Tag>
constexpr bool IsValid(ElementID<Tag> id) noexcept {
    return id.IsValid();
}

struct VertexTag {};
struct HalfEdgeTag {};
struct EdgeTag {};
struct FaceTag {};

using VertexID = ElementID<VertexTag>;
using HalfEdgeID = ElementID<HalfEdgeTag>;
using EdgeID = ElementID<EdgeTag>;
using FaceID = ElementID<FaceTag>;

inline constexpr VertexID   INVALID_VERTEX_ID{0xFFFFFFFF, 0};
inline constexpr HalfEdgeID INVALID_HALFEDGE_ID{0xFFFFFFFF, 0};
inline constexpr EdgeID     INVALID_EDGE_ID{0xFFFFFFFF, 0};
inline constexpr FaceID     INVALID_FACE_ID{0xFFFFFFFF, 0};

} // namespace fw

// Specializzazione std::hash per consentire ElementID come chiavi in unordered map/set
namespace std {
    template<typename Tag>
    struct hash<fw::ElementID<Tag>> {
        size_t operator()(const fw::ElementID<Tag>& id) const noexcept {
            return std::hash<uint64_t>{}(id.ToPacked());
        }
    };
} // namespace std

namespace fw {

// =============================================================================
// ATTRIBUTI PER CORNER / LOOP
// Memorizzati per-corner (half-edge incidente), consentendo UV seams e discontinuità
// =============================================================================
struct CornerAttributes {
    glm::vec2 uv{0.0f, 0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float customWeight = 1.0f;

    bool operator==(const CornerAttributes& o) const {
        return uv == o.uv && normal == o.normal && color == o.color && customWeight == o.customWeight;
    }
};

// =============================================================================
// ELEMENTI TOPOLOGICI HALF-EDGE (DCEL)
// =============================================================================

struct MeshVertex {
    VertexID id = INVALID_VERTEX_ID;
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    HalfEdgeID halfEdge = INVALID_HALFEDGE_ID; // Uno degli half-edge uscenti
    glm::vec3 normal{0.0f, 1.0f, 0.0f};       // Normale d'area ponderata derivata
    bool isDeleted = false;
};

struct MeshHalfEdge {
    HalfEdgeID id = INVALID_HALFEDGE_ID;
    VertexID origin = INVALID_VERTEX_ID;      // Vertice sorgente da cui parte l'half-edge
    HalfEdgeID twin = INVALID_HALFEDGE_ID;    // Half-edge gemello opposto (o INVALID se confine aperto)
    HalfEdgeID next = INVALID_HALFEDGE_ID;    // Successivo nel perimetro della faccia
    HalfEdgeID prev = INVALID_HALFEDGE_ID;    // Precedente nel perimetro della faccia
    FaceID face = INVALID_FACE_ID;            // Faccia incidente (o INVALID se boundary loop)
    EdgeID edge = INVALID_EDGE_ID;            // Spigolo logico non orientato associato
    CornerAttributes corner;                  // Attributi d'angolo del vertice origin per questa faccia
    bool isDeleted = false;
};

struct MeshEdge {
    EdgeID id = INVALID_EDGE_ID;
    HalfEdgeID halfEdge = INVALID_HALFEDGE_ID; // Primo half-edge
    HalfEdgeID twin = INVALID_HALFEDGE_ID;     // Secondo half-edge (o INVALID se boundary)
    bool isSharp = false;                      // Hard edge flag per normal splitting
    float seamWeight = 0.0f;                   // Marcatore cucitura UV
    bool isDeleted = false;
};

struct MeshFace {
    FaceID id = INVALID_FACE_ID;
    HalfEdgeID halfEdge = INVALID_HALFEDGE_ID; // Uno degli half-edge nel ciclo perimetrale
    uint32_t surfaceId = 0;                    // SurfaceID semantico (BM-C)
    uint32_t materialId = 0;                   // MaterialID associato
    glm::vec3 faceNormal{0.0f, 1.0f, 0.0f};    // Normale piana della faccia
    float area = 0.0f;                         // Area superficiale calcolata
    bool isDeleted = false;
};

// =============================================================================
// REPORT DIAGNOSTICO VALIDAZIONE TOPOLOGICA
// =============================================================================
struct MeshValidationReport {
    bool isValid = true;
    size_t vertexCount = 0;
    size_t edgeCount = 0;
    size_t faceCount = 0;
    size_t halfEdgeCount = 0;
    size_t boundaryEdgeCount = 0;
    size_t triangleCount = 0;
    size_t quadCount = 0;
    size_t ngonCount = 0;
    bool isClosedManifold = true;

    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    void AddError(const std::string& err) {
        errors.push_back(err);
        isValid = false;
    }
    void AddWarning(const std::string& warn) {
        warnings.push_back(warn);
    }
    void Reset() {
        isValid = true;
        vertexCount = edgeCount = faceCount = halfEdgeCount = boundaryEdgeCount = 0;
        triangleCount = quadCount = ngonCount = 0;
        isClosedManifold = true;
        errors.clear();
        warnings.clear();
    }
};

// =============================================================================
// EDITABLE MESH (BM-S2)
// Struttura topologica CPU deterministica indipendente da Vulkan/GPU
// =============================================================================
class EditableMesh {
public:
    EditableMesh();
    ~EditableMesh() = default;

    // Movimento e copia espliciti
    EditableMesh(const EditableMesh& other);
    EditableMesh& operator=(const EditableMesh& other);
    EditableMesh(EditableMesh&& other) noexcept = default;
    EditableMesh& operator=(EditableMesh&& other) noexcept = default;

    void Clear();

    // --- Revision Tracking ---
    uint64_t GetTopologyRevision() const { return m_topologyRevision; }
    uint64_t GetGeometryRevision() const { return m_geometryRevision; }
    uint64_t GetAttributeRevision() const { return m_attributeRevision; }

    void IncrementTopologyRevision() {
        m_topologyRevision++;
        m_geometryRevision++;
        m_attributeRevision++;
    }
    void IncrementGeometryRevision() { m_geometryRevision++; }
    void IncrementAttributeRevision() { m_attributeRevision++; }
    void SetGeometryRevision(uint64_t rev) { m_geometryRevision = rev; }
    void SetTopologyRevision(uint64_t rev) { m_topologyRevision = rev; }

    // --- Conteggi Attivi ---
    size_t GetVertexCount() const { return m_activeVertexCount; }
    size_t GetHalfEdgeCount() const { return m_activeHalfEdgeCount; }
    size_t GetEdgeCount() const { return m_activeEdgeCount; }
    size_t GetFaceCount() const { return m_activeFaceCount; }
    bool IsEmpty() const { return m_activeVertexCount == 0; }

    // --- Accesso Elementi ---
    bool IsValid(VertexID id) const;
    bool IsValid(HalfEdgeID id) const;
    bool IsValid(EdgeID id) const;
    bool IsValid(FaceID id) const;

    const MeshVertex*   GetVertex(VertexID id) const;
    MeshVertex*         GetVertexMutable(VertexID id);
    const MeshHalfEdge* GetHalfEdge(HalfEdgeID id) const;
    MeshHalfEdge*       GetHalfEdgeMutable(HalfEdgeID id);
    const MeshEdge*     GetEdge(EdgeID id) const;
    MeshEdge*           GetEdgeMutable(EdgeID id);
    const MeshFace*     GetFace(FaceID id) const;
    MeshFace*           GetFaceMutable(FaceID id);

    // Iteratori sugli ID attivi (non eliminati)
    std::vector<VertexID>   GetActiveVertexIDs() const;
    std::vector<HalfEdgeID> GetActiveHalfEdgeIDs() const;
    std::vector<EdgeID>     GetActiveEdgeIDs() const;
    std::vector<FaceID>     GetActiveFaceIDs() const;

    // --- Manipolazione Geometria ---
    VertexID AddVertex(const glm::vec3& position);
    bool SetVertexPosition(VertexID id, const glm::vec3& newPos);
    bool DeleteVertex(VertexID id);

    // --- Costruzione Facce ---
    // Aggiunge una faccia poligonale data una lista ordinata (CCW) di vertici
    FaceID AddFace(const std::vector<VertexID>& vertexIds, 
                   uint32_t surfaceId = 0, 
                   uint32_t materialId = 0, 
                   const std::vector<CornerAttributes>* corners = nullptr);

    bool DeleteFace(FaceID id, bool deleteUnusedEdges = true, bool deleteUnusedVertices = false);
    bool DeleteEdge(EdgeID id);

    // --- Query di Adiacenza e Navigazione Locale O(1) ---
    std::vector<HalfEdgeID> GetVertexOutgoingHalfEdges(VertexID v) const;
    std::vector<VertexID>   GetVertexOneRingNeighbors(VertexID v) const;
    std::vector<FaceID>     GetVertexIncidentFaces(VertexID v) const;

    std::vector<HalfEdgeID>      GetFaceHalfEdges(FaceID f) const;
    std::vector<VertexID>        GetFaceVertices(FaceID f) const;
    std::vector<CornerAttributes> GetFaceCorners(FaceID f) const;
    size_t                       GetFaceVertexCount(FaceID f) const;

    bool IsBoundaryVertex(VertexID v) const;
    bool IsBoundaryEdge(EdgeID e) const;
    bool IsBoundaryHalfEdge(HalfEdgeID h) const;

    // --- Calcolo Attributi Derivati ---
    void ComputeFaceNormalAndArea(FaceID f);
    void ComputeAllFaceNormals();
    void ComputeVertexNormals();
    AABB ComputeAABB() const;

    // --- Validazione Topologica ---
    MeshValidationReport ValidateTopology() const;

    // --- Primitivi di Partenza ---
    static EditableMesh CreateCube(float size = 1.0f);
    static EditableMesh CreateQuad(float width = 1.0f, float height = 1.0f);
    static EditableMesh CreatePlane(int resolutionX = 2, int resolutionZ = 2, float size = 1.0f);
    static EditableMesh CreateFromIndexedMesh(const std::vector<glm::vec3>& positions,
                                             const std::vector<std::vector<uint32_t>>& faceIndices,
                                             const std::vector<std::vector<glm::vec2>>* faceUVs = nullptr,
                                             uint32_t defaultSurfaceId = 0,
                                             uint32_t defaultMaterialId = 0);

    // --- Triangolazione & Bridge Verso GeometryCompiler / Vulkan ---
    // Triangola una singola faccia poligonale preservando l'ordine degli indici (Ear Clipping per N-gon non convessi)
    bool TriangulateFace(FaceID f, std::vector<uint32_t>& outTriangles) const;

    // Compila l'intera mesh CPU in MeshComponent conforme all'ABI a 64 byte di Vulkan
    MeshComponent CompileToMeshComponent(const std::string& name = "EditableMeshCompiled") const;

    // --- Serializzazione Versionata (JSON) ---
    nlohmann::json ToJson() const;
    bool FromJson(const nlohmann::json& j);

    // --- Suite di Test Deterministica BM-S2 ---
    struct ContractTestResult {
        std::string testId;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        bool passed = false;
        std::string details;
    };
    static std::vector<ContractTestResult> RunContractTests();

private:
    // SlotMaps con generazione per ciascun elemento
    std::vector<MeshVertex>   m_vertices;
    std::vector<uint32_t>     m_vertexGenerations;
    std::vector<uint32_t>     m_freeVertexIndices;
    size_t                    m_activeVertexCount = 0;

    std::vector<MeshHalfEdge> m_halfEdges;
    std::vector<uint32_t>     m_halfEdgeGenerations;
    std::vector<uint32_t>     m_freeHalfEdgeIndices;
    size_t                    m_activeHalfEdgeCount = 0;

    std::vector<MeshEdge>     m_edges;
    std::vector<uint32_t>     m_edgeGenerations;
    std::vector<uint32_t>     m_freeEdgeIndices;
    size_t                    m_activeEdgeCount = 0;

    std::vector<MeshFace>     m_faces;
    std::vector<uint32_t>     m_faceGenerations;
    std::vector<uint32_t>     m_freeFaceIndices;
    size_t                    m_activeFaceCount = 0;

    // Mappa di lookup spigoli orientati (vStart, vEnd) -> HalfEdgeID per stitch rapido in costruzione
    struct DirectedEdgeKey {
        uint32_t v0;
        uint32_t v1;
        bool operator==(const DirectedEdgeKey& o) const { return v0 == o.v0 && v1 == o.v1; }
    };
    struct DirectedEdgeHash {
        size_t operator()(const DirectedEdgeKey& k) const noexcept {
            return (static_cast<size_t>(k.v0) * 73856093ULL) ^ (static_cast<size_t>(k.v1) * 19349663ULL);
        }
    };
    std::unordered_map<DirectedEdgeKey, HalfEdgeID, DirectedEdgeHash> m_directedEdgeLookup;

    // Revision counters
    uint64_t m_topologyRevision = 1;
    uint64_t m_geometryRevision = 1;
    uint64_t m_attributeRevision = 1;

    // Allocatori interni SlotMap
    HalfEdgeID AllocateHalfEdge();
    EdgeID     AllocateEdge();
    FaceID     AllocateFace();
};

} // namespace fw
