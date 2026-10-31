#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>

#include "core/app/LifecycleRemediationContractTests.h"

int main(int argc, char* argv[]) {
    std::cout << "====================================================================================================\n";
    std::cout << "  FAIRWORLD LIFECYCLE & OWNERSHIP REMEDIATION TEST RUNNER (MEM-07A & MEM-07B)\n";
    std::cout << "  SUITE: LC (8 Tests: LC-01 .. LC-08) | TRANSITION, DESTRUCTION ORDER & OWNERSHIP SAFETY\n";
    std::cout << "====================================================================================================\n\n";

    auto startTime = std::chrono::steady_clock::now();

    auto results = fw::LifecycleRemediationContractTests::RunAllTests();

    auto endTime = std::chrono::steady_clock::now();
    double totalDurationMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    size_t passedCount = 0;
    size_t failedCount = 0;

    std::cout << std::left 
              << std::setw(8)  << "ID"
              << std::setw(15) << "TARGET"
              << std::setw(65) << "TEST NAME"
              << std::setw(10) << "STATUS"
              << "\n";
    std::cout << std::string(100, '-') << "\n";

    for (const auto& r : results) {
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

    std::cout << "====================================================================================================\n";
    std::cout << "  TOTAL TESTS:   " << results.size() << " / 8\n";
    std::cout << "  PASSED:        " << passedCount << "\n";
    std::cout << "  FAILED:        " << failedCount << "\n";
    std::cout << "  DURATION:      " << std::fixed << std::setprecision(2) << totalDurationMs << " ms\n";
    std::cout << "  STATUS:        " << (failedCount == 0 && results.size() == 8 ? "ALL PASS (SUCCESS)" : "FAILURE") << "\n";
    std::cout << "====================================================================================================\n";

    return (failedCount == 0 && results.size() == 8) ? 0 : 1;
}
