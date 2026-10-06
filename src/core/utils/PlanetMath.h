#pragma once
#include <cstdint>
#include <functional>
#include <cassert>
#include "ChunkDimensions.h"

namespace fw {

    struct PlanetID {
        uint32_t value;
        bool operator==(const PlanetID& other) const { return value == other.value; }
        bool operator!=(const PlanetID& other) const { return value != other.value; }
        static constexpr PlanetID Invalid() { return {0xFFFFFFFF}; }
        bool IsValid() const { return value != 0xFFFFFFFF; }
    };

    enum class CubeFace : uint8_t {
        PositiveZ = 0,
        NegativeZ = 1,
        PositiveX = 2,
        NegativeX = 3,
        PositiveY = 4,
        NegativeY = 5
    };

    struct PlanetChunkCoord {
        PlanetID planet;
        CubeFace face;
        int32_t col;
        int32_t row;
        int32_t layer;

        bool operator==(const PlanetChunkCoord& other) const {
            return planet == other.planet &&
                   face == other.face &&
                   col == other.col &&
                   row == other.row &&
                   layer == other.layer;
        }

        bool operator!=(const PlanetChunkCoord& other) const {
            return !(*this == other);
        }
    };

    struct PlanetChunkCoordHash {
        std::size_t operator()(const PlanetChunkCoord& coord) const {
            std::size_t h = 17;
            h = h * 31 + std::hash<uint32_t>()(coord.planet.value);
            h = h * 31 + std::hash<uint8_t>()(static_cast<uint8_t>(coord.face));
            h = h * 31 + std::hash<int32_t>()(coord.col);
            h = h * 31 + std::hash<int32_t>()(coord.row);
            h = h * 31 + std::hash<int32_t>()(coord.layer);
            return h;
        }
    };

    enum class FaceEdge : uint8_t {
        Left,
        Right,
        Top,
        Bottom
    };

    struct EdgeTransitionResult {
        CubeFace face;
        int32_t col;
        int32_t row;
    };

    enum class PlanetSize : uint8_t {
        Tiny = 0,   // 3x3
        Micro,      // 5x5
        Small,      // 7x7
        Medium,     // 11x11
        Large,      // 21x21
        Huge        // 41x41
    };

    struct PlanetMath {
        // Ritorna la risoluzione della faccia (N chunk per lato)
        static constexpr uint32_t GetFaceResolution(PlanetSize size) {
            switch (size) {
                case PlanetSize::Tiny:   return 3;
                case PlanetSize::Micro:  return 5;
                case PlanetSize::Small:  return 7;
                case PlanetSize::Medium: return 11;
                case PlanetSize::Large:  return 21;
                case PlanetSize::Huge:   return 41;
                default:                 return 11;
            }
        }

        // Calcola il raggio perfetto per il Cube-Sphere (R = FaceWidth / 2)
        static constexpr float GetPlanetRadius(PlanetSize size) {
            return static_cast<float>(GetFaceResolution(size)) * (ChunkDimensions::HorizontalWorldExtent * 0.5f);
        }

        // Helper per la UI del Chunk Editor: calcola il limite della mappa (es. -5 a +5)
        static constexpr int32_t GetEditorCanvasExtents(PlanetSize size) {
            return (static_cast<int32_t>(GetFaceResolution(size)) - 1) / 2;
        }

