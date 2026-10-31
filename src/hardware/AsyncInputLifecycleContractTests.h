#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// MEM-03: SUITE TEST REGRESSIONE CONTRATTO ASYNC INPUT OWNERSHIP E LIFECYCLE
// (ASYNC-01 .. ASYNC-07)
// Verifica formale isolata della ownership, RAII in DeviceManager, prestiti
// non-owning negli stati (BlockMakerState, ForgeState), shutdown e concorrenza.
// =============================================================================

struct AsyncInputTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class AsyncInputLifecycleContractTests {
public:
    static std::vector<AsyncInputTestResult> RunAllTests();

    // ASYNC-01: Ownership centralizzata in DeviceManager e binding RAII a SharedContext
    static AsyncInputTestResult Test01_DeviceManagerOwnershipAndRAII();

    // ASYNC-02: Entrata e uscita ripetuta da BlockMakerState senza allocazioni orfane
    static AsyncInputTestResult Test02_RepeatedBlockMakerStateLifecycle();

    // ASYNC-03: Entrata e uscita ripetuta da ForgeState senza allocazioni orfane
    static AsyncInputTestResult Test03_RepeatedForgeStateLifecycle();

    // ASYNC-04: Alternanza ripetuta fra BlockMakerState e ForgeState preservando il prestito non-owning
    static AsyncInputTestResult Test04_StateAlternationPreservesNonOwningBorrow();

    // ASYNC-05: Shutdown sicuro dopo inizializzazione parziale o assenza di sottosistemi
    static AsyncInputTestResult Test05_PartialInitializationShutdownSafety();

    // ASYNC-06: Sicurezza di distacco: nessun accesso/dangling pointer dopo la distruzione di DeviceManager
    static AsyncInputTestResult Test06_DetachmentOnDestructionSafety();

    // ASYNC-07: Integrità e thread-safety del double-buffering asincrono sotto carico concorrente
    static AsyncInputTestResult Test07_ConcurrentDoubleBufferIntegrity();
};

} // namespace fw
