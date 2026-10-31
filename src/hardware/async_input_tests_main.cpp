#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>

#include "hardware/AsyncInputLifecycleContractTests.h"

int main(int argc, char* argv[]) {
    std::cout << "====================================================================================\n";
    std::cout << "  FAIRWORLD ASYNC INPUT LIFECYCLE TEST RUNNER (MEM-03)\n";
    std::cout << "  SUITE: ASYNC (7 Tests: ASYNC-01 .. ASYNC-07) | RAII OWNERSHIP & THREAD-SAFETY CONTRACT\n";
    std::cout << "====================================================================================\n\n";

    auto startTime = std::chrono::steady_clock::now();

    auto results = fw::AsyncInputLifecycleContractTests::RunAllTests();

    auto endTime = std::chrono::steady_clock::now();
    double totalDurationMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    size_t passedCount = 0;
    size_t failedCount = 0;

    for (const auto& r : results) {
        if (r.passed) {
            passedCount++;
            std::cout << "  [PASS] " << std::setw(10) << std::left << r.testId 
                      << " | " << r.testName << "\n";
            std::cout << "         Observed: " << r.observedDesc << "\n\n";
        } else {
            failedCount++;
            std::cout << "  [FAIL] " << std::setw(10) << std::left << r.testId 
                      << " | " << r.testName << "\n";
            std::cout << "         Input:    " << r.inputDesc << "\n";
            std::cout << "         Expected: " << r.expectedDesc << "\n";
            std::cout << "         Observed: " << r.observedDesc << "\n";
            if (!r.details.empty()) {
                std::cout << "         Details:  " << r.details << "\n";
            }
            std::cout << "\n";
        }
    }

    std::cout << "------------------------------------------------------------------------------------\n";
    std::cout << "  TOTAL TESTS: " << results.size() 
              << " | PASSED: " << passedCount 
              << " | FAILED: " << failedCount 
              << " | DURATION: " << std::fixed << std::setprecision(2) << totalDurationMs << " ms\n";
    std::cout << "====================================================================================\n";

    return (failedCount == 0) ? 0 : 1;
}
