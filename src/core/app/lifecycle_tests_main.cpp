#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>

#include "core/app/LifecycleRemediationContractTests.h"
#include "core/app/RuntimeLifecycleContractTests.h"

int main(int argc, char* argv[]) {
    std::cout << "====================================================================================================\n";
    std::cout << "  FAIRWORLD RUNTIME LIFECYCLE & OWNERSHIP VERIFICATION TEST RUNNER\n";
    std::cout << "  SUITES: LC (8 Tests: LC-01 .. LC-08) + RLC (5 Tests: RLC-01 .. RLC-05) | TOTAL: 13 TESTS\n";
    std::cout << "====================================================================================================\n\n";

    auto startTime = std::chrono::steady_clock::now();

    // 1. Esegui Suite LC (MEM-07A & MEM-07B)
    auto lcResults = fw::LifecycleRemediationContractTests::RunAllTests();

    // 2. Esegui Suite RLC (P0.3C3 Runtime Lifecycle)
    auto rlcResults = fw::RuntimeLifecycleContractTests::RunAllTests();

    auto endTime = std::chrono::steady_clock::now();
    double totalDurationMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    size_t passedCount = 0;
    size_t failedCount = 0;

    std::cout << "--- SUITE 1: MEM-07 LIFECYCLE REMEDIATION (LC-01 .. LC-08) ---\n";
    std::cout << std::left 
              << std::setw(8)  << "ID"
              << std::setw(15) << "TARGET"
              << std::setw(65) << "TEST NAME"
              << std::setw(10) << "STATUS"
              << "\n";
    std::cout << std::string(100, '-') << "\n";

    for (const auto& r : lcResults) {
        if (r.passed) passedCount++; else failedCount++;

        std::cout << std::left 
                  << std::setw(8)  << r.testId
                  << std::setw(15) << r.targetFinding
                  << std::setw(65) << r.testName
                  << std::setw(10) << (r.passed ? "[PASS]" : "[FAIL]")
                  << "\n";

        std::cout << "   -> Observed: " << r.observed << "\n";
        if (!r.details.empty()) {
            std::cout << "   -> Details:  " << r.details << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "\n--- SUITE 2: P0.3C3 RUNTIME LIFECYCLE GATE (RLC-01 .. RLC-05) ---\n";
    std::cout << std::left 
              << std::setw(8)  << "ID"
              << std::setw(15) << "TARGET"
              << std::setw(65) << "TEST NAME"
              << std::setw(10) << "STATUS"
              << "\n";
    std::cout << std::string(100, '-') << "\n";

    for (const auto& r : rlcResults) {
        if (r.passed) passedCount++; else failedCount++;

        std::cout << std::left 
                  << std::setw(8)  << r.testId
                  << std::setw(15) << r.targetRequirement
                  << std::setw(65) << r.testName
                  << std::setw(10) << (r.passed ? "[PASS]" : "[FAIL]")
                  << "\n";

        std::cout << "   -> Observed: " << r.observed << "\n";
        std::cout << "   -> Duration: " << std::fixed << std::setprecision(2) << r.durationMs << " ms\n";
        if (!r.details.empty()) {
            std::cout << "   -> Details:  " << r.details << "\n";
        }
        std::cout << "\n";
    }

    size_t totalTests = lcResults.size() + rlcResults.size();

    std::cout << "====================================================================================================\n";
    std::cout << "  TOTAL TESTS:   " << totalTests << " (8 LC + 5 RLC)\n";
    std::cout << "  PASSED:        " << passedCount << "\n";
    std::cout << "  FAILED:        " << failedCount << "\n";
    std::cout << "  DURATION:      " << std::fixed << std::setprecision(2) << totalDurationMs << " ms\n";
    std::cout << "  STATUS:        " << (failedCount == 0 && totalTests == 13 ? "ALL PASS (SUCCESS)" : "FAILURE") << "\n";
    std::cout << "====================================================================================================\n";

    return (failedCount == 0 && totalTests == 13) ? 0 : 1;
}
