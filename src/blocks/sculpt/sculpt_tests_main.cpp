#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <functional>
#include <sstream>
#include <algorithm>
#include <cctype>

#include "blocks/sculpt/EditableMesh.h"
#include "blocks/sculpt/MeshSelectionService.h"
#include "blocks/sculpt/MeshModelingService.h"
#include "blocks/sculpt/MeshSculptService.h"
#include "blocks/sculpt/MeshTopologyService.h"
#include "blocks/sculpt/MeshRepairService.h"
#include "blocks/sculpt/MeshHistoryService.h"
#include "blocks/sculpt/MeshBridgeService.h"

namespace {

struct GenericTestResult {
    std::string suiteCode;
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

struct SuiteDescriptor {
    std::string code;
    std::string name;
    size_t expectedCount = 0;
    std::function<std::vector<GenericTestResult>()> runFunc;
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
        size_t first = item.find_first_not_of(" \t\r\n");
        if (first != std::string::npos) {
            size_t last = item.find_last_not_of(" \t\r\n");
            tokens.push_back(item.substr(first, last - first + 1));
        }
    }
    return tokens;
}

template <typename TFunc>
std::function<std::vector<GenericTestResult>()> makeRunner(const std::string& suiteCode, TFunc func) {
    return [suiteCode, func]() {
        auto specific = func();
        std::vector<GenericTestResult> out;
        out.reserve(specific.size());
        for (const auto& r : specific) {
            GenericTestResult res;
            res.suiteCode = suiteCode;
            res.testId = r.testId;
            res.testName = r.testName;
            res.inputDesc = r.inputDesc;
            res.expectedDesc = r.expectedDesc;
            res.observedDesc = r.observedDesc;
            res.passed = r.passed;
            res.details = r.details;
            out.push_back(res);
        }
        return out;
    };
}

std::vector<SuiteDescriptor> buildRegistry() {
    std::vector<SuiteDescriptor> reg;
    reg.reserve(8);

    // 1. SM: EditableMesh Core DCEL & Generational Topology (10 tests)
    reg.push_back({
        "SM",
        "EditableMesh Core & Generational DCEL Topology",
        10,
        makeRunner("SM", []() { return fw::EditableMesh::RunContractTests(); })
    });

    // 2. SEL: MeshSelectionService & Topological Loop/Ring Selection (20 tests, SEL-1..20)
    reg.push_back({
        "SEL",
        "MeshSelectionService & Topological Selection (Loop/Ring)",
        20,
        makeRunner("SEL", []() { return fw::MeshSelectionService::RunSelectionContractTests(); })
    });

    // 3. MOD: MeshModelingService (Transform & Local Mesh Ops) (10 tests)
    reg.push_back({
        "MOD",
        "MeshModelingService (Extrude, Inset, Bevel, Transforms)",
        10,
        makeRunner("MOD", []() { return fw::MeshModelingService::RunModelingContractTests(); })
    });

    // 4. SCL: MeshSculptService (Brushes & Spatial Grid Index) (12 tests)
    reg.push_back({
        "SCL",
        "MeshSculptService (Brushes, Falloff & Spatial Grid)",
        12,
        makeRunner("SCL", []() { return fw::MeshSculptService::RunSculptContractTests(); })
    });

    // 5. TOP: MeshTopologyService (Mirror, Subdivide, Loop Cut) (10 tests)
    reg.push_back({
        "TOP",
        "MeshTopologyService (Mirror, Subdivide, Loop Cut)",
        10,
        makeRunner("TOP", []() { return fw::MeshTopologyService::RunTopologyContractTests(); })
    });

    // 6. REP: MeshRepairService (Manifold Healing & Remeshing) (10 tests)
    reg.push_back({
        "REP",
        "MeshRepairService (Manifold Healing & Uniform Remesh)",
        10,
        makeRunner("REP", []() { return fw::MeshRepairService::RunRepairContractTests(); })
    });

    // 7. HIS: MeshHistoryService (Differential Undo/Redo Engine) (12 tests)
    reg.push_back({
        "HIS",
        "MeshHistoryService (Differential Undo/Redo Engine)",
        12,
        makeRunner("HIS", []() { return fw::MeshHistoryService::RunHistoryContractTests(); })
    });

    // 8. BRG: MeshBridgeService (Factory Draft & Packaging Sync) (10 tests)
    reg.push_back({
        "BRG",
        "MeshBridgeService (Factory Draft & Packaging Sync)",
        10,
        makeRunner("BRG", []() { return fw::MeshBridgeService::RunContractTests(); })
    });

    return reg;
}

void printHelp(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n\n"
              << "FAIRWORLD Sculpting & Modeling Subsystem Headless Test Runner\n\n"
              << "Options:\n"
              << "  --suite <CODE>[,<CODE>...]  Run only specified suite(s) (e.g. --suite SEL or --suite SEL,TOP)\n"
              << "  --test <TEST_ID>            Run only specific test ID (e.g. --test SEL-11)\n"
              << "  --list-suites               List all registered sculpt suites with expected test counts\n"
              << "  --help, -h                  Display this help message and exit\n\n"
              << "Registered Suites (8 suites, 94 tests total):\n"
              << "  SM   (10 tests) - EditableMesh Core & Generational DCEL Topology\n"
              << "  SEL  (20 tests) - MeshSelectionService & Topological Selection (BM-S1 SEL-1..20)\n"
              << "  MOD  (10 tests) - MeshModelingService (Extrude, Inset, Bevel, Transforms)\n"
              << "  SCL  (12 tests) - MeshSculptService (Brushes, Falloff & Spatial Grid)\n"
              << "  TOP  (10 tests) - MeshTopologyService (Mirror, Subdivide, Loop Cut)\n"
              << "  REP  (10 tests) - MeshRepairService (Manifold Healing & Uniform Remesh)\n"
              << "  HIS  (12 tests) - MeshHistoryService (Differential Undo/Redo Engine)\n"
              << "  BRG  (10 tests) - MeshBridgeService (Factory Draft & Packaging Sync)\n";
}

} // anonymous namespace

