#pragma once
#include <cstdint>

namespace fw {

// Dimensione esatta: 64 bit (8 Byte)
struct alignas(8) Tile {
    uint16_t zone_type : 8;   // 256 Zone personalizzabili
    uint16_t density   : 2;   // Bassa, Media, Alta, Ultra
    uint16_t flags     : 6;   // 6 flag liberi (Powered, Watered, Road...)
    uint16_t network_id;      // Rete idrica/elettrica
    uint32_t entity_id;       // ID entità EnTT (coincide con entt::entity)
};
static_assert(sizeof(Tile) == 8, "CRITICAL: Tile size must be exactly 8 bytes (64-bit)");

constexpr int CITY_CHUNK_SIZE = 32;

struct CityChunk {
    Tile tiles[CITY_CHUNK_SIZE * CITY_CHUNK_SIZE];
    bool is_asleep = false; // Culling logico per il Job System
    
    // Accesso tramite (x, y) locali [0, CITY_CHUNK_SIZE - 1]
    [[nodiscard]] inline Tile& GetTile(int localX, int localY) {
        return tiles[localY * CITY_CHUNK_SIZE + localX];
    }
    
    [[nodiscard]] inline const Tile& GetTile(int localX, int localY) const {
        return tiles[localY * CITY_CHUNK_SIZE + localX];
    }
    
    // Flat index mapping
    [[nodiscard]] inline int GetFlatIndex(int localX, int localY) const {
        return localY * CITY_CHUNK_SIZE + localX;
    }
    
    [[nodiscard]] inline static void GetCoordsFromFlat(int flatIndex, int& outX, int& outY) {
        outX = flatIndex % CITY_CHUNK_SIZE;
        outY = flatIndex / CITY_CHUNK_SIZE;
    }
};

} // namespace fw
