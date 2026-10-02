#pragma once
#include <cstdint>
#include <functional>

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

    enum class PlanetSize : uint8_t {
        Tiny = 0,   // 3x3
        Micro,      // 5x5
        Small,      // 7x7
        Medium,     // 11x11
        Large,      // 21x21
        Huge        // 41x41
    };

    struct PlanetMath {
        static constexpr float CHUNK_WORLD_SIZE = 16.0f;

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
            return static_cast<float>(GetFaceResolution(size)) * (CHUNK_WORLD_SIZE * 0.5f);
        }

        // Helper per la UI del Chunk Editor: calcola il limite della mappa (es. -5 a +5)
        static constexpr int32_t GetEditorCanvasExtents(PlanetSize size) {
            return (static_cast<int32_t>(GetFaceResolution(size)) - 1) / 2;
        }
    };

} // namespace fw
