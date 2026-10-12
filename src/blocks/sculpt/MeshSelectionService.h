#pragma once

#include <vector>
#include <unordered_set>
#include <string>
#include <limits>
#include <glm/glm.hpp>
#include "EditableMesh.h"

namespace fw {

// =============================================================================
// BM-S3: MODALITA ED OPERAZIONI DI SELEZIONE
// =============================================================================

enum class SelectionMode : uint8_t {
    Vertex = 0,
    Edge = 1,
    Face = 2
};

enum class SelectionOp : uint8_t {
    Replace = 0, // Sostituisce l'intera selezione corrente
    Add = 1,     // Aggiunge all'insieme esistente (Shift)
    Subtract = 2,// Rimuove dall'insieme (Ctrl o Alt)
    Toggle = 3   // Inverte lo stato dell'elemento (Shift+Ctrl)
};

// =============================================================================
// STRUTTURE DATI DI PICKING E CONTESTO VIEWPORT
// =============================================================================

struct PickRay {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

struct PickHit {
    bool hit = false;
    SelectionMode mode = SelectionMode::Face;
    VertexID vertexId = INVALID_VERTEX_ID;
    EdgeID edgeId = INVALID_EDGE_ID;
    FaceID faceId = INVALID_FACE_ID;
    float distance = std::numeric_limits<float>::infinity();
    glm::vec3 worldHitPoint{0.0f};
    glm::vec3 localHitPoint{0.0f};
};

struct CameraViewportContext {
    glm::mat4 viewMatrix{1.0f};
    glm::mat4 projMatrix{1.0f}; // Matrice prospettica/ortografica (senza Y-flip Vulkan)
    glm::vec2 viewportPos{0.0f, 0.0f};
    glm::vec2 viewportSize{1280.0f, 720.0f};
    glm::mat4 modelMatrix{1.0f}; // Matrice da object-space a world-space dell'asset
};

struct ScreenBox {
    glm::vec2 minPos{0.0f};
    glm::vec2 maxPos{0.0f};
};

// =============================================================================
// MESH SELECTION SERVICE (BM-S3)
// Servizio disaccoppiato da UI, ImGui e Vulkan per la selezione interattiva
// =============================================================================
class MeshSelectionService {
public:
    MeshSelectionService();
    ~MeshSelectionService() = default;

    // --- Modalità di Selezione ---
    void SetMode(SelectionMode mode);
    SelectionMode GetMode() const { return m_mode; }

    // --- Gestione Selezione Vertici ---
    void SelectVertex(VertexID id, SelectionOp op = SelectionOp::Replace);
    void DeselectVertex(VertexID id);
    bool IsVertexSelected(VertexID id) const;
    const std::unordered_set<VertexID>& GetSelectedVertices() const { return m_selectedVertices; }
    VertexID GetActiveVertex() const { return m_activeVertex; }
    void SetActiveVertex(VertexID id) { m_activeVertex = id; }

    // --- Gestione Selezione Edge ---
    void SelectEdge(EdgeID id, SelectionOp op = SelectionOp::Replace);
    void DeselectEdge(EdgeID id);
    bool IsEdgeSelected(EdgeID id) const;
    const std::unordered_set<EdgeID>& GetSelectedEdges() const { return m_selectedEdges; }
    EdgeID GetActiveEdge() const { return m_activeEdge; }
    void SetActiveEdge(EdgeID id) { m_activeEdge = id; }

    // --- Gestione Selezione Facce ---
    void SelectFace(FaceID id, SelectionOp op = SelectionOp::Replace);
    void DeselectFace(FaceID id);
    bool IsFaceSelected(FaceID id) const;
    const std::unordered_set<FaceID>& GetSelectedFaces() const { return m_selectedFaces; }
    FaceID GetActiveFace() const { return m_activeFace; }
    void SetActiveFace(FaceID id) { m_activeFace = id; }

    // --- Operazioni Comuni ---
    void Clear();
    void ClearCurrentMode();
    void SelectAll(const EditableMesh& mesh);
    void InvertSelection(const EditableMesh& mesh);

    size_t GetSelectedCount() const;
    bool HasSelection() const;

    // Revision counter per Zero-Work e caching del rendering
    uint64_t GetSelectionRevision() const { return m_selectionRevision; }

    // Pruning degli ID invalidati a seguito di modifiche topologiche
    void PruneInvalidated(const EditableMesh& mesh);

    // --- CPU Ray Picking (BM-S3.3) ---
    // Calcola il ray in world-space da coordinate schermo (pixel del mouse)
    static PickRay ComputeWorldRayFromScreen(const glm::vec2& mouseScreenPos, const CameraViewportContext& ctx);

    // Esegue il raycast contro la mesh editabile in accordo con la SelectionMode corrente
    PickHit Pick(const PickRay& worldRay, 
                 const EditableMesh& mesh, 
                 const CameraViewportContext& ctx, 
                 float vertexPixelRadius = 12.0f, 
                 float edgePixelRadius = 8.0f) const;

    // Esegue il picking mirato con applicazione immediata dell'operazione di selezione
    bool PickAndSelect(const glm::vec2& mouseScreenPos, 
                       const EditableMesh& mesh, 
                       const CameraViewportContext& ctx, 
                       SelectionOp op = SelectionOp::Replace,
                       float vertexPixelRadius = 12.0f, 
                       float edgePixelRadius = 8.0f);

    // --- Box Selection nello Spazio Schermo (BM-S3.4) ---
    size_t BoxSelect(const ScreenBox& box, 
                     const EditableMesh& mesh, 
                     const CameraViewportContext& ctx, 
                     SelectionOp op = SelectionOp::Replace,
                     bool occludedCheck = false);

    // --- Suite di Test Deterministica BM-S3 (SEL-1 .. SEL-10) ---
    struct SelectionTestResult {
        std::string testId;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        bool passed = false;
        std::string details;
    };
    static std::vector<SelectionTestResult> RunSelectionContractTests();

private:
    SelectionMode m_mode = SelectionMode::Face;

    std::unordered_set<VertexID> m_selectedVertices;
    std::unordered_set<EdgeID>   m_selectedEdges;
    std::unordered_set<FaceID>   m_selectedFaces;

    VertexID m_activeVertex = INVALID_VERTEX_ID;
    EdgeID   m_activeEdge   = INVALID_EDGE_ID;
    FaceID   m_activeFace   = INVALID_FACE_ID;

    uint64_t m_selectionRevision = 1;

    // Caching picking dati derivati
    mutable uint64_t m_cachedMeshTopoRev = 0;
    mutable uint64_t m_cachedMeshGeomRev = 0;

    void IncrementRevision() { m_selectionRevision++; }

    // Picking specializzati
    PickHit PickFace(const PickRay& localRay, const PickRay& worldRay, const EditableMesh& mesh, const CameraViewportContext& ctx) const;
    PickHit PickVertex(const PickRay& worldRay, const EditableMesh& mesh, const CameraViewportContext& ctx, float pixelRadius) const;
    PickHit PickEdge(const PickRay& worldRay, const EditableMesh& mesh, const CameraViewportContext& ctx, float pixelRadius) const;

    static bool RayTriangleIntersect(const glm::vec3& orig, const glm::vec3& dir,
                                     const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                                     float& t, float& u, float& v);

    static float DistancePointToSegment2D(const glm::vec2& pt, const glm::vec2& s0, const glm::vec2& s1);
};

} // namespace fw
