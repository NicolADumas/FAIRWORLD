#include "pch.h"
#include "MemorySystem.h"
#include "FreeListAllocator.h"
#include "MemoryMath.h"
#include <new>
#include <cstdlib>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <cassert>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

// 256 MB di main heap per l'engine (in un gioco finale potrebbe essere 1-2 GB)
static constexpr size_t GLOBAL_HEAP_SIZE = 1024ULL * 1024ULL * 256ULL; 

enum class MemoryLifecycleState : uint8_t {
    Uninitialized,
    Active,
    Shutdown
};

static std::atomic<MemoryLifecycleState> g_LifecycleState{MemoryLifecycleState::Uninitialized};
static std::atomic<fw::memory::FreeListAllocator*> g_GlobalAllocator{nullptr};
static void* g_GlobalHeapBuffer = nullptr;
static uintptr_t g_TrackedHeapStart = 0;
static uintptr_t g_TrackedHeapEnd = 0;
static std::mutex g_GlobalAllocatorMutex;

// Usa placement new su un piccolo buffer statico correttamente allineato per l'istanza della classe
alignas(fw::memory::FreeListAllocator) static uint8_t g_AllocatorBuffer[sizeof(fw::memory::FreeListAllocator)];

namespace fw::memory {

    void InitializeGlobalMemory() {
        std::lock_guard<std::mutex> lock(g_GlobalAllocatorMutex);
        if (!g_GlobalAllocator.load(std::memory_order_acquire)) {
            if (!g_GlobalHeapBuffer) {
                g_GlobalHeapBuffer = std::malloc(GLOBAL_HEAP_SIZE);
                if (!g_GlobalHeapBuffer) {
                    throw std::bad_alloc();
                }
                g_TrackedHeapStart = reinterpret_cast<uintptr_t>(g_GlobalHeapBuffer);
                g_TrackedHeapEnd = g_TrackedHeapStart + GLOBAL_HEAP_SIZE;
            }

            auto* alloc = new (g_AllocatorBuffer) FreeListAllocator(GLOBAL_HEAP_SIZE, g_GlobalHeapBuffer);
            alloc->Init();
            g_GlobalAllocator.store(alloc, std::memory_order_release);
            g_LifecycleState.store(MemoryLifecycleState::Active, std::memory_order_release);
        }
    }

    bool ShutdownGlobalMemory(bool force) {
        std::lock_guard<std::mutex> lock(g_GlobalAllocatorMutex);
        auto* alloc = g_GlobalAllocator.load(std::memory_order_acquire);
        if (!alloc) {
            g_LifecycleState.store(MemoryLifecycleState::Shutdown, std::memory_order_release);
            return true;
        }

        size_t liveAllocs = alloc->GetNumAllocations();
        if (liveAllocs > 0 && !force) {
            // MEM-04: Non liberare l'heap mentre esistono allocazioni attive.
            // Preservando l'heap residente preveniamo categoricamente Use-After-Free
            // e heap corruption durante la distruzione degli oggetti statici del CRT.
            return false;
        }

        alloc->~FreeListAllocator();
        g_GlobalAllocator.store(nullptr, std::memory_order_release);

        if (g_GlobalHeapBuffer) {
            std::free(g_GlobalHeapBuffer);
            g_GlobalHeapBuffer = nullptr;
            // MEM-04 / Tombstone Bounds Guard:
            // Preserviamo g_TrackedHeapStart e g_TrackedHeapEnd affinche' operator delete
            // e operator delete[] continuino a riconoscere i puntatori residui dell'arena
            // dismessa ed evitino categoricamente di passarli a std::free(), prevenendo
            // l'Access Violation 0xC0000005 durante lo shutdown.
        }

        g_LifecycleState.store(MemoryLifecycleState::Shutdown, std::memory_order_release);
        return true;
    }

