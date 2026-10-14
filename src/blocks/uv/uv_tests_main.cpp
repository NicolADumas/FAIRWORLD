#include <iostream>
#include <vector>
#include <string>

#include "blocks/uv/PrimitiveUVContractTests.h"
#include "blocks/uv/GeneralUVContractTests.h"
#include "blocks/uv/AtlasUVContractTests.h"
#include "blocks/uv/UVEditorContractTests.h"
#include "blocks/uv/TextureReprojectionContractTests.h"
#include "blocks/uv/UVFactoryIntegrationTests.h"

int main(int argc, char* argv[]) {
    std::cout << "========================================================\n";
    std::cout << "  FAIRWORLD BM-UV CONSOLIDATED HEADLESS TEST RUNNER\n";
    std::cout << "  SUITES: UVP(15) UVG(18) UVK(18) UVE(18) UVR(20) UVI(10)\n";
    std::cout << "========================================================\n";

    int totalDiscovered = 0;
    int totalPassed = 0;
    int totalFailed = 0;

    auto runSuite = [&](const std::string& suiteName, const auto& results) {
        std::cout << "\n>>> SUITE: " << suiteName << " [" << results.size() << " tests]\n";
        for (const auto& r : results) {
            totalDiscovered++;
            if (r.passed) {
                totalPassed++;
                std::cout << "  [PASS] " << r.testId << " - " << r.testName << "\n";
            } else {
                totalFailed++;
                std::cout << "  [FAIL] " << r.testId << " - " << r.testName << " -> " << r.details << "\n";
            }
        }
    };

    runSuite("UVP - Primitive & Parametric UV", fw::PrimitiveUVContractTests::RunAllTests());
    runSuite("UVG - General Mesh Unwrapping", fw::GeneralUVContractTests::RunAllTests());
    runSuite("UVK - Universal Atlas Packing", fw::AtlasUVContractTests::RunAllTests());
    runSuite("UVE - 2D UV Editor Backend", fw::UVEditorContractTests::RunAllTests());
    runSuite("UVR - Texture Reprojection", fw::TextureReprojectionContractTests::RunAllTests());
    runSuite("UVI - Factory End-to-End Integration", fw::UVFactoryIntegrationTests::RunAllTests());

    std::cout << "\n========================================================\n";
    std::cout << "EXECUTION SUMMARY:\n";
    std::cout << "  DISCOVERED: " << totalDiscovered << "\n";
    std::cout << "  PASSED:     " << totalPassed << "\n";
    std::cout << "  FAILED:     " << totalFailed << "\n";
    std::cout << "  SKIPPED:    0\n";
    std::cout << "========================================================\n";

    return (totalFailed == 0 && totalDiscovered == 99) ? 0 : 1;
}
