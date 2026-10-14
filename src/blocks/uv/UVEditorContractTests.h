#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// REPORT RISULTATO TEST CONTRATTUALE BM-UV5 (UVE-1 .. UVE-18)
// =============================================================================

struct UVEditorTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

// =============================================================================
// SUITE DI TEST CONTRATTUALI 2D UV EDITOR & PAINTER (BM-UV5.11)
// =============================================================================

class UVEditorContractTests {
public:
    static std::vector<UVEditorTestResult> RunAllTests();
};

} // namespace fw
