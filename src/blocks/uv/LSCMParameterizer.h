#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>
#include "blocks/sculpt/EditableMesh.h"
#include "blocks/uv/UVLayout.h"

namespace fw {

// =============================================================================
// OPZIONI DEL SOLVER LSCM (BM-UV3.4)
// =============================================================================

struct LSCMOptions {
    float tolerance = 1e-6f;          // Tolleranza residuo Conjugate Gradient
    uint32_t maxIterations = 2000;    // Numero massimo iterazioni CG
    bool normalizeTexelDensity = true;// Scala l'isola in base all'area 3D fisica
    bool fallbackToPlanarOnFailure = true; // Applica proiezione planare se LSCM diverge o degenere
    float pinDistanceScale = 1.0f;    // Scala distanza di ancoraggio pin boundary
};

// =============================================================================
// REPORT SOLVER LSCM (BM-UV3.4)
// =============================================================================

struct LSCMResult {
    bool success = false;
    bool usedFallback = false;
    uint32_t iterationsExecuted = 0;
    float finalResidual = 0.0f;
    uint32_t pinnedVertexCount = 0;
    IslandDistortion distortion;
    std::string diagnosticMessage;
};

// =============================================================================
// LSCM PARAMETERIZER (BM-UV3.4)
// Least Squares Conformal Maps headless solver su triangolazione autoritativa
// =============================================================================

class LSCMParameterizer {
public:
    LSCMParameterizer() = default;
    ~LSCMParameterizer() = default;

    // Parametrizza una singola UVIsland scrivendo le coordinate UV nei corner di EditableMesh
    static LSCMResult ParameterizeIsland(EditableMesh& mesh, 
                                         UVIsland& island, 
                                         const LSCMOptions& options = {});

    // Proiezione planare ortogonale (Fallback primario per patch piatte o divergenza LSCM)
    static LSCMResult ParameterizePlanarFallback(EditableMesh& mesh, 
                                                UVIsland& island);

    // Calcolo metriche di distorsione d'area, angolare e triangoli UV invertiti
    static IslandDistortion ComputeDistortion(const EditableMesh& mesh, 
                                             const UVIsland& island);

private:
    struct LocalTriangle {
        VertexID v[3];
        HalfEdgeID he[3]; // HalfEdge associati a ciascun corner
        glm::vec2 local2D[3];
        float area3D = 0.0f;
    };

    // Estrae i triangoli dell'isola e le coordinate 2D conformi
    static bool TriangulateIsland(const EditableMesh& mesh, 
                                  const UVIsland& island, 
                                  std::vector<LocalTriangle>& outTriangles, 
                                  std::vector<VertexID>& outVertices, 
                                  std::unordered_map<VertexID, uint32_t>& outVertexMap);

    // Identifica la coppia di vertici di bordo con massima distanza geodetica/euclidea 3D
    static bool SelectPinVertices(const EditableMesh& mesh, 
                                  const UVIsland& island, 
                                  VertexID& outPinA, 
                                  VertexID& outPinB);
};

} // namespace fw
