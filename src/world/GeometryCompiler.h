#pragma once
#include "ShapeRegistry.h"
#include "ForgeComponents.h"
#include "SurfaceDefinition.h"
#include <string>
#include <vector>

namespace fw {

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
