#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// REPORT RISULTATO TEST AUDIT TRANSAZIONALE BM-F2C.1 (TRX-01 .. TRX-10)
// =============================================================================

struct TransactionAuditTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

// =============================================================================
// SUITE DI TEST INTEGRITÀ TRANSAZIONALE & PERSISTENZA (BM-F2C.1)
// =============================================================================

class TransactionIntegrityAuditTests {
public:
    static std::vector<TransactionAuditTestResult> RunAllTests();
};

} // namespace fw
