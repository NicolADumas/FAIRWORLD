#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// BM-F4.4: SUITE TEST CONTRATTUALE GRAFO DIPENDENZE (DEP-01 .. DEP-10)
// =============================================================================

struct AssetDependencyTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class AssetDependencyContractTests {
public:
    static std::vector<AssetDependencyTestResult> RunAllTests();
};

} // namespace fw
