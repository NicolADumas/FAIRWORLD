#include "pch.h"
#include "VramSlabAllocator.h"
#include <iostream>
#include <limits>

namespace fw {
namespace {
uint64_t PackHandle(uint32_t slot, uint32_t generation) {
    return (static_cast<uint64_t>(generation) << 32) | slot;
}
uint32_t HandleSlot(VramAllocationHandle handle) { return static_cast<uint32_t>(handle); }
uint32_t HandleGeneration(VramAllocationHandle handle) { return static_cast<uint32_t>(handle >> 32); }
}

VramSlabAllocator::VramSlabAllocator(uint32_t maxMemoryBytes, uint32_t compartmentSizeBytes, uint32_t pageSizeBytes)
    : m_maxMemory(maxMemoryBytes), m_compartmentSize(compartmentSizeBytes), m_pageSize(pageSizeBytes) {
    m_configurationValid = compartmentSizeBytes != 0 && pageSizeBytes != 0 &&
        pageSizeBytes <= compartmentSizeBytes && compartmentSizeBytes % pageSizeBytes == 0;
    constexpr uint32_t sizes[] = {8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576, 2097152, 4194304};
    for (uint32_t size : sizes) m_buckets.push_back(Bucket{size, {}});
}

VramSlabAllocator::~VramSlabAllocator() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::cout << "[VramSlabAllocator] Distrutto. " << m_statAllocated << " bytes ancora attivi/ritirati/quarantinati.\n";
}

int VramSlabAllocator::GetBucketIndex(uint32_t size) const {
    for (size_t i = 0; i < m_buckets.size(); ++i) {
        if (size <= m_buckets[i].slotSize) return static_cast<int>(i);
    }
    return -1;
}

bool VramSlabAllocator::AllocateNewCompartment() {
    if (!m_configurationValid || m_compartmentSize == 0 || m_pageSize == 0) return false;
    const uint32_t maxCompartments = m_maxMemory / m_compartmentSize;
    if (m_allocatedCompartments >= maxCompartments) {
        std::cerr << "[VRAM] ERROR: Raggiunto il limite massimo di " << (m_maxMemory / (1024 * 1024)) << " MB!\n";
        return false;
    }
    const uint32_t pagesPerCompartment = m_compartmentSize / m_pageSize;
    try {
        m_freePages.reserve(m_freePages.size() + pagesPerCompartment);
    } catch (...) {
        std::cerr << "[VRAM] ERROR: memoria host insufficiente per metadata delle pagine.\n";
        return false;
    }

    const uint32_t newCompartmentIdx = m_allocatedCompartments;
    if (m_allocateCompartmentCallback) {
        bool created = false;
        try { created = m_allocateCompartmentCallback(newCompartmentIdx); }
        catch (...) { created = false; }
        if (!created) {
            std::cerr << "[VRAM] ERROR: creazione del compartimento " << newCompartmentIdx << " fallita.\n";
            return false;
        }
    }

    ++m_allocatedCompartments;
    for (uint32_t i = 0; i < pagesPerCompartment; ++i) {
        VramAllocationInfo pageInfo;
        pageInfo.compartmentIdx = newCompartmentIdx;
        pageInfo.offset = (pagesPerCompartment - 1 - i) * m_pageSize;
        pageInfo.size = m_pageSize;
        pageInfo.valid = true;
        m_freePages.push_back(pageInfo);
    }
    return true;
}

