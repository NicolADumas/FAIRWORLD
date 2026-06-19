#pragma once
#include "IAllocator.h"

namespace fw::memory {

    class PoolAllocator : public IAllocator {
    public:
        // chunkSize è la dimensione di ogni blocco che questo pool gestirà
        PoolAllocator(size_t totalSize, size_t chunkSize, uint8_t chunkAlignment = 8, void* startPtr = nullptr);
        ~PoolAllocator() override;

        void* Allocate(size_t size, uint8_t alignment = 8) override;
        
        // Inserisce il blocco nuovamente nella free list in O(1)
        void Free(void* ptr) override;
        
        void Init() override;

        // Ricostruisce la free list riportando l'allocatore allo stato iniziale
        void Reset();

    private:
        struct FreeNode {
            FreeNode* next;
        };

        void* m_StartPtr;
        size_t m_ChunkSize;
        uint8_t m_ChunkAlignment;
        bool m_OwnsMemory;

        FreeNode* m_FreeList;
    };

} // namespace fw::memory
