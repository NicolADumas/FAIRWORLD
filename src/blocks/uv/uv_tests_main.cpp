#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <exception>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <functional>
#include <set>
#include <cctype>

#include "TestRunContext.h"

// --- FG-01R SUITES (151 tests) ---
#include "blocks/uv/PrimitiveUVContractTests.h"
#include "blocks/uv/GeneralUVContractTests.h"
#include "blocks/uv/AtlasUVContractTests.h"
#include "blocks/uv/UVEditorContractTests.h"
#include "blocks/uv/TextureReprojectionContractTests.h"
#include "blocks/uv/UVFactoryIntegrationTests.h"
#include "blocks/uv/CompiledTopologyTests.h"
#include "blocks/uv/MeshBridgeContractTests.h"

// --- BM-F FACTORY & INTEGRITY SUITES (118 tests) ---
#include "blocks/factory/PainterStationContractTests.h"
#include "blocks/factory/TextureBakingContractTests.h"
#include "blocks/factory/AssetPackagingContractTests.h"
#include "blocks/factory/TransactionIntegrityAuditTests.h"
#include "blocks/factory/SculptedMeshRuntimeBridgeTests.h"
#include "blocks/factory/AssetIntegrityRoundTripTests.h"
#include "blocks/factory/AssetIntegrityHardeningTests.h"
#include "blocks/factory/AssetDigestTrustTests.h"
#include "blocks/factory/AssetCrashRecoveryTests.h"
#include "blocks/factory/AssetCrashRecoveryHardeningTests.h"
#include "blocks/factory/AssetDependencyContractTests.h"
#include "blocks/factory/AssetSchemaMigrationTests.h"

namespace {

struct SuiteDescriptor {
    std::string code;
    std::string name;
    size_t expectedCount = 0;
    std::function<std::vector<fw::TestCaseResult>()> runFunc;
    std::vector<std::string> knownTestIds;
};

std::string toUpper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> splitString(const std::string& str, char delim) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delim)) {
        // Trim whitespace
        size_t first = item.find_first_not_of(" \t\r\n");
        if (first != std::string::npos) {
            size_t last = item.find_last_not_of(" \t\r\n");
            tokens.push_back(item.substr(first, last - first + 1));
        }
    }
    return tokens;
}

template <typename TRunFunc>
std::function<std::vector<fw::TestCaseResult>()> makeSuiteRunner(const std::string& suiteCode, TRunFunc runFunc) {
    return [suiteCode, runFunc]() {
        auto specific = runFunc();
        std::vector<fw::TestCaseResult> out;
        out.reserve(specific.size());
        for (const auto& r : specific) {
            fw::TestCaseResult tc;
            tc.suiteCode = suiteCode;
            tc.testId = r.testId;
            tc.testName = r.testName;
            tc.inputDesc = r.inputDesc;
            tc.expectedDesc = r.expectedDesc;
            tc.observedDesc = r.observedDesc;
            tc.passed = r.passed;
            tc.details = r.details;
            out.push_back(tc);
        }
        return out;
    };
}

std::vector<std::string> generateIds(const std::string& prefix, int count, int padWidth = 2) {
    std::vector<std::string> ids;
    ids.reserve(count);
    for (int i = 1; i <= count; ++i) {
        std::ostringstream oss;
        oss << prefix << "-" << std::setw(padWidth) << std::setfill('0') << i;
        ids.push_back(oss.str());
    }
    return ids;
}

