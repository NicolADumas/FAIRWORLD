#include "pch.h"
#include "VramSlabAllocator.h"
#include <iostream>

namespace fw {

VramSlabAllocator::VramSlabAllocator(uint32_t maxMemoryBytes, uint32_t compartmentSizeBytes, uint32_t pageSizeBytes)
    : m_maxMemory(maxMemoryBytes), m_compartmentSize(compartmentSizeBytes), m_pageSize(pageSizeBytes) {
    
    // Inizializza le classi di Bucket (Potenze di 2 partendo da 8KB fino a 4MB)
    uint32_t sizes[] = { 8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576, 2097152, 4194304 };
    for (uint32_t size : sizes) {
        Bucket b;
        b.slotSize = size;
        m_buckets.push_back(std::move(b));
    }
}

VramSlabAllocator::~VramSlabAllocator() {
    std::cout << "[VramSlabAllocator] Distrutto. " << m_statAllocated << " bytes rimasti allocati.\n";
}

int VramSlabAllocator::GetBucketIndex(uint32_t size) const {
    for (int i = 0; i < m_buckets.size(); ++i) {
        if (size <= m_buckets[i].slotSize) {
            return i;
        }
    }
    return -1; // Troppo grande per l'allocatore a bucket!
}

bool VramSlabAllocator::AllocateNewCompartment() {
    uint32_t maxCompartments = m_maxMemory / m_compartmentSize;
    if (m_allocatedCompartments >= maxCompartments) {
        std::cerr << "[VRAM] ERROR: Raggiunto il limite massimo di " << (m_maxMemory / (1024*1024)) << " MB!\n";
        return false;
    }

    uint32_t newCompartmentIdx = m_allocatedCompartments;
    m_allocatedCompartments++;

    std::cout << "[VRAM] Allocazione dinamica nuovo compartimento: " << newCompartmentIdx << " (256MB)\n";

    // Chiama il VulkanMemory per creare il VkBuffer fisico
    if (m_allocateCompartmentCallback) {
        m_allocateCompartmentCallback(newCompartmentIdx);
    }

    // Aggiungi le nuove pagine logiche (da 4MB) per questo compartimento
    uint32_t pagesPerCompartment = m_compartmentSize / m_pageSize;
    for (uint32_t i = 0; i < pagesPerCompartment; ++i) {
        VramAllocationInfo pageInfo;
        pageInfo.compartmentIdx = newCompartmentIdx;
        // Le mettiamo in ordine inverso così il pop_back() ci darà l'offset più basso
        pageInfo.offset = (pagesPerCompartment - 1 - i) * m_pageSize;
        pageInfo.size = m_pageSize;
        pageInfo.valid = true;
        m_freePages.push_back(pageInfo);
    }

    return true;
}

bool VramSlabAllocator::ExpandBucket(int bucketIdx) {
    if (m_freePages.empty()) {
        if (!AllocateNewCompartment()) {
            return false;
        }
    }

    VramAllocationInfo pageInfo = m_freePages.back();
    m_freePages.pop_back();

    Bucket& b = m_buckets[bucketIdx];
    uint32_t slotsInPage = m_pageSize / b.slotSize;
    
    // Affettiamo la pagina di 4MB in N slot della taglia richiesta dal bucket
    for (uint32_t i = 0; i < slotsInPage; ++i) {
        VramAllocationInfo slotInfo;
        slotInfo.compartmentIdx = pageInfo.compartmentIdx;
        slotInfo.offset = pageInfo.offset + (slotsInPage - 1 - i) * b.slotSize;
        slotInfo.size = b.slotSize;
        slotInfo.bucketIdx = bucketIdx;
        slotInfo.valid = true;
        b.freeSlots.push_back(slotInfo);
    }

    return true;
}

VramAllocationHandle VramSlabAllocator::CreateHandle(const VramAllocationInfo& info) {
    if (!m_freeHandles.empty()) {
        VramAllocationHandle handle = m_freeHandles.back();
        m_freeHandles.pop_back();
        m_allocations[handle] = info;
        return handle;
    }
    
    VramAllocationHandle handle = static_cast<VramAllocationHandle>(m_allocations.size());
    m_allocations.push_back(info);
    return handle;
}

VramAllocationHandle VramSlabAllocator::Allocate(uint32_t requestedSize) {
    std::lock_guard<std::mutex> lock(m_mutex);

    int bIdx = GetBucketIndex(requestedSize);
    if (bIdx == -1) {
        std::cerr << "[VRAM] Richiesta (" << requestedSize << " byte) superiore al bucket massimo!\n";
        return INVALID_VRAM_HANDLE;
    }

    Bucket& b = m_buckets[bIdx];

    // Se il bucket è vuoto, espandiamolo con una nuova pagina (potrebbe allocare un nuovo compartimento fisico)
    if (b.freeSlots.empty()) {
        if (!ExpandBucket(bIdx)) {
            return INVALID_VRAM_HANDLE; // Out of memory
        }
    }

    VramAllocationInfo slotInfo = b.freeSlots.back();
    b.freeSlots.pop_back();

    m_statAllocated += requestedSize;
    m_statWasted += (b.slotSize - requestedSize);

    return CreateHandle(slotInfo);
}

void VramSlabAllocator::Free(VramAllocationHandle handle) {
    if (handle == INVALID_VRAM_HANDLE) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    if (handle >= m_allocations.size() || !m_allocations[handle].valid) {
        std::cerr << "[VRAM ERROR] Tentativo di liberare un handle non valido: " << handle << "\n";
        return;
    }

    VramAllocationInfo& info = m_allocations[handle];
    
    assert(info.bucketIdx < m_buckets.size());
    Bucket& b = m_buckets[info.bucketIdx];
    
    // Reinseriamo lo slot in O(1)
    b.freeSlots.push_back(info);
    
    // Invalida l'handle e riciclalo
    info.valid = false;
    m_freeHandles.push_back(handle);
}

VramAllocationInfo VramSlabAllocator::GetAllocation(VramAllocationHandle handle) const {
    if (handle != INVALID_VRAM_HANDLE && handle < m_allocations.size()) {
        return m_allocations[handle];
    }
    return VramAllocationInfo{};
}

void VramSlabAllocator::AddCompartment(uint32_t compartmentIdx) {
    // Usato se VulkanMemory pre-alloca, ma ora usiamo il callback AllocateNewCompartment
}

uint32_t VramSlabAllocator::GetAllocatedBytes() const { return m_statAllocated; }
uint32_t VramSlabAllocator::GetWastedBytes() const { return m_statWasted; }

} // namespace fw
