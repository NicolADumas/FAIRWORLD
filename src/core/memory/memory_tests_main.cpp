#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "core/memory/GlobalMemoryLifecycleContractTests.h"

struct ChildRunOutcome {
    bool executed = false;
    unsigned long exitCode = 0;
    std::string testId;
    std::string testName;
    std::string observedDesc;
    bool passed = false;
};

#if defined(_WIN32)
static ChildRunOutcome RunTestInChildProcess(const std::string& exePath, const std::string& testId) {
    ChildRunOutcome outcome;
    outcome.testId = testId;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hReadPipe = NULL;
    HANDLE hWritePipe = NULL;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        outcome.executed = false;
        return outcome;
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::string cmd = "\"" + exePath + "\" --child " + testId;

    BOOL success = CreateProcessA(
        NULL,
        cmd.data(),
        NULL,
        NULL,
        TRUE,
        0,
        NULL,
        NULL,
        &si,
        &pi
    );

    // Il genitore chiude subito la write pipe per ricevere EOF quando il figlio termina
    CloseHandle(hWritePipe);

    if (!success) {
        CloseHandle(hReadPipe);
        outcome.executed = false;
        return outcome;
    }

    outcome.executed = true;

    // Lettura output del processo figlio
    std::string output;
    char buffer[512];
    DWORD bytesRead = 0;
    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output.append(buffer, bytesRead);
    }
    CloseHandle(hReadPipe);

    // Attesa completamento con timeout di sicurezza (15 secondi)
    WaitForSingleObject(pi.hProcess, 15000);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    outcome.exitCode = exitCode;

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Parsing output strutturato: CHILD_RESULT|<PASS/FAIL>|<testId>|<testName>|<observedDesc>
    size_t tagPos = output.find("CHILD_RESULT|");
    if (tagPos != std::string::npos) {
        std::string line = output.substr(tagPos + 13);
        size_t newline = line.find('\n');
        if (newline != std::string::npos) line = line.substr(0, newline);

        std::vector<std::string> tokens;
        size_t start = 0;
        size_t delim = line.find('|');
        while (delim != std::string::npos) {
            tokens.push_back(line.substr(start, delim - start));
            start = delim + 1;
            delim = line.find('|', start);
        }
        tokens.push_back(line.substr(start));

        if (tokens.size() >= 4) {
            outcome.passed = (tokens[0] == "PASS" && exitCode == 0);
            outcome.testId = tokens[1];
            outcome.testName = tokens[2];
            outcome.observedDesc = tokens[3];
        }
    }

    if (!outcome.passed && outcome.observedDesc.empty()) {
        if (exitCode == 0xC0000005) {
            outcome.observedDesc = "CRASH: Access Violation (0xC0000005) nel processo figlio";
        } else if (exitCode != 0) {
            outcome.observedDesc = "Processo figlio terminato con codice anomalo: " + std::to_string(exitCode);
        }
    }

    return outcome;
}
#endif

int main(int argc, char* argv[]) {
    // -------------------------------------------------------------------------
    // 1. Modalita' Processo Figlio Isolato (--child <testId>)
    // -------------------------------------------------------------------------
    if (argc >= 3 && std::strcmp(argv[1], "--child") == 0) {
        return fw::memory::GlobalMemoryLifecycleContractTests::RunChildTest(argv[2]);
    }

    // -------------------------------------------------------------------------
    // 2. Modalita' In-Process Diretta (--direct)
    // -------------------------------------------------------------------------
    bool forceDirect = (argc >= 2 && std::string(argv[1]) == "--direct");

    std::cout << "====================================================================================\n";
    std::cout << "  FAIRWORLD GLOBAL MEMORY ALLOCATOR TEST RUNNER (MEM-04)\n";
    std::cout << "  SUITE: MEM04 (7 Tests: MEM04-01 .. MEM04-07) | C++17 RAII & SHUTDOWN SAFETY CONTRACT\n";
    std::cout << "  MODALITA': " << (forceDirect ? "IN-PROCESS (DIRECT)" : "ISOLATED PROCESS RUNNER (MULTI-PROCESS RAII)") << "\n";
    std::cout << "====================================================================================\n\n" << std::flush;

    auto startTime = std::chrono::steady_clock::now();

    const std::vector<std::string> testIds = {
        "MEM04-01",
        "MEM04-02",
        "MEM04-03",
        "MEM04-04",
        "MEM04-05",
        "MEM04-06",
        "MEM04-07"
    };

    size_t passedCount = 0;
    size_t failedCount = 0;

    std::string exePath = argv[0];
#if defined(_WIN32)
    char fullPath[MAX_PATH];
    if (GetModuleFileNameA(NULL, fullPath, MAX_PATH) > 0) {
        exePath = fullPath;
    }
#endif

    for (const auto& tid : testIds) {
        ChildRunOutcome outcome;
        bool ranInChild = false;

#if defined(_WIN32)
        if (!forceDirect) {
            outcome = RunTestInChildProcess(exePath, tid);
            ranInChild = outcome.executed;
        }
#endif

        if (!ranInChild) {
            // Fallback in-process se non e' stato possibile avviare il processo figlio
            auto r = fw::memory::GlobalMemoryLifecycleContractTests::RunTestById(tid);
            outcome.executed = true;
            outcome.passed = r.passed;
            outcome.testId = r.testId;
            outcome.testName = r.testName;
            outcome.observedDesc = r.observedDesc;
            outcome.exitCode = r.passed ? 0 : 1;
        }

        if (outcome.passed) {
            passedCount++;
            std::cout << "  [PASS] " << std::setw(10) << std::left << outcome.testId 
                      << " | " << outcome.testName << "\n";
            std::cout << "         Exit Code: " << outcome.exitCode << " (0x" 
                      << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << outcome.exitCode 
                      << std::dec << std::setfill(' ') << ")\n";
            std::cout << "         Observed:  " << outcome.observedDesc << "\n\n" << std::flush;
        } else {
            failedCount++;
            std::cout << "  [FAIL] " << std::setw(10) << std::left << outcome.testId 
                      << " | " << outcome.testName << "\n";
            std::cout << "         Exit Code: " << outcome.exitCode << " (0x" 
                      << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << outcome.exitCode 
                      << std::dec << std::setfill(' ') << ")\n";
            std::cout << "         Observed:  " << outcome.observedDesc << "\n\n" << std::flush;
        }
    }

    auto endTime = std::chrono::steady_clock::now();
    double totalDurationMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    std::cout << "====================================================================================\n";
    std::cout << "  TEST SUMMARY (ISOLATED PROCESS RUNNER)\n";
    std::cout << "====================================================================================\n";
    std::cout << "  TOTAL TESTS:   " << testIds.size() << " / 7\n";
    std::cout << "  PASSED:        " << passedCount << "\n";
    std::cout << "  FAILED:        " << failedCount << "\n";
    std::cout << "  DURATION:      " << std::fixed << std::setprecision(2) << totalDurationMs << " ms\n";
    std::cout << "  STATUS:        " << (failedCount == 0 && passedCount == 7 ? "ALL PASS (SUCCESS)" : "FAILURE") << "\n";
    std::cout << "====================================================================================\n" << std::flush;

    return (failedCount == 0 && passedCount == 7) ? 0 : 1;
}