bool VramSlabAllocator::ExpandBucket(int bucketIdx) {
    if (bucketIdx < 0 || static_cast<size_t>(bucketIdx) >= m_buckets.size() || !m_configurationValid) return false;
    if (m_freePages.empty() && !AllocateNewCompartment()) return false;
    const VramAllocationInfo pageInfo = m_freePages.back();
    Bucket& bucket = m_buckets[static_cast<size_t>(bucketIdx)];
    if (!pageInfo.valid || bucket.slotSize == 0 || bucket.slotSize > pageInfo.size) return false;
    const uint32_t slotsInPage = pageInfo.size / bucket.slotSize;
    if (slotsInPage == 0) return false;
    try { bucket.freeSlots.reserve(bucket.freeSlots.size() + slotsInPage); }
    catch (...) { return false; }
    m_freePages.pop_back();
    for (uint32_t i = 0; i < slotsInPage; ++i) {
        VramAllocationInfo slotInfo;
        slotInfo.compartmentIdx = pageInfo.compartmentIdx;
        slotInfo.offset = pageInfo.offset + (slotsInPage - 1 - i) * bucket.slotSize;
        slotInfo.size = bucket.slotSize;
        slotInfo.bucketIdx = static_cast<uint32_t>(bucketIdx);
        slotInfo.valid = true;
        bucket.freeSlots.push_back(slotInfo);
    }
    return true;
}

VramAllocationHandle VramSlabAllocator::CreateHandle(const VramAllocationInfo& info, uint32_t requestedSize) {
    uint32_t slot = 0;
    if (!m_freeHandles.empty()) {
        slot = m_freeHandles.back();
        m_freeHandles.pop_back();
        if (slot >= m_allocations.size()) return INVALID_VRAM_HANDLE;
    } else {
        if (m_allocations.size() >= std::numeric_limits<uint32_t>::max()) return INVALID_VRAM_HANDLE;
        slot = static_cast<uint32_t>(m_allocations.size());
        m_allocations.emplace_back();
    }
    AllocationRecord& record = m_allocations[slot];
    if (record.generation == 0 || record.generation == UINT32_MAX) {
        record.state = VramAllocationState::Invalid;
        return INVALID_VRAM_HANDLE;
    }
    record.info = info;
    record.requestedSize = requestedSize;
    record.retireAfterSerial = 0;
    record.state = VramAllocationState::Active;
    return PackHandle(slot, record.generation);
}

VramSlabAllocator::AllocationRecord* VramSlabAllocator::ResolveHandle(VramAllocationHandle handle) {
    if (handle == INVALID_VRAM_HANDLE) return nullptr;
    const uint32_t slot = HandleSlot(handle);
    if (slot >= m_allocations.size()) return nullptr;
    auto& record = m_allocations[slot];
    if (record.generation != HandleGeneration(handle) || record.state == VramAllocationState::Invalid) return nullptr;
    return &record;
}

const VramSlabAllocator::AllocationRecord* VramSlabAllocator::ResolveHandle(VramAllocationHandle handle) const {
    if (handle == INVALID_VRAM_HANDLE) return nullptr;
    const uint32_t slot = HandleSlot(handle);
    if (slot >= m_allocations.size()) return nullptr;
    const auto& record = m_allocations[slot];
    if (record.generation != HandleGeneration(handle) || record.state == VramAllocationState::Invalid) return nullptr;
    return &record;
}

bool VramSlabAllocator::ReleaseRecord(uint32_t slotIndex, AllocationRecord& record) {
    if (record.info.bucketIdx >= m_buckets.size()) return false;
    Bucket& bucket = m_buckets[record.info.bucketIdx];
    try { bucket.freeSlots.push_back(record.info); }
    catch (...) {
        std::cerr << "[VRAM] ERROR: slot non rilasciato: metadata host insufficiente.\n";
        return false;
    }
    m_statAllocated -= std::min(m_statAllocated, record.requestedSize);
    const uint32_t wasted = record.info.size - record.requestedSize;
    m_statWasted -= std::min(m_statWasted, wasted);
    record.info.valid = false;
    record.requestedSize = 0;
    record.retireAfterSerial = 0;
    record.state = VramAllocationState::Invalid;
    if (record.generation != UINT32_MAX) {
        ++record.generation;
        try { m_freeHandles.push_back(slotIndex); }
        catch (...) { /* The physical slot is free; only this handle-table slot is retired. */ }
    }
    return true;
}

