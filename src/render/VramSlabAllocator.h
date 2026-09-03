#pragma once
#include <vector>
#include <mutex>
#include <cstdint>
#include <unordered_map>
#include <cassert>
#include <functional>

namespace fw {

// =====================================================================
// VramSlabAllocator - Multi-Bucket VRAM Manager in O(1)
// =====================================================================
// Gestisce logicamente un monolite di VRAM (es. 512 MB).
// Previene la frammentazione esterna usando bucket a taglia fissa.
// Quando un bucket (es. 16KB) si svuota, richiede una "Page" di VRAM 
// (es. 4MB) dal pool globale e la affetta in N slot esatti.

using VramAllocationHandle = uint32_t;
constexpr VramAllocationHandle INVALID_VRAM_HANDLE = 0xFFFFFFFF;

struct VramAllocationInfo {
    uint32_t compartmentIdx; // Indice del VkBuffer (Compartimento 256MB)
    uint32_t offset;         // L'offset in byte dentro il compartimento
    uint32_t size;           // La dimensione allocata (capacità del bucket, >= requested size)
    uint32_t bucketIdx;      // Indice del bucket di provenienza
    bool valid = false;
};

class VramSlabAllocator {
public:
    VramSlabAllocator(uint32_t maxMemoryBytes = 2048ULL * 1024ULL * 1024ULL, uint32_t compartmentSizeBytes = 256 * 1024 * 1024, uint32_t pageSizeBytes = 4 * 1024 * 1024);
    ~VramSlabAllocator();

    // Ritorna l'handle O(1) nel bucket appropriato
    VramAllocationHandle Allocate(uint32_t requestedSize);

    // Libera lo slot in O(1)
    void Free(VramAllocationHandle handle);

    // Ritorna le info per il bind a Vulkan
    VramAllocationInfo GetAllocation(VramAllocationHandle handle) const;

    // Statistiche
    uint32_t GetAllocatedBytes() const;
    uint32_t GetWastedBytes() const;

    // Gestione Compartimenti (richiama VulkanMemory)
    void AddCompartment(uint32_t compartmentIdx);
    
    // Callback per avvisare l'engine che serve un nuovo buffer Vulkan da 256MB
    void SetAllocateCompartmentCallback(std::function<void(uint32_t)> callback) {
        m_allocateCompartmentCallback = std::move(callback);
    }

private:
    struct Bucket {
        uint32_t slotSize;
        std::vector<VramAllocationInfo> freeSlots; // Slot disponibili per questo bucket
    };

    std::function<void(uint32_t)> m_allocateCompartmentCallback;

    uint32_t m_maxMemory;
    uint32_t m_compartmentSize;
    uint32_t m_pageSize;
    
    // Pagine allocate fisicamente ma non ancora assegnate a nessun bucket
    std::vector<VramAllocationInfo> m_freePages;
    uint32_t m_allocatedCompartments = 0;
    
    std::vector<Bucket> m_buckets;
    
    // Mappa Handle -> Info
    std::vector<VramAllocationInfo> m_allocations;
    std::vector<VramAllocationHandle> m_freeHandles;

    std::mutex m_mutex;

    // Statistiche
    uint32_t m_statAllocated = 0;
    uint32_t m_statWasted = 0;

    int GetBucketIndex(uint32_t size) const;
    bool ExpandBucket(int bucketIdx);
    bool AllocateNewCompartment();
    VramAllocationHandle CreateHandle(const VramAllocationInfo& info);
};

} // namespace fw
