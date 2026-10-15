#pragma once
#include "ShapeRegistry.h"
#include "ForgeComponents.h"
#include "SurfaceDefinition.h"
#include <string>
#include <vector>
#include <optional>

namespace fw {

// =============================================================================
// FG-01: COMPILED TOPOLOGY (Authoritative Primitive Topology)
// Rappresentazione geometrica primaria indicizzata generata da GeometryCompiler
// prima dell'unrolling GPU in MeshComponent.
// Preserva l'identità topologica dei vertici, attributi per-corner, indici e winding.
// =============================================================================

struct CompiledCorner {
    uint32_t geometricVertexIndex = 0;      // Indice topologico nel vettore geometricPositions
    glm::vec2 uv{0.0f, 0.0f};              // Coordinate UV d'angolo
    glm::vec3 normal{0.0f, 1.0f, 0.0f};    // Normale d'angolo per-corner
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct CompiledFace {
    std::vector<CompiledCorner> corners;   // Angoli nel perimetro della faccia (ordine CCW)
    uint32_t surfaceId = 0;                // SurfaceID semantico indipendente
    uint32_t materialId = 0;               // MaterialID PBR indipendente
    glm::vec3 faceNormal{0.0f, 1.0f, 0.0f};
};

struct CompiledTopology {
    // Vertici con identità topologica reale (non unificati indiscriminatamente per posizione)
    std::vector<glm::vec3> geometricPositions;
    // Facce con orientamento CCW coerente e attributi per-corner
    std::vector<CompiledFace> faces;
};

struct GeometryCompileOptions {
    int maxResolution = 32; // Limit tessellation
    const class ShapeRegistry* shapeRegistry = nullptr;
    int recursionDepth = 0;

    // BM-C: Surface & UV mapping options
    const std::vector<SurfaceOverride>* surfaceOverrides = nullptr;
    const UVTransform* defaultUVTransform = nullptr;
    UVProjectionMode defaultProjection = UVProjectionMode::Default;
    uint32_t defaultMaterialId = 0;
};

struct GeometryCompileResult {
    bool success = false;
    std::string errorMessage;
    MeshComponent mesh;
    std::optional<CompiledTopology> topology;
};

class EditableMesh;

class GeometryCompiler {
public:
    static GeometryCompileResult Compile(const ShapeDefinition& definition, const GeometryCompileOptions& options = {});
    static GeometryCompileResult CompileEditableMesh(const EditableMesh& mesh, const GeometryCompileOptions& options = {});

private:
    static void AddQuad(MeshComponent& mesh, int v0, int v1, int v2, int v3, const glm::vec3& normal);
    static void AddTriangle(MeshComponent& mesh, int v0, int v1, int v2, const glm::vec3& normal);
    static Vertex CreateVertex(const glm::vec3& pos, const glm::vec3& normal);
};

} // namespace fw