VramAllocationHandle VramSlabAllocator::Allocate(uint32_t requestedSize) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (requestedSize == 0 || !m_configurationValid) return INVALID_VRAM_HANDLE;
    const int bucketIndex = GetBucketIndex(requestedSize);
    if (bucketIndex < 0) {
        std::cerr << "[VRAM] Richiesta (" << requestedSize << " byte) superiore al bucket massimo!\n";
        return INVALID_VRAM_HANDLE;
    }
    Bucket& bucket = m_buckets[static_cast<size_t>(bucketIndex)];
    if (bucket.slotSize > m_pageSize) return INVALID_VRAM_HANDLE;
    if (bucket.freeSlots.empty() && !ExpandBucket(bucketIndex)) return INVALID_VRAM_HANDLE;
    const VramAllocationInfo slotInfo = bucket.freeSlots.back();
    bucket.freeSlots.pop_back();
    VramAllocationHandle handle = INVALID_VRAM_HANDLE;
    try { handle = CreateHandle(slotInfo, requestedSize); }
    catch (...) { handle = INVALID_VRAM_HANDLE; }
    if (handle == INVALID_VRAM_HANDLE) {
        bucket.freeSlots.push_back(slotInfo);
        return INVALID_VRAM_HANDLE;
    }
    m_statAllocated += requestedSize;
    m_statWasted += slotInfo.size - requestedSize;
    return handle;
}

void VramSlabAllocator::Free(VramAllocationHandle handle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    AllocationRecord* record = ResolveHandle(handle);
    if (!record || record->state != VramAllocationState::Active) {
        std::cerr << "[VRAM] Free ignorato per handle stale o non attivo: " << handle << "\n";
        return;
    }
    ReleaseRecord(HandleSlot(handle), *record);
}

bool VramSlabAllocator::Retire(VramAllocationHandle handle, uint64_t lastGraphicsUseSerial) {
    std::lock_guard<std::mutex> lock(m_mutex);
    AllocationRecord* record = ResolveHandle(handle);
    if (!record || record->state != VramAllocationState::Active) return false;
    record->state = VramAllocationState::Retired;
    record->retireAfterSerial = lastGraphicsUseSerial;
    return true;
}

size_t VramSlabAllocator::CollectRetired(uint64_t completedGraphicsSerial) {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t released = 0;
    for (uint32_t i = 0; i < m_allocations.size(); ++i) {
        auto& record = m_allocations[i];
        if (record.state == VramAllocationState::Retired && record.retireAfterSerial <= completedGraphicsSerial) {
            if (ReleaseRecord(i, record)) ++released;
        }
    }
    return released;
}

bool VramSlabAllocator::Quarantine(VramAllocationHandle handle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    AllocationRecord* record = ResolveHandle(handle);
    if (!record || record->state != VramAllocationState::Active) return false;
    record->state = VramAllocationState::Quarantined;
    return true;
}

bool VramSlabAllocator::ReleaseQuarantined(VramAllocationHandle handle, bool completionConfirmed) {
    if (!completionConfirmed) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    AllocationRecord* record = ResolveHandle(handle);
    if (!record || record->state != VramAllocationState::Quarantined) return false;
    return ReleaseRecord(HandleSlot(handle), *record);
}

VramAllocationInfo VramSlabAllocator::GetAllocation(VramAllocationHandle handle) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const AllocationRecord* record = ResolveHandle(handle);
    return record ? record->info : VramAllocationInfo{};
}

VramAllocationState VramSlabAllocator::GetAllocationState(VramAllocationHandle handle) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const AllocationRecord* record = ResolveHandle(handle);
    return record ? record->state : VramAllocationState::Invalid;
}

uint32_t VramSlabAllocator::GetAllocatedBytes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_statAllocated;
}
uint32_t VramSlabAllocator::GetWastedBytes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_statWasted;
}
uint32_t VramSlabAllocator::GetCompartmentCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_allocatedCompartments;
}
void VramSlabAllocator::AddCompartment(uint32_t) {}
void VramSlabAllocator::SetAllocateCompartmentCallback(std::function<bool(uint32_t)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_allocateCompartmentCallback = std::move(callback);
}

} // namespace fw
