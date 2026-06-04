#include "pch.h"
#include "MemorySystem.h"
#include "FreeListAllocator.h"
#include <new>
#include <cstdlib>

// Buffer per il grande blocco di memoria
static void* g_GlobalHeapBuffer = nullptr;

// 256 MB di main heap per l'engine (in un gioco finale potrebbe essere 1-2 GB)
static constexpr size_t GLOBAL_HEAP_SIZE = 1024ULL * 1024ULL * 256ULL; 

static fw::memory::FreeListAllocator* g_GlobalAllocator = nullptr;

namespace fw::memory {

    void InitializeGlobalMemory() {
        if (!g_GlobalAllocator) {
            g_GlobalHeapBuffer = std::malloc(GLOBAL_HEAP_SIZE);
            if (!g_GlobalHeapBuffer) {
                throw std::bad_alloc();
            }

            // Usa placement new su un piccolo buffer statico per l'istanza della classe
            // per evitare il static initialization order fiasco.
            static uint8_t allocatorBuffer[sizeof(FreeListAllocator)];
            g_GlobalAllocator = new (allocatorBuffer) FreeListAllocator(GLOBAL_HEAP_SIZE, g_GlobalHeapBuffer);
            g_GlobalAllocator->Init();
        }
    }

    void ShutdownGlobalMemory() {
        if (g_GlobalAllocator) {
            g_GlobalAllocator->~FreeListAllocator();
            g_GlobalAllocator = nullptr;
            
            if (g_GlobalHeapBuffer) {
                std::free(g_GlobalHeapBuffer);
                g_GlobalHeapBuffer = nullptr;
            }
        }
    }

    IAllocator* GetGlobalAllocator() {
        return g_GlobalAllocator;
    }

} // namespace fw::memory


// ==============================================================================
// OVERRIDE GLOBALE DI NEW E DELETE
// Sostituisce l'allocazione dinamica standard del C++ in tutta l'applicazione
// ==============================================================================

void* operator new(size_t size) {
    if (!g_GlobalAllocator) {
        fw::memory::InitializeGlobalMemory();
    }
    
    void* ptr = g_GlobalAllocator->Allocate(size);
    if (!ptr) {
        // Fallback se l'heap custom è esaurito, in modo che l'engine non craschi istantaneamente
        ptr = std::malloc(size);
        if (!ptr) throw std::bad_alloc();
    }
    return ptr;
}

void* operator new[](size_t size) {
    if (!g_GlobalAllocator) {
        fw::memory::InitializeGlobalMemory();
    }
    
    void* ptr = g_GlobalAllocator->Allocate(size);
    if (!ptr) {
        ptr = std::malloc(size);
        if (!ptr) throw std::bad_alloc();
    }
    return ptr;
}

void operator delete(void* ptr) noexcept {
    if (!ptr) return;

    if (g_GlobalAllocator && g_GlobalHeapBuffer) {
        uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t start = reinterpret_cast<uintptr_t>(g_GlobalHeapBuffer);
        uintptr_t end = start + GLOBAL_HEAP_SIZE;
        
        // Verifica se l'indirizzo appartiene al nostro blocco (Memory Bound Checking)
        if (p >= start && p < end) {
            g_GlobalAllocator->Free(ptr);
            return;
        }
    }
    
    // Se il puntatore è al di fuori del nostro heap (es. memoria allocata dalle librerie 
    // di runtime C/C++ o DLL esterne), usiamo std::free come fallback robusto.
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept {
    ::operator delete(ptr);
}

void operator delete(void* ptr, size_t size) noexcept {
    ::operator delete(ptr);
}

void operator delete[](void* ptr, size_t size) noexcept {
    ::operator delete(ptr);
}
