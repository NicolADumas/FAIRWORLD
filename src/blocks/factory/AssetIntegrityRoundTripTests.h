#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// REPORT RISULTATO TEST CONTRATTUALE BM-F4 (INT-01 .. INT-10)
// Asset Integrity & End-to-End Round-Trip Suite
// =============================================================================
struct AssetIntegrityTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class AssetIntegrityRoundTripTests {
public:
    static std::vector<AssetIntegrityTestResult> RunAllTests();
};

} // namespace fw
