#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// REPORT RISULTATO TEST CONTRATTUALE BM-F3 (SBR-01 .. SBR-10)
// Sculpted Mesh Runtime Bridge Suite
// =============================================================================
struct SculptedBridgeTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class SculptedMeshRuntimeBridgeTests {
public:
    static std::vector<SculptedBridgeTestResult> RunAllTests();
};

} // namespace fw
