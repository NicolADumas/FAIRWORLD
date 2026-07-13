#pragma once
#include <cstdint>

namespace fw {

// Struttura per CellData termico
struct alignas(8) CellData {
    float heat;
    float pressure;
};
static_assert(sizeof(CellData) == 8, "CRITICAL: CellData size must be exactly 8 bytes for std430 alignment");

// Struttura che la GPU scriverà nel buffer di output mappato in CPU
// Allineamento a 16 byte essenziale per lettura sicura da buffer std430
struct alignas(16) ReadbackEvent {
    uint32_t cell_index;     // 4 Byte
    float event_magnitude;   // 4 Byte
    uint32_t event_type;     // 4 Byte (0 = Fusione, 1 = Collasso, ecc.)
    uint32_t _padding;       // 4 Byte (Padding esplicito per allineamento perfetto a 16-byte std430)
};
static_assert(sizeof(ReadbackEvent) == 16, "CRITICAL: ReadbackEvent must be exactly 16 bytes aligned for std430");

} // namespace fw
