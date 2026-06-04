#include "pch.h"
#include "StackAllocator.h"
#include <cstdlib>
#include <cassert>

namespace fw::memory {

    StackAllocator::StackAllocator(size_t totalSize, void* startPtr)
        : IAllocator(totalSize), m_StartPtr(startPtr), m_Offset(0), m_OwnsMemory(false) {
    }

    StackAllocator::~StackAllocator() {
        if (m_OwnsMemory && m_StartPtr != nullptr) {
            std::free(m_StartPtr);
            m_StartPtr = nullptr;
        }
    }

    void StackAllocator::Init() {
        if (m_StartPtr == nullptr) {
            m_StartPtr = std::malloc(m_TotalSize);
            m_OwnsMemory = true;
        }
    }

    void* StackAllocator::Allocate(size_t size, uint8_t alignment) {
        assert(size != 0);
        assert(m_StartPtr != nullptr && "StackAllocator non inizializzato.");

        uintptr_t currentAddress = reinterpret_cast<uintptr_t>(m_StartPtr) + m_Offset;
        
        // L'allocazione necessita di spazio aggiuntivo per memorizzare l'header (l'aggiustamento)
        uint8_t adjustment = AlignForwardAdjustmentWithHeader(
            reinterpret_cast<void*>(currentAddress), alignment, sizeof(AllocationHeader));

        if (m_Offset + adjustment + size > m_TotalSize) {
            return nullptr; // Out of memory
        }

        uintptr_t alignedAddress = currentAddress + adjustment;
        
        // Scriviamo l'header subito prima dell'indirizzo allineato
        AllocationHeader* header = reinterpret_cast<AllocationHeader*>(alignedAddress - sizeof(AllocationHeader));
        header->adjustment = adjustment;

        m_Offset += adjustment + size;
        m_Used = m_Offset;
        if (m_Used > m_Peak) {
            m_Peak = m_Used;
        }
        m_NumAllocations++;

        return reinterpret_cast<void*>(alignedAddress);
    }

    void StackAllocator::Free(void* ptr) {
        assert(ptr != nullptr);
        
        uintptr_t ptrAddress = reinterpret_cast<uintptr_t>(ptr);
        
        // Recuperiamo l'header
        AllocationHeader* header = reinterpret_cast<AllocationHeader*>(ptrAddress - sizeof(AllocationHeader));
        
        // Calcoliamo l'offset iniziale di questo blocco
        size_t prevOffset = (ptrAddress - header->adjustment) - reinterpret_cast<uintptr_t>(m_StartPtr);
        
        // Impostiamo l'offset a quello che avevamo prima dell'allocazione
        m_Offset = prevOffset;
        m_Used = m_Offset;
        
        if (m_NumAllocations > 0) {
            m_NumAllocations--;
        }
    }

    StackAllocator::Marker StackAllocator::GetMarker() const {
        return m_Offset;
    }

    void StackAllocator::FreeToMarker(Marker marker) {
        assert(marker <= m_Offset && "Marker non valido (più grande dell'offset corrente)");
        m_Offset = marker;
        m_Used = m_Offset;
        // La statistica m_NumAllocations perde precisione con il roll-back dei marker,
        // ma in uno stack allocator il tracking dell'uso della memoria (m_Used) è più importante.
    }

    void StackAllocator::Reset() {
        m_Offset = 0;
        m_Used = 0;
        m_NumAllocations = 0;
    }

} // namespace fw::memory
