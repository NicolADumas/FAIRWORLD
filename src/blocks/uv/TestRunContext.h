#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <optional>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cstddef>

namespace fw {

// =============================================================================
// TEST EXECUTION & RUNNER CONTEXT (BUILD-RECOVERY-04A)
// Centralized coordination of test suites to eliminate recursive re-execution
// and ensure single-execution semantics across full engineering certification.
// =============================================================================

enum class TestExecutionStatus {
    NotExecuted,
    Passed,
    Failed,
    Skipped
};

struct TestCaseResult {
    std::string suiteCode;
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
    double durationMs = 0.0;
};

struct TestSuiteResult {
    std::string suiteCode;
    std::string suiteName;
    size_t expectedCount = 0;
    size_t discoveredCount = 0;
    size_t passedCount = 0;
    size_t failedCount = 0;
    size_t skippedCount = 0;
    double durationMs = 0.0;
    bool executed = false;
    std::vector<TestCaseResult> testCases;
};

struct RegressionGateDependencyCheck {
    std::string suiteCode;
    bool executed = false;
    size_t expectedCount = 0;
    size_t actualCount = 0;
    size_t passedCount = 0;
    size_t failedCount = 0;
    bool passed = false;
    std::string errorReason;
};

struct RegressionGateEvaluation {
    bool allDependenciesPassed = false;
    size_t totalExpected = 0;
    size_t totalDiscovered = 0;
    size_t totalPassed = 0;
    size_t totalFailed = 0;
    std::vector<RegressionGateDependencyCheck> checks;
    std::string observedSummary;
    std::string failureDetails;
};

class TestRunContext {
public:
    static TestRunContext& Instance() {
        static TestRunContext instance;
        return instance;
    }

    void BeginSession() {
        m_active = true;
        m_suites.clear();
        m_sessionStartTime = std::chrono::steady_clock::now();
    }

    void EndSession() {
        m_sessionEndTime = std::chrono::steady_clock::now();
        m_active = false;
    }

    bool IsActive() const {
        return m_active;
    }

    void RecordSuiteResult(const TestSuiteResult& suiteRes) {
        m_suites[suiteRes.suiteCode] = suiteRes;
    }

    bool HasSuiteRun(const std::string& suiteCode) const {
        auto it = m_suites.find(suiteCode);
        return (it != m_suites.end() && it->second.executed);
    }

    std::optional<TestSuiteResult> GetSuiteResult(const std::string& suiteCode) const {
        auto it = m_suites.find(suiteCode);
        if (it != m_suites.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    const std::unordered_map<std::string, TestSuiteResult>& GetAllSuiteResults() const {
        return m_suites;
    }

    // Evaluates a regression gate over a set of required prior suites
    RegressionGateEvaluation EvaluateRegressionGate(
        const std::vector<std::pair<std::string, size_t>>& requiredSuites) const 
    {
        RegressionGateEvaluation eval;
        eval.allDependenciesPassed = true;

        for (const auto& req : requiredSuites) {
            const std::string& code = req.first;
            size_t expected = req.second;
            eval.totalExpected += expected;

            RegressionGateDependencyCheck chk;
            chk.suiteCode = code;
            chk.expectedCount = expected;

            auto it = m_suites.find(code);
            if (it == m_suites.end() || !it->second.executed) {
                chk.executed = false;
                chk.passed = false;
                chk.errorReason = "Suite [" + code + "] non eseguita nella sessione attiva";
                eval.allDependenciesPassed = false;
                eval.checks.push_back(chk);
                continue;
            }

            const auto& s = it->second;
            chk.executed = true;
            chk.actualCount = s.discoveredCount;
            chk.passedCount = s.passedCount;
            chk.failedCount = s.failedCount;

            eval.totalDiscovered += s.discoveredCount;
            eval.totalPassed += s.passedCount;
            eval.totalFailed += s.failedCount;

            if (s.discoveredCount != expected) {
                chk.passed = false;
                chk.errorReason = "Suite [" + code + "] conteggio incoerente (atteso: " + 
                                  std::to_string(expected) + ", scoperto: " + std::to_string(s.discoveredCount) + ")";
                eval.allDependenciesPassed = false;
            } else if (s.failedCount > 0 || s.passedCount != expected) {
                chk.passed = false;
                chk.errorReason = "Suite [" + code + "] ha fallimenti (" + 
                                  std::to_string(s.failedCount) + " FAIL su " + std::to_string(s.discoveredCount) + ")";
                eval.allDependenciesPassed = false;
            } else {
                chk.passed = true;
            }

            eval.checks.push_back(chk);
        }

        std::ostringstream obs;
        obs << "Discovered: " << eval.totalDiscovered 
            << ", Passed: " << eval.totalPassed 
            << ", Failed: " << eval.totalFailed;
        eval.observedSummary = obs.str();

        if (!eval.allDependenciesPassed) {
            std::ostringstream err;
            err << "Regressione riscontrata: ";
            bool first = true;
            for (const auto& c : eval.checks) {
                if (!c.passed) {
                    if (!first) err << "; ";
                    err << c.suiteCode << " -> " << c.errorReason;
                    first = false;
                }
            }
            eval.failureDetails = err.str();
        }

        return eval;
    }

private:
    TestRunContext() = default;
    bool m_active = false;
    std::unordered_map<std::string, TestSuiteResult> m_suites;
    std::chrono::steady_clock::time_point m_sessionStartTime;
    std::chrono::steady_clock::time_point m_sessionEndTime;
};

} // namespace fw
