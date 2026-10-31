#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// MEM-07A: LIFECYCLE & OWNERSHIP REMEDIATION CONTRACT TESTS (LC-01 .. LC-05)
// Verifica empirica rigorosa dell'ordine di distruzione, assenza di dangling
// pointers in SharedContext, idempotenza del cleanup fisico e zero listener leaks.
// =============================================================================

struct LifecycleTestResult {
    std::string testId;
    std::string testName;
    std::string targetFinding;
    bool passed = false;
    std::string expected;
    std::string observed;
    std::string details;
};

class LifecycleRemediationContractTests {
public:
    static std::vector<LifecycleTestResult> RunAllTests();

    // LC-01: Sequenza Cross-State reale: BlockMaker -> Forge -> Hub -> BlockMaker (F01)
    static LifecycleTestResult Test01_CrossStateTransition_BlockMaker_Forge_Hub_BlockMaker();

    // LC-02: Distruzione di BlockMaker con ForgeWorld condiviso / ripristino sicuro (F01)
    static LifecycleTestResult Test02_BlockMakerDestruction_SharedForgeWorldPreserved();

    // LC-03: Shutdown con stato PhysicsLab attivo e corpi rigidi Jolt in simulazione (F03, F04)
    static LifecycleTestResult Test03_ShutdownWithActivePhysicsLabState();

    // LC-04: Shutdown con listener EventManager registrati e verifica zero-leak (F02)
    static LifecycleTestResult Test04_ShutdownWithRegisteredEventManagerListeners();

    // LC-05: Inizializzazione parziale di stati e StateManager seguita da cleanup immediato (F03, F04)
    static LifecycleTestResult Test05_PartialInitializationFollowedByCleanup();

    // LC-06: Distruzione GameWorld senza riallocazioni né risurrezione del cancel token (MEM07-F05)
    static LifecycleTestResult Test06_GameWorldTeardownNoReallocations();

    // LC-07: Allocazioni over-aligned e accoppiamento corretto _aligned_malloc/free (MEM07-F06)
    static LifecycleTestResult Test07_AlignedAllocatorFallbackAndPairing();

    // LC-08: StateManager rollback da Init() fallita, ReplaceState e robustezza (MEM07-F07)
    static LifecycleTestResult Test08_StateManagerFailureRollbackAndReplace();
};

} // namespace fw
