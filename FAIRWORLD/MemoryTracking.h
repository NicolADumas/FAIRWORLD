#pragma once

// ==============================================================================
// FAIRWORLD MEMORY TRACKING
// Questo header espone le macro per la telemetria degli allocatori custom.
// Se TRACY_ENABLE è definito nel build system, le chiamate verranno registrate
// dal Tracy Profiler in tempo reale.
// ==============================================================================

#ifdef TRACY_ENABLE
    #include <tracy/Tracy.hpp>
    
    // Registra un'allocazione specificando il puntatore, la dimensione e il nome dell'allocatore
    #define FW_PROFILE_ALLOC(ptr, size, name) TracyAllocN(ptr, size, name)
    
    // Registra una deallocazione
    #define FW_PROFILE_FREE(ptr, name) TracyFreeN(ptr, name)
#else
    // Compilazione in Release o senza profiler attivo: overhead zero garantito
    #define FW_PROFILE_ALLOC(ptr, size, name)
    #define FW_PROFILE_FREE(ptr, name)
#endif
