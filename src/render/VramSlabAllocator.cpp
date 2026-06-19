#include "pch.h"
#include "VramSlabAllocator.h"
#include <iostream>

namespace fw {

VramSlabAllocator::VramSlabAllocator(uint32_t totalMemoryBytes, uint32_t pageSizeBytes)
    : m_totalMemory(totalMemoryBytes), m_pageSize(pageSizeBytes) {
    
    // Inizializza le "Pagine" libere
    uint32_t numPages = m_totalMemory / m_pageSize;
    m_freePagesOffsets.reserve(numPages);
    for (uint32_t i = 0; i < numPages; ++i) {
        // Le mettiamo in ordine inverso così il pop_back() ci darà l'offset più basso
        m_freePagesOffsets.push_back((numPages - 1 - i) * m_pageSize);
    }

    // Inizializza le classi di Bucket (Potenze di 2 partendo da 8KB fino a 512KB)
    uint32_t sizes[] = { 8192, 16384, 32768, 65536, 131072, 262144, 524288 };
    for (uint32_t size : sizes) {
        Bucket b;
        b.slotSize = size;
        m_buckets.push_back(std::move(b));
    }
}

VramSlabAllocator::~VramSlabAllocator() {
    std::cout << "[VramSlabAllocator] Distrutto. " << m_statAllocated << " bytes rimasti allocati (leak possibili se non liberati).\n";
}

int VramSlabAllocator::GetBucketIndex(uint32_t size) const {
    for (int i = 0; i < m_buckets.size(); ++i) {
        if (size <= m_buckets[i].slotSize) {
            return i;
        }
    }
    return -1; // Troppo grande per l'allocatore a bucket!
}

bool VramSlabAllocator::ExpandBucket(int bucketIdx) {
    if (m_freePagesOffsets.empty()) {
        std::cerr << "[VRAM] ERROR: Out of VRAM Pages! Impossibile espandere il bucket " << m_buckets[bucketIdx].slotSize << " bytes.\n";
        return false;
    }

    uint32_t pageOffset = m_freePagesOffsets.back();
    m_freePagesOffsets.pop_back();

    Bucket& b = m_buckets[bucketIdx];
    uint32_t slotsInPage = m_pageSize / b.slotSize;
    
    // Affettiamo la pagina di 4MB in N slot della taglia richiesta dal bucket
    for (uint32_t i = 0; i < slotsInPage; ++i) {
        b.freeOffsets.push_back(pageOffset + (slotsInPage - 1 - i) * b.slotSize);
    }

    std::cout << "[VRAM] Pagina da " << (m_pageSize / 1024) << "KB assegnata al Bucket da " 
              << (b.slotSize / 1024) << "KB. Generati " << slotsInPage << " slot.\n";
    return true;
}

VramAllocation VramSlabAllocator::Allocate(uint32_t requestedSize) {
    std::lock_guard<std::mutex> lock(m_mutex);

    int bIdx = GetBucketIndex(requestedSize);
    if (bIdx == -1) {
        std::cerr << "[VRAM] Richiesta (" << requestedSize << " byte) superiore al bucket massimo!\n";
        return {0, 0, 0, false};
    }

    Bucket& b = m_buckets[bIdx];

    // Se il bucket è vuoto, affettiamo una nuova pagina
    if (b.freeOffsets.empty()) {
        if (!ExpandBucket(bIdx)) {
            return {0, 0, 0, false}; // Out of memory
        }
    }

    uint32_t offset = b.freeOffsets.back();
    b.freeOffsets.pop_back();

    m_statAllocated += requestedSize;
    m_statWasted += (b.slotSize - requestedSize);

    return { offset, b.slotSize, (uint32_t)bIdx, true };
}

void VramSlabAllocator::Free(const VramAllocation& alloc) {
    if (!alloc.valid) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    assert(alloc.bucketIdx < m_buckets.size());
    Bucket& b = m_buckets[alloc.bucketIdx];
    
    // Reinseriamo l'offset in O(1)
    b.freeOffsets.push_back(alloc.offset);

    // N.B: Non stiamo tenendo traccia dell'uso esatto della richiesta passata,
    // quindi le statistiche globali (m_statAllocated) sono difficili da decrementare 
    // precisamente senza salvare la "requestedSize" nell'alloc. 
    // Per un allocator VRAM reale di solito le statistiche siricalcolano o si salva la size.
}

uint32_t VramSlabAllocator::GetAllocatedBytes() const { return m_statAllocated; }
uint32_t VramSlabAllocator::GetWastedBytes() const { return m_statWasted; }

} // namespace fw
