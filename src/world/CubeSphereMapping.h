#pragma once
#include <glm/glm.hpp>

namespace fw {

class CubeSphereMapping {
public:
    // Convert a normalized 3D direction vector to Face Index (0-5) and UV coordinates [0, 1]
    static void DirectionToFaceUV(const glm::vec3& direction, int& outFace, glm::vec2& outUV);

    // Convert Face Index and UV [0, 1] back to a normalized 3D direction vector
    static glm::vec3 FaceUVToDirection(int face, const glm::vec2& uv);

    // Given a face, UV, and grid resolution (N_lato), find the cell coordinate [col, row]
    static void FaceUVToCell(const glm::vec2& uv, int resolution, int& outCol, int& outRow);

    // Convert a specific grid cell [col, row] on a face back to a central 3D direction
    static glm::vec3 CellToDirection(int face, int col, int row, int resolution);
};

} // namespace fw
