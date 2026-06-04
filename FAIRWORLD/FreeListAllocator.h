#pragma once
#include "IAllocator.h"

namespace fw::memory {

    class FreeListAllocator : public IAllocator {
    public:
        FreeListAllocator(size_t totalSize, void* startPtr = nullptr);
        ~FreeListAllocator() override;

        void* Allocate(size_t size, uint8_t alignment = 8) override;
        void Free(void* ptr) override;
        void Init() override;

    private:
        struct AllocationHeader {
            size_t size;
            uint8_t adjustment;
        };

        struct FreeNode {
            size_t size;
            FreeNode* next;
        };

        void* m_StartPtr;
        bool m_OwnsMemory;
        FreeNode* m_FreeList;
    };

} // namespace fw::memory
