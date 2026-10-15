#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// REPORT RISULTATO TEST CONTRATTUALE BM-F4.1 (SEC-01 .. SEC-10)
// Asset Integrity Hardening Gate Suite
// =============================================================================
struct AssetHardeningTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class AssetIntegrityHardeningTests {
public:
    static std::vector<AssetHardeningTestResult> RunAllTests();
};

} // namespace fw
