#pragma once

#include <unordered_map>
#include <cstdint>
#include <functional>

namespace fw {

enum class ChunkType : uint8_t {
    Normal = 0,
    SafeZone,
    BossArena,
    OuterBoundary // Per il rendering dell'orizzonte non interattivo
};

struct ChunkMetadata {
    ChunkType type = ChunkType::Normal;
    uint16_t biomeID = 0;
    bool canSpawnMobs = true;
    bool isDestructible = true;
};

// Struttura per l'hashing veloce delle coordinate della griglia
struct ChunkCoord {
    int32_t x;
    int32_t z;

    bool operator==(const ChunkCoord& other) const {
        return x == other.x && z == other.z;
    }
};

struct ChunkCoordHash {
    std::size_t operator()(const ChunkCoord& coord) const noexcept {
        // Combinazione dei bit (Szudzik's o Jenkins hash semplice per performance)
        return std::hash<int32_t>{}(coord.x) ^ (std::hash<int32_t>{}(coord.z) << 1);
    }
};

class DimensionsManager {
public:
    DimensionsManager() = default;

    void SetBounds(int32_t minX, int32_t maxX, int32_t minZ, int32_t maxZ);
    void SetChunkMetadata(int32_t cx, int32_t cz, const ChunkMetadata& meta);
    
    [[nodiscard]] bool IsOutOfBounds(int32_t cx, int32_t cz) const;
    [[nodiscard]] const ChunkMetadata* GetChunkMetadata(int32_t cx, int32_t cz) const;

    [[nodiscard]] int32_t GetMinX() const { return m_minX; }
    [[nodiscard]] int32_t GetMaxX() const { return m_maxX; }
    [[nodiscard]] int32_t GetMinZ() const { return m_minZ; }
    [[nodiscard]] int32_t GetMaxZ() const { return m_maxZ; }

    [[nodiscard]] const std::unordered_map<ChunkCoord, ChunkMetadata, ChunkCoordHash>& GetAllMetadata() const { return m_chunksGrid; }

private:
    int32_t m_minX = -6; // Default safe values
    int32_t m_maxX = 6;
    int32_t m_minZ = -6;
    int32_t m_maxZ = 6;

    std::unordered_map<ChunkCoord, ChunkMetadata, ChunkCoordHash> m_chunksGrid;
};

} // namespace fw