std::vector<SuiteDescriptor> buildSuiteRegistry() {
    std::vector<SuiteDescriptor> registry;
    registry.reserve(20);

    // 1. FG-01R (151 tests)
    registry.push_back({"UVP", "Primitive & Parametric UV", 15,
        makeSuiteRunner("UVP", []() { return fw::PrimitiveUVContractTests::RunAllTests(); }),
        generateIds("UVP", 15)});

    registry.push_back({"UVG", "General Mesh Unwrapping", 18,
        makeSuiteRunner("UVG", []() { return fw::GeneralUVContractTests::RunAllTests(); }),
        generateIds("UVG", 18)});

    registry.push_back({"UVK", "Universal Atlas Packing", 18,
        makeSuiteRunner("UVK", []() { return fw::AtlasUVContractTests::RunAllTests(); }),
        generateIds("UVK", 18)});

    registry.push_back({"UVE", "2D UV Editor Backend", 18,
        makeSuiteRunner("UVE", []() { return fw::UVEditorContractTests::RunAllTests(); }),
        generateIds("UVE", 18)});

    registry.push_back({"UVR", "Texture Reprojection", 20,
        makeSuiteRunner("UVR", []() { return fw::TextureReprojectionContractTests::RunAllTests(); }),
        generateIds("UVR", 20)});

    registry.push_back({"UVI", "Factory End-to-End Integration", 10,
        makeSuiteRunner("UVI", []() { return fw::UVFactoryIntegrationTests::RunAllTests(); }),
        generateIds("UVI", 10)});

    registry.push_back({"TOP", "Compiled Topology Validation", 36,
        makeSuiteRunner("TOP", []() { return fw::CompiledTopologyTests::RunAllTests(); }),
        generateIds("TOP", 36)});

    registry.push_back({"MBR", "MeshBridge Authoritative Integration", 16,
        makeSuiteRunner("MBR", []() { return fw::MeshBridgeContractTests::RunAllTests(); }),
        generateIds("MBR", 16)});

    // 2. BM-F FACTORY & INTEGRITY PIPELINE (118 tests)
    registry.push_back({"PNT", "Painter Station Composite Albedo Integration", 10,
        makeSuiteRunner("PNT", []() { return fw::PainterStationContractTests::RunAllTests(); }),
        generateIds("PNT", 10)});

    registry.push_back({"TXB", "Texture Baking Service & PNG Validation", 10,
        makeSuiteRunner("TXB", []() { return fw::TextureBakingContractTests::RunAllTests(); }),
        generateIds("TXB", 10)});

    registry.push_back({"PKG", "Asset Packaging & Registry Binding", 10,
        makeSuiteRunner("PKG", []() { return fw::AssetPackagingContractTests::RunAllTests(); }),
        generateIds("PKG", 10)});

    registry.push_back({"TRX", "Transaction Integrity & In-Memory Fault Injection", 10,
        makeSuiteRunner("TRX", []() { return fw::TransactionIntegrityAuditTests::RunAllTests(); }),
        generateIds("TRX", 10)});

    registry.push_back({"SBR", "Sculpted Mesh Runtime Bridge (DCEL -> Runtime)", 10,
        makeSuiteRunner("SBR", []() { return fw::SculptedMeshRuntimeBridgeTests::RunAllTests(); }),
        generateIds("SBR", 10)});

    registry.push_back({"INT", "Asset Integrity & End-to-End Round-Trip", 10,
        makeSuiteRunner("INT", []() { return fw::AssetIntegrityRoundTripTests::RunAllTests(); }),
        generateIds("INT", 10)});

    registry.push_back({"SEC", "Asset Integrity Hardening (SHA-256 / Texture Cache)", 10,
        makeSuiteRunner("SEC", []() { return fw::AssetIntegrityHardeningTests::RunAllTests(); }),
        generateIds("SEC", 10)});

    registry.push_back({"DGV", "Digest Verification & Trust Policy (Strict/AllowLegacy)", 10,
        makeSuiteRunner("DGV", []() { return fw::AssetDigestTrustTests::RunAllTests(); }),
        generateIds("DGV", 10)});

    registry.push_back({"CRH", "Transactional Asset Publication & Crash Recovery", 10,
        makeSuiteRunner("CRH", []() { return fw::AssetCrashRecoveryTests::RunAllTests(); }),
        generateIds("CRH", 10)});

    registry.push_back({"CRH-H", "Crash Recovery Hardening & Commit Semantics", 8,
        makeSuiteRunner("CRH-H", []() { return fw::AssetCrashRecoveryHardeningTests::RunAllTests(); }),
        generateIds("CRH-H", 8)});

    registry.push_back({"DEP", "Asset Dependency Graph & Referential Integrity", 10,
        makeSuiteRunner("DEP", []() { return fw::AssetDependencyContractTests::RunAllTests(); }),
        generateIds("DEP", 10)});

    registry.push_back({"MIG", "Asset Schema Migration & Backward Compatibility", 10,
        makeSuiteRunner("MIG", []() { return fw::AssetSchemaMigrationTests::RunAllTests(); }),
        generateIds("MIG", 10)});

    return registry;
}

