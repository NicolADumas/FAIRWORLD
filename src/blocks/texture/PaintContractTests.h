#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// BM-TX3.10 — SUITE DI TEST CONTRATTUALI PAINT ENGINE (PT-1 .. PT-10)
// =============================================================================

struct PaintTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class PaintContractTests {
public:
    static std::vector<PaintTestResult> RunContractTests();
};

} // namespace fw
