#include "pch.h"
#include "CubeSphereMapping.h"
#include <cmath>
#include <algorithm>

namespace fw {

glm::vec2 CubeSphereMapping::FaceUVToChartCoordinates(const glm::vec2& uv) {
    return glm::vec2(2.0f * uv.x - 1.0f, 1.0f - 2.0f * uv.y);
}

glm::vec3 CubeSphereMapping::FaceChartToDirection(CubeFace face, const glm::vec2& chart) {
    const glm::vec2 uv((chart.x + 1.0f) * 0.5f, (1.0f - chart.y) * 0.5f);
    return FaceUVToDirection(face, uv);
}

void CubeSphereMapping::DirectionToFaceUV(const glm::vec3& direction, CubeFace& outFace, glm::vec2& outUV) {
    glm::vec3 absNormal = glm::abs(direction);
    int face = 0;
    float maxAxis = absNormal.z;
    
    if (absNormal.x > maxAxis) { maxAxis = absNormal.x; face = 2; }
    if (absNormal.y > maxAxis) { maxAxis = absNormal.y; face = 4; }
    
    if (face == 0 && direction.z < 0) face = 1;
    if (face == 2 && direction.x < 0) face = 3;
    if (face == 4 && direction.y < 0) face = 5;
    
    outFace = static_cast<CubeFace>(face);
    
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

glm::vec3 CubeSphereMapping::FaceUVToCubeDir(CubeFace face, const glm::vec2& uv) {
    float cx = uv.x * 2.0f - 1.0f;
    float cy = 1.0f - uv.y * 2.0f;
    
    glm::vec3 dir(0.0f);
    switch (static_cast<int>(face)) {
        case 0: dir = glm::vec3(cx, cy, 1.0f); break;
        case 1: dir = glm::vec3(-cx, cy, -1.0f); break;
        case 2: dir = glm::vec3(1.0f, cy, -cx); break;
        case 3: dir = glm::vec3(-1.0f, cy, cx); break;
        case 4: dir = glm::vec3(cx, 1.0f, -cy); break;
        case 5: dir = glm::vec3(cx, -1.0f, cy); break;
    }
    return dir;
}

#if defined(_MSC_VER)
#pragma float_control(precise, on, push)
#pragma fp_contract(off)
#endif
glm::vec3 CubeSphereMapping::FaceUVToDirection(CubeFace face, const glm::vec2& uv) {
    const glm::vec3 cubeDirection = FaceUVToCubeDir(face, uv);
    const float squaredLength = (cubeDirection.x * cubeDirection.x +
        cubeDirection.y * cubeDirection.y) + cubeDirection.z * cubeDirection.z;
    const float inverseLength = glm::inversesqrt(squaredLength);
    const auto canonicalizeZero = [](float component) {
        return component == 0.0f ? 0.0f : component;
    };
    return glm::vec3(
        canonicalizeZero(cubeDirection.x * inverseLength),
        canonicalizeZero(cubeDirection.y * inverseLength),
        canonicalizeZero(cubeDirection.z * inverseLength));
}
#if defined(_MSC_VER)
#pragma float_control(pop)
#endif

void CubeSphereMapping::UVToGrid(const glm::vec2& uv, int resolution, int& outCol, int& outRow) {
    const auto mapAxis = [resolution](float coordinate) {
        // Compare against the representable float boundaries used to construct
        // UV values (float(k) / float(N)). Exact boundary values belong to cell k;
        // the immediately preceding float remains in cell k - 1.
        int firstBoundaryAfterCoordinate = 1;
        int boundaryEnd = resolution;
        while (firstBoundaryAfterCoordinate < boundaryEnd) {
            const int candidate = firstBoundaryAfterCoordinate +
                (boundaryEnd - firstBoundaryAfterCoordinate) / 2;
            const float boundary = static_cast<float>(candidate) / static_cast<float>(resolution);
            if (coordinate >= boundary) {
                firstBoundaryAfterCoordinate = candidate + 1;
            } else {
                boundaryEnd = candidate;
            }
        }

        return std::clamp(firstBoundaryAfterCoordinate - 1, 0, resolution - 1);
    };

    outCol = mapAxis(uv.x);
    outRow = mapAxis(uv.y);
}

glm::vec2 CubeSphereMapping::GridToUV(int col, int row, int resolution) {
    return glm::vec2((col + 0.5f) / resolution, (row + 0.5f) / resolution);
}

glm::vec3 CubeSphereMapping::CellToDirection(CubeFace face, int col, int row, int resolution) {
    return FaceUVToDirection(face, GridToUV(col, row, resolution));
}

} // namespace fw