void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " [options]\n\n"
              << "Options:\n"
              << "  --suite <CODE>[,<CODE>...]  Execute only specified suite(s) (e.g. --suite PNT or --suite DEP,MIG)\n"
              << "  --test <TEST_ID>            Execute specific test ID within its suite (e.g. --test PNT-03)\n"
              << "  --list-suites               List all 20 registered suites with nominal counts and exit\n"
              << "  --list-tests                List all 269 nominal tests grouped by suite and exit\n"
              << "  --help, -h                  Display this help message and exit\n\n"
              << "Notes:\n"
              << "  If no options are provided, a FULL CERTIFICATION run of all 20 suites (269 tests)\n"
              << "  is performed in topological dependency order without recursive re-execution.\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    const auto registry = buildSuiteRegistry();

    bool listSuitesRequested = false;
    bool listTestsRequested = false;
    std::vector<std::string> requestedSuiteCodes;
    std::string requestedTestId;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--list-suites") {
            listSuitesRequested = true;
        } else if (arg == "--list-tests") {
            listTestsRequested = true;
        } else if (arg == "--suite") {
            if (i + 1 >= argc) {
                std::cerr << "[ERROR] Argument --suite requires one or more comma-separated suite codes.\n";
                return 2;
            }
            auto parts = splitString(argv[++i], ',');
            for (const auto& p : parts) {
                requestedSuiteCodes.push_back(toUpper(p));
            }
        } else if (arg == "--test") {
            if (i + 1 >= argc) {
                std::cerr << "[ERROR] Argument --test requires a test ID.\n";
                return 2;
            }
            requestedTestId = toUpper(argv[++i]);
        } else {
            std::cerr << "[ERROR] Unrecognized command-line argument: " << arg << "\n";
            printUsage(argv[0]);
            return 2;
        }
    }

    // Handle --list-suites
    if (listSuitesRequested) {
        std::cout << "====================================================================================\n";
        std::cout << "  FAIRWORLD CONSOLIDATED TEST SUITES CATALOG (20 SUITES | 269 TESTS NOMINAL)\n";
        std::cout << "====================================================================================\n";
        size_t totalExpected = 0;
        for (size_t i = 0; i < registry.size(); ++i) {
            std::cout << "  [" << std::setw(2) << std::right << (i + 1) << "] "
                      << std::setw(6) << std::left << registry[i].code << " | "
                      << std::setw(55) << std::left << registry[i].name << " | "
                      << std::setw(3) << std::right << registry[i].expectedCount << " tests\n";
            totalExpected += registry[i].expectedCount;
        }
        std::cout << "------------------------------------------------------------------------------------\n";
        std::cout << "  TOTAL NOMINAL TESTS: " << totalExpected << "\n";
        std::cout << "====================================================================================\n";
        return 0;
    }

    // Handle --list-tests
    if (listTestsRequested) {
        std::cout << "====================================================================================\n";
        std::cout << "  FAIRWORLD CONSOLIDATED TEST CASES CATALOG (269 NOMINAL TESTS)\n";
        std::cout << "====================================================================================\n";
        size_t globalIndex = 0;
        for (const auto& s : registry) {
            std::cout << "\n--- SUITE [" << s.code << "] " << s.name << " (" << s.knownTestIds.size() << " tests) ---\n";
            for (const auto& tid : s.knownTestIds) {
                globalIndex++;
                std::cout << "  [" << std::setw(3) << std::right << globalIndex << "] "
                          << std::setw(8) << std::left << tid
                          << " (Suite: " << s.code << ")\n";
            }
        }
        std::cout << "\n====================================================================================\n";
        std::cout << "  TOTAL REGISTERED TESTS: " << globalIndex << " / 269\n";
        std::cout << "====================================================================================\n";
        return 0;
    }

    // Determine execution plan
    std::vector<SuiteDescriptor> executionSuites;
    bool isPartialRun = false;
    std::string runFilterDescription;

    if (!requestedTestId.empty()) {
        isPartialRun = true;
        runFilterDescription = "Single Test Filter: " + requestedTestId;
        // Locate suite containing requestedTestId
        const SuiteDescriptor* matchingSuite = nullptr;
        for (const auto& s : registry) {
            for (const auto& tid : s.knownTestIds) {
                if (tid == requestedTestId) {
                    matchingSuite = &s;
                    break;
                }
            }
            if (matchingSuite) break;
        }

        if (!matchingSuite) {
            std::cerr << "[ERROR] Specified test ID '" << requestedTestId << "' was not found in any registered suite.\n";
            std::cerr << "        Use --list-tests to inspect all valid test IDs.\n";
            return 2;
        }

        executionSuites.push_back(*matchingSuite);

    } else if (!requestedSuiteCodes.empty()) {
        isPartialRun = true;
        std::ostringstream ss;
        ss << "Suite Filter: ";
        for (size_t i = 0; i < requestedSuiteCodes.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << requestedSuiteCodes[i];
        }
        runFilterDescription = ss.str();

        for (const auto& code : requestedSuiteCodes) {
            auto it = std::find_if(registry.begin(), registry.end(), [&](const SuiteDescriptor& d) {
                return d.code == code;
            });
            if (it == registry.end()) {
                std::cerr << "[ERROR] Specified suite code '" << code << "' was not found in registered suites.\n";
                std::cerr << "        Use --list-suites to view all 20 valid suite codes.\n";
                return 2;
            }
            // Avoid duplicate additions
            if (std::find_if(executionSuites.begin(), executionSuites.end(), [&](const SuiteDescriptor& d) {
                return d.code == it->code;
            }) == executionSuites.end()) {
                executionSuites.push_back(*it);
            }
        }

    } else {
        // Full run of all 20 suites
        executionSuites = registry;
        isPartialRun = false;
        runFilterDescription = "FULL CERTIFICATION (ALL 20 SUITES | 269 TESTS)";
    }

    // Execution Banner
    std::cout << "====================================================================================\n";
    if (isPartialRun) {
        std::cout << "  FAIRWORLD PARTIAL DIAGNOSTIC TEST RUN (BUILD-RECOVERY-04A)\n";
        std::cout << "  MODE: PARTIAL DIAGNOSTIC RUN | " << runFilterDescription << "\n";
    } else {
        std::cout << "  FAIRWORLD CONSOLIDATED HEADLESS ENGINEERING TEST RUNNER (BUILD-RECOVERY-04A)\n";
        std::cout << "  MODE: FULL CERTIFICATION | 20 SUITES | BASELINE CONSOLIDATA: 269 TESTS\n";
    }
    std::cout << "====================================================================================\n" << std::flush;

    // Begin centralized session
    fw::TestRunContext::Instance().BeginSession();

    int totalDiscovered = 0;
    int totalPassed = 0;
    int totalFailed = 0;
    int totalSkipped = 0;
    size_t suiteInvocations = 0;

    auto globalStartTime = std::chrono::steady_clock::now();

    for (const auto& suite : executionSuites) {
        suiteInvocations++;
        std::cout << "\n------------------------------------------------------------------------------------\n";
        std::cout << "[BEGIN SUITE] [" << suite.code << "] " << suite.name 
                  << " (Expected: " << suite.expectedCount << " tests)\n";
        std::cout << "------------------------------------------------------------------------------------\n" << std::flush;

        fw::TestSuiteResult sResult;
        sResult.suiteCode = suite.code;
        sResult.suiteName = suite.name;
        sResult.expectedCount = suite.expectedCount;

        auto t0 = std::chrono::steady_clock::now();
        std::vector<fw::TestCaseResult> results;
        try {
            results = suite.runFunc();
            sResult.executed = true;
        } catch (const std::exception& ex) {
            std::cout << "  [CRASH] Eccezione non gestita durante l'esecuzione della suite: " << ex.what() << "\n" << std::flush;
            sResult.executed = false;
        } catch (...) {
            std::cout << "  [CRASH] Errore critico sconosciuto durante l'esecuzione della suite!\n" << std::flush;
            sResult.executed = false;
        }
        auto t1 = std::chrono::steady_clock::now();
        double suiteDurationMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        sResult.durationMs = suiteDurationMs;

        // Process test cases
        int suitePassed = 0;
        int suiteFailed = 0;
        int suiteSkipped = 0;

        for (const auto& r : results) {
            bool matchesFilter = requestedTestId.empty() || (toUpper(r.testId) == requestedTestId);

            if (r.passed) {
                suitePassed++;
                if (matchesFilter) {
                    totalDiscovered++;
                    totalPassed++;
                    std::cout << "  [PASS] " << r.testId << " | " << r.testName << " -> " << r.observedDesc << "\n" << std::flush;
                }
            } else {
                suiteFailed++;
                if (matchesFilter) {
                    totalDiscovered++;
                    totalFailed++;
                    std::cout << "  [FAIL] " << r.testId << " | " << r.testName << " -> " << r.details << "\n" << std::flush;
                }
            }
        }

        sResult.discoveredCount = results.size();
        sResult.passedCount = suitePassed;
        sResult.failedCount = suiteFailed;
        sResult.testCases = results;

        if (results.size() < suite.expectedCount) {
            size_t missing = suite.expectedCount - results.size();
            suiteSkipped += static_cast<int>(missing);
            if (requestedTestId.empty()) {
                totalSkipped += static_cast<int>(missing);
            }
            std::cout << "  [WARN] Suite parziale: " << missing << " test non eseguiti!\n" << std::flush;
        }
        sResult.skippedCount = suiteSkipped;

        // Record into centralized context
        fw::TestRunContext::Instance().RecordSuiteResult(sResult);

        std::cout << "[END SUITE] [" << suite.code << "] " << suite.name 
                  << " -> Passed: " << suitePassed << "/" << results.size() 
                  << " | Failed: " << suiteFailed
                  << " | Skipped: " << suiteSkipped
                  << " | Duration: " << std::fixed << std::setprecision(2) << suiteDurationMs << " ms\n" << std::flush;
    }

    auto globalEndTime = std::chrono::steady_clock::now();
    double totalDurationSec = std::chrono::duration<double>(globalEndTime - globalStartTime).count();

    fw::TestRunContext::Instance().EndSession();

    // Summary Section
    std::cout << "\n====================================================================================\n";
    if (isPartialRun) {
        std::cout << "  FAIRWORLD PARTIAL DIAGNOSTIC RUN SUMMARY (NOT A FULL CERTIFICATION)\n";
        std::cout << "====================================================================================\n";
        std::cout << "  MODE:               PARTIAL DIAGNOSTIC RUN\n";
        std::cout << "  FILTER:             " << runFilterDescription << "\n";
        std::cout << "  SUITES EXECUTED:    " << suiteInvocations << "\n";
        std::cout << "  TESTS EVALUATED:    " << totalDiscovered << "\n";
        std::cout << "  PASSED:             " << totalPassed << "\n";
        std::cout << "  FAILED:             " << totalFailed << "\n";
        std::cout << "  SKIPPED:            " << totalSkipped << "\n";
        std::cout << "  TOTAL TIME:         " << std::fixed << std::setprecision(2) << totalDurationSec << " s\n";
        std::cout << "  STATUS:             " << ((totalFailed == 0 && totalDiscovered > 0) ? "PASS (DIAGNOSTIC)" : "FAIL / INCOMPLETE") << "\n";
        std::cout << "  NOTE:               Partial execution cannot certify the 269 baseline.\n";
        std::cout << "====================================================================================\n" << std::flush;

        return (totalFailed == 0 && totalDiscovered > 0) ? 0 : 1;
    } else {
        std::cout << "  FAIRWORLD HEADLESS ENGINEERING FULL CERTIFICATION SUMMARY\n";
        std::cout << "====================================================================================\n";
        std::cout << "  MODE:               FULL CERTIFICATION\n";
        std::cout << "  SUITES EXECUTED:    " << suiteInvocations << " / 20 (Single Invocation Contract Verified)\n";
        std::cout << "  DISCOVERED:         " << totalDiscovered << " / 269\n";
        std::cout << "  PASSED:             " << totalPassed << "\n";
        std::cout << "  FAILED:             " << totalFailed << "\n";
        std::cout << "  SKIPPED:            " << totalSkipped << "\n";
        std::cout << "  TOTAL TIME:         " << std::fixed << std::setprecision(2) << totalDurationSec << " s\n";
        std::cout << "  OVERALL:            " << ((totalFailed == 0 && totalDiscovered == 269) ? "SUCCESS (PASS)" : "FAILURE / INCOMPLETE") << "\n";
        std::cout << "====================================================================================\n" << std::flush;

        return (totalFailed == 0 && totalDiscovered == 269) ? 0 : 1;
    }
}
