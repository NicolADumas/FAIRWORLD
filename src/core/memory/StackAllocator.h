#pragma once
#include "IAllocator.h"

namespace fw::memory {

    class StackAllocator : public IAllocator {
    public:
        StackAllocator(size_t totalSize, void* startPtr = nullptr);
        ~StackAllocator() override;

        void* Allocate(size_t size, uint8_t alignment = 8) override;
        
        // Dealloca l'ultimo blocco allocato (deve essere rigorosamente LIFO)
        void Free(void* ptr) override;
        
        void Init() override;

        typedef size_t Marker;
        
        // Restituisce un marker allo stato corrente (l'offset attuale)
        Marker GetMarker() const;
        
        // Riporta lo stack allo stato del marker, scartando tutto ciò che è stato allocato dopo
        void FreeToMarker(Marker marker);
        
        // Azzera completamente lo stack
        void Reset();

    private:
        struct AllocationHeader {
            uint8_t adjustment;
        };

        void* m_StartPtr;
        size_t m_Offset;
        bool m_OwnsMemory;
    };

} // namespace fw::memory
