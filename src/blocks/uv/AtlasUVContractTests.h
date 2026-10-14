#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// REPORT RISULTATO TEST CONTRATTUALE BM-UV4 (UVK-1 .. UVK-18)
// =============================================================================

struct AtlasUVTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

// =============================================================================
// SUITE DI TEST CONTRATTUALI ATLAS PACKING (BM-UV4.10)
// =============================================================================

class AtlasUVContractTests {
public:
    static std::vector<AtlasUVTestResult> RunAllTests();
};

} // namespace fw
