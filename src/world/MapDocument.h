#pragma once
#include <string>
#include <vector>
#include <optional>
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

// ==========================================
// FASE 4.1: SMART TERRAIN GENERATION RULES
// ==========================================

enum class LayerBlendMode {
    Solid = 0,
    Dithered,
    Noise
};

struct TerrainLayer {
    std::string blockName = "fairworld:stone"; // ZERO MAGIC IDs
    float minDepth = 0.0f; // Profondità relativa alla superficie (es. 0 = appena sotto l'erba)
    float maxDepth = 4.0f; // Fine dello strato
    
    float noiseStrength = 0.0f;
    float noiseScale = 0.03f;
    LayerBlendMode blendMode = LayerBlendMode::Solid;
};

enum class TerrainAlgorithmType : uint8_t {
    Plains,
    Hills,
    Mountains,
    Dunes
};

struct HeightRules {
    TerrainAlgorithmType algorithm = TerrainAlgorithmType::Plains;
    
    // CONTINUOUS (Blend)
    float baseHeight = 25.0f;
    float amplitude = 25.0f;
    float frequency = 0.03f;
    float persistence = 0.5f;
    float lacunarity = 2.0f;
    float macroScale = 1.0f;
    float regionalScale = 1.0f;
    float detailScale = 1.0f;
    float ridgeStrength = 0.0f;
    float valleyStrength = 0.0f;
    
    // DISCRETE (Dominant/Override)
    int octaves = 4;
};

struct HeightRuleOverrides {
    std::optional<TerrainAlgorithmType> algorithm;
    std::optional<float> baseHeight;
    std::optional<float> amplitude;
    std::optional<float> frequency;
    std::optional<float> persistence;
    std::optional<float> lacunarity;
    std::optional<float> macroScale;
    std::optional<float> regionalScale;
    std::optional<float> detailScale;
    std::optional<float> ridgeStrength;
    std::optional<float> valleyStrength;
    std::optional<int> octaves;
};

struct LayerRules {
    std::vector<TerrainLayer> layers;
    std::string coreBlockName = "fairworld:stone"; // Riempie da maxDepth(ultimo strato) fino al fondo
};

struct LayerRuleOverrides {
    std::optional<std::vector<TerrainLayer>> layers;
    std::optional<std::string> coreBlockName;
};

struct CaveTunnelRules {
    float scale = 1.0f;
    float frequency = 0.05f;
    float strength = 1.0f;
    float threshold = 0.55f;
};

struct CaveChamberRules {
    float scale = 1.0f;
    float strength = 1.0f;
    float frequency = 0.02f;
};

struct CaveRules {
    bool enabled = false;
    
    CaveTunnelRules tunnels;
    CaveChamberRules chambers;
    
    float verticalBias = 0.0f;
    float minDepth = 4.0f;
    float maxDepth = 120.0f;
};

struct CaveRuleOverrides {
    std::optional<bool> enabled;
    std::optional<CaveTunnelRules> tunnels;
    std::optional<CaveChamberRules> chambers;
    std::optional<float> verticalBias;
    std::optional<float> minDepth;
    std::optional<float> maxDepth;
};

struct WaterRules {
    bool enabled = false;
    int globalLevel = 16;
    std::string liquidBlockName = "fairworld:water";
    std::string floorBlockName = "fairworld:sand";
    
    float regionalVariation = 0.0f;
    float basinStrength = 0.0f;
    float shorelineFalloff = 0.0f;
};

struct WaterRuleOverrides {
    std::optional<bool> enabled;
    std::optional<int> globalLevel;
    std::optional<std::string> liquidBlockName;
    std::optional<std::string> floorBlockName;
    std::optional<float> regionalVariation;
    std::optional<float> basinStrength;
    std::optional<float> shorelineFalloff;
};

struct BiomeEnvironment {
    float temperature = 0.5f;
    float humidity = 0.5f;
    float moisture = 0.5f;
    float altitude = 0.5f;
    float slope = 0.0f;
    float latitude = 0.0f;
    float waterProximity = 0.0f;
};

struct BiomeEnvironmentOverrides {
    std::optional<float> temperature;
    std::optional<float> humidity;
    std::optional<float> moisture;
    std::optional<float> altitude;
    std::optional<float> slope;
    std::optional<float> latitude;
    std::optional<float> waterProximity;
};

struct BiomeRules {
    BiomeEnvironment environment;
};

struct BiomeRuleOverrides {
    std::optional<BiomeEnvironmentOverrides> environment;
};

struct ErosionRules {
    bool enabled = false;
    float hydraulicErosion = 0.0f;
    float thermalErosion = 0.0f;
    float sedimentation = 0.0f;
    float weathering = 0.0f;
};

struct ErosionRuleOverrides {
    std::optional<bool> enabled;
    std::optional<float> hydraulicErosion;
    std::optional<float> thermalErosion;
    std::optional<float> sedimentation;
    std::optional<float> weathering;
};

struct TerrainGenerationRules {
    HeightRules height;
    LayerRules layers;
    CaveRules caves;
    WaterRules water;
    BiomeRules biome;
    ErosionRules erosion;
};

struct TerrainRuleOverrides {
    std::optional<HeightRuleOverrides> height;
    std::optional<LayerRuleOverrides> layers;
    std::optional<CaveRuleOverrides> caves;
    std::optional<WaterRuleOverrides> water;
    std::optional<BiomeRuleOverrides> biome;
    std::optional<ErosionRuleOverrides> erosion;
};

struct ResolvedTerrainRules {
    TerrainGenerationRules rules;
    
    // Runtime-resolved references for TerrainSolver
    uint32_t resolvedCoreBlock = 0;
    std::vector<uint32_t> resolvedLayerBlocks;
    uint32_t resolvedWaterBlock = 0;
    uint32_t resolvedWaterFloorBlock = 0;
};

// Forward declaration per il registry
class BlockRegistry;

// Funzione di risoluzione (Blend/Override)
ResolvedTerrainRules ResolveTerrainRules(
    const TerrainGenerationRules& baseRules,
    const TerrainRuleOverrides& overrides,
    float influence,
    const BlockRegistry* registry
);

// Hash deterministico delle regole risolte
uint64_t ComputeRuleHash(const ResolvedTerrainRules& resolved);

// ==========================================

// Le vecchie WaterSettings vengono rimosse in favore di WaterRules all'interno di TerrainGenerationRules
// struct WaterSettings è rimosso

struct MapRegion {
    glm::vec3 eulerAngles = glm::vec3(0.0f); // X: Latitudine, Y: Longitudine, Z: Roll
    float angularRadius = 0.2f; // Raggio di influenza (in radianti)
    float influence = 1.0f;
    // Legacy 2D grid
    glm::ivec2 rectMin = glm::ivec2(-2, -2);
    glm::ivec2 rectMax = glm::ivec2(2, 2);
    MapRegionType type = MapRegionType::Forest;
    RegionShape shape = RegionShape::Rectangle; // Forma della struttura (Rettangolo, Cerchio, Rombo, Stella)
    std::string label;
    uint32_t seed = 0;
    
    // OVERRIDES LOCALI (eredita tutto il resto dal Template)
    TerrainRuleOverrides overrides;
    
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

    uint32_t seed = 0;
    float baseAngularRadius = 0.2f; // Estensione spaziale (Raggio Angolare)
    
    // REGOLE DI BASE COMPLETE DEL CHUNK
    TerrainGenerationRules baseRules;
    
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
    TerrainGenerationRules baseRules;
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
