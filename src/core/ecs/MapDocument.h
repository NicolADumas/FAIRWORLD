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
    glm::vec2 center;       // Coordinate normalizzate [0..1]
    float radius;           // Raggio d'influenza
    MapRegionType type;
    std::string label;
    uint32_t seed;
    float gravityModifier = 1.0f;
    float perlinFrequency = 0.03f;
    float treeDensity = 0.5f;
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
    int32_t minX = -6;
    int32_t maxX = 6;
    int32_t minZ = -6;
    int32_t maxZ = 6;
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
