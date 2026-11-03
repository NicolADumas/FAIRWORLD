#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>

#include "blocks/MaterialAuthoringContractTests.h"

int main(int argc, char* argv[]) {
    std::cout << "====================================================================================\n";
    std::cout << "  FAIRWORLD MATERIAL AUTHORING CONTRACT TEST RUNNER (P0.3C1/P0.3C2)\n";
    std::cout << "  SUITE: AUTH (11 Tests: AUTH-01..11) | TRANSACTIONAL WORKFLOW & PERSISTENCE GATE\n";
    std::cout << "====================================================================================\n\n";

    auto startTime = std::chrono::steady_clock::now();

    auto results = fw::MaterialAuthoringContractTests::RunAllTests();

    auto endTime = std::chrono::steady_clock::now();
    double totalDurationMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    size_t passedCount = 0;
    size_t failedCount = 0;

    for (const auto& r : results) {
        if (r.passed) {
            passedCount++;
            std::cout << "  [PASS] " << std::setw(8) << std::left << r.testId 
                      << " | " << r.testName << "\n";
            std::cout << "         Observed: " << r.observedDesc << "\n\n";
        } else {
            failedCount++;
            std::cout << "  [FAIL] " << std::setw(8) << std::left << r.testId 
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
    std::cout << "  TOTAL TESTS:   " << results.size() << " / 11\n";
    std::cout << "  PASSED:        " << passedCount << "\n";
    std::cout << "  FAILED:        " << failedCount << "\n";
    std::cout << "  DURATION:      " << std::fixed << std::setprecision(2) << totalDurationMs << " ms\n";
    std::cout << "  STATUS:        " << (failedCount == 0 && results.size() == 11 ? "ALL PASS (SUCCESS)" : "FAILURE") << "\n";
    std::cout << "====================================================================================\n";

    return (failedCount == 0 && results.size() == 11) ? 0 : 1;
}
