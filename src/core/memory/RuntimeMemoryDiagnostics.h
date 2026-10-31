#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace fw {
    class GameWorld;
    class JoltPhysicsSystem;
}
class StateManager;

namespace fw::diagnostics {

// =============================================================================
// FAIRWORLD — MEM-06: RUNTIME MEMORY DIAGNOSTICS & GAMEPLAY LEAK VERIFICATION
// Telemetria thread-safe, disattivabile e a zero-allocazioni sul critical path.
// =============================================================================

// 1. Metriche Processo OS (Windows PSAPI: RSS e Virtual Commit)
struct ProcessMemoryMetrics {
    size_t workingSetBytes = 0;       // Memoria fisica residente nel working set (RSS)
    size_t peakWorkingSetBytes = 0;   // Picco di memoria fisica residente
    size_t privateCommitBytes = 0;    // Memoria virtuale privata riservata/committata dal processo
    size_t pagefileUsageBytes = 0;    // Commit charge totale (pagefile / swap backing)
};

// 2. Metriche Heap Globale (FreeListAllocator 256MB Monolitico)
struct GlobalHeapMetrics {
    size_t reservedCapacityBytes = 0; // Capacità virtuale totale riservata (256 MB)
    size_t liveAllocatedBytes = 0;    // Byte attualmente allocati da oggetti vivi
    size_t peakAllocatedBytes = 0;    // Picco storico di byte allocati nell'arena
    size_t liveAllocationCount = 0;   // Numero di allocazioni attive (liveAllocs)
    size_t freeBytes = 0;             // Capacità attualmente libera nell'arena
    bool isInitialized = false;       // Stato Active
    bool isShutdown = false;          // Stato Shutdown
};

// 3. Metriche GameWorld & Sotto-Arene (Master Block 128MB)
struct GameWorldArenaMetrics {
    size_t masterBlockSizeBytes = 0;  // 128 MB se inizializzato
    size_t persistentUsedBytes = 0;   // 32 MB FreeListAllocator (oggetti persistenti del mondo)
    size_t chunkPoolUsedBytes = 0;    // 64 MB PoolAllocator (componenti voxel chunk a dimensione fissa)
    size_t frameStackUsedBytes = 0;   // 32 MB StackAllocator (geometria transiente per frame)
    size_t liveEntities = 0;          // Numero di entità ECS EnTT vive
    size_t loadedChunks = 0;          // Numero di chunk voxel residenti nel ChunkManager
    bool hasMasterBlock = false;
};

// 4. Metriche Sottosistema Eventi & Subscriptions
struct EventSystemMetrics {
    size_t activeSubscriptionCount = 0; // Listener vivi registrati nell'EventManager
};

// 5. Metriche StateManager & Stack degli Stati
struct StateLifecycleMetrics {
    size_t stateStackDepth = 0;       // Profondità corrente dello stack degli stati
    const char* topStateName = "";     // Nome dello stato in cima allo stack
};

// 6. Metriche Motore Fisico Jolt
struct JoltPhysicsMetrics {
    size_t tempAllocatorCapacityBytes = 0; // 10 MB buffer dedicato di simulazione
    size_t activeBodies = 0;               // Numero di corpi rigidi simulati (attivi)
    size_t totalBodies = 0;                // Numero totale di corpi registrati nel BodyManager
    size_t totalRegisteredConstraints = 0; // Vincoli totali registrati (GetConstraints().size())
    size_t maxContactConstraints = 1024;   // Capacità massima vincoli di contatto configurata
    size_t activeConstraints = static_cast<size_t>(-1); // UNAVAILABLE in Jolt v5.0.0
    bool activeConstraintsAvailable = false;
};

// Snapshot Globale di Telemetria Runtime
struct RuntimeMemorySnapshot {
    const char* label = "";
    uint64_t timestampNs = 0;
    ProcessMemoryMetrics os;
    GlobalHeapMetrics globalHeap;
    GameWorldArenaMetrics gameWorld;
    EventSystemMetrics eventSystem;
    StateLifecycleMetrics stateManager;
    JoltPhysicsMetrics jolt;
};

// Delta di Memoria e Analisi del Drift
struct RuntimeMemoryDelta {
    int64_t deltaWorkingSetBytes = 0;
    int64_t deltaPrivateCommitBytes = 0;
    int64_t deltaGlobalHeapBytes = 0;
    int64_t deltaGlobalHeapAllocs = 0;
    int64_t deltaLiveEntities = 0;
    int64_t deltaLoadedChunks = 0;
    int64_t deltaSubscriptions = 0;
    int64_t deltaStateDepth = 0;
    int64_t deltaBodies = 0;

