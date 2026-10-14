#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// REPORT RISULTATO TEST CONTRATTUALE BM-UV6 (UVR-1 .. UVR-20)
// =============================================================================

struct ReprojectionTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

// =============================================================================
// SUITE DI TEST CONTRATTUALI TEXTURE REPROJECTION & PAINT PRESERVATION (BM-UV6.10)
// =============================================================================

class TextureReprojectionContractTests {
public:
    static std::vector<ReprojectionTestResult> RunAllTests();
};

} // namespace fw
