#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// FAIRWORLD — P0.3C3 RUNTIME LIFECYCLE CONTRACT TESTS (RLC-01 .. RLC-05)
// Collaudo empirico del modello a 2 fasi: CPU Preparation -> GPU Commit.
// =============================================================================

struct RuntimeLifecycleTestResult {
    std::string testId;
    std::string testName;
    std::string targetRequirement;
    bool passed = false;
    std::string expected;
    std::string observed;
    std::string details;
    double durationMs = 0.0;
};

class RuntimeLifecycleContractTests {
public:
    static std::vector<RuntimeLifecycleTestResult> RunAllTests();

    // RLC-01: Richieste concorrenti durante la fase finale o di arresto del worker (no lost requests / batching atomico)
    static RuntimeLifecycleTestResult Test01_ConcurrentRequestsDuringWorkerTermination();

    // RLC-02: Transizione rapida cross-state (Hub -> Play -> BlockMaker) con risoluzione DAG dipendenze
    static RuntimeLifecycleTestResult Test02_RapidCrossStateTransition_Hub_Play_BlockMaker();

    // RLC-03: Chiusura dello stato e cancellazione durante la preparazione CPU asincrona
    static RuntimeLifecycleTestResult Test03_StateExitDuringCpuPreparation();

    // RLC-04: Gestione errori durante il commit GPU con rollback e successivo retry sicuro
    static RuntimeLifecycleTestResult Test04_GpuCommitFailureRollbackAndRetry();

    // RLC-05: Indipendenza di ownership delle feature condivise attraverso le schermate (nessuna distruzione prematura)
    static RuntimeLifecycleTestResult Test05_SharedFeatureIndependentOwnershipAcrossScreens();
};

} // namespace fw
