#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>

#include "world/noise/NoiseContractTests.h"

int main(int argc, char* argv[]) {
    std::cout << "====================================================================================\n";
    std::cout << "  FAIRWORLD NOISE RULE OF FIVE TEST RUNNER (MEM-02)\n";
    std::cout << "  SUITE: NOISE (7 Tests: NOISE-01 .. NOISE-07) | RAII & NUMERICAL DETERMINISM CONTRACT\n";
    std::cout << "====================================================================================\n\n";

    auto startTime = std::chrono::steady_clock::now();

    auto results = fw::NoiseContractTests::RunAllTests();

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

    std::cout << "====================================================================================\n";
    std::cout << "  TEST SUMMARY\n";
    std::cout << "====================================================================================\n";
    std::cout << "  TOTAL TESTS:   " << results.size() << " / 7\n";
    std::cout << "  PASSED:        " << passedCount << "\n";
    std::cout << "  FAILED:        " << failedCount << "\n";
    std::cout << "  DURATION:      " << std::fixed << std::setprecision(2) << totalDurationMs << " ms\n";
    std::cout << "  STATUS:        " << (failedCount == 0 && results.size() == 7 ? "ALL PASS (SUCCESS)" : "FAILURE") << "\n";
    std::cout << "====================================================================================\n";

    return (failedCount == 0 && results.size() == 7) ? 0 : 1;
}
