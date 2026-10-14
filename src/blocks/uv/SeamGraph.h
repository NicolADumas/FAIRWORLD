#pragma once

#include <cstdint>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <glm/glm.hpp>
#include "blocks/sculpt/EditableMesh.h"
#include "blocks/uv/UVLayout.h"

namespace fw {

// =============================================================================
// OPZIONI DI GENERAZIONE SEAM TOPOLOGICO (BM-UV3.2)
// =============================================================================

struct SeamOptions {
    float dihedralAngleThresholdDegrees = 45.0f; // Soglia spigoli vivi (gradi)
    bool seamOnSharpEdges = true;                // Considera MeshEdge::isSharp come seam
    bool seamOnSurfaceBoundary = true;           // Taglia lungo confini di SurfaceID differenti
    bool seamOnMaterialBoundary = true;          // Taglia lungo confini di MaterialID differenti
    bool autoCutClosedComponents = true;         // Introduce tagli minimi su componenti chiuse (genus reduction)
    bool preserveManualSeams = true;             // Preserva MeshEdge::seamWeight >= 0.99f
    float defaultAutoSeamWeight = 0.5f;          // Peso assegnato a MeshEdge::seamWeight per seam automatiche
};

// =============================================================================
// SEAM EDGE METADATA (BM-UV3.2)
// Informazioni estese per spigolo senza duplicare la connettivita DCEL
// =============================================================================

struct SeamEdgeInfo {
    EdgeID edgeId = INVALID_EDGE_ID;
    SeamType type = SeamType::None;
    float dihedralAngleDegrees = 0.0f;
    bool isBoundary = false;
    bool isSharp = false;
    bool isManual = false;
};

// =============================================================================
// SEAM GRAPH TOPOLOGICO (BM-UV3.2)
// Grafo delle cuciture derivato dalla topologia autoritativa EditableMesh
// =============================================================================

class SeamGraph {
public:
    SeamGraph() = default;
    ~SeamGraph() = default;

    // --- Costruzione & Analisi ---
    // Costruisce il grafo delle cuciture a partire da EditableMesh e opzioni
    bool BuildFromMesh(const EditableMesh& mesh, const SeamOptions& options = {});

    // --- Query spigoli ---
    bool IsSeamOrBoundary(EdgeID edgeId) const;
    bool IsSeam(EdgeID edgeId) const;
    bool IsBoundary(EdgeID edgeId) const;
    SeamType GetSeamType(EdgeID edgeId) const;
    const SeamEdgeInfo* GetEdgeInfo(EdgeID edgeId) const;

    // --- Accessori & Collezioni ---
    const std::vector<EdgeID>& GetSeamEdges() const { return m_seamEdges; }
    const std::vector<EdgeID>& GetBoundaryEdges() const { return m_boundaryEdges; }
    const std::vector<EdgeID>& GetManualSeamEdges() const { return m_manualSeamEdges; }
    const std::vector<EdgeID>& GetAutoSeamEdges() const { return m_autoSeamEdges; }
    const std::vector<std::string>& GetDiagnostics() const { return m_diagnostics; }

    // --- Applicazione & Mutazione controllata su EditableMesh ---
    // Applica le cuciture a EditableMesh aggiornando MeshEdge::seamWeight
    void ApplySeamsToMesh(EditableMesh& mesh) const;

    // Imposta / rimuove una seam manuale su un singolo spigolo
    static void SetManualSeam(EditableMesh& mesh, EdgeID edgeId, bool isSeam);

    // Rimuove solo le seam automatiche (seamWeight < 0.99f), preservando quelle manuali
    static void ClearAutomaticSeams(EditableMesh& mesh);

private:
    void Reset();
    void DetectBoundaries(const EditableMesh& mesh);
    void DetectManualSeams(const EditableMesh& mesh);
    void DetectFeatureSeams(const EditableMesh& mesh, const SeamOptions& options);
    void CutClosedManifoldComponents(const EditableMesh& mesh, const SeamOptions& options);

    // Ricerca cammino minimo di edge fra due vertici per introduzione tagli
    std::vector<EdgeID> FindShortestEdgePath(const EditableMesh& mesh, VertexID startV, VertexID endV) const;

private:
    std::unordered_map<EdgeID, SeamEdgeInfo> m_edgeMap;
    std::vector<EdgeID> m_seamEdges;
    std::vector<EdgeID> m_boundaryEdges;
    std::vector<EdgeID> m_manualSeamEdges;
    std::vector<EdgeID> m_autoSeamEdges;
    std::vector<std::string> m_diagnostics;
};

} // namespace fw
