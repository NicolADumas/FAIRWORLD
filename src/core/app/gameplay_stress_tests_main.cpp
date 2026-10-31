#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>

#include "core/app/GameplayLifecycleStressTests.h"

int main(int argc, char* argv[]) {
    std::cout << "====================================================================================================\n";
    std::cout << "  FAIRWORLD GAMEPLAY LIFECYCLE STRESS TEST RUNNER (MEM-05)\n";
    std::cout << "  SUITE: STRESS (8 Systems: STRESS-01 .. STRESS-08) | CREATE -> LOAD -> SIMULATE -> UNLOAD -> DESTROY\n";
    std::cout << "====================================================================================================\n\n";

    auto startTime = std::chrono::steady_clock::now();

    auto results = fw::GameplayLifecycleStressTests::RunAllStressTests();

    auto endTime = std::chrono::steady_clock::now();
    double totalDurationMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    size_t passedCount = 0;
    size_t failedCount = 0;

    std::cout << std::left 
              << std::setw(11) << "ID"
              << std::setw(38) << "SISTEMA"
              << std::setw(8)  << "CICLI"
              << std::setw(12) << "PEAK RES"
              << std::setw(12) << "POST DEST"
              << std::setw(10) << "STATO"
              << "\n";
    std::cout << std::string(100, '-') << "\n";

    for (const auto& r : results) {
        if (r.passed) passedCount++; else failedCount++;

        size_t peakResource = 0;
        size_t postResource = 0;

        if (r.testId == "STRESS-01" || r.testId == "STRESS-02" || r.testId == "STRESS-04") {
            peakResource = r.peakMetrics.liveEntities;
            postResource = r.finalMetrics.liveEntities;
        } else if (r.testId == "STRESS-03") {
            peakResource = r.peakMetrics.loadedChunks;
            postResource = r.finalMetrics.loadedChunks;
        } else if (r.testId == "STRESS-05" || r.testId == "STRESS-07") {
            peakResource = r.peakMetrics.referencedSnapshots;
            postResource = r.finalMetrics.referencedSnapshots;
        } else if (r.testId == "STRESS-06") {
            peakResource = r.peakMetrics.physicsBodies;
            postResource = r.finalMetrics.physicsBodies;
        } else if (r.testId == "STRESS-08") {
            peakResource = r.peakMetrics.registeredListeners;
            postResource = r.finalMetrics.registeredListeners;
        }

        std::cout << std::left 
                  << std::setw(11) << r.testId
                  << std::setw(38) << r.subsystemName
                  << std::setw(8)  << r.totalCycles
                  << std::setw(12) << peakResource
                  << std::setw(12) << postResource
                  << std::setw(10) << (r.passed ? "[PASS]" : "[FAIL]")
                  << "\n";

        std::cout << "   -> Esito:   " << r.details << "\n";
        std::cout << "   -> Memoria: " << r.memoryEvaluation << "\n\n";
    }

    std::cout << "====================================================================================================\n";
    std::cout << "  TOTAL SUB-SYSTEMS TESTED: " << results.size() 
              << " | PASSED: " << passedCount 
              << " | FAILED: " << failedCount 
              << " | DURATION: " << std::fixed << std::setprecision(2) << totalDurationMs << " ms\n";
    std::cout << "====================================================================================================\n";

    return (failedCount == 0) ? 0 : 1;
}
