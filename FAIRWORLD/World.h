#pragma once
#include "glm/glm.hpp"
#include <vector>
#include <array>
#include <cstdint>
#include "AIAssistant.h"

#include <unordered_map>
#include <memory>

// Dimensioni del Chunk
static constexpr int CHUNK_SIZE = 16;   // Larghezza/Profondità (X/Z)
static constexpr int CHUNK_HEIGHT = 128; // Altezza (Y)

// Tipi di blocco
enum class BlockType : uint8_t {
    Air         = 0,
    Grass       = 1,
    Dirt        = 2,
    Stone       = 3,
    Wood        = 4,  // Tronco d'albero
    Sand        = 5,  // Sabbia
    Water       = 6,  // Acqua
    Lava        = 7,  // Lava
    Leaves      = 8,  // Foglie
    MobSpawner  = 9,  // Spawner di mob (tool speciale)
    LightSource = 10, // Sorgente di luce (torcia)
    Mushroom    = 11, // Fungo bioluminescente (sottosuolo)
    Ore         = 12, // Minerale grezzo
    Ice         = 13, // Ghiaccio
    StargateFrame = 14, // Cornice del portale
    StargatePortal = 15, // Portale interplanetario
};

// Struttura vertice — deve corrispondere al layout dello shader
struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;
    float texIndex;
};

enum class PlanetType {
    EarthPrime,
    MarsDesolation,
    Glacies
};

struct PlanetDef {
    PlanetType type;
    float gravity;         // Gravità (es. 9.81 per Terra)
    float baseTemp;        // Temperatura in Kelvin (es. 293 per Terra)
    glm::vec3 skyNight;    // Colore cielo notturno
    glm::vec3 skyDay;      // Colore cielo diurno
    
    // Parametri Orbitali (Kepler)
    double a; // Semiasse maggiore
    double e; // Eccentricità
    double T; // Periodo orbitale
    double M0; // Anomalia media al tempo t=0
    
    // Stato Orbitale Corrente
    glm::vec3 currentPosition; // Posizione eliocentrica nel piano orbitale
};

struct ChunkCoord {
    int x, z;
    bool operator==(const ChunkCoord& o) const { return x == o.x && z == o.z; }
};

struct ChunkHash {
    std::size_t operator()(const ChunkCoord& c) const {
        return std::hash<int>()(c.x) ^ (std::hash<int>()(c.z) << 1);
    }
};

class Chunk {
public:
    int cx, cz;
    uint8_t blocks[CHUNK_SIZE][CHUNK_HEIGHT][CHUNK_SIZE]{};

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    bool isDirty = true;
    bool isMeshEmpty = true; // Optimization

    // --- TERMODINAMICA SI ---
    // N = Numero effettivo di particelle (atomi/molecole) stimate nel chunk
    // k_B = Costante di Boltzmann (1.380649e-23 J/K)
    // C = Capacità termica = 3 * N * k_B (J/K) per la Legge di Dulong-Petit (Solidi)
    double heatCapacity = 100000.0; // J/K di default per il chunk
    
    // E = Energia interna totale del chunk (Joule)
    double internalEnergy = 293.15 * 100000.0; // Inizializzato a ~20 C
    
    // T = Temperatura calcolata (Kelvin). T = E / C
    double temperature = 293.15; 
    
    // Accumulatore di calore latente (Joule) per transizioni (es. Acqua <-> Ghiaccio)
    double latentHeatAccumulator = 0.0;
    
    // Calcolo helper
    void UpdateTemperature() {
        if (heatCapacity > 0.0) temperature = internalEnergy / heatCapacity;
    }

    Chunk(int x, int z) : cx(x), cz(z) {}
};

class World {
public:
    World();

    // Accesso ai blocchi globale
    BlockType GetBlock(int x, int y, int z) const;
    void SetBlock(int x, int y, int z, BlockType type);
    bool IsInBounds(int x, int y, int z) const;

    // Gestione Chunk Dinamici
    // Genera l'intera mappa RPG una sola volta
    void InitWorld();

    // --- Sistema Planetario (Opzione C) ---
    std::vector<PlanetDef> m_solarSystem;
    PlanetType m_currentPlanetType;
    const PlanetDef* GetCurrentPlanet() const;
    
    void ChangePlanet(PlanetType newPlanet);
    void SimulateOrbits(float deltaSeconds);


    // --- Ciclo Giorno / Notte (Fase 2) ---
    float m_timeOfDay = 0.5f; // 0.0 = mezzanotte, 0.5 = mezzogiorno, 1.0 = fine giorno
    void AdvanceTime(float deltaSeconds);
    float GetSunIntensity() const;
    glm::vec3 GetSkyColor() const;

    // --- Fisica dell'acqua (Fase 4) ---
    void SimulateWaterTick();
    float m_waterTickAccum = 0.0f;

    // --- Termodinamica (Fase 6) ---
    void SimulateThermodynamicsTick();
    float m_thermoTickAccum = 0.0f;
    float GetTemperatureAt(int x, int z) const;

    // Ricalcola solo i chunk sporchi e restituisce le coordinate dei chunk aggiornati
    std::vector<ChunkCoord> BuildDirtyChunks();
    
    // Ritorna l'elenco dei chunk (per il render manager)
    const std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkHash>& GetChunks() const { return m_chunks; }
    Chunk* GetChunk(int cx, int cz);

    void BuildGhostMesh(const std::vector<GhostBlock>& ghosts);
    const std::vector<Vertex>&   GetGhostVertices() const { return m_ghostVertices; }
    const std::vector<uint32_t>& GetGhostIndices()  const { return m_ghostIndices;  }

private:
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkHash> m_chunks;
    
    std::vector<Vertex>   m_ghostVertices;
    std::vector<uint32_t> m_ghostIndices;
    
    void GenerateChunk(Chunk* chunk);
    void PlaceTree(int x, int y, int z);
    void CarveRiver(class PerlinNoise& pn, Chunk* chunk);

    void AddFace(int x, int y, int z, int face, const glm::vec3& color, float texIndex, std::vector<Vertex>& outVerts, std::vector<uint32_t>& outIndices, bool lowerY = false);
    static constexpr glm::vec3 BlockColor(BlockType t);
};
