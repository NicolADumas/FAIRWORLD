#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// REPORT RISULTATO TEST MESH BRIDGE CONTRATTUALE (MBR-1 .. MBR-10)
// FG-01R MICRO-GATE 3B - Authoritative MeshBridge Integration
// =============================================================================

struct MeshBridgeContractTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class MeshBridgeContractTests {
public:
    static std::vector<MeshBridgeContractTestResult> RunAllTests();
};

} // namespace fw
