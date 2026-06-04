#include "pch.h"
#include "PoolAllocator.h"
#include <cstdlib>
#include <cassert>

namespace fw::memory {

    PoolAllocator::PoolAllocator(size_t totalSize, size_t chunkSize, uint8_t chunkAlignment, void* startPtr)
        : IAllocator(totalSize), 
          // Allineiamo il chunk size per garantire che ogni elemento successivo sia allineato
          m_ChunkSize(chunkAlignment * ((chunkSize + chunkAlignment - 1) / chunkAlignment)), 
          m_ChunkAlignment(chunkAlignment), 
          m_StartPtr(startPtr), 
          m_OwnsMemory(false), 
          m_FreeList(nullptr) 
    {
        assert(m_ChunkSize >= sizeof(FreeNode) && "La dimensione del chunk deve essere grande almeno quanto un puntatore per memorizzare la free list");
        assert(m_TotalSize >= m_ChunkSize && "La memoria totale deve contenere almeno un chunk");
    }

    PoolAllocator::~PoolAllocator() {
        if (m_OwnsMemory && m_StartPtr != nullptr) {
            std::free(m_StartPtr);
            m_StartPtr = nullptr;
        }
    }

    void PoolAllocator::Init() {
        if (m_StartPtr == nullptr) {
            m_StartPtr = std::malloc(m_TotalSize);
            m_OwnsMemory = true;
        }
        Reset();
    }

    void PoolAllocator::Reset() {
        // Calcoliamo l'aggiustamento per il primo blocco in modo che tutto il pool sia allineato
        uint8_t initialAdjustment = AlignForwardAdjustment(m_StartPtr, m_ChunkAlignment);
        
        // Quanti chunk interi entrano nello spazio rimanente?
        size_t numChunks = (m_TotalSize - initialAdjustment) / m_ChunkSize;
        
        uintptr_t startAddress = reinterpret_cast<uintptr_t>(m_StartPtr) + initialAdjustment;

        // Inizializziamo la free list collegando tutti i blocchi
        m_FreeList = reinterpret_cast<FreeNode*>(startAddress);
        FreeNode* current = m_FreeList;

        for (size_t i = 0; i < numChunks - 1; ++i) {
            current->next = reinterpret_cast<FreeNode*>(startAddress + (i + 1) * m_ChunkSize);
            current = current->next;
        }
        current->next = nullptr; // L'ultimo nodo non ha un successivo
        
        m_Used = 0;
        m_NumAllocations = 0;
        m_Peak = 0;
    }

    void* PoolAllocator::Allocate(size_t size, uint8_t alignment) {
        assert(size <= m_ChunkSize && "L'allocazione richiede un blocco più grande del chunk size del pool");
        assert(alignment <= m_ChunkAlignment && "L'allineamento richiesto non e' supportato da questo pool");
        
        if (m_FreeList == nullptr) {
            return nullptr; // Pool esaurito
        }

        // Prendiamo il primo nodo disponibile dalla free list in O(1)
        FreeNode* node = m_FreeList;
        m_FreeList = m_FreeList->next;

        m_Used += m_ChunkSize;
        if (m_Used > m_Peak) {
            m_Peak = m_Used;
        }
        m_NumAllocations++;

        return reinterpret_cast<void*>(node);
    }

    void PoolAllocator::Free(void* ptr) {
        if (ptr == nullptr) return;

        // Reinseriamo il nodo in testa alla free list in O(1)
        FreeNode* node = reinterpret_cast<FreeNode*>(ptr);
        node->next = m_FreeList;
        m_FreeList = node;

        m_Used -= m_ChunkSize;
        if (m_NumAllocations > 0) {
            m_NumAllocations--;
        }
    }

} // namespace fw::memory