    size_t GetLiveAllocationCount() {
        std::lock_guard<std::mutex> lock(g_GlobalAllocatorMutex);
        auto* alloc = g_GlobalAllocator.load(std::memory_order_acquire);
        return alloc ? alloc->GetNumAllocations() : 0;
    }

    size_t GetLiveAllocatedBytes() {
        std::lock_guard<std::mutex> lock(g_GlobalAllocatorMutex);
        auto* alloc = g_GlobalAllocator.load(std::memory_order_acquire);
        return alloc ? alloc->GetUsed() : 0;
    }

    bool CanSafelyShutdownGlobalMemory() {
        std::lock_guard<std::mutex> lock(g_GlobalAllocatorMutex);
        auto* alloc = g_GlobalAllocator.load(std::memory_order_acquire);
        return !alloc || alloc->GetNumAllocations() == 0;
    }

    bool IsGlobalMemoryActive() {
        return g_LifecycleState.load(std::memory_order_acquire) == MemoryLifecycleState::Active;
    }

    bool IsGlobalMemoryShutdown() {
        return g_LifecycleState.load(std::memory_order_acquire) == MemoryLifecycleState::Shutdown;
    }

    GlobalMemoryMetrics GetGlobalMemoryMetrics() {
        std::lock_guard<std::mutex> lock(g_GlobalAllocatorMutex);
        GlobalMemoryMetrics m;
        m.totalSizeBytes = GLOBAL_HEAP_SIZE;
        auto* alloc = g_GlobalAllocator.load(std::memory_order_acquire);
        if (alloc) {
            m.usedBytes = alloc->GetUsed();
            m.peakBytes = alloc->GetPeak();
            m.activeAllocations = alloc->GetNumAllocations();
        }
        auto state = g_LifecycleState.load(std::memory_order_acquire);
        m.isInitialized = (state == MemoryLifecycleState::Active);
        m.isShutdown = (state == MemoryLifecycleState::Shutdown);
        return m;
    }

    IAllocator* GetGlobalAllocator() {
        return g_GlobalAllocator.load(std::memory_order_acquire);
    }

} // namespace fw::memory


// ==============================================================================
// OVERRIDE GLOBALE DI NEW E DELETE (C++17 COMPLETO E SICURO)
// Sostituisce l'allocazione dinamica standard del C++ in tutta l'applicazione
// ==============================================================================

// --- 1. Forme Scalari e Array Standard ---

void* operator new(size_t size) {
    if (size == 0) size = 1; // Conformità standard C++: operator new(0) deve restituire un puntatore valido non-nullo
    if (g_LifecycleState.load(std::memory_order_acquire) != MemoryLifecycleState::Active) {
        void* ptr = std::malloc(size);
        if (!ptr) throw std::bad_alloc();
        return ptr;
    }
    
    void* ptr = nullptr;
    auto* alloc = g_GlobalAllocator.load(std::memory_order_acquire);
    if (alloc) {
        std::lock_guard<std::mutex> lock(g_GlobalAllocatorMutex);
        ptr = alloc->Allocate(size);
    }

    if (!ptr) {
        // Fallback se l'heap custom e' esaurito o non disponibile
        ptr = std::malloc(size);
        if (!ptr) throw std::bad_alloc();
    }
    return ptr;
}

void* operator new[](size_t size) {
    return ::operator new(size);
}

// --- 2. Forme Scalari e Array Allineate (C++17) ---

