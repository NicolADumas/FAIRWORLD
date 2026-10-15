#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// REPORT RISULTATO TEST TOPOLOGIA COMPILATA (TOP-1 .. TOP-7)
// =============================================================================

struct TopologyTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

// =============================================================================
// SUITE DI TEST CONTRATTUALI COMPILED TOPOLOGY (MICRO-GATE 2 - PHASE A)
// =============================================================================

class CompiledTopologyTests {
public:
    static std::vector<TopologyTestResult> RunAllTests();
};

} // namespace fw
