#pragma once

#include "core/utils/PlanetMath.h"
#include <cmath>
#include <glm/glm.hpp>

namespace fw::planet_grid_compat {

// Converts a legacy face-local row (row 0 at the lower chart edge) to the
// canonical CubeSphereMapping row (row 0 at the upper chart edge).
// Precondition: resolution > 0 and 0 <= legacyRow < resolution.
inline bool TryLegacyFaceRowToCanonicalRow(int legacyRow, int resolution, int& canonicalRow) {
    if (resolution <= 0 || legacyRow < 0 || legacyRow >= resolution) {
        return false;
    }

    canonicalRow = resolution - 1 - legacyRow;
    return true;
}

// Converts the old 3x2 face net cell key into canonical planet coordinates.
// The net key remains the storage/ChunkManager identity; row reflection only
// adapts its orientation to CubeSphereMapping's canonical row direction.
inline bool TryLegacyNetToPlanetChunkCoord(
    int globalX, int globalZ, int resolution, PlanetID planet, int layer,
    PlanetChunkCoord& coordinate) {
    if (resolution <= 0 || globalX < 0 || globalX >= 3 * resolution ||
        globalZ < 0 || globalZ >= 2 * resolution) {
        return false;
    }

    const int faceColumn = globalX / resolution;
    const int faceRow = globalZ / resolution;
    const int faceIndex = faceColumn + faceRow * 3;
    int canonicalRow = -1;
    if (faceIndex < 0 || faceIndex >= 6 ||
        !TryLegacyFaceRowToCanonicalRow(globalZ % resolution, resolution, canonicalRow)) {
        return false;
    }

    coordinate = PlanetChunkCoord{
        planet, static_cast<CubeFace>(faceIndex), globalX % resolution, canonicalRow, layer};
    return true;
}

// Inverse adapter. This returns the unchanged legacy net key used by existing
// ChunkManager lookups and persisted document grid coordinates.
inline bool TryPlanetChunkCoordToLegacyNet(
    const PlanetChunkCoord& coordinate, int resolution, int& globalX, int& globalZ) {
    if (resolution <= 0 || static_cast<int>(coordinate.face) >= 6 ||
        coordinate.col < 0 || coordinate.col >= resolution ||
        coordinate.row < 0 || coordinate.row >= resolution) {
        return false;
    }

    const int face = static_cast<int>(coordinate.face);
    const int legacyRow = resolution - 1 - coordinate.row;
    globalX = (face % 3) * resolution + coordinate.col;
    globalZ = (face / 3) * resolution + legacyRow;
    return true;
}

// Maps canonical face chart coordinates to continuous coordinates measured in
// legacy net-cell-center units. Integer chart cell centers map to integer net
// keys; this deliberately does not change the storage layout.
inline bool TryFaceChartToLegacyNetContinuous(
    CubeFace face, float chartX, float chartY, int resolution, float& globalX, float& globalZ) {
    const int faceIndex = static_cast<int>(face);
    if (resolution <= 0 || faceIndex < 0 || faceIndex >= 6 ||
        !std::isfinite(chartX) || !std::isfinite(chartY) ||
        chartX < -1.0f || chartX > 1.0f || chartY < -1.0f || chartY > 1.0f) {
        return false;
    }

    globalX = static_cast<float>(faceIndex % 3 * resolution) +
        ((chartX + 1.0f) * static_cast<float>(resolution) * 0.5f - 0.5f);
    globalZ = static_cast<float>(faceIndex / 3 * resolution) +
        ((chartY + 1.0f) * static_cast<float>(resolution) * 0.5f - 0.5f);
    return true;
}

// Converts a point within a legacy net cell to canonical face UV. localX and
// localZ are fractions of the cell extent; localZ increases toward decreasing
// canonical row because the historical net's row axis points the other way.
inline bool TryLegacyNetCellToFaceUV(
    int globalX, int globalZ, float localX, float localZ, int resolution,
    CubeFace& face, glm::vec2& uv) {
    if (!std::isfinite(localX) || !std::isfinite(localZ)) return false;
    PlanetChunkCoord coordinate{};
    if (!TryLegacyNetToPlanetChunkCoord(
            globalX, globalZ, resolution, PlanetID::Invalid(), 0, coordinate)) {
        return false;
    }

    face = coordinate.face;
    uv.x = (static_cast<float>(coordinate.col) + localX) / static_cast<float>(resolution);
    uv.y = (static_cast<float>(coordinate.row + 1) - localZ) / static_cast<float>(resolution);
    return true;
}

} // namespace fw::planet_grid_compat
