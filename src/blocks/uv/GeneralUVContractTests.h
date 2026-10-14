#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// REPORT RISULTATO TEST CONTRATTUALE BM-UV3 (UVG-1 .. UVG-18)
// =============================================================================

struct GeneralUVTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

// =============================================================================
// SUITE DI TEST CONTRATTUALI GENERAL UV (BM-UV3.11)
// =============================================================================

class GeneralUVContractTests {
public:
    static std::vector<GeneralUVTestResult> RunAllTests();
};

} // namespace fw