        static constexpr EdgeTransitionResult GetNeighborCrossFace(CubeFace face, FaceEdge edge, int32_t edgePos, PlanetSize size) {
            int32_t N = static_cast<int32_t>(GetFaceResolution(size));
            assert(edgePos >= 0 && edgePos < N && "PlanetMath::GetNeighborCrossFace edgePos out of bounds");
            if (edgePos < 0 || edgePos >= N) return {face, -1, -1};

            switch (face) {
                case CubeFace::PositiveZ:
                    switch (edge) {
                        case FaceEdge::Left:   return {CubeFace::NegativeX, N - 1, edgePos};
                        case FaceEdge::Right:  return {CubeFace::PositiveX, 0, edgePos};
                        case FaceEdge::Top:    return {CubeFace::PositiveY, edgePos, N - 1};
                        case FaceEdge::Bottom: return {CubeFace::NegativeY, edgePos, 0};
                        default: break;
                    }
                    break;
                case CubeFace::NegativeZ:
                    switch (edge) {
                        case FaceEdge::Left:   return {CubeFace::PositiveX, N - 1, edgePos};
                        case FaceEdge::Right:  return {CubeFace::NegativeX, 0, edgePos};
                        case FaceEdge::Top:    return {CubeFace::PositiveY, (N - 1) - edgePos, 0};
                        case FaceEdge::Bottom: return {CubeFace::NegativeY, (N - 1) - edgePos, N - 1};
                        default: break;
                    }
                    break;
                case CubeFace::PositiveX:
                    switch (edge) {
                        case FaceEdge::Left:   return {CubeFace::PositiveZ, N - 1, edgePos};
                        case FaceEdge::Right:  return {CubeFace::NegativeZ, 0, edgePos};
                        case FaceEdge::Top:    return {CubeFace::PositiveY, N - 1, (N - 1) - edgePos};
                        case FaceEdge::Bottom: return {CubeFace::NegativeY, N - 1, edgePos};
                        default: break;
                    }
                    break;
                case CubeFace::NegativeX:
                    switch (edge) {
                        case FaceEdge::Left:   return {CubeFace::NegativeZ, N - 1, edgePos};
                        case FaceEdge::Right:  return {CubeFace::PositiveZ, 0, edgePos};
                        case FaceEdge::Top:    return {CubeFace::PositiveY, 0, edgePos};
                        case FaceEdge::Bottom: return {CubeFace::NegativeY, 0, (N - 1) - edgePos};
                        default: break;
                    }
                    break;
                case CubeFace::PositiveY:
                    switch (edge) {
                        case FaceEdge::Left:   return {CubeFace::NegativeX, edgePos, 0};
                        case FaceEdge::Right:  return {CubeFace::PositiveX, (N - 1) - edgePos, 0};
                        case FaceEdge::Top:    return {CubeFace::NegativeZ, (N - 1) - edgePos, 0};
                        case FaceEdge::Bottom: return {CubeFace::PositiveZ, edgePos, 0};
                        default: break;
                    }
                    break;
                case CubeFace::NegativeY:
                    switch (edge) {
                        case FaceEdge::Left:   return {CubeFace::NegativeX, (N - 1) - edgePos, N - 1};
                        case FaceEdge::Right:  return {CubeFace::PositiveX, edgePos, N - 1};
                        case FaceEdge::Top:    return {CubeFace::PositiveZ, edgePos, N - 1};
                        case FaceEdge::Bottom: return {CubeFace::NegativeZ, (N - 1) - edgePos, N - 1};
                        default: break;
                    }
                    break;
                default: break;
            }
            assert(false && "PlanetMath::GetNeighborCrossFace invalid CubeFace or FaceEdge");
            return {face, -1, -1};
        }

        static constexpr PlanetChunkCoord ResolveNeighbor(const PlanetChunkCoord& coord, int32_t dCol, int32_t dRow, PlanetSize size) {
            if (dCol == 0 && dRow == 0) return coord;

            bool isCardinal = (dCol == 1 && dRow == 0) || (dCol == -1 && dRow == 0) || 
                              (dCol == 0 && dRow == 1) || (dCol == 0 && dRow == -1);
            assert(isCardinal && "PlanetMath::ResolveNeighbor supports only cardinal one-step offsets");
            if (!isCardinal) return coord; // Unreachable defensive fallback

            int32_t N = static_cast<int32_t>(GetFaceResolution(size));
            int32_t newCol = coord.col + dCol;
            int32_t newRow = coord.row + dRow;

            if (newCol >= 0 && newCol < N && newRow >= 0 && newRow < N) {
                PlanetChunkCoord result = coord;
                result.col = newCol;
                result.row = newRow;
                return result;
            }

            FaceEdge edge = FaceEdge::Left;
            int32_t edgePos = 0;

            if (dCol == -1) {
                edge = FaceEdge::Left;
                edgePos = coord.row;
            } else if (dCol == 1) {
                edge = FaceEdge::Right;
                edgePos = coord.row;
            } else if (dRow == -1) {
                edge = FaceEdge::Top;
                edgePos = coord.col;
            } else if (dRow == 1) {
                edge = FaceEdge::Bottom;
                edgePos = coord.col;
            } else {
                assert(false && "PlanetMath::ResolveNeighbor logic error on edge detection");
            }

            EdgeTransitionResult trans = GetNeighborCrossFace(coord.face, edge, edgePos, size);
            if (trans.col < 0 || trans.row < 0) {
                return coord; // Unreachable defensive fallback
            }

            PlanetChunkCoord result = coord;
            result.face = trans.face;
            result.col = trans.col;
            result.row = trans.row;
            return result;
        }
    };

} // namespace fw
