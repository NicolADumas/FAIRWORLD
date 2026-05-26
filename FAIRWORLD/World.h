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
};

// Struttura vertice — deve corrispondere al layout dello shader
struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;
    float texIndex;
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

    void AddFace(int x, int y, int z, int face, const glm::vec3& color, float texIndex, std::vector<Vertex>& outVerts, std::vector<uint32_t>& outIndices, bool lowerY = false);
    static constexpr glm::vec3 BlockColor(BlockType t);
};
