#include "pch.h"
#include "components/ForgeComponents.h"
#include "world/CubeSphereMapping.h"
#include "world/MeshGenerators.h"
#include "world/VoxelFaceUV.h"

#include <array>
#include <cmath>
#include <iostream>
#include <set>

namespace {
bool finiteUV(const fw::Vec2& uv) {
    return std::isfinite(uv.x) && std::isfinite(uv.y) &&
        uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f;
}

float uvTriangleArea(const fw::Vec2& a, const fw::Vec2& b, const fw::Vec2& c) {
    return std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) * 0.5f;
}

bool testVoxelFaceOrientations() {
    const std::array<glm::vec3, 6> normals = {
        glm::vec3(1,0,0), glm::vec3(-1,0,0), glm::vec3(0,1,0),
        glm::vec3(0,-1,0), glm::vec3(0,0,1), glm::vec3(0,0,-1)
    };
    for (const auto& normal : normals) {
        std::set<std::pair<int, int>> mapped;
        for (float x : {-0.5f, 0.5f}) {
            for (float y : {-0.5f, 0.5f}) {
                for (float z : {-0.5f, 0.5f}) {
                    const fw::Vec2 uv = fw::VoxelFaceUV(glm::vec3(x, y, z), normal);
                    if (!finiteUV(uv)) return false;
                    mapped.emplace(static_cast<int>(std::lround(uv.x)), static_cast<int>(std::lround(uv.y)));
                }
            }
        }
        if (mapped.size() != 4) return false;
    }
    return true;
}

bool testCubeSphereUVs() {
    for (int face = 0; face < 6; ++face) {
        for (const glm::vec2 uv : {glm::vec2(0.2f,0.3f), glm::vec2(0.5f,0.5f), glm::vec2(0.8f,0.7f)}) {
            const auto direction = fw::CubeSphereMapping::FaceUVToDirection(face, uv);
            fw::CubeFace roundTripFace;
            glm::vec2 roundTripUV;
            fw::CubeSphereMapping::DirectionToFaceUV(direction, roundTripFace, roundTripUV);
            if (static_cast<int>(roundTripFace) != face || glm::length(roundTripUV - uv) > 1.0e-5f) return false;
        }
    }
    return true;
}

bool testMeshProducers() {
    const auto cube = fw::MeshGenerators::MakeCube(2.0f);
    if (cube.vertices.size() != 36) return false;
    for (size_t face = 0; face < 6; ++face) {
        const size_t first = face * 6;
        std::set<std::pair<int, int>> values;
        for (size_t i = first; i < first + 6; ++i) {
            const auto& v = cube.vertices[i];
            if (!finiteUV(v.roughMetal) || v.materialID != 0) return false;
            values.emplace(static_cast<int>(std::lround(v.roughMetal.x)), static_cast<int>(std::lround(v.roughMetal.y)));
        }
        if (values.size() != 4) return false;
        if (uvTriangleArea(cube.vertices[first].roughMetal, cube.vertices[first + 1].roughMetal,
                           cube.vertices[first + 2].roughMetal) <= 0.0f ||
            uvTriangleArea(cube.vertices[first + 3].roughMetal, cube.vertices[first + 4].roughMetal,
                           cube.vertices[first + 5].roughMetal) <= 0.0f) return false;
    }

    const auto preview = fw::MeshGenerators::MakeVoxelPreview(12, nullptr);
    if (preview.vertices.size() != 36) return false;
    for (const auto& v : preview.vertices) {
        if (!finiteUV(v.roughMetal) || v.materialID != 12) return false;
    }
    for (size_t i = 0; i < preview.vertices.size(); i += 3) {
        if (uvTriangleArea(preview.vertices[i].roughMetal, preview.vertices[i + 1].roughMetal,
                           preview.vertices[i + 2].roughMetal) <= 0.0f) return false;
    }

    const auto sphere = fw::MeshGenerators::MakeSphere(12, 6);
    const auto superSphere = fw::MeshGenerators::MakeSuperSphere(2.0f, 1.0f, 8);
    for (const auto* mesh : {&sphere, &superSphere}) {
        if (mesh->vertices.empty()) return false;
        std::set<std::pair<int, int>> coordinates;
        for (const auto& v : mesh->vertices) {
            if (!finiteUV(v.roughMetal)) return false;
            coordinates.emplace(static_cast<int>(std::lround(v.roughMetal.x * 1000.0f)),
                                static_cast<int>(std::lround(v.roughMetal.y * 1000.0f)));
        }
        if (coordinates.size() < 4) return false;
    }
    return true;
}
}

int main() {
    static_assert(sizeof(fw::Vertex) == 64);
    static_assert(offsetof(fw::Vertex, roughMetal) == 28);
    static_assert(offsetof(fw::Vertex, materialID) == 36);

    const bool voxel = testVoxelFaceOrientations();
    const bool planet = testCubeSphereUVs();
    const bool producers = testMeshProducers();
    std::cout << "Forge vertex contract: voxel orientations=" << (voxel ? "PASS" : "FAIL")
              << ", cube-sphere=" << (planet ? "PASS" : "FAIL")
              << ", mesh producers=" << (producers ? "PASS" : "FAIL") << '\n';
    return voxel && planet && producers ? 0 : 1;
}
