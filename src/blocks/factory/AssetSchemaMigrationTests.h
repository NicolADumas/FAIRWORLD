#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// BM-F4.5: SUITE TEST CONTRATTUALE MIGRAZIONE SCHEMA (MIG-01 .. MIG-10)
// =============================================================================

struct AssetSchemaMigrationTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class AssetSchemaMigrationTests {
public:
    static std::vector<AssetSchemaMigrationTestResult> RunAllTests();
};

} // namespace fw
