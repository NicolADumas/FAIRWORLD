#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <glm/glm.hpp>
#include "blocks/sculpt/EditableMesh.h"
#include "blocks/uv/UVLayout.h"

namespace fw {

// =============================================================================
// MODALITA DI CALCOLO TEXEL DENSITY (BM-UV4.4)
// =============================================================================

enum class TexelDensityMode : uint8_t {
    GlobalUniform = 0, // Densita uniforme identica per tutte le isole
    PerSurface,        // Densita ponderata in base al SurfaceID semantico
    PerIsland,         // Densita configurabile per singola isola
    CustomWeighted     // Ponderata da tabella di pesi personalizzata
};

inline const char* GetTexelDensityModeName(TexelDensityMode m) {
    switch (m) {
        case TexelDensityMode::GlobalUniform: return "GlobalUniform";
        case TexelDensityMode::PerSurface:    return "PerSurface";
        case TexelDensityMode::PerIsland:     return "PerIsland";
        case TexelDensityMode::CustomWeighted: return "CustomWeighted";
        default:                              return "Unknown";
    }
}

// =============================================================================
// OPZIONI DI TEXEL DENSITY (BM-UV4.4)
// =============================================================================

struct TexelDensityOptions {
    TexelDensityMode mode = TexelDensityMode::GlobalUniform;
    float targetTexelDensity = 512.0f; // pixel per metro (lineare)
    bool autoCalculateTargetDensity = true; // Calcola la massima densita compatibile con l'area atlas
    glm::vec3 physicalScale{1.0f, 1.0f, 1.0f};

    // Mappe di pesi semantici per PerSurface e CustomWeighted
    std::unordered_map<uint32_t, float> surfaceWeights; // surfaceId -> moltiplicatore di scala (default 1.0)
    std::unordered_map<UVIslandID, float> islandWeights; // islandId -> moltiplicatore di scala (default 1.0)
};

// =============================================================================
// REPORT DIAGNOSTICO TEXEL DENSITY (BM-UV4.4)
// =============================================================================

struct TexelDensityReport {
    float minLinearDensity = 0.0f; // px / metro
    float maxLinearDensity = 0.0f;
    float averageLinearDensity = 0.0f;
    float standardDeviation = 0.0f;
    float minArealDensity = 0.0f; // px² / metro²
    float maxArealDensity = 0.0f;
    float averageArealDensity = 0.0f;
    bool isUniform = true; // Deviazione standard < tolleranza
    std::vector<std::string> warnings;
};

// =============================================================================
// TEXEL DENSITY CALCULATOR (BM-UV4.4)
// Servizio headless per bilanciamento e normalizzazione della risoluzione dei texel
// =============================================================================

class TexelDensityCalculator {
public:
    TexelDensityCalculator() = default;
    ~TexelDensityCalculator() = default;

    // Calcola l'area 3D scalata fisicamente per una singola isola
    static float ComputeScaledArea3D(const EditableMesh& mesh, 
                                     const UVIsland& island, 
                                     const glm::vec3& scale);

    // Calcola l'area 2D normalizzata coperta dall'isola nello spazio UV [0, 1]
    static float ComputeIslandUVArea(const EditableMesh& mesh, 
                                     const UVIsland& island);

    // Calcola i fattori di scala dimensionali relativi (in texel) per ciascuna isola
    static std::vector<glm::vec2> ComputeIslandTexelDimensions(const EditableMesh& mesh, 
                                                               std::vector<UVIsland>& islands, 
                                                               const TexelDensityOptions& options, 
                                                               uint32_t atlasW, 
                                                               uint32_t atlasH, 
                                                               float& outCalculatedGlobalDensity);

    // Esegue l'analisi statistica della distribuzione della texel density su un UVLayout
    static TexelDensityReport AnalyzeLayoutDensity(const EditableMesh& mesh, 
                                                  const UVLayout& layout, 
                                                  const glm::vec3& physicalScale = glm::vec3(1.0f));
};

} // namespace fw
