#pragma once
#include <vector>
#include <mutex>
#include <cstdint>
#include <functional>

namespace fw {

// 32-bit slot index + 32-bit generation. MeshComponent handles are runtime-only;
// world serializers do not persist this value.
using VramAllocationHandle = uint64_t;
constexpr VramAllocationHandle INVALID_VRAM_HANDLE = UINT64_MAX;

struct VramAllocationInfo {
    uint32_t compartmentIdx = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t bucketIdx = 0;
    bool valid = false;
};

enum class VramAllocationState : uint8_t {
    Invalid,
    Active,
    Retired,
    Quarantined
};

class VramSlabAllocator {
public:
    VramSlabAllocator(uint32_t maxMemoryBytes = 2048ULL * 1024ULL * 1024ULL,
        uint32_t compartmentSizeBytes = 256 * 1024 * 1024,
        uint32_t pageSizeBytes = 4 * 1024 * 1024);
    ~VramSlabAllocator();

    VramAllocationHandle Allocate(uint32_t requestedSize);
    void Free(VramAllocationHandle handle); // Immediate release: only for allocations never used by graphics.
    bool Retire(VramAllocationHandle handle, uint64_t lastGraphicsUseSerial);
    size_t CollectRetired(uint64_t completedGraphicsSerial);
    bool Quarantine(VramAllocationHandle handle);
    bool ReleaseQuarantined(VramAllocationHandle handle, bool completionConfirmed);

    VramAllocationInfo GetAllocation(VramAllocationHandle handle) const;
    VramAllocationState GetAllocationState(VramAllocationHandle handle) const;
    uint32_t GetAllocatedBytes() const;
    uint32_t GetWastedBytes() const;
    uint32_t GetCompartmentCount() const;

    void AddCompartment(uint32_t compartmentIdx);
    void SetAllocateCompartmentCallback(std::function<bool(uint32_t)> callback);

private:
    struct Bucket {
        uint32_t slotSize = 0;
        std::vector<VramAllocationInfo> freeSlots;
    };
    struct AllocationRecord {
        VramAllocationInfo info;
        uint32_t generation = 1;
        uint32_t requestedSize = 0;
        uint64_t retireAfterSerial = 0;
        VramAllocationState state = VramAllocationState::Invalid;
    };

    std::function<bool(uint32_t)> m_allocateCompartmentCallback;
    uint32_t m_maxMemory;
    uint32_t m_compartmentSize;
    uint32_t m_pageSize;
    std::vector<VramAllocationInfo> m_freePages;
    uint32_t m_allocatedCompartments = 0;
    std::vector<Bucket> m_buckets;
    std::vector<AllocationRecord> m_allocations;
    std::vector<uint32_t> m_freeHandles;
    mutable std::mutex m_mutex;
    uint32_t m_statAllocated = 0;
    uint32_t m_statWasted = 0;
    bool m_configurationValid = true;

    int GetBucketIndex(uint32_t size) const;
    bool ExpandBucket(int bucketIdx);
    bool AllocateNewCompartment();
    VramAllocationHandle CreateHandle(const VramAllocationInfo& info, uint32_t requestedSize);
    AllocationRecord* ResolveHandle(VramAllocationHandle handle);
    const AllocationRecord* ResolveHandle(VramAllocationHandle handle) const;
    bool ReleaseRecord(uint32_t slotIndex, AllocationRecord& record);
};

} // namespace fw
