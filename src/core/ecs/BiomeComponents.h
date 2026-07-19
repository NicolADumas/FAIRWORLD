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
};

// Tag Component: Indica che il chunk ha bisogno di generare il terreno base (roccia, terra, acqua)
struct TerrainGenTag {};

// Tag Component: Indica che il chunk ha il terreno base e deve spawnare decorazioni (alberi, rocce)
struct DecoratorGenTag {};

} // namespace fw
