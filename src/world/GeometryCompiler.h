#pragma once
#include "ShapeRegistry.h"
#include "ForgeComponents.h"
#include <string>
#include <vector>

namespace fw {

struct GeometryCompileOptions {
    int maxResolution = 32; // Limit tessellation
};

struct GeometryCompileResult {
    bool success = false;
    std::string errorMessage;
    MeshComponent mesh;
};

class GeometryCompiler {
public:
    static GeometryCompileResult Compile(const ShapeDefinition& definition, const GeometryCompileOptions& options = {});

private:
    static void AddQuad(MeshComponent& mesh, int v0, int v1, int v2, int v3, const glm::vec3& normal);
    static void AddTriangle(MeshComponent& mesh, int v0, int v1, int v2, const glm::vec3& normal);
    static Vertex CreateVertex(const glm::vec3& pos, const glm::vec3& normal);
};

} // namespace fw
