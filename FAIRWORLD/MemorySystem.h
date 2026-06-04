#pragma once
#include "IAllocator.h"

namespace fw::memory {

    // Chiama questa funzione all'avvio del programma per allocare l'heap principale
    void InitializeGlobalMemory();
    
    // Chiama alla chiusura
    void ShutdownGlobalMemory();
    
    // Restituisce l'allocatore principale (FreeListAllocator)
    IAllocator* GetGlobalAllocator();

} // namespace fw::memory
