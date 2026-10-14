#pragma once

#include <vector>
#include <string>
#include <memory>
#include <glm/glm.hpp>
#include "blocks/sculpt/EditableMesh.h"
#include "blocks/uv/UVLayout.h"
#include "blocks/uv/TexelDensityCalculator.h"

namespace fw {

// =============================================================================
// OPZIONI DI ATLAS PACKING (BM-UV4.2 / BM-UV4.5 / BM-UV4.6)
// =============================================================================

struct AtlasPackOptions {
    uint32_t targetWidth = 1024;
    uint32_t targetHeight = 1024;
    float paddingPixels = 4.0f;       // Gutter interno tra le isole in pixel
    float marginPixels = 4.0f;        // Margine esterno dai bordi dell'atlas
    bool allowRotation = true;        // Consente rotazioni ortogonali a 90 gradi per massimizzare la resa
    bool allowMultiAtlas = true;      // Se lo spazio su una singola pagina e esaurito, genera pagine successive
    bool allowDensityDownscale = true;// Se disabilitato multi-atlas, scala uniformemente la densita per far entrare tutto
    bool allowResolutionUpscale = false; // Se necessario, raddoppia la risoluzione fino a maxResolution
    uint32_t maxResolution = 4096;
    uint32_t maxAtlasPages = 4;
    TexelDensityOptions densityOptions;
    bool enableZeroWork = true;
};

// =============================================================================
// ESITO PACKING ATLAS (BM-UV4.2)
// =============================================================================

struct AtlasPackResult {
    bool success = false;
    uint32_t pagesUsed = 1;
    float occupancyRatio = 0.0f; // Percentuale di texel utili utilizzati
    float globalTexelDensity = 0.0f; // px / m
    bool zeroWorkCacheHit = false;
    bool overlapsDetected = false;
    bool outOfBoundsDetected = false;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

// =============================================================================
// STRUTTURA DEL NODO SKYLINE (BM-UV4.2)
// =============================================================================

struct SkylineNode {
    int x = 0;
    int y = 0;
    int width = 0;
};

// =============================================================================
// UV ATLAS PACKER (BM-UV4.2)
// Sistema deterministico di Skyline Bin Packing e trasformazione delle coordinate UV
// =============================================================================

class UVAtlasPacker {
public:
    UVAtlasPacker() = default;
    ~UVAtlasPacker() = default;

    // Esegue il packing dell'UVLayout e aggiorna le coordinate per-corner di EditableMesh
    AtlasPackResult PackLayout(EditableMesh& mesh, 
                               UVLayout& layout, 
                               const AtlasPackOptions& options = {});

    // --- Trasformazioni di coordinate formalmente invertibili (BM-UV4.3) ---
    static glm::vec2 LocalToAtlas(const glm::vec2& localUV, const UVIsland& island);
    static glm::vec2 AtlasToLocal(const glm::vec2& atlasUV, const UVIsland& island);
    static glm::vec2 AtlasToPixel(const glm::vec2& atlasUV, uint32_t atlasW, uint32_t atlasH);
    static glm::vec2 PixelToAtlas(const glm::vec2& pixelUV, uint32_t atlasW, uint32_t atlasH);

    // --- Validazione di assenza di overlap e confini (BM-UV4.7) ---
    static bool ValidateAtlasLayout(const UVLayout& layout, 
                                    float paddingPixels, 
                                    std::vector<std::string>& outErrors);

    // Invalida la cache Zero-Work
    void InvalidateCache() { m_cacheValid = false; }

private:
    struct RectToPack {
        UVIslandID islandIndex = 0;
        int width = 0;
        int height = 0;
        bool rotated = false;
        int page = 0;
        int x = 0;
        int y = 0;
    };

    // Algoritmo di inserimento Skyline per una singola pagina
    static bool PackSkylinePage(std::vector<RectToPack*>& rects, 
                                int binWidth, 
                                int binHeight, 
                                bool allowRotation);

    uint64_t ComputeFingerprint(const UVLayout& layout, const AtlasPackOptions& options) const;

private:
    bool m_cacheValid = false;
    uint64_t m_lastFingerprint = 0;
    AtlasPackResult m_cachedResult;
};

} // namespace fw
