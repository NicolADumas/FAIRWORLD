#pragma once

#include "components/ForgeComponents.h"
#include "core/utils/PlanetMath.h"
#include "world/CubeSphereMapping.h"
#include "world/PlanetGridCompatibility.h"

#include <cmath>

namespace fw::planetary_neighborhood {

enum class Availability : uint8_t {
    Available,
    Missing,
    Pending,
    Invalid
};

struct Address {
    PlanetChunkCoord coordinate{};
    int storageCx = 0;
    int storageCz = 0;
};

inline bool IsValidCoordinate(const PlanetChunkCoord& coordinate, int resolution) {
    const int face = static_cast<int>(coordinate.face);
    return coordinate.planet.IsValid() && face >= 0 && face < 6 && resolution > 0 &&
        coordinate.col >= 0 && coordinate.col < resolution &&
        coordinate.row >= 0 && coordinate.row < resolution;
}

// dx/dz are the chunk-grid axes used by VoxelChunkMesher; canonical row grows
// opposite to the legacy/world z key, hence dRow=-dz.
inline bool TryResolveAddress(
    const PlanetChunkCoord& source, int dx, int dz, PlanetSize size, Address& out) {
    const int resolution = static_cast<int>(PlanetMath::GetFaceResolution(size));
    const bool cardinal = (dx == -1 && dz == 0) || (dx == 1 && dz == 0) ||
        (dx == 0 && dz == -1) || (dx == 0 && dz == 1);
    if (!cardinal || !IsValidCoordinate(source, resolution)) return false;

    const PlanetChunkCoord target = PlanetMath::ResolveNeighbor(source, dx, -dz, size);
    if (!IsValidCoordinate(target, resolution) || target.planet != source.planet ||
        target.layer != source.layer) return false;

    int storageX = 0;
    int storageZ = 0;
    if (!planet_grid_compat::TryPlanetChunkCoordToLegacyNet(
            target, resolution, storageX, storageZ)) return false;

    out = Address{target, storageX, storageZ};
    return true;
}

inline Availability Classify(
    bool found, bool entityValid, bool hasChunk, bool identityMatches, bool generated) {
    if (!found) return Availability::Missing;
    if (!entityValid || !hasChunk || !identityMatches) return Availability::Invalid;
    return generated ? Availability::Available : Availability::Pending;
}

inline glm::vec3 FaceLocalAxis(CubeFace face, bool localX) {
    const glm::vec2 centerUv(0.5f, 0.5f);
    const glm::vec3 center = CubeSphereMapping::FaceUVToCubeDir(face, centerUv);
    const glm::vec2 sampleUv = localX ? glm::vec2(0.75f, 0.5f) : glm::vec2(0.5f, 0.25f);
    return glm::normalize(CubeSphereMapping::FaceUVToCubeDir(face, sampleUv) - center);
}

// Reorients a cross-face neighbor snapshot into the source chunk's local x/z
// frame. This keeps VoxelChunkMesher::Build's existing 3x3 array/API intact.
inline bool TryOrientCrossFaceSnapshot(
    const PlanetChunkCoord& sourceCoord,
    int sourceDx,
    int sourceDz,
    PlanetSize size,
    const VoxelChunkComponent& target,
    VoxelChunkComponent& oriented) {
    Address address;
    if (!TryResolveAddress(sourceCoord, sourceDx, sourceDz, size, address) ||
        address.coordinate != target.planetCoord ||
        address.coordinate.face == sourceCoord.face) return false;

    int backCol = 0;
    int backRow = 0;
    bool reverseFound = false;
    constexpr int cardinal[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
    for (const auto& offset : cardinal) {
        const auto back = PlanetMath::ResolveNeighbor(
            address.coordinate, offset[0], offset[1], size);
        if (back == sourceCoord) {
            backCol = offset[0];
            backRow = offset[1];
            reverseFound = true;
            break;
        }
    }
    if (!reverseFound || ((backCol == 0) == (backRow == 0))) return false;

    const bool sourceCrossesX = sourceDx != 0;
    const glm::vec3 sourceTangent = FaceLocalAxis(sourceCoord.face, !sourceCrossesX);
    const bool targetNormalIsX = backCol != 0;
    const glm::vec3 targetTangent = FaceLocalAxis(address.coordinate.face, !targetNormalIsX);
    const float tangentDot = glm::dot(sourceTangent, targetTangent);
    if (std::abs(tangentDot) < 0.999f) return false;
    const bool reverseTangent = tangentDot < 0.0f;

    // Build only reads the boundary plane for a cardinal neighbor. Keeping the
    // rest empty avoids copying/transposing the whole voxel volume per seam.
    oriented = VoxelChunkComponent{};
    oriented.cx = target.cx;
    oriented.cz = target.cz;
    oriented.planetCoord = target.planetCoord;
    oriented.isGenerated = target.isGenerated;
    for (int tangent = 0; tangent < CHUNK_SIZE; ++tangent) {
        const int targetTangentCoordinate = reverseTangent
            ? CHUNK_SIZE - 1 - tangent : tangent;
        int targetX = 0;
        int targetZ = 0;
        const int virtualX = sourceCrossesX
            ? (sourceDx > 0 ? 0 : CHUNK_SIZE - 1) : tangent;
        const int virtualZ = sourceCrossesX
            ? tangent : (sourceDz > 0 ? 0 : CHUNK_SIZE - 1);
        if (targetNormalIsX) {
            targetX = backCol < 0 ? 0 : CHUNK_SIZE - 1;
            targetZ = targetTangentCoordinate;
        } else {
            targetX = targetTangentCoordinate;
            targetZ = backRow < 0 ? CHUNK_SIZE - 1 : 0;
        }
        if (targetX < 0 || targetX >= CHUNK_SIZE || targetZ < 0 || targetZ >= CHUNK_SIZE)
            return false;
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            oriented.blocks[virtualX][y][virtualZ] = target.blocks[targetX][y][targetZ];
        }
    }
    return true;
}

} // namespace fw::planetary_neighborhood
