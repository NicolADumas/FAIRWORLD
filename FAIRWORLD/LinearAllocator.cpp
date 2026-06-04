#include "pch.h"
#include "LinearAllocator.h"
#include <cstdlib>
#include <cassert>

namespace fw::memory {

    LinearAllocator::LinearAllocator(size_t totalSize, void* startPtr)
        : IAllocator(totalSize), m_StartPtr(startPtr), m_Offset(0), m_OwnsMemory(false) {
    }

    LinearAllocator::~LinearAllocator() {
        if (m_OwnsMemory && m_StartPtr != nullptr) {
            std::free(m_StartPtr);
            m_StartPtr = nullptr;
        }
    }

    void LinearAllocator::Init() {
        if (m_StartPtr == nullptr) {
            m_StartPtr = std::malloc(m_TotalSize);
            m_OwnsMemory = true;
        }
    }

    void* LinearAllocator::Allocate(size_t size, uint8_t alignment) {
        assert(size != 0);
        assert(m_StartPtr != nullptr && "LinearAllocator non inizializzato. Chiama Init() prima di allocare.");

        uintptr_t currentAddress = reinterpret_cast<uintptr_t>(m_StartPtr) + m_Offset;
        uint8_t adjustment = AlignForwardAdjustment(reinterpret_cast<void*>(currentAddress), alignment);

        // Controllo memoria esaurita
        if (m_Offset + adjustment + size > m_TotalSize) {
            return nullptr; 
        }

        uintptr_t alignedAddress = currentAddress + adjustment;
        m_Offset += adjustment + size;
        
        m_Used = m_Offset;
        if (m_Used > m_Peak) {
            m_Peak = m_Used;
        }
        m_NumAllocations++;

        return reinterpret_cast<void*>(alignedAddress);
    }

    void LinearAllocator::Free(void* ptr) {
        assert(false && "LinearAllocator non supporta la deallocazione singola. Usa Reset() a fine ciclo vitale (es. fine frame).");
    }

    void LinearAllocator::Reset() {
        m_Offset = 0;
        m_Used = 0;
        m_NumAllocations = 0;
    }

} // namespace fw::memory
