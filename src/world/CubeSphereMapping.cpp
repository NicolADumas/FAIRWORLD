#include "pch.h"
#include "CubeSphereMapping.h"
#include <cmath>
#include <algorithm>

namespace fw {

void CubeSphereMapping::DirectionToFaceUV(const glm::vec3& direction, int& outFace, glm::vec2& outUV) {
    glm::vec3 absNormal = glm::abs(direction);
    int face = 0;
    float maxAxis = absNormal.z;
    
    // +Z = 0, -Z = 1, +X = 2, -X = 3, +Y = 4, -Y = 5
    if (absNormal.x > maxAxis) { maxAxis = absNormal.x; face = 2; }
    if (absNormal.y > maxAxis) { maxAxis = absNormal.y; face = 4; }
    
    if (face == 0 && direction.z < 0) face = 1;
    if (face == 2 && direction.x < 0) face = 3;
    if (face == 4 && direction.y < 0) face = 5;
    
    outFace = face;
    
    glm::vec3 projected = direction / maxAxis;
    float cx = 0.0f, cy = 0.0f;
    
    switch (face) {
        case 0: cx = projected.x;  cy = projected.y;  break;
        case 1: cx = -projected.x; cy = projected.y;  break;
        case 2: cx = -projected.z; cy = projected.y;  break;
        case 3: cx = projected.z;  cy = projected.y;  break;
        case 4: cx = projected.x;  cy = -projected.z; break;
        case 5: cx = projected.x;  cy = projected.z;  break;
    }
    
    outUV.x = (cx + 1.0f) * 0.5f;
    outUV.y = (1.0f - cy) * 0.5f;
}

glm::vec3 CubeSphereMapping::FaceUVToDirection(int face, const glm::vec2& uv) {
    float cx = uv.x * 2.0f - 1.0f;
    float cy = 1.0f - uv.y * 2.0f;
    
    glm::vec3 dir(0.0f);
    switch (face) {
        case 0: dir = glm::vec3(cx, cy, 1.0f); break;
        case 1: dir = glm::vec3(-cx, cy, -1.0f); break;
        case 2: dir = glm::vec3(1.0f, cy, -cx); break;
        case 3: dir = glm::vec3(-1.0f, cy, cx); break;
        case 4: dir = glm::vec3(cx, 1.0f, -cy); break;
        case 5: dir = glm::vec3(cx, -1.0f, cy); break;
    }
    return glm::normalize(dir);
}

void CubeSphereMapping::FaceUVToCell(const glm::vec2& uv, int resolution, int& outCol, int& outRow) {
    outCol = std::clamp((int)(uv.x * resolution), 0, resolution - 1);
    outRow = std::clamp((int)(uv.y * resolution), 0, resolution - 1);
}

glm::vec3 CubeSphereMapping::CellToDirection(int face, int col, int row, int resolution) {
    glm::vec2 uv((col + 0.5f) / resolution, (row + 0.5f) / resolution);
    return FaceUVToDirection(face, uv);
}

} // namespace fw
