#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// REPORT RISULTATO TEST CONTRATTUALE BM-F4.3 (CRH-01 .. CRH-10)
// Transactional Asset Publication & Crash Recovery Suite
// =============================================================================
struct AssetCrashRecoveryTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class AssetCrashRecoveryTests {
public:
    static std::vector<AssetCrashRecoveryTestResult> RunAllTests();
};

} // namespace fw
