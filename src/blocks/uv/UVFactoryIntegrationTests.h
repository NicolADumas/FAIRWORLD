#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// REPORT RISULTATO TEST CONTRATTUALE BM-UV8 (UVI-1 .. UVI-10)
// =============================================================================

struct FactoryIntegrationTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

// =============================================================================
// SUITE DI TEST CONTRATTUALI FACTORY END-TO-END INTEGRATION (BM-UV8.7)
// =============================================================================

class UVFactoryIntegrationTests {
public:
    static std::vector<FactoryIntegrationTestResult> RunAllTests();
};

} // namespace fw
