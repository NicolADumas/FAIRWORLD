#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>

#include "blocks/CustomScientificMaterialContractTests.h"

int main(int argc, char* argv[]) {
    std::cout << "====================================================================================\n";
    std::cout << "  FAIRWORLD CUSTOM SCIENTIFIC MATERIAL CONTRACT TEST RUNNER (P0.2A / P0.2B / P0.2C / P0.2D)\n";
    std::cout << "  SUITE: CUST (27 Tests: CUST-01..11 + P0.2A 12..17 + P0.2B 18..19 + P0.2C 20..22 + P0.2D 23..27) | CONCURRENCY & PERSISTENCE GATE\n";
    std::cout << "====================================================================================\n\n";

    size_t iterations = 1;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--stress" || arg == "--repeat") && i + 1 < argc) {
            iterations = static_cast<size_t>(std::max(1, std::atoi(argv[++i])));
        }
    }

    if (iterations > 1) {
        std::cout << "  [STRESS MODE ACTIVATED] Running " << iterations << " complete iterations of the test suite...\n\n";
    }

    auto startTime = std::chrono::steady_clock::now();

    std::vector<fw::CustomScientificTestResult> results;
    size_t passedCount = 0;
    size_t failedCount = 0;

    for (size_t iter = 0; iter < iterations; ++iter) {
        results = fw::CustomScientificMaterialContractTests::RunAllTests();
        passedCount = 0;
        failedCount = 0;

        for (const auto& r : results) {
            if (r.passed) {
                passedCount++;
            } else {
                failedCount++;
            }
        }

        if (failedCount > 0) {
            std::cout << "  [FAIL AT ITERATION " << (iter + 1) << " / " << iterations << "]\n";
            break;
        }
    }

    auto endTime = std::chrono::steady_clock::now();
    double totalDurationMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    passedCount = 0;
    failedCount = 0;
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
    std::cout << "  TOTAL TESTS:   " << results.size() << " / 27\n";
    std::cout << "  PASSED:        " << passedCount << "\n";
    std::cout << "  FAILED:        " << failedCount << "\n";
    std::cout << "  DURATION:      " << std::fixed << std::setprecision(2) << totalDurationMs << " ms\n";
    std::cout << "  STATUS:        " << (failedCount == 0 && results.size() == 27 ? "ALL PASS (SUCCESS)" : "FAILURE") << "\n";
    std::cout << "====================================================================================\n";

    return (failedCount == 0 && results.size() == 27) ? 0 : 1;
}
