#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace fw {

// =============================================================================
// MEM-05: GAMEPLAY LIFECYCLE STRESS TEST SUITE (STRESS-01 .. STRESS-08)
// Verifica empirica che il gameplay non accumuli memoria CPU o risorse logiche
// durante cicli ripetuti di: Create -> Load -> Simulate -> Unload -> Destroy
// =============================================================================

struct StressTestMetricSample {
    size_t liveEntities = 0;
    size_t activeWorlds = 0;
    size_t registeredListeners = 0;
    size_t loadedChunks = 0;
    size_t physicsBodies = 0;
    size_t activeConstraints = 0;
    size_t referencedSnapshots = 0;
    size_t liveCpuAllocations = 0;
    size_t residentMemoryBytes = 0;
};

struct StressTestResult {
    std::string testId;
    std::string subsystemName;
    int totalCycles = 0;
    StressTestMetricSample initialMetrics;
    StressTestMetricSample peakMetrics;
    StressTestMetricSample finalMetrics;
    bool passed = false;
    std::string details;
    std::string memoryEvaluation; // Leak vs Retention vs Intentionally Preserved
};

class GameplayLifecycleStressTests {
public:
    static std::vector<StressTestResult> RunAllStressTests();

    // STRESS-01: StateManager & State Transitions (50 cicli push/pop/change)
    static StressTestResult Test01_StateManagerStress();

    // STRESS-02: GameWorld & ECS EnTT (25 cicli con 200 entità/componenti ciascuno)
    static StressTestResult Test02_GameWorldAndEcsStress();

    // STRESS-03: Chunk Streaming & Unload (20 cicli con 32 chunk voxel)
    static StressTestResult Test03_ChunkStreamingAndUnloadStress();

    // STRESS-04: Planet Hierarchy & LOD Spherical Geometry (30 cicli)
    static StressTestResult Test04_PlanetHierarchyAndLodStress();

    // STRESS-05: Block Maker / Sculpting Factory Transactions (25 cicli undo/redo)
    static StressTestResult Test05_BlockMakerFactoryTransactionStress();

    // STRESS-06: PhysicsLab & Jolt Physics Simulation (15 cicli con 21 corpi rigidi)
    static StressTestResult Test06_PhysicsLabAndJoltStress();

    // STRESS-07: Registri e Snapshot Scientifici (25 cicli cross-validation)
    static StressTestResult Test07_ScientificRegistrySnapshotStress();

    // STRESS-08: EventManager Subscriptions & AsyncInput (20 cicli concorrenti)
    static StressTestResult Test08_EventManagerAndAsyncInputStress();
};

} // namespace fw
