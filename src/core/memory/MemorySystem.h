#pragma once
#include "IAllocator.h"

namespace fw::memory {

    struct GlobalMemoryMetrics {
        size_t totalSizeBytes = 0;
        size_t usedBytes = 0;
        size_t peakBytes = 0;
        size_t activeAllocations = 0;
        bool isInitialized = false;
        bool isShutdown = false;
    };

    // Chiama all'avvio del programma per allocare l'heap principale (o invocato lazily dal primo operator new)
    void InitializeGlobalMemory();
    
    // Tenta lo shutdown controllato: se force == false (default), rilascia l'heap SOLO se non ci sono allocazioni vive.
    // Restituisce true se l'heap e' stato effettivamente rilasciato, false se e' stato preservato residente per evitare Use-After-Free.
    bool ShutdownGlobalMemory(bool force = false);
    
    // Telemetria e stato del ciclo di vita della memoria globale
    size_t GetLiveAllocationCount();
    size_t GetLiveAllocatedBytes();
    bool CanSafelyShutdownGlobalMemory();
    GlobalMemoryMetrics GetGlobalMemoryMetrics();
    bool IsGlobalMemoryActive();
    bool IsGlobalMemoryShutdown();

    // Restituisce l'allocatore principale (FreeListAllocator)
    IAllocator* GetGlobalAllocator();

} // namespace fw::memory

