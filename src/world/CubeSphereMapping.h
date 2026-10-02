#pragma once
#include <glm/glm.hpp>
#include "core/utils/PlanetMath.h"

namespace fw {

class CubeSphereMapping {
public:
    // Convert a normalized 3D direction vector to Face and UV coordinates [0, 1]
    static void DirectionToFaceUV(const glm::vec3& direction, CubeFace& outFace, glm::vec2& outUV);
    static void DirectionToFaceUV(const glm::vec3& direction, int& outFace, glm::vec2& outUV) {
        CubeFace face;
        DirectionToFaceUV(direction, face, outUV);
        outFace = static_cast<int>(face);
    }

    // Convert Face and UV [0, 1] back to a normalized 3D direction vector
    static glm::vec3 FaceUVToDirection(CubeFace face, const glm::vec2& uv);
    static glm::vec3 FaceUVToDirection(int face, const glm::vec2& uv) {
        return FaceUVToDirection(static_cast<CubeFace>(face), uv);
    }

    // Convert Face and UV [0, 1] to a vector on the cube surface (unnormalized)
    static glm::vec3 FaceUVToCubeDir(CubeFace face, const glm::vec2& uv);
    static glm::vec3 FaceUVToCubeDir(int face, const glm::vec2& uv) {
        return FaceUVToCubeDir(static_cast<CubeFace>(face), uv);
    }

    // Convert continuous UV to discrete Grid Cell [col, row]
    static void UVToGrid(const glm::vec2& uv, int resolution, int& outCol, int& outRow);
    
    // Legacy bridge
    static void FaceUVToCell(const glm::vec2& uv, int resolution, int& outCol, int& outRow) {
        UVToGrid(uv, resolution, outCol, outRow);
    }

    // Convert Grid Cell [col, row] to central UV
    static glm::vec2 GridToUV(int col, int row, int resolution);

    // Convert a specific grid cell [col, row] on a face back to a central 3D direction
    static glm::vec3 CellToDirection(CubeFace face, int col, int row, int resolution);
    static glm::vec3 CellToDirection(int face, int col, int row, int resolution) {
        return CellToDirection(static_cast<CubeFace>(face), col, row, resolution);
    }
};

} // namespace fw