void* operator new(size_t size, std::align_val_t al) {
    if (size == 0) size = 1; // Conformità standard C++ over-aligned new(0)
    size_t alignment = static_cast<size_t>(al);
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }
    if (g_LifecycleState.load(std::memory_order_acquire) != MemoryLifecycleState::Active) {
#if defined(_MSC_VER)
        void* p = _aligned_malloc(size, alignment);
#else
        void* p = std::aligned_alloc(alignment, size);
#endif
        if (!p) throw std::bad_alloc();
        return p;
    }

    void* ptr = nullptr;
    auto* alloc = g_GlobalAllocator.load(std::memory_order_acquire);
    if (alloc && alignment <= 128 && fw::memory::IsPowerOfTwo(alignment)) {
        std::lock_guard<std::mutex> lock(g_GlobalAllocatorMutex);
        ptr = alloc->Allocate(size, static_cast<uint8_t>(alignment));
    }

    if (!ptr) {
#if defined(_MSC_VER)
        ptr = _aligned_malloc(size, alignment);
#else
        ptr = std::aligned_alloc(alignment, size);
#endif
        if (!ptr) throw std::bad_alloc();
    }
    return ptr;
}

void* operator new[](size_t size, std::align_val_t al) {
    return ::operator new(size, al);
}

// --- 3. Forme Nothrow ---

void* operator new(size_t size, const std::nothrow_t&) noexcept {
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](size_t size, const std::nothrow_t&) noexcept {
    try {
        return ::operator new[](size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new(size_t size, std::align_val_t al, const std::nothrow_t&) noexcept {
    try {
        return ::operator new(size, al);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](size_t size, std::align_val_t al, const std::nothrow_t&) noexcept {
    try {
        return ::operator new[](size, al);
    } catch (...) {
        return nullptr;
    }
}

// --- 4. Deallocazioni Standard e Sized ---

void operator delete(void* ptr) noexcept {
    if (!ptr) return;

    uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
    auto* alloc = g_GlobalAllocator.load(std::memory_order_acquire);

    if (alloc && g_GlobalHeapBuffer) {
        if (p >= g_TrackedHeapStart && p < g_TrackedHeapEnd) {
            std::lock_guard<std::mutex> lock(g_GlobalAllocatorMutex);
            alloc->Free(ptr);
            return;
        }
    } else if (g_TrackedHeapStart != 0) {
        // L'allocatore e' stato disattivato o spento
        if (p >= g_TrackedHeapStart && p < g_TrackedHeapEnd) {
            // Il puntatore apparteneva all'heap globale dismesso: non passiamo mai a std::free()!
            return;
        }
    }
    
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept {
    ::operator delete(ptr);
}

void operator delete(void* ptr, size_t /*size*/) noexcept {
    ::operator delete(ptr);
}

void operator delete[](void* ptr, size_t /*size*/) noexcept {
    ::operator delete(ptr);
}

// --- 5. Deallocazioni Allineate (C++17) ---

void operator delete(void* ptr, std::align_val_t al) noexcept {
    if (!ptr) return;

    uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
    auto* alloc = g_GlobalAllocator.load(std::memory_order_acquire);

    if (alloc && g_GlobalHeapBuffer) {
        if (p >= g_TrackedHeapStart && p < g_TrackedHeapEnd) {
            std::lock_guard<std::mutex> lock(g_GlobalAllocatorMutex);
            alloc->Free(ptr);
            return;
        }
    } else if (g_TrackedHeapStart != 0) {
        if (p >= g_TrackedHeapStart && p < g_TrackedHeapEnd) {
            return;
        }
    }

#if defined(_MSC_VER)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

void operator delete[](void* ptr, std::align_val_t al) noexcept {
    ::operator delete(ptr, al);
}

void operator delete(void* ptr, size_t /*size*/, std::align_val_t al) noexcept {
    ::operator delete(ptr, al);
}

void operator delete[](void* ptr, size_t /*size*/, std::align_val_t al) noexcept {
    ::operator delete(ptr, al);
}

// --- 6. Deallocazioni Nothrow ---

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    ::operator delete(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    ::operator delete[](ptr);
}

void operator delete(void* ptr, std::align_val_t al, const std::nothrow_t&) noexcept {
    ::operator delete(ptr, al);
}

void operator delete[](void* ptr, std::align_val_t al, const std::nothrow_t&) noexcept {
    ::operator delete[](ptr, al);
}

