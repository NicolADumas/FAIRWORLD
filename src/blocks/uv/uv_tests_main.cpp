#include <iostream>
#include <vector>
#include <string>

#include "blocks/uv/PrimitiveUVContractTests.h"
#include "blocks/uv/GeneralUVContractTests.h"
#include "blocks/uv/AtlasUVContractTests.h"
#include "blocks/uv/UVEditorContractTests.h"
#include "blocks/uv/TextureReprojectionContractTests.h"
#include "blocks/uv/UVFactoryIntegrationTests.h"
#include "blocks/uv/CompiledTopologyTests.h"
#include "blocks/uv/MeshBridgeContractTests.h"

int main(int argc, char* argv[]) {
    std::cout << "========================================================\n";
    std::cout << "  FAIRWORLD BM-UV CONSOLIDATED HEADLESS TEST RUNNER\n";
    std::cout << "  SUITES: UVP(15) UVG(18) UVK(18) UVE(18) UVR(20) UVI(10) TOP(36) MBR(16)\n";
    std::cout << "========================================================\n";

    int totalDiscovered = 0;
    int totalPassed = 0;
    int totalFailed = 0;

    auto runSuite = [&](const std::string& suiteName, auto runFunc) {
        std::cout << "\n[BEGIN] SUITE: " << suiteName << "\n" << std::flush;
        auto results = runFunc();
        int suitePassed = 0;
        int suiteFailed = 0;
        for (const auto& r : results) {
            totalDiscovered++;
            std::cout << "  [RUN] " << r.testId << " - " << r.testName << "\n" << std::flush;
            if (r.passed) {
                totalPassed++;
                suitePassed++;
                std::cout << "  [PASS] " << r.testId << " - " << r.testName << "\n" << std::flush;
            } else {
                totalFailed++;
                suiteFailed++;
                std::cout << "  [FAIL] " << r.testId << " - " << r.testName << " -> " << r.details << "\n" << std::flush;
            }
        }
        std::cout << "[END] SUITE: " << suiteName << " (Passed: " << suitePassed << "/" << results.size() << ")\n" << std::flush;
    };

    runSuite("UVP - Primitive & Parametric UV", []() { return fw::PrimitiveUVContractTests::RunAllTests(); });
    runSuite("UVG - General Mesh Unwrapping", []() { return fw::GeneralUVContractTests::RunAllTests(); });
    runSuite("UVK - Universal Atlas Packing", []() { return fw::AtlasUVContractTests::RunAllTests(); });
    runSuite("UVE - 2D UV Editor Backend", []() { return fw::UVEditorContractTests::RunAllTests(); });
    runSuite("UVR - Texture Reprojection", []() { return fw::TextureReprojectionContractTests::RunAllTests(); });
    runSuite("UVI - Factory End-to-End Integration", []() { return fw::UVFactoryIntegrationTests::RunAllTests(); });
    runSuite("TOP - Compiled Topology Validation", []() { return fw::CompiledTopologyTests::RunAllTests(); });
    runSuite("MBR - MeshBridge Authoritative Integration", []() { return fw::MeshBridgeContractTests::RunAllTests(); });

    std::cout << "\n========================================================\n";
    std::cout << "EXECUTION SUMMARY:\n";
    std::cout << "  DISCOVERED: " << totalDiscovered << "\n";
    std::cout << "  PASSED:     " << totalPassed << "\n";
    std::cout << "  FAILED:     " << totalFailed << "\n";
    std::cout << "  SKIPPED:    0\n";
    std::cout << "========================================================\n" << std::flush;

    return (totalFailed == 0 && totalDiscovered == 151) ? 0 : 1;
}
