#pragma once
#include <cstdint>

#include "MapDocument.h"

namespace fw {

// Componente principale che memorizza i dati del bioma per un chunk
struct BiomeDataComponent {
    MapRegionType type = MapRegionType::Forest;
    uint8_t surfaceBlockId = 1;
    uint8_t subsurfaceBlockId = 3;
    float temperature = 0.5f;
    float humidity = 0.5f;
    bool isCustomMapped = false;
    
    // Campi per SDF Blending su Chunk Voxel
    bool hasBaseRegion = false;
    MapRegion baseRegion;
    std::vector<MapRegion> overlappingRegions;
    float planetRadius = 50.0f; // Raggio sferico (se > 0)
    glm::vec3 chunkCenterWorld = glm::vec3(0.0f);
};

// Tag Component: Indica che il chunk ha bisogno di generare il terreno base (roccia, terra, acqua)
struct TerrainGenTag {};

// Tag Component: Indica che il chunk ha il terreno base e deve spawnare decorazioni (alberi, rocce)
struct DecoratorGenTag {};

// --- Tag specifici per Bioma (usati per smistare la generazione ai singoli Sistemi) ---
struct ForestBiomeTag {};
struct DesertBiomeTag {};
struct TundraBiomeTag {};
struct OceanBiomeTag {};
struct VolcanoBiomeTag {};
struct CityBiomeTag {};
struct DungeonBiomeTag {};
struct PortalBiomeTag {};
struct FlatBiomeTag {};

} // namespace fw
