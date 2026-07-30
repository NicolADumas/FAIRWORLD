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

enum class RegionShape : int {
    Rectangle = 0,
    Circle = 1,
    Rhombus = 2,
    Star = 3
};

struct MapRegion {
    glm::vec3 eulerAngles = glm::vec3(0.0f); // X: Latitudine, Y: Longitudine, Z: Roll
    float angularRadius = 0.2f; // Raggio di influenza (in radianti)
    // Legacy 2D grid
    glm::ivec2 rectMin = glm::ivec2(-2, -2);
    glm::ivec2 rectMax = glm::ivec2(2, 2);
    MapRegionType type;
    RegionShape shape = RegionShape::Rectangle; // Forma della struttura (Rettangolo, Cerchio, Rombo, Stella)
    std::string label;
    uint32_t seed;
    float gravityModifier = 1.0f;
    float perlinFrequency = 0.03f;
    float treeDensity = 0.5f;
    
    // Configurazione Blocchi
    uint8_t surfaceBlockId = 1;     // Grass
    uint8_t subsurfaceBlockId = 3;  // Dirt
    
    // Cube-Sphere Grid Mapping
    bool isGridAligned = false;
    int faceIndex = -1;
    int gridX = -1;
    int gridY = -1;
};

// Struttura serializzabile per una cella della griglia
struct ChunkDataExport {
    ChunkCoord coord;
    ChunkMetadata meta;
};

struct TerrainTemplate {
    std::string id = "default_terrain";
    std::string name = "Nuovo Terreno";
    MapRegionType baseType = MapRegionType::Forest;
    float basePerlinFrequency = 0.03f;
    float baseGravityModifier = 1.0f;
    uint32_t seed = 0;
    float baseAngularRadius = 0.2f; // Estensione spaziale (Raggio Angolare)
    std::vector<MapRegion> subRegions; // 2D layout (dettagli dipinti)
};

struct PlanetChunkInstance {
    std::string name = "Nuova Zona"; // Nuovo campo per il nome personalizzato
    std::string templateId; // Riferimento al TerrainTemplate
    glm::vec3 eulerAngles = glm::vec3(0.0f); // X: Latitudine (Pitch), Y: Longitudine (Yaw), Z: Rotazione Locale (Roll)
    float angularRadius = 0.2f;
    
    // Nuovi campi per la Legge della Superficie Sferica (Tabella Chunk Excel)
    bool isGridAligned = false; 
    int faceIndex = -1; // 0: +Z (Front), 1: -Z (Back), 2: +X (Right), 3: -X (Left), 4: +Y (Top), 5: -Y (Bottom)
    int gridX = -1;
    int gridY = -1;
};

struct PlanetMap {
    PlanetType type;
    std::string name;
    std::vector<MapRegion> regions; // Legacy / Fallback
    std::vector<PlanetChunkInstance> chunkInstances;
    
    float planetRadius = 50.0f; // Raggio del pianeta
    
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
    std::vector<TerrainTemplate> terrainLibrary;
    std::vector<PlanetMap> planets;
    bool isCompiled = false;

    // Dichiarazione dei metodi di I/O
    bool SaveJSON(const std::string& path);
    bool LoadJSON(const std::string& path);
};

} // namespace fw
