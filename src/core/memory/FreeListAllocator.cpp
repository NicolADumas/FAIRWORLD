#include "pch.h"
#include "FreeListAllocator.h"
#include <cstdlib>
#include <cassert>

namespace fw::memory {

    FreeListAllocator::FreeListAllocator(size_t totalSize, void* startPtr)
        : IAllocator(totalSize), m_StartPtr(startPtr), m_OwnsMemory(false), m_FreeList(nullptr) {
    }

    FreeListAllocator::~FreeListAllocator() {
        if (m_OwnsMemory && m_StartPtr != nullptr) {
            std::free(m_StartPtr);
            m_StartPtr = nullptr;
        }
    }

    void FreeListAllocator::Init() {
        if (m_StartPtr == nullptr) {
            m_StartPtr = std::malloc(m_TotalSize);
            m_OwnsMemory = true;
        }
        
        // Al momento dell'inizializzazione, abbiamo un unico grande blocco libero
        m_FreeList = reinterpret_cast<FreeNode*>(m_StartPtr);
        m_FreeList->size = m_TotalSize;
        m_FreeList->next = nullptr;
        
        m_Used = 0;
        m_Peak = 0;
        m_NumAllocations = 0;
    }

    void* FreeListAllocator::Allocate(size_t size, uint8_t alignment) {
        assert(size != 0 && "Cannot allocate 0 bytes");

        FreeNode* prevNode = nullptr;
        FreeNode* freeNode = m_FreeList;

        uint8_t adjustment = 0;
        size_t totalSize = 0;

        // Ricerca First-fit
        while (freeNode != nullptr) {
            // L'allocazione ha bisogno di spazio per l'AllocationHeader subito prima del puntatore restituito
            adjustment = AlignForwardAdjustmentWithHeader(freeNode, alignment, sizeof(AllocationHeader));
            totalSize = size + adjustment;

            if (freeNode->size >= totalSize) {
                break; // Trovato un blocco abbastanza grande
            }

            prevNode = freeNode;
            freeNode = freeNode->next;
        }

        if (freeNode == nullptr) {
            return nullptr; // Memoria esaurita o troppo frammentata
        }

        // Se lo spazio rimanente in questo blocco è sufficientemente grande da formare un nuovo FreeNode, lo splittiamo
        if (freeNode->size - totalSize >= sizeof(FreeNode)) {
            FreeNode* newFreeNode = reinterpret_cast<FreeNode*>(reinterpret_cast<uintptr_t>(freeNode) + totalSize);
            newFreeNode->size = freeNode->size - totalSize;
            newFreeNode->next = freeNode->next;
            
            if (prevNode != nullptr) {
                prevNode->next = newFreeNode;
            } else {
                m_FreeList = newFreeNode;
            }
        } else {
            // Altrimenti assegniamo tutto il blocco senza splittare
            totalSize = freeNode->size; // Consuma anche i byte extra
            if (prevNode != nullptr) {
                prevNode->next = freeNode->next;
            } else {
                m_FreeList = freeNode->next;
            }
        }

        uintptr_t alignedAddress = reinterpret_cast<uintptr_t>(freeNode) + adjustment;
        
        AllocationHeader* header = reinterpret_cast<AllocationHeader*>(alignedAddress - sizeof(AllocationHeader));
        header->size = totalSize;
        header->adjustment = adjustment;

        m_Used += totalSize;
        if (m_Used > m_Peak) {
            m_Peak = m_Used;
        }
        m_NumAllocations++;

        return reinterpret_cast<void*>(alignedAddress);
    }

    void FreeListAllocator::Free(void* ptr) {
        if (ptr == nullptr) return;

        AllocationHeader* header = reinterpret_cast<AllocationHeader*>(reinterpret_cast<uintptr_t>(ptr) - sizeof(AllocationHeader));
        
        uintptr_t blockStart = reinterpret_cast<uintptr_t>(ptr) - header->adjustment;
        size_t blockSize = header->size;

        FreeNode* newFreeNode = reinterpret_cast<FreeNode*>(blockStart);
        newFreeNode->size = blockSize;
        newFreeNode->next = nullptr;

        // Inseriamo il nuovo nodo nella free list in modo ordinato per indirizzo (permette il coalescing)
        FreeNode* prevNode = nullptr;
        FreeNode* freeNode = m_FreeList;

        while (freeNode != nullptr) {
            if (reinterpret_cast<uintptr_t>(freeNode) > blockStart) {
                break;
            }
            prevNode = freeNode;
            freeNode = freeNode->next;
        }

        if (prevNode == nullptr) {
            newFreeNode->next = m_FreeList;
            m_FreeList = newFreeNode;
        } else {
            prevNode->next = newFreeNode;
            newFreeNode->next = freeNode;
        }

        // Tentativo di unire (Coalesce) con il nodo successivo
        if (newFreeNode->next != nullptr && 
            reinterpret_cast<uintptr_t>(newFreeNode) + newFreeNode->size == reinterpret_cast<uintptr_t>(newFreeNode->next)) {
            newFreeNode->size += newFreeNode->next->size;
            newFreeNode->next = newFreeNode->next->next;
        }

        // Tentativo di unire con il nodo precedente
        if (prevNode != nullptr && 
            reinterpret_cast<uintptr_t>(prevNode) + prevNode->size == reinterpret_cast<uintptr_t>(newFreeNode)) {
            prevNode->size += newFreeNode->size;
            prevNode->next = newFreeNode->next;
        }

        m_Used -= blockSize;
        m_NumAllocations--;
    }

} // namespace fw::memory
