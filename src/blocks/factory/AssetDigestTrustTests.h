#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// REPORT RISULTATO TEST CONTRATTUALE BM-F4.2 (DGV-01 .. DGV-10)
// Digest Verification, Trust Policy & Zero-Work Safety Suite
// =============================================================================
struct AssetDigestTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class AssetDigestTrustTests {
public:
    static std::vector<AssetDigestTestResult> RunAllTests();
};

} // namespace fw
