#include <iostream>
#include <cstddef>
#include <cstdint>

// Mocks per emulare la compilazione senza l'intero engine
namespace entt {
    using entity = uint32_t;
}

namespace fw {

struct alignas(8) Tile {
    uint16_t zone_type : 3;
    uint16_t density   : 2;
    uint16_t flags     : 11;
    uint16_t network_id;
    uint32_t entity_id;
};
static_assert(sizeof(Tile) == 8, "CRITICAL: Tile size must be exactly 8 bytes (64-bit)");

constexpr int CITY_CHUNK_SIZE = 32;

struct CityChunk {
    Tile tiles[CITY_CHUNK_SIZE * CITY_CHUNK_SIZE];
    bool is_asleep = false;
};

struct alignas(8) CellData {
    float heat;
    float pressure;
};
static_assert(sizeof(CellData) == 8, "CRITICAL: CellData size must be exactly 8 bytes for std430 alignment");

struct alignas(16) ReadbackEvent {
    uint32_t cell_index;
    float event_magnitude;
    uint32_t event_type;
    uint32_t _padding;
};
static_assert(sizeof(ReadbackEvent) == 16, "CRITICAL: ReadbackEvent must be exactly 16 bytes aligned for std430");

} // namespace fw

int main() {
    std::cout << "--- Memory Alignment & Size Validation Test ---" << std::endl;
    
    std::cout << "sizeof(fw::Tile) = " << sizeof(fw::Tile) << " bytes (Expected 8)" << std::endl;
    std::cout << "sizeof(fw::CityChunk) = " << sizeof(fw::CityChunk) << " bytes" << std::endl;
    std::cout << "sizeof(fw::CellData) = " << sizeof(fw::CellData) << " bytes (Expected 8)" << std::endl;
    std::cout << "sizeof(fw::ReadbackEvent) = " << sizeof(fw::ReadbackEvent) << " bytes (Expected 16)" << std::endl;
    
    std::cout << "\nOffsets in ReadbackEvent:" << std::endl;
    std::cout << "  cell_index offset: " << offsetof(fw::ReadbackEvent, cell_index) << std::endl;
    std::cout << "  event_magnitude offset: " << offsetof(fw::ReadbackEvent, event_magnitude) << std::endl;
    std::cout << "  event_type offset: " << offsetof(fw::ReadbackEvent, event_type) << std::endl;
    std::cout << "  _padding offset: " << offsetof(fw::ReadbackEvent, _padding) << std::endl;

    std::cout << "\nTest SUCCESSFUL. All alignments meet the strict std430 standards." << std::endl;
    return 0;
}
