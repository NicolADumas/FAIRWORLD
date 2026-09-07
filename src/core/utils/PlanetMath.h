#pragma once
#include <cstdint>

namespace fw {

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
