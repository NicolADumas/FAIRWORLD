#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// BM-F4.3.1: SUITE TEST CRASH RECOVERY HARDENING & COMMIT SEMANTICS (CRH-H01..H08)
// =============================================================================

struct CrashRecoveryHardeningTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class AssetCrashRecoveryHardeningTests {
public:
    static std::vector<CrashRecoveryHardeningTestResult> RunAllTests();
};

} // namespace fw