    bool hasGlobalHeapDrift = false;
    bool hasEntityLeak = false;
    bool hasChunkLeak = false;
    bool hasSubscriptionLeak = false;
    bool isZeroDrift = false;
};

// Risultato di uno Scenario di Stress / Ciclo di Vita
struct ScenarioResult {
    std::string scenarioId;
    std::string scenarioName;
    int cyclesExecuted = 0;
    RuntimeMemorySnapshot baseline;
    RuntimeMemorySnapshot peak;
    RuntimeMemorySnapshot postCleanup;
    RuntimeMemoryDelta delta;
    bool passed = false;
    std::string evaluation; // Zero Drift, Acceptable Retention, Leak Detected
    std::string details;
};

// =============================================================================
// CLASSE DIAGNOSTICA RUNTIME
// =============================================================================
class RuntimeMemoryDiagnostics {
public:
    // Raccolta Metriche Individuali
    static ProcessMemoryMetrics QueryProcessMemory();
    static GlobalHeapMetrics QueryGlobalHeap();
    static GameWorldArenaMetrics QueryGameWorld(const fw::GameWorld* world);
    static EventSystemMetrics QueryEventSystem();
    static StateLifecycleMetrics QueryStateManager(const StateManager* sm);
    static JoltPhysicsMetrics QueryJolt(const fw::JoltPhysicsSystem* jolt);

    // Snapshot Completo
    static RuntimeMemorySnapshot TakeSnapshot(
        const char* label,
        const fw::GameWorld* world = nullptr,
        const StateManager* sm = nullptr,
        const fw::JoltPhysicsSystem* jolt = nullptr
    );

    // Calcolo Delta tra Snapshot
    static RuntimeMemoryDelta ComputeDelta(
        const RuntimeMemorySnapshot& before,
        const RuntimeMemorySnapshot& after
    );

    // Stampa Diagnostica Formattata
    static void PrintSnapshot(const RuntimeMemorySnapshot& snap);
    static void PrintDelta(const char* tag, const RuntimeMemoryDelta& delta);
};

// =============================================================================
// SUITE DI VERIFICA AUTOMATIZZATA SCENARI RUNTIME (MEM-06)
// =============================================================================
class RuntimeMemoryDiagnosticsSuite {
public:
    static std::vector<ScenarioResult> RunAllScenarios(int numCycles = 100);

    // Scenario 1: Avvio, Inizializzazione e Chiusura Ordinata Lifecycle
    static ScenarioResult Scenario01_EditorBootAndShutdownLifecycle();

    // Scenario 2: Hub -> BlockMaker -> Hub (Transazioni Mesh & Factory)
    static ScenarioResult Scenario02_HubToBlockMakerTransitions(int cycles = 100);

    // Scenario 3: Hub -> Forge -> Hub (Editing Strutture & Chunk Voxel)
    static ScenarioResult Scenario03_HubToForgeTransitions(int cycles = 100);

    // Scenario 4: Hub -> PhysicsLab -> Hub (Corpi Rigidi & Simulazione Jolt)
    static ScenarioResult Scenario04_HubToPhysicsLabTransitions(int cycles = 100);

    // Scenario 5: Creazione e Distruzione Ripetuta GameWorld (128MB Master Block)
    static ScenarioResult Scenario05_GameWorldArenaCreateDestroy(int cycles = 100);

    // Scenario 6: Caricamento, Modifica e Scaricamento Chunk Voxel
    static ScenarioResult Scenario06_ChunkStreamingAndUnload(int cycles = 100);

    // Scenario 7: Endurance 100 Cicli e Analisi del Drift Cumulativo
    static ScenarioResult Scenario07_ContinuousEnduranceAndDriftSlope(int cycles = 100);

    // Scenario 8: Chiusura Ordinata Applicazione e Verifica Drained vs Resident
    static ScenarioResult Scenario08_ApplicationShutdownDrainedVsResident();
};

} // namespace fw::diagnostics
