#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "World.h" // Assicurati che PlanetType sia definito qui

#include "DimensionsManager.h" // Aggiunto per ChunkMetadata e ChunkCoord

namespace fw {

enum class MapRegionType : int {
    Forest = 0,
    Desert,
    Tundra,
    Ocean,
    Volcano,
    City,
    Dungeon,
    Portal
};

struct MapRegion {
    glm::ivec2 rectMin = glm::ivec2(-2, -2); // Chunk min
    glm::ivec2 rectMax = glm::ivec2(2, 2);   // Chunk max
    MapRegionType type;
    std::string label;
    uint32_t seed;
    float gravityModifier = 1.0f;
    float perlinFrequency = 0.03f;
    float treeDensity = 0.5f;
    
    // Configurazione Blocchi
    uint8_t surfaceBlockId = 1;     // Grass
    uint8_t subsurfaceBlockId = 3;  // Dirt
};

// Struttura serializzabile per una cella della griglia
struct ChunkDataExport {
    ChunkCoord coord;
    ChunkMetadata meta;
};

struct PlanetMap {
    PlanetType type;
    std::string name;
    std::vector<MapRegion> regions;
    
    // Parametri DimensionsManager per Rigid Grid Map
    int32_t minX = -16;
    int32_t maxX = 16;
    int32_t minY = 0;
    int32_t maxY = 128;
    int32_t minZ = -16;
    int32_t maxZ = 16;
    std::vector<ChunkDataExport> chunkOverrides; // Solo i chunk custom
};

struct MapDocument {
    std::vector<PlanetMap> planets;
    bool isCompiled = false;

    // Dichiarazione dei metodi di I/O
    bool SaveJSON(const std::string& path);
    bool LoadJSON(const std::string& path);
};

} // namespace fw