int main(int argc, char** argv) {
    std::vector<std::string> requestedSuites;
    std::string requestedTestId;
    bool listSuitesOnly = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printHelp(argv[0]);
            return 0;
        } else if (arg == "--list-suites") {
            listSuitesOnly = true;
        } else if (arg == "--suite" && i + 1 < argc) {
            std::string val = argv[++i];
            auto tokens = splitString(val, ',');
            for (auto& t : tokens) requestedSuites.push_back(toUpper(t));
        } else if (arg == "--test" && i + 1 < argc) {
            requestedTestId = toUpper(argv[++i]);
        } else {
            std::cerr << "Unknown or incomplete option: " << arg << "\n";
            printHelp(argv[0]);
            return 1;
        }
    }

    auto registry = buildRegistry();

    if (listSuitesOnly) {
        std::cout << "\n====================================================================================\n";
        std::cout << "FAIRWORLD SCULPT & MODELING REGISTERED TEST SUITES:\n";
        std::cout << "====================================================================================\n";
        size_t totalExpected = 0;
        for (const auto& s : registry) {
            std::cout << "  [" << std::setw(4) << std::left << s.code << "] "
                      << std::setw(55) << std::left << s.name
                      << " : " << s.expectedCount << " tests\n";
            totalExpected += s.expectedCount;
        }
        std::cout << "------------------------------------------------------------------------------------\n";
        std::cout << "TOTAL: " << registry.size() << " suites, " << totalExpected << " nominal tests\n\n";
        return 0;
    }

    std::cout << "\n====================================================================================\n";
    std::cout << "FAIRWORLD SCULPTING & MODELING HEADLESS TEST RUNNER\n";
    std::cout << "====================================================================================\n";

    auto startTime = std::chrono::high_resolution_clock::now();

    size_t grandTotalExecuted = 0;
    size_t grandTotalPassed = 0;
    size_t grandTotalFailed = 0;
    size_t grandTotalSkipped = 0;
    size_t suitesPassed = 0;
    size_t suitesFailed = 0;

    for (const auto& suite : registry) {
        // Filtro suite
        if (!requestedSuites.empty()) {
            if (std::find(requestedSuites.begin(), requestedSuites.end(), suite.code) == requestedSuites.end()) {
                continue;
            }
        }

        std::cout << "\n------------------------------------------------------------------------------------\n";
        std::cout << "[" << suite.code << "] " << suite.name << "\n";
        std::cout << "------------------------------------------------------------------------------------\n";

        std::vector<GenericTestResult> results;
        try {
            results = suite.runFunc();
        } catch (const std::exception& ex) {
            std::cerr << "  [EXCEPTION] Suite " << suite.code << " ha sollevato eccezione: " << ex.what() << "\n";
            suitesFailed++;
            grandTotalFailed += suite.expectedCount;
            continue;
        } catch (...) {
            std::cerr << "  [EXCEPTION] Suite " << suite.code << " ha sollevato un'eccezione sconosciuta.\n";
            suitesFailed++;
            grandTotalFailed += suite.expectedCount;
            continue;
        }

        size_t suitePassed = 0;
        size_t suiteFailed = 0;
        size_t suiteSkipped = 0;

        for (const auto& r : results) {
            // Filtro test ID
            if (!requestedTestId.empty() && toUpper(r.testId) != requestedTestId) {
                suiteSkipped++;
                continue;
            }

            if (r.passed) {
                suitePassed++;
                std::cout << "  [PASS] " << std::setw(8) << std::left << r.testId
                          << " | " << r.testName
                          << " | " << r.observedDesc << "\n";
            } else {
                suiteFailed++;
                std::cout << "  [FAIL] " << std::setw(8) << std::left << r.testId
                          << " | " << r.testName
                          << " | " << r.details
                          << " | Observed: " << r.observedDesc << "\n";
            }
        }

        grandTotalExecuted += (suitePassed + suiteFailed);
        grandTotalPassed += suitePassed;
        grandTotalFailed += suiteFailed;
        grandTotalSkipped += suiteSkipped;

        std::cout << ">> Suite [" << suite.code << "] Summary: "
                  << suitePassed << " PASS, "
                  << suiteFailed << " FAIL, "
                  << suiteSkipped << " SKIPPED (Nominal: " << suite.expectedCount << ")\n";

        if (suiteFailed == 0 && (suitePassed > 0 || suiteSkipped > 0)) {
            suitesPassed++;
        } else {
            suitesFailed++;
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    std::cout << "\n====================================================================================\n";
    std::cout << "FAIRWORLD SCULPT & MODELING AUDIT REPORT SUMMARY\n";
    std::cout << "====================================================================================\n";
    std::cout << "  Suites Evaluated : " << (suitesPassed + suitesFailed) << " (" << suitesPassed << " PASS, " << suitesFailed << " FAIL)\n";
    std::cout << "  Tests Executed   : " << grandTotalExecuted << "\n";
    std::cout << "  Tests Passed     : " << grandTotalPassed << "\n";
    std::cout << "  Tests Failed     : " << grandTotalFailed << "\n";
    std::cout << "  Tests Skipped    : " << grandTotalSkipped << "\n";
    std::cout << "  Execution Time   : " << std::fixed << std::setprecision(2) << elapsedMs << " ms\n";
    std::cout << "====================================================================================\n";

    if (grandTotalFailed == 0 && grandTotalExecuted > 0) {
        std::cout << "RESULT: 100% SUCCESS — ALL SCULPT & MODELING TESTS PASSED\n\n";
        return 0;
    } else {
        std::cout << "RESULT: TEST REGRESSION DETECTED — CHECK FAILURES ABOVE\n\n";
        return 1;
    }
}
