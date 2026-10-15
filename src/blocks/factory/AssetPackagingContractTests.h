#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// REPORT RISULTATO TEST CONTRATTUALE BM-F2C (PKG-01 .. PKG-10)
// =============================================================================

struct AssetPackagingTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

// =============================================================================
// SUITE DI TEST CONTRATTUALI ASSET PACKAGING & REGISTRY BINDING (BM-F2C)
// =============================================================================

class AssetPackagingContractTests {
public:
    static std::vector<AssetPackagingTestResult> RunAllTests();
};

} // namespace fw
