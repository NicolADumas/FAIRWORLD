#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// REPORT RISULTATO TEST CONTRATTUALE BM-F2A.1 (PNT-01 .. PNT-10)
// =============================================================================

struct PainterStationTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

// =============================================================================
// SUITE DI TEST CONTRATTUALI PAINTER STATION INTEGRATION (BM-F2A)
// =============================================================================

class PainterStationContractTests {
public:
    static std::vector<PainterStationTestResult> RunAllTests();
};

} // namespace fw
