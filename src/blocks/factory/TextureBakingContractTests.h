#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// REPORT RISULTATO TEST CONTRATTUALE BM-F2B (TXB-01 .. TXB-10)
// =============================================================================

struct TextureBakingTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

// =============================================================================
// SUITE DI TEST CONTRATTUALI TEXTURE BAKING INTEGRATION (BM-F2B)
// =============================================================================

class TextureBakingContractTests {
public:
    static std::vector<TextureBakingTestResult> RunAllTests();
};

} // namespace fw
