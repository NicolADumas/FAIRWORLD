#pragma once

#include <string>
#include <vector>

namespace fw::memory {

// =============================================================================
// MEM-04: SUITE TEST REGRESSIONE CONTRATTO GLOBAL MEMORY ALLOCATOR LIFECYCLE
// (MEM04-01 .. MEM04-07)
// Verifica formale del ciclo di vita dell'heap globale (256 MB), degli overload
// C++17 (allineati, sized, nothrow), prevenzione Use-After-Free e safe shutdown.
// =============================================================================

struct MemoryContractTestResult {
    const char* testId = "";
    const char* testName = "";
    const char* inputDesc = "";
    const char* expectedDesc = "";
    const char* observedDesc = "";
    bool passed = false;
    const char* details = "";
    int exitCode = 0;
};

class GlobalMemoryLifecycleContractTests {
public:
    static std::vector<MemoryContractTestResult> RunAllTests();
    static std::vector<MemoryContractTestResult> RunAllTestsDirect();
    static MemoryContractTestResult RunTestById(const char* testId);
    static MemoryContractTestResult RunTestById(const std::string& testId);
    static int RunChildTest(const char* testId);
    static int RunChildTest(const std::string& testId);

    // MEM04-01: Inizializzazione, allocazione e shutdown controllato a heap svuotato
    static MemoryContractTestResult Test01_NormalStartupAndDrainedShutdown();

    // MEM04-02: 500 allocazioni/deallocazioni ripetute con verifica zero drift e metriche
    static MemoryContractTestResult Test02_RepeatedAllocationsAndDeallocations();

    // MEM04-03: Rifiuto dello shutdown con allocazioni vive (Prevenzione categorica Use-After-Free)
    static MemoryContractTestResult Test03_RefuseShutdownWhenAllocationsAreLive();

    // MEM04-04: Shutdown dopo inizializzazione parziale e idempotenza di ShutdownGlobalMemory
    static MemoryContractTestResult Test04_ShutdownAfterPartialInitialization();

    // MEM04-05: Over-aligned new e delete (alignas 16, 32, 64) e conformità C++17
    static MemoryContractTestResult Test05_OverAlignedAllocationAndDeallocation();

    // MEM04-06: Sicurezza di deallocazione post-shutdown (Tombstone Bounds Guard contro heap corruption)
    static MemoryContractTestResult Test06_PostShutdownDeleteSafety();

    // MEM04-07: Fallback sicuro di operator new post-shutdown (Nessuna riallocazione orfana di 256MB)
    static MemoryContractTestResult Test07_PostShutdownNewFallback();
};

} // namespace fw::memory
