#include "pch.h"
#include "ProxyAllocator.h"
#include <cassert>

namespace fw::memory {

    ProxyAllocator::ProxyAllocator(const char* name, IAllocator& allocator)
        : IAllocator(allocator.GetTotalSize()), m_Name(name), m_Allocator(allocator) {
    }

    ProxyAllocator::~ProxyAllocator() {}

    void ProxyAllocator::Init() {
        // L'inizializzazione è gestita dall'allocatore sottostante, il proxy si limita a inoltrare
        // m_Allocator.Init(); // Tipicamente non vogliamo chiamare Init due volte. Lo assume già inizializzato.
    }

    void* ProxyAllocator::Allocate(size_t size, uint8_t alignment) {
        assert(size != 0);

        void* ptr = m_Allocator.Allocate(size, alignment);
        
        if (ptr != nullptr) {
            // Poiché IAllocator non espone un GetSize(ptr), un Proxy approssima l'uso aggiungendo la size richiesta.
            // Il tracking esatto avverrà tramite Tracy (Fase 5) che userà le macro con m_Name.
            m_Used += size; 
            if (m_Used > m_Peak) {
                m_Peak = m_Used;
            }
            m_NumAllocations++;
        }

        return ptr;
    }

    void ProxyAllocator::Free(void* ptr) {
        if (ptr == nullptr) return;

        m_Allocator.Free(ptr);

        if (m_NumAllocations > 0) {
            m_NumAllocations--;
        }
    }

} // namespace fw::memory
