#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include "blocks/sculpt/EditableMesh.h"
#include "blocks/uv/UVLayout.h"
#include "blocks/uv/SeamGraph.h"

namespace fw {

// =============================================================================
// REPORT ESTRATTORE ISOLE UV (BM-UV3.3)
// =============================================================================

struct IslandExtractionReport {
    bool success = true;
    uint32_t totalFacesProcessed = 0;
    uint32_t totalIslandsExtracted = 0;
    uint32_t closedIslandsDetected = 0; // Isole senza bordi (richiedono ulteriore seam)
    uint32_t multiBoundaryIslandsDetected = 0; // Isole con fori interni (buchi)
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

// =============================================================================
// UV ISLAND EXTRACTOR (BM-UV3.3)
// Attraversamento topologico autoritativo DCEL per estrazione UVIsland
// =============================================================================

class UVIslandExtractor {
public:
    UVIslandExtractor() = default;
    ~UVIslandExtractor() = default;

    // Estrae tutte le isole UV da EditableMesh utilizzando il SeamGraph autoritativo
    static std::vector<UVIsland> ExtractIslands(const EditableMesh& mesh, 
                                                const SeamGraph& seamGraph, 
                                                std::unordered_map<FaceID, UVIslandID>& outFaceToIslandMap, 
                                                IslandExtractionReport& outReport);

    // Valida la topologia di una singola isola (computo Eulero e cicli di bordo)
    static bool ValidateIslandTopology(const EditableMesh& mesh, 
                                       const UVIsland& island, 
                                       const SeamGraph& seamGraph, 
                                       int& outEulerCharacteristic, 
                                       size_t& outBoundaryLoopCount);
};

} // namespace fw
