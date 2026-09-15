#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "World.h" // Assicurati che PlanetType sia definito qui

#include "DimensionsManager.h" // Aggiunto per ChunkMetadata e ChunkCoord
#include "PlanetMath.h"

namespace fw {

enum class MapRegionType : int {
    Forest = 0,
    Desert,
    Tundra,
    Ocean,
    Volcano,
    City,
    Dungeon,
    Portal,
    Flat
};

enum class RegionShape : int {
    Rectangle = 0,
    Circle = 1,
    Rhombus = 2,
    Star = 3
};

struct WaterSettings {
    bool enabled = true;
    int seaLevel = 16;
    uint8_t liquidBlockId = 6; // Water
    uint8_t oceanFloorBlockId = 2; // Sand
};

struct MapRegion {
    glm::vec3 eulerAngles = glm::vec3(0.0f); // X: Latitudine, Y: Longitudine, Z: Roll
    float angularRadius = 0.2f; // Raggio di influenza (in radianti)
    // Legacy 2D grid
    glm::ivec2 rectMin = glm::ivec2(-2, -2);
    glm::ivec2 rectMax = glm::ivec2(2, 2);
    MapRegionType type = MapRegionType::Forest;
    RegionShape shape = RegionShape::Rectangle; // Forma della struttura (Rettangolo, Cerchio, Rombo, Stella)
    std::string label;
    uint32_t seed = 0;
    float gravityModifier = 1.0f;
    float perlinFrequency = 0.03f;
    float treeDensity = 0.5f;
    
    // Configurazione Blocchi
    uint8_t surfaceBlockId = 1;     // Grass
    uint8_t subsurfaceBlockId = 3;  // Dirt
    WaterSettings water;
    
    // Cube-Sphere Grid Mapping
    bool isGridAligned = false;
    int faceIndex = -1;
    int gridX = -1;
    int gridY = -1;
    
    // Generazione sparsa
    bool isBackgroundFill = false; // Se true, questa regione non forza la generazione del chunk, serve solo come base
};

// Struttura serializzabile per una cella della griglia
struct ChunkDataExport {
    ChunkCoord coord;
    ChunkMetadata meta;
};

struct TerrainTemplate {
    std::string id = "default_terrain";
    std::string name = "Nuovo Terreno";
    PlanetSize planetSize = PlanetSize::Small; // Associato al Macro-Chunk
    MapRegionType baseType = MapRegionType::Forest;
    float basePerlinFrequency = 0.03f;
    float baseGravityModifier = 1.0f;
    uint32_t seed = 0;
    float baseAngularRadius = 0.2f; // Estensione spaziale (Raggio Angolare)
    uint8_t baseSurfaceBlockId = 1;     // Valore di default (es. Erba)
    uint8_t baseSubsurfaceBlockId = 3;  // Valore di default (es. Terra)
    WaterSettings water;
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
    
    // Validazione & Auto-Repair
    bool isActive = true; // Se false, il chunk esiste nel salvataggio ma è fuori dai bounds (OOB) e non viene renderizzato
};

struct SpawnPoint {
    std::string name = "Nuovo Spawn";
    int faceIndex = 4; // Default: +Y (Top/Cielo)
    float localX = 0.0f; // Offset dalla faccia
    float localZ = 0.0f;
    float heightOffset = 10.0f; // Altezza di spawn dalla superficie del pianeta
    glm::vec4 color = glm::vec4(1.0f, 0.2f, 0.2f, 1.0f); // Colore del puntatore
};

struct PlanetBaseTerrain {
    MapRegionType biome = MapRegionType::Flat; // Updated biome
    uint32_t surfaceBlock = 0;     // Air / Canvas Neutro
    uint32_t subsurfaceBlock = 0;  // Air / Canvas Neutro
    float perlinFrequency = 0.5f;
    float gravityModifier = 1.0f;
    WaterSettings water;
};

struct PlanetMap {
    PlanetType type;
    std::string name;
    std::vector<MapRegion> regions; // Legacy / Fallback
    std::vector<PlanetChunkInstance> chunkInstances;
    std::vector<SpawnPoint> spawnPoints;
    
    PlanetBaseTerrain baseTerrain;
    
    PlanetSize planetSize = PlanetSize::Medium; // Sostituisce planetRadius
    bool isFlat = false;        // Sostituisce il check planetRadius <= 0.0f
    float axialTilt = 23.44f;   // Inclinazione asse terrestre (gradi)
    float yearLength = 365.0f;  // Durata dell'anno (in giorni)
    
    // Parametri DimensionsManager per Rigid Grid Map
    int32_t minX = -16;
    int32_t maxX = 16;
    int32_t minY = 0;
    int32_t maxY = 128;
    int32_t minZ = -16;
    int32_t maxZ = 16;
    std::vector<ChunkDataExport> chunkOverrides; // Solo i chunk custom

    // --- Dirty Tracking per GPU Upload Incrementale ---
    std::vector<uint32_t> dirtyChunkIndices;
    bool structuralDirty = true; // True all'avvio per forzare un full upload iniziale

    void MarkChunkDirty(uint32_t index) {
        // Evita duplicati (potrebbe essere ottimizzato in futuro se necessario)
        if (std::find(dirtyChunkIndices.begin(), dirtyChunkIndices.end(), index) == dirtyChunkIndices.end()) {
            dirtyChunkIndices.push_back(index);
        }
    }

    void MarkStructuralChange() {
        structuralDirty = true;
        dirtyChunkIndices.clear(); // Il full rebuild copre tutto, non servono i parziali
    }
};

struct DocumentValidationResult {
    bool changed = false;
    uint32_t outOfBoundsHidden = 0;
    uint32_t outOfBoundsRestored = 0;
    uint32_t missingTemplatesFixed = 0;
    uint32_t duplicatesRemoved = 0;
};

struct MapDocument {
    bool isCompiled = false;
    std::vector<TerrainTemplate> terrainLibrary;
    std::vector<PlanetMap> planets;

    // Validazione & Auto-Repair Intelligente
    static DocumentValidationResult ValidateAndRepairDocument(MapDocument& doc);

    // Dichiarazione dei metodi di I/O
    bool SaveJSON(const std::string& path);
    bool LoadJSON(const std::string& path);

    // Formato binario rapido (.fwb - FairWorld Binary)
    bool SaveBinary(const std::string& path) const;
    bool LoadBinary(const std::string& path);

    // Helper: carica il binario se esiste e aggiornato, altrimenti JSON (e genera binario)
    bool LoadSmart(const std::string& jsonPath);
};

} // namespace fw
