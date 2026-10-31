#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <string>

#include "core/memory/RuntimeMemoryDiagnostics.h"
#include "core/memory/MemorySystem.h"

int main(int argc, char* argv[]) {
    // Inizializza l'allocatore globale per il runner diagnostico
    fw::memory::InitializeGlobalMemory();

    std::cout << "====================================================================================================\n";
    std::cout << "  FAIRWORLD RUNTIME MEMORY DIAGNOSTICS & LEAK VERIFICATION (MEM-06)\n";
    std::cout << "  SUITE: MEM-06 (8 Scenari Runtime x 100 Cicli) | VERIFICA DRIFT CPU, HEAP ARENAS & OS WORKING SET\n";
    std::cout << "====================================================================================================\n\n";

    auto startTime = std::chrono::steady_clock::now();

    int cycles = 100;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--cycles" && i + 1 < argc) {
            cycles = std::atoi(argv[++i]);
            if (cycles <= 0) cycles = 100;
        }
    }

    std::cout << "[MEM-06] Avvio esecuzione 8 scenari runtime (Cicli impostati: " << cycles << ")...\n\n";

    auto results = fw::diagnostics::RuntimeMemoryDiagnosticsSuite::RunAllScenarios(cycles);

    auto endTime = std::chrono::steady_clock::now();
    double totalDurationMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    size_t passedCount = 0;
    size_t failedCount = 0;

    std::cout << std::left 
              << std::setw(20) << "SCENARIO ID"
              << std::setw(42) << "SCENARIO"
              << std::setw(8)  << "CICLI"
              << std::setw(12) << "DELTA HEAP"
              << std::setw(10) << "DELTA RSS"
              << std::setw(10) << "STATO"
              << "\n";
    std::cout << std::string(102, '-') << "\n";

    for (const auto& r : results) {
        if (r.passed) passedCount++; else failedCount++;

        double deltaRssMb = static_cast<double>(r.delta.deltaWorkingSetBytes) / (1024.0 * 1024.0);

        std::string deltaHeapStr = std::to_string(r.delta.deltaGlobalHeapBytes) + " B";

        std::cout << std::left 
                  << std::setw(20) << r.scenarioId
                  << std::setw(42) << r.scenarioName
                  << std::setw(8)  << r.cyclesExecuted
                  << std::setw(12) << deltaHeapStr
                  << std::setw(10) << std::fixed << std::setprecision(2) << deltaRssMb
                  << std::setw(10) << (r.passed ? "[PASS]" : "[FAIL]")
                  << "\n";

        std::cout << "   -> Valutazione: " << r.evaluation << "\n";
        std::cout << "   -> Dettaglio:   " << r.details << "\n\n";
    }

    std::cout << "====================================================================================================\n";
    std::cout << "  TOTAL SCENARIOS TESTED: " << results.size() 
              << " | PASSED: " << passedCount 
              << " | FAILED: " << failedCount 
              << " | DURATION: " << std::fixed << std::setprecision(2) << totalDurationMs << " ms\n";
    std::cout << "  STATUS: " << (failedCount == 0 && passedCount == 8 ? "ALL PASS (ZERO MEMORY DRIFT CONFIRMED)" : "FAILURE") << "\n";
    std::cout << "====================================================================================================\n";

    return (failedCount == 0 && passedCount == 8) ? 0 : 1;
}
