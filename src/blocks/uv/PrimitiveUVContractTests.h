#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// REPORT RISULTATO TEST CONTRATTUALE BM-UV2 (UVP-1 .. UVP-15)
// =============================================================================

struct PrimitiveUVTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

// =============================================================================
// SUITE DI TEST CONTRATTUALI PRIMITIVE UV (BM-UV2.12)
// =============================================================================

class PrimitiveUVContractTests {
public:
    static std::vector<PrimitiveUVTestResult> RunAllTests();
};

} // namespace fw
