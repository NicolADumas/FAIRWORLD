#include "pch.h"
#include "PlanetaryMeshGenerator.h"
#include <cassert>
#include "core/utils/ChunkDimensions.h"
#include "core/utils/CubeSphereMapping.h"
#include "PlanetRadialMapping.h"

namespace fw {

void PlanetaryMeshGenerator::ConvertToPlanetaryPositions(
    std::vector<Vertex>& inOutVertices,
    const fw::PlanetChunkCoord& planetCoord,
    fw::PlanetSize planetSize)
{
    int N = PlanetMath::GetFaceResolution(planetSize);
    float R = PlanetMath::GetPlanetRadius(planetSize);
    
    float faceWidthX = static_cast<float>(N * ChunkDimensions::VoxelsX);
    float faceWidthZ = static_cast<float>(N * ChunkDimensions::VoxelsZ);
    
    auto computeWarp = [&](glm::vec3 pos, glm::vec3& outWarped) -> bool {
        float globalX = (static_cast<float>(planetCoord.col) * ChunkDimensions::VoxelsX) + pos.x;
        float globalZ = (static_cast<float>(planetCoord.row) * ChunkDimensions::VoxelsZ) + pos.z;
        float u = globalX / faceWidthX;
        float v_uv = globalZ / faceWidthZ;
        glm::vec3 direction = CubeSphereMapping::FaceUVToDirection(planetCoord.face, glm::vec2(u, v_uv));
        float g = PlanetRadialMapping::GetGlobalRadialOffset(planetCoord.layer, pos.y);
        float radialDistance = PlanetRadialMapping::GetRadialDistance(g, R);
        
        if (!PlanetRadialMapping::IsValidRadialDistance(radialDistance)) {
            return false;
        }
        
        outWarped = direction * radialDistance;
        return true;
    };

    size_t writeIdx = 0;
    for (size_t i = 0; i + 2 < inOutVertices.size(); i += 3) {
        auto v0 = inOutVertices[i];
        auto v1 = inOutVertices[i+1];
        auto v2 = inOutVertices[i+2];

        glm::vec3 warped0, warped1, warped2;
        bool valid0 = computeWarp(v0.position, warped0);
        bool valid1 = computeWarp(v1.position, warped1);
        bool valid2 = computeWarp(v2.position, warped2);

        if (!valid0 || !valid1 || !valid2) {
            // Defensive safety check. Spatially invalid cells should have been rejected upstream.
            assert(false && "Planetary mesh vertex crossed the core singularity!");
            continue; // Entire face rejected
        }

        glm::vec3 oldE1 = v1.position - v0.position;
        glm::vec3 oldE2 = v2.position - v0.position;
        glm::vec3 oldGeoNormal = glm::cross(oldE1, oldE2);

        glm::vec3 newE1 = warped1 - warped0;
        glm::vec3 newE2 = warped2 - warped0;
        glm::vec3 newGeoNormal = glm::cross(newE1, newE2);

        if (glm::dot(oldGeoNormal, v0.normal) < 0.0f) {
            newGeoNormal = -newGeoNormal;
        }

        float len = glm::length(newGeoNormal);
        if (len > 1e-6f) {
            newGeoNormal /= len;
        } else {
            newGeoNormal = v0.normal;
        }

        v0.position = warped0;
        v1.position = warped1;
        v2.position = warped2;
        
        v0.normal = newGeoNormal;
        v1.normal = newGeoNormal;
        v2.normal = newGeoNormal;

        inOutVertices[writeIdx++] = v0;
        inOutVertices[writeIdx++] = v1;
        inOutVertices[writeIdx++] = v2;
    }

    if (writeIdx < inOutVertices.size()) {
        inOutVertices.resize(writeIdx);
    }
}

} // namespace fw
