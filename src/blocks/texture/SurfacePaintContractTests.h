#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// BM-TX4.16 — SUITE DI TEST CONTRATTUALI 3D SURFACE BRUSH (BP-1 .. BP-15)
// =============================================================================

struct SurfacePaintTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class SurfacePaintContractTests {
public:
    static std::vector<SurfacePaintTestResult> RunContractTests();
};

} // namespace fw
