#pragma once
#include <vector>
#include <mutex>
#include <cstdint>
#include <unordered_map>
#include <cassert>

namespace fw {

// =====================================================================
// VramSlabAllocator - Multi-Bucket VRAM Manager in O(1)
// =====================================================================
// Gestisce logicamente un monolite di VRAM (es. 512 MB).
// Previene la frammentazione esterna usando bucket a taglia fissa.
// Quando un bucket (es. 16KB) si svuota, richiede una "Page" di VRAM 
// (es. 4MB) dal pool globale e la affetta in N slot esatti.

struct VramAllocation {
    uint32_t offset;    // L'offset in byte dentro il buffer Vulkan DEVICE_LOCAL
    uint32_t size;      // La dimensione allocata (capacità del bucket, >= requested size)
    uint32_t bucketIdx; // Indice del bucket di provenienza
    bool valid = false;
};

class VramSlabAllocator {
public:
    VramSlabAllocator(uint32_t totalMemoryBytes, uint32_t pageSizeBytes = 4 * 1024 * 1024);
    ~VramSlabAllocator();

    // Ritorna l'allocazione O(1) nel bucket appropriato
    VramAllocation Allocate(uint32_t requestedSize);

    // Libera lo slot e lo reinserisce nella pool del bucket in O(1)
    void Free(const VramAllocation& allocation);

    // Statistiche
    uint32_t GetAllocatedBytes() const;
    uint32_t GetWastedBytes() const; // Frammentazione interna (bucket size - requested size)

private:
    struct Bucket {
        uint32_t slotSize;
        std::vector<uint32_t> freeOffsets; // Offset disponibili per questo bucket
    };

    uint32_t m_totalMemory;
    uint32_t m_pageSize;
    
    // Lista delle Pagine da 4MB ancora "vergini" e mai assegnate a nessun bucket
    std::vector<uint32_t> m_freePagesOffsets;
    
    // I bucket disponibili (potenze di 2: 8KB, 16KB, 32KB, 64KB, 128KB, 256KB, 512KB)
    std::vector<Bucket> m_buckets;
    
    std::mutex m_mutex;

    // Statistiche
    uint32_t m_statAllocated = 0;
    uint32_t m_statWasted = 0;

    int GetBucketIndex(uint32_t size) const;
    bool ExpandBucket(int bucketIdx);
};

} // namespace fw
