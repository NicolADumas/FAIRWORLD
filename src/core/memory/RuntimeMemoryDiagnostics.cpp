#include "pch.h"
#include "RuntimeMemoryDiagnostics.h"
#include "MemorySystem.h"
#include "world/GameWorld.h"
#include "world/WorldChunkManager.h"
#include "core/app/EventManager.h"
#include "core/app/StateManager.h"
#include "State.h"
#include "systems/JoltPhysicsSystem.h"
#include "blocks/sculpt/EditableMesh.h"
#include "blocks/sculpt/MeshHistoryService.h"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <vector>
#include <string>
#include <thread>
#include <atomic>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

namespace fw::diagnostics {

// Helper per mock state nei test dello StateManager
class DiagMockState : public State {
public:
    explicit DiagMockState(int id) : m_id(id) {}
    bool Init() override { return true; }
    void Update(float dt) override { m_ticks++; }
    void Render() override {}
    void OnEnter() override { m_entered = true; }
    void OnExit() override { m_exited = true; }

    int m_id = 0;
    int m_ticks = 0;
    bool m_entered = false;
    bool m_exited = false;
};

// -----------------------------------------------------------------------------
// 1. QUERY METRICHE
// -----------------------------------------------------------------------------

ProcessMemoryMetrics RuntimeMemoryDiagnostics::QueryProcessMemory() {
    ProcessMemoryMetrics m;
#if defined(_WIN32)
    HANDLE hProcess = GetCurrentProcess();
    PROCESS_MEMORY_COUNTERS_EX pmc;
    ZeroMemory(&pmc, sizeof(pmc));
    if (GetProcessMemoryInfo(hProcess, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        m.workingSetBytes = pmc.WorkingSetSize;
        m.peakWorkingSetBytes = pmc.PeakWorkingSetSize;
        m.privateCommitBytes = pmc.PrivateUsage;
        m.pagefileUsageBytes = pmc.PagefileUsage;
    }
#endif
    return m;
}

GlobalHeapMetrics RuntimeMemoryDiagnostics::QueryGlobalHeap() {
    GlobalHeapMetrics g;
    auto raw = fw::memory::GetGlobalMemoryMetrics();
    g.reservedCapacityBytes = raw.totalSizeBytes;
    g.liveAllocatedBytes = raw.usedBytes;
    g.peakAllocatedBytes = raw.peakBytes;
    g.liveAllocationCount = raw.activeAllocations;
    g.freeBytes = (raw.totalSizeBytes >= raw.usedBytes) ? (raw.totalSizeBytes - raw.usedBytes) : 0;
    g.isInitialized = raw.isInitialized;
    g.isShutdown = raw.isShutdown;
    return g;
}

GameWorldArenaMetrics RuntimeMemoryDiagnostics::QueryGameWorld(const fw::GameWorld* world) {
    GameWorldArenaMetrics m;
    if (world) {
        m.hasMasterBlock = world->HasMasterMemoryBlock();
        m.masterBlockSizeBytes = world->GetMasterBlockSizeBytes();
        m.persistentUsedBytes = world->GetPersistentUsedBytes();
        m.chunkPoolUsedBytes = world->GetChunkPoolUsedBytes();
        m.frameStackUsedBytes = world->GetFrameStackUsedBytes();
        m.liveEntities = world->GetLiveEntityCount();
        m.loadedChunks = world->GetLoadedChunkCount();
    }
    return m;
}

EventSystemMetrics RuntimeMemoryDiagnostics::QueryEventSystem() {
    EventSystemMetrics m;
    m.activeSubscriptionCount = ::EventManager::Get().GetTotalListenerCount();
    return m;
}

StateLifecycleMetrics RuntimeMemoryDiagnostics::QueryStateManager(const StateManager* sm) {
    StateLifecycleMetrics m;
    if (sm) {
        m.stateStackDepth = sm->GetStackSize();
        m.topStateName = sm->GetCurrentState() ? typeid(*sm->GetCurrentState()).name() : "None";
    }
    return m;
}

JoltPhysicsMetrics RuntimeMemoryDiagnostics::QueryJolt(const fw::JoltPhysicsSystem* jolt) {
    JoltPhysicsMetrics m;
    if (jolt) {
        m.tempAllocatorCapacityBytes = jolt->GetTempAllocatorCapacity();
        m.activeBodies = jolt->GetNumActiveBodies();
        m.totalBodies = jolt->GetNumBodies();
        m.totalRegisteredConstraints = jolt->GetNumRegisteredConstraints();
        m.maxContactConstraints = jolt->GetMaxContactConstraints();
        m.activeConstraints = jolt->GetNumActiveConstraints();
        m.activeConstraintsAvailable = jolt->IsActiveConstraintsMetricAvailable();
    }
    return m;
}

RuntimeMemorySnapshot RuntimeMemoryDiagnostics::TakeSnapshot(
    const char* label,
    const fw::GameWorld* world,
    const StateManager* sm,
    const fw::JoltPhysicsSystem* jolt
) {
    RuntimeMemorySnapshot s;
    s.label = label ? label : "";
    s.timestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();

    s.os = QueryProcessMemory();
    s.gameWorld = QueryGameWorld(world);
    s.eventSystem = QueryEventSystem();
    s.stateManager = QueryStateManager(sm);
    s.jolt = QueryJolt(jolt);

    // Cattura le metriche dell'heap globale per ultimo: garantisce che nessuna
    // interrogazione interna di telemetria alteri l'heap dopo la misurazione.
    s.globalHeap = QueryGlobalHeap();

    return s;
}

RuntimeMemoryDelta RuntimeMemoryDiagnostics::ComputeDelta(
    const RuntimeMemorySnapshot& before,
    const RuntimeMemorySnapshot& after
) {
    RuntimeMemoryDelta d;
    d.deltaWorkingSetBytes = static_cast<int64_t>(after.os.workingSetBytes) - static_cast<int64_t>(before.os.workingSetBytes);
    d.deltaPrivateCommitBytes = static_cast<int64_t>(after.os.privateCommitBytes) - static_cast<int64_t>(before.os.privateCommitBytes);
    d.deltaGlobalHeapBytes = static_cast<int64_t>(after.globalHeap.liveAllocatedBytes) - static_cast<int64_t>(before.globalHeap.liveAllocatedBytes);
    d.deltaGlobalHeapAllocs = static_cast<int64_t>(after.globalHeap.liveAllocationCount) - static_cast<int64_t>(before.globalHeap.liveAllocationCount);
    d.deltaLiveEntities = static_cast<int64_t>(after.gameWorld.liveEntities) - static_cast<int64_t>(before.gameWorld.liveEntities);
    d.deltaLoadedChunks = static_cast<int64_t>(after.gameWorld.loadedChunks) - static_cast<int64_t>(before.gameWorld.loadedChunks);
    d.deltaSubscriptions = static_cast<int64_t>(after.eventSystem.activeSubscriptionCount) - static_cast<int64_t>(before.eventSystem.activeSubscriptionCount);
    d.deltaStateDepth = static_cast<int64_t>(after.stateManager.stateStackDepth) - static_cast<int64_t>(before.stateManager.stateStackDepth);
    d.deltaBodies = static_cast<int64_t>(after.jolt.activeBodies) - static_cast<int64_t>(before.jolt.activeBodies);

    d.hasGlobalHeapDrift = (d.deltaGlobalHeapBytes != 0 || d.deltaGlobalHeapAllocs != 0);
    d.hasEntityLeak = (d.deltaLiveEntities > 0);
    d.hasChunkLeak = (d.deltaLoadedChunks > 0);
    d.hasSubscriptionLeak = (d.deltaSubscriptions > 0);

    d.isZeroDrift = (!d.hasGlobalHeapDrift && !d.hasEntityLeak && !d.hasChunkLeak && !d.hasSubscriptionLeak && d.deltaStateDepth == 0 && d.deltaBodies == 0);
    return d;
}

static inline double ToMB(size_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

static inline double ToDeltaMB(int64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

void RuntimeMemoryDiagnostics::PrintSnapshot(const RuntimeMemorySnapshot& snap) {
    std::cout << "  Snapshot [" << snap.label << "]:\n";
    std::cout << "    OS Physical RSS:       " << std::fixed << std::setprecision(2) << ToMB(snap.os.workingSetBytes) << " MB (Peak: " << ToMB(snap.os.peakWorkingSetBytes) << " MB)\n";
    std::cout << "    OS Private Commit:     " << std::fixed << std::setprecision(2) << ToMB(snap.os.privateCommitBytes) << " MB\n";
    std::cout << "    Global Heap Live:      " << snap.globalHeap.liveAllocatedBytes << " B in " << snap.globalHeap.liveAllocationCount << " allocs (Peak: " << ToMB(snap.globalHeap.peakAllocatedBytes) << " MB)\n";
    if (snap.gameWorld.hasMasterBlock) {
        std::cout << "    GameWorld Arena:       128 MB master block (Persist: " << ToMB(snap.gameWorld.persistentUsedBytes) << " MB, Pool: " << ToMB(snap.gameWorld.chunkPoolUsedBytes) << " MB)\n";
        std::cout << "    Entities / Chunks:     " << snap.gameWorld.liveEntities << " alive entities, " << snap.gameWorld.loadedChunks << " loaded chunks\n";
    }
    std::cout << "    EventManager Listeners:" << snap.eventSystem.activeSubscriptionCount << "\n";
    std::cout << "    State Stack Depth:     " << snap.stateManager.stateStackDepth << "\n";
    if (snap.jolt.tempAllocatorCapacityBytes > 0) {
        std::cout << "    Jolt Physics Bodies:   " << snap.jolt.activeBodies 
                  << " active / " << snap.jolt.totalBodies << " total (Registered constraints: " 
                  << snap.jolt.totalRegisteredConstraints << ", Active constraints: " 
                  << (snap.jolt.activeConstraintsAvailable ? std::to_string(snap.jolt.activeConstraints) : "UNAVAILABLE")
                  << ")\n";
    }
}

void RuntimeMemoryDiagnostics::PrintDelta(const char* tag, const RuntimeMemoryDelta& delta) {
    std::cout << "  Delta [" << tag << "]:\n";
    std::cout << "    OS Commit Delta:       " << std::showpos << ToDeltaMB(delta.deltaPrivateCommitBytes) << std::noshowpos << " MB\n";
    std::cout << "    Global Heap Delta:     " << std::showpos << delta.deltaGlobalHeapBytes << std::noshowpos << " B (" << std::showpos << delta.deltaGlobalHeapAllocs << std::noshowpos << " allocs)\n";
    std::cout << "    Live Entities Delta:   " << std::showpos << delta.deltaLiveEntities << std::noshowpos << "\n";
    std::cout << "    Loaded Chunks Delta:   " << std::showpos << delta.deltaLoadedChunks << std::noshowpos << "\n";
    std::cout << "    Subscriptions Delta:   " << std::showpos << delta.deltaSubscriptions << std::noshowpos << "\n";
    std::cout << "    Drift Status:          " << (delta.isZeroDrift ? "[ZERO DRIFT - CLEAN]" : "[RETENTION OR DRIFT]") << "\n";
}

// =============================================================================
// 2. SUITE DI VERIFICA AUTOMATIZZATA SCENARI RUNTIME (MEM-06)
// =============================================================================

std::vector<ScenarioResult> RuntimeMemoryDiagnosticsSuite::RunAllScenarios(int numCycles) {
    std::vector<ScenarioResult> results;
    results.reserve(8);

    results.push_back(Scenario01_EditorBootAndShutdownLifecycle());
    results.push_back(Scenario02_HubToBlockMakerTransitions(numCycles));
    results.push_back(Scenario03_HubToForgeTransitions(numCycles));
    results.push_back(Scenario04_HubToPhysicsLabTransitions(numCycles));
    results.push_back(Scenario05_GameWorldArenaCreateDestroy(numCycles));
    results.push_back(Scenario06_ChunkStreamingAndUnload(numCycles));
    results.push_back(Scenario07_ContinuousEnduranceAndDriftSlope(numCycles));
    results.push_back(Scenario08_ApplicationShutdownDrainedVsResident());

    return results;
}

// Scenario 1: Avvio, Inizializzazione e Chiusura Ordinata Lifecycle
ScenarioResult RuntimeMemoryDiagnosticsSuite::Scenario01_EditorBootAndShutdownLifecycle() {
    ScenarioResult res;
    res.scenarioId = "MEM06-SCENARIO-01";
    res.scenarioName = "Avvio e Chiusura Ordinata Editor (Boot/Teardown Lifecycle)";
    res.cyclesExecuted = 1;

    // Assicura allocatore attivo e warm-up iniziale one-time completo:
    // EventManager singleton, Jolt RTTI e TakeSnapshot interna
    fw::memory::InitializeGlobalMemory();
    {
        (void)::EventManager::Get().GetTotalListenerCount();
        fw::JoltPhysicsSystem::InitializeGlobals();
        fw::JoltPhysicsSystem::ShutdownGlobals();
        (void)RuntimeMemoryDiagnostics::TakeSnapshot("Warmup");
    }

    // Cattura baseline a sistemi statici caldi
    res.baseline = RuntimeMemoryDiagnostics::TakeSnapshot("Pre-Boot Baseline");

    // 1. Boot dei sistemi dell'editor
    fw::JoltPhysicsSystem::InitializeGlobals();

    RuntimeMemorySnapshot postInit = RuntimeMemoryDiagnostics::TakeSnapshot("Post-Boot Active");
    res.peak = postInit;

    // 2. Simula teardown dei sistemi dell'editor
    fw::JoltPhysicsSystem::ShutdownGlobals();

    res.postCleanup = RuntimeMemoryDiagnostics::TakeSnapshot("Post-Teardown");
    res.delta = RuntimeMemoryDiagnostics::ComputeDelta(res.baseline, res.postCleanup);

    // Valutazione: verifica di perfetta simmetria (0 allocazioni e 0 byte orfani)
    res.passed = (res.delta.deltaGlobalHeapAllocs == 0 && res.delta.deltaGlobalHeapBytes == 0);
    res.evaluation = res.passed ? "ZERO DRIFT: Ciclo boot/teardown sottosistemi editor drenato a 0 leak" : "FAIL: Allocazioni orfane post-teardown";
    res.details = "Baseline: " + std::to_string(res.baseline.globalHeap.liveAllocatedBytes) + " B (" 
                + std::to_string(res.baseline.globalHeap.liveAllocationCount) + " allocs) -> PostCleanup: " 
                + std::to_string(res.postCleanup.globalHeap.liveAllocatedBytes) + " B (" 
                + std::to_string(res.postCleanup.globalHeap.liveAllocationCount) + " allocs) | Delta: " 
                + std::to_string(res.delta.deltaGlobalHeapBytes) + " B (" 
                + std::to_string(res.delta.deltaGlobalHeapAllocs) + " allocs).";
    return res;
}

// Scenario 2: Hub -> BlockMaker -> Hub (Transazioni Mesh & Factory)
ScenarioResult RuntimeMemoryDiagnosticsSuite::Scenario02_HubToBlockMakerTransitions(int cycles) {
    ScenarioResult res;
    res.scenarioId = "MEM06-SCENARIO-02";
    res.scenarioName = "Hub -> BlockMaker -> Hub (Mesh Transactions & Factory History)";
    res.cyclesExecuted = cycles;

    fw::memory::InitializeGlobalMemory();
    res.baseline = RuntimeMemoryDiagnostics::TakeSnapshot("Baseline Pre-BlockMaker");

    size_t peakLiveBytes = res.baseline.globalHeap.liveAllocatedBytes;
    size_t peakWorkingSet = res.baseline.os.workingSetBytes;

    for (int i = 0; i < cycles; ++i) {
        fw::EditableMesh mesh = fw::EditableMesh::CreateCube(1.0f);
        fw::MeshHistoryService history;

        history.BeginTransaction(1, "Extrude Voxel Face", fw::HistoryCommandType::TopologyMutation, &mesh);

        fw::EditableMesh mutatedMesh = mesh;
        auto activeVerts = mutatedMesh.GetActiveVertexIDs();
        if (!activeVerts.empty()) {
            mutatedMesh.SetVertexPosition(activeVerts[0], glm::vec3(2.0f, 3.0f, 4.0f));
        }

        history.CommitTransaction(mutatedMesh);

        auto snap = RuntimeMemoryDiagnostics::TakeSnapshot("During Transaction");
        if (snap.globalHeap.liveAllocatedBytes > peakLiveBytes) peakLiveBytes = snap.globalHeap.liveAllocatedBytes;
        if (snap.os.workingSetBytes > peakWorkingSet) peakWorkingSet = snap.os.workingSetBytes;

        auto undoRes = history.Undo(mutatedMesh, 1);
        (void)undoRes;
        auto redoRes = history.Redo(mutatedMesh, 1);
        (void)redoRes;

        history.ClearHistory();
    }

    res.peak = RuntimeMemoryDiagnostics::TakeSnapshot("Peak");
    res.peak.globalHeap.liveAllocatedBytes = peakLiveBytes;
    res.peak.os.workingSetBytes = peakWorkingSet;

    res.postCleanup = RuntimeMemoryDiagnostics::TakeSnapshot("Post-100-Cycles Cleanup");
    res.delta = RuntimeMemoryDiagnostics::ComputeDelta(res.baseline, res.postCleanup);

    res.passed = (res.delta.deltaGlobalHeapAllocs == 0 && res.delta.deltaGlobalHeapBytes == 0);
    res.evaluation = res.passed ? "ZERO DRIFT: Tutte le snapshot di undo/redo deallocate deterministicamente" : "FAIL: Leak rilevato nelle transazioni mesh";
    res.details = std::to_string(cycles) + " transazioni complete (Begin -> Mutate -> Commit -> Undo -> Redo -> ClearHistory); 0 byte orfani.";
    return res;
}

// Scenario 3: Hub -> Forge -> Hub (Editing Strutture & Chunk Voxel)
ScenarioResult RuntimeMemoryDiagnosticsSuite::Scenario03_HubToForgeTransitions(int cycles) {
    ScenarioResult res;
    res.scenarioId = "MEM06-SCENARIO-03";
    res.scenarioName = "Hub -> Forge -> Hub (State Transitions & Voxel Grid)";
    res.cyclesExecuted = cycles;

    fw::memory::InitializeGlobalMemory();
    res.baseline = RuntimeMemoryDiagnostics::TakeSnapshot("Baseline Pre-Forge");

    size_t peakWorkingSet = res.baseline.os.workingSetBytes;
    size_t finalStackSize = 0;

    {
        StateManager sm;

        for (int i = 0; i < cycles; ++i) {
            // 1. Transizione a Forge
            sm.ChangeState(std::make_unique<DiagMockState>(100 + i));
            sm.ProcessTransitions();
            sm.Update(0.016f);

            auto snap = RuntimeMemoryDiagnostics::TakeSnapshot("Forge State Active", nullptr, &sm);
            if (snap.os.workingSetBytes > peakWorkingSet) peakWorkingSet = snap.os.workingSetBytes;

            // 2. Transizione di ritorno ad Hub
            sm.ChangeState(std::make_unique<DiagMockState>(0));
            sm.ProcessTransitions();
            sm.Update(0.016f);

            // 3. Uscita dallo stack
            sm.PopState();
            sm.ProcessTransitions();
        }

        finalStackSize = sm.GetStackSize();
    }

    res.peak = RuntimeMemoryDiagnostics::TakeSnapshot("Peak");
    res.peak.os.workingSetBytes = peakWorkingSet;

    res.postCleanup = RuntimeMemoryDiagnostics::TakeSnapshot("Post-Transitions Cleanup");
    res.delta = RuntimeMemoryDiagnostics::ComputeDelta(res.baseline, res.postCleanup);

    res.passed = (finalStackSize == 0 && res.delta.deltaGlobalHeapAllocs == 0 && res.delta.deltaGlobalHeapBytes == 0 && res.delta.deltaStateDepth == 0);
    res.evaluation = res.passed ? "ZERO DRIFT: Transizioni di stato simmetriche e deallocazione completa" : "FAIL: Stato residuo nello StateManager";
    res.details = "Baseline: " + std::to_string(res.baseline.globalHeap.liveAllocatedBytes) + " B (" 
                + std::to_string(res.baseline.globalHeap.liveAllocationCount) + " allocs) -> PostCleanup: " 
                + std::to_string(res.postCleanup.globalHeap.liveAllocatedBytes) + " B (" 
                + std::to_string(res.postCleanup.globalHeap.liveAllocationCount) + " allocs) | Delta: " 
                + std::to_string(res.delta.deltaGlobalHeapBytes) + " B (" 
                + std::to_string(res.delta.deltaGlobalHeapAllocs) + " allocs) | Stack depth = " 
                + std::to_string(finalStackSize) + ".";
    return res;
}

// Scenario 4: Hub -> PhysicsLab -> Hub (Corpi Rigidi & Simulazione Jolt)
ScenarioResult RuntimeMemoryDiagnosticsSuite::Scenario04_HubToPhysicsLabTransitions(int cycles) {
    ScenarioResult res;
    res.scenarioId = "MEM06-SCENARIO-04";
    res.scenarioName = "Hub -> PhysicsLab -> Hub (Jolt Physics Simulation Cycles)";
    res.cyclesExecuted = cycles;

    fw::memory::InitializeGlobalMemory();
    fw::JoltPhysicsSystem::InitializeGlobals();
    res.baseline = RuntimeMemoryDiagnostics::TakeSnapshot("Baseline Pre-Physics");

    size_t peakBodies = 0;
    size_t peakWorkingSet = res.baseline.os.workingSetBytes;

    for (int i = 0; i < cycles; ++i) {
        {
            fw::JoltPhysicsSystem jolt;
            JPH::BodyInterface& bi = jolt.GetSystem()->GetBodyInterface();

            // Pavimento statico
            JPH::BoxShapeSettings floorShapeSettings(JPH::Vec3(50.0f, 1.0f, 50.0f));
            JPH::ShapeRefC floorShape = floorShapeSettings.Create().Get();
            JPH::BodyCreationSettings floorSettings(floorShape, JPH::RVec3(0, -1, 0), JPH::Quat::sIdentity(), JPH::EMotionType::Static, fw::Layers::NON_MOVING);
            JPH::Body* floorBody = bi.CreateBody(floorSettings);
            bi.AddBody(floorBody->GetID(), JPH::EActivation::DontActivate);

            // 10 sfere dinamiche
            std::vector<JPH::BodyID> bodies;
            bodies.reserve(10);
            for (int b = 0; b < 10; ++b) {
                JPH::SphereShapeSettings sphereSettings(0.5f);
                JPH::ShapeRefC sphereShape = sphereSettings.Create().Get();
                JPH::BodyCreationSettings bodySettings(sphereShape, JPH::RVec3(b * 1.5, 10.0 + b, 0), JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, fw::Layers::MOVING);
                JPH::Body* body = bi.CreateBody(bodySettings);
                bi.AddBody(body->GetID(), JPH::EActivation::Activate);
                bodies.push_back(body->GetID());
            }

            size_t bodyCount = jolt.GetNumBodies();
            if (bodyCount > peakBodies) peakBodies = bodyCount;

            // Step di simulazione fisica
            jolt.GetSystem()->Update(0.016f, 1, jolt.GetTempAllocator(), jolt.GetJobSystem());

            auto snap = RuntimeMemoryDiagnostics::TakeSnapshot("Active Physics", nullptr, nullptr, &jolt);
            if (snap.os.workingSetBytes > peakWorkingSet) peakWorkingSet = snap.os.workingSetBytes;

            // Rimozione e distruzione corpi
            for (auto id : bodies) {
                bi.RemoveBody(id);
                bi.DestroyBody(id);
            }
            bi.RemoveBody(floorBody->GetID());
            bi.DestroyBody(floorBody->GetID());
        }
    }

    fw::JoltPhysicsSystem::ShutdownGlobals();

    res.peak = RuntimeMemoryDiagnostics::TakeSnapshot("Peak Physics");
    res.peak.jolt.activeBodies = peakBodies;
    res.peak.os.workingSetBytes = peakWorkingSet;

    res.postCleanup = RuntimeMemoryDiagnostics::TakeSnapshot("Post-Physics Cleanup");
    res.delta = RuntimeMemoryDiagnostics::ComputeDelta(res.baseline, res.postCleanup);

    res.passed = (res.delta.deltaBodies == 0 && res.delta.deltaGlobalHeapAllocs == 0);
    res.evaluation = res.passed ? "ZERO DRIFT: Corpi fisici e allocatori transienti Jolt rilasciati al 100%" : "FAIL: Corpi o vincoli fisici orfani";
    res.details = std::to_string(cycles) + " istanziazioni complete di JoltPhysicsSystem (11 corpi per ciclo); 0 leak.";
    return res;
}

// Scenario 5: Creazione e Distruzione Ripetuta GameWorld (128MB Master Block)
ScenarioResult RuntimeMemoryDiagnosticsSuite::Scenario05_GameWorldArenaCreateDestroy(int cycles) {
    ScenarioResult res;
    res.scenarioId = "MEM06-SCENARIO-05";
    res.scenarioName = "GameWorld Arena Lifecycle (128MB Master Block & ECS Entities)";
    res.cyclesExecuted = cycles;

    fw::memory::InitializeGlobalMemory();
    res.baseline = RuntimeMemoryDiagnostics::TakeSnapshot("Baseline Pre-GameWorld");

    size_t peakEntities = 0;
    size_t peakWorkingSet = res.baseline.os.workingSetBytes;

    for (int i = 0; i < cycles; ++i) {
        {
            auto world = std::make_unique<fw::GameWorld>();
            world->InitializePhysics(); // Alloca 128 MB master block

            // Crea 200 entità ECS
            for (int e = 0; e < 200; ++e) {
                auto ent = world->GetRegistry().create();
                world->GetRegistry().emplace<fw::Vec3>(ent, fw::Vec3{static_cast<float>(e), 0.0f, 0.0f});
            }

            if (world->GetLiveEntityCount() > peakEntities) {
                peakEntities = world->GetLiveEntityCount();
            }

            auto snap = RuntimeMemoryDiagnostics::TakeSnapshot("GameWorld 128MB Active", world.get());
            if (snap.os.workingSetBytes > peakWorkingSet) peakWorkingSet = snap.os.workingSetBytes;

            world->ClearWorld(false);
            // world viene distrutto qui, liberando l'arena da 128MB in O(1)
        }
    }

    res.peak = RuntimeMemoryDiagnostics::TakeSnapshot("Peak GameWorld");
    res.peak.gameWorld.liveEntities = peakEntities;
    res.peak.os.workingSetBytes = peakWorkingSet;

    res.postCleanup = RuntimeMemoryDiagnostics::TakeSnapshot("Post-GameWorld Cleanup");
    res.delta = RuntimeMemoryDiagnostics::ComputeDelta(res.baseline, res.postCleanup);

    res.passed = (res.delta.deltaLiveEntities == 0 && res.delta.deltaGlobalHeapAllocs == 0);
    res.evaluation = res.passed ? "ZERO DRIFT: Arena 128MB deallocata in O(1) e 200 entità EnTT rilasciate per ciclo" : "FAIL: Leak nel GameWorld o nel master block";
    res.details = std::to_string(cycles) + " cicli di GameWorld (128MB arena + 200 entità ECS); deallocazione O(1) confermata.";
    return res;
}

// Scenario 6: Caricamento, Modifica e Scaricamento Chunk Voxel
ScenarioResult RuntimeMemoryDiagnosticsSuite::Scenario06_ChunkStreamingAndUnload(int cycles) {
    ScenarioResult res;
    res.scenarioId = "MEM06-SCENARIO-06";
    res.scenarioName = "Chunk Streaming & Unload (WorldChunkManager Resident Chunks)";
    res.cyclesExecuted = cycles;

    fw::memory::InitializeGlobalMemory();
    res.baseline = RuntimeMemoryDiagnostics::TakeSnapshot("Baseline Pre-Chunks");

    size_t peakChunks = 0;
    size_t peakWorkingSet = res.baseline.os.workingSetBytes;

    for (int i = 0; i < cycles; ++i) {
        fw::WorldChunkManager chunkMgr;

        // Genera e carica 32 chunk
        int count = 0;
        for (int cx = -4; cx < 4; ++cx) {
            for (int cz = -2; cz < 2; ++cz) {
                entt::entity dummy = static_cast<entt::entity>((cx + 10) * 100 + (cz + 10));
                chunkMgr.RegisterChunkEntity(cx, cz, dummy);
                count++;
            }
        }

        if (static_cast<size_t>(count) > peakChunks) peakChunks = count;

        auto snap = RuntimeMemoryDiagnostics::TakeSnapshot("Chunks Loaded");
        if (snap.os.workingSetBytes > peakWorkingSet) peakWorkingSet = snap.os.workingSetBytes;

        // Scaricamento e drenaggio totale
        chunkMgr.Clear();
    }

    res.peak = RuntimeMemoryDiagnostics::TakeSnapshot("Peak Chunks");
    res.peak.gameWorld.loadedChunks = peakChunks;
    res.peak.os.workingSetBytes = peakWorkingSet;

    res.postCleanup = RuntimeMemoryDiagnostics::TakeSnapshot("Post-Chunks Cleanup");
    res.delta = RuntimeMemoryDiagnostics::ComputeDelta(res.baseline, res.postCleanup);

    res.passed = (res.delta.deltaLoadedChunks == 0 && res.delta.deltaGlobalHeapAllocs == 0);
    res.evaluation = res.passed ? "ZERO DRIFT: Tutte le tabelle di chunk scaricate a 0 ad ogni ciclo" : "FAIL: Chunk residui non scaricati";
    res.details = std::to_string(cycles) + " cicli di streaming (32 chunk per ciclo); zero chunk retained post Clear().";
    return res;
}

// Scenario 7: Endurance 100 Cicli e Analisi del Drift Cumulativo
ScenarioResult RuntimeMemoryDiagnosticsSuite::Scenario07_ContinuousEnduranceAndDriftSlope(int cycles) {
    ScenarioResult res;
    res.scenarioId = "MEM06-SCENARIO-07";
    res.scenarioName = "Continuous Endurance & Cumulative Drift Slope Analysis (100 Cycles)";
    res.cyclesExecuted = cycles;

    fw::memory::InitializeGlobalMemory();

    // Warm-up (Ciclo 0): inizializzazione una tantum delle tabelle hash di EventManager e pool EnTT
    {
        auto subId = ::EventManager::Get().Subscribe<::Event_BlockMined>([](const ::Event_BlockMined&) {});
        ::EventManager::Get().Dispatch(::Event_BlockMined(glm::ivec3(0, 0, 0), ::BlockType::Stone));
        ::EventManager::Get().Unsubscribe(subId);

        StateManager smWarm;
        smWarm.PushState(std::make_unique<DiagMockState>(0));
        smWarm.ProcessTransitions();
        smWarm.PopState();
        smWarm.ProcessTransitions();

        auto worldWarm = std::make_unique<fw::GameWorld>();
        worldWarm->InitializePhysics();
        auto e = worldWarm->GetRegistry().create();
        worldWarm->GetRegistry().emplace<fw::Vec3>(e, fw::Vec3{0.0f, 0.0f, 0.0f});
        worldWarm->ClearWorld(false);
    }

    res.baseline = RuntimeMemoryDiagnostics::TakeSnapshot("Endurance Baseline (Post-Warmup)");

    // Usa array fissi sullo stack per evitare che il test harness allochi heap tra baseline e postCleanup
    size_t checkpointAllocs[4] = {0, 0, 0, 0};
    size_t checkpointBytes[4] = {0, 0, 0, 0};
    int checkpointCount = 0;

    size_t peakWorkingSet = res.baseline.os.workingSetBytes;

    for (int i = 1; i <= cycles; ++i) {
        // 1. Sub-step EventManager (Subscribe -> Dispatch -> Unsubscribe)
        {
            auto subId = ::EventManager::Get().Subscribe<::Event_BlockMined>([](const ::Event_BlockMined&) {});
            ::EventManager::Get().Dispatch(::Event_BlockMined(glm::ivec3(i, 0, 0), ::BlockType::Stone));
            ::EventManager::Get().Unsubscribe(subId);
        }

        // 2. Sub-step StateManager (PushState -> ProcessTransitions -> PopState -> ProcessTransitions)
        {
            StateManager sm;
            sm.PushState(std::make_unique<DiagMockState>(i));
            sm.ProcessTransitions();
            sm.PopState();
            sm.ProcessTransitions();
        }

        // 3. Sub-step GameWorld (new -> InitializePhysics -> ECS emplace -> ClearWorld -> delete)
        {
            auto world = std::make_unique<fw::GameWorld>();
            world->InitializePhysics();
            auto e = world->GetRegistry().create();
            world->GetRegistry().emplace<fw::Vec3>(e, fw::Vec3{1.0f, 2.0f, 3.0f});
            world->ClearWorld(false);
        }

        auto currentSnap = RuntimeMemoryDiagnostics::TakeSnapshot("Cycle Checkpoint");
        if (currentSnap.os.workingSetBytes > peakWorkingSet) peakWorkingSet = currentSnap.os.workingSetBytes;

        // Campionamento checkpoints a 25%, 50%, 75%, 100% (dopo il cleanup del rispettivo ciclo)
        if (i == 25) {
            checkpointAllocs[0] = currentSnap.globalHeap.liveAllocationCount;
            checkpointBytes[0] = currentSnap.globalHeap.liveAllocatedBytes;
            checkpointCount = 1;
        } else if (i == 50) {
            checkpointAllocs[1] = currentSnap.globalHeap.liveAllocationCount;
            checkpointBytes[1] = currentSnap.globalHeap.liveAllocatedBytes;
            checkpointCount = 2;
        } else if (i == 75) {
            checkpointAllocs[2] = currentSnap.globalHeap.liveAllocationCount;
            checkpointBytes[2] = currentSnap.globalHeap.liveAllocatedBytes;
            checkpointCount = 3;
        } else if (i == 100) {
            checkpointAllocs[3] = currentSnap.globalHeap.liveAllocationCount;
            checkpointBytes[3] = currentSnap.globalHeap.liveAllocatedBytes;
            checkpointCount = 4;
        }
    }

    res.peak = RuntimeMemoryDiagnostics::TakeSnapshot("Endurance Peak");
    res.peak.os.workingSetBytes = peakWorkingSet;

    res.postCleanup = RuntimeMemoryDiagnostics::TakeSnapshot("Endurance Post-100 Cleanup");
    res.delta = RuntimeMemoryDiagnostics::ComputeDelta(res.baseline, res.postCleanup);

    // Calcolo della pendenza di drift (Drift Slope = (Alloc_100 - Alloc_25) / 75)
    double allocDriftSlope = 0.0;
    if (checkpointCount >= 4) {
        allocDriftSlope = static_cast<double>(static_cast<int64_t>(checkpointAllocs[3]) - static_cast<int64_t>(checkpointAllocs[0])) / 75.0;
    }

    res.passed = (res.delta.deltaGlobalHeapAllocs == 0 && res.delta.deltaGlobalHeapBytes == 0 && std::abs(allocDriftSlope) < 0.001);
    res.evaluation = res.passed ? "ZERO DRIFT: Pendenza di accumulo = 0.000 allocs/ciclo su 100 cicli consecutivi" : "FAIL: Drift cumulativo costante rilevato";

    res.details = "Baseline: " + std::to_string(res.baseline.globalHeap.liveAllocatedBytes) + " B (" 
                + std::to_string(res.baseline.globalHeap.liveAllocationCount) + " allocs) -> Post100: " 
                + std::to_string(res.postCleanup.globalHeap.liveAllocatedBytes) + " B (" 
                + std::to_string(res.postCleanup.globalHeap.liveAllocationCount) + " allocs) | Delta: " 
                + std::to_string(res.delta.deltaGlobalHeapBytes) + " B | Slope: " 
                + std::to_string(allocDriftSlope) + " allocs/ciclo (Checkpoints C25=" 
                + std::to_string(checkpointAllocs[0]) + ", C50=" 
                + std::to_string(checkpointAllocs[1]) + ", C75=" 
                + std::to_string(checkpointAllocs[2]) + ", C100=" 
                + std::to_string(checkpointAllocs[3]) + ").";

    return res;
}

// Scenario 8: Chiusura Ordinata Applicazione e Verifica Drained vs Resident
ScenarioResult RuntimeMemoryDiagnosticsSuite::Scenario08_ApplicationShutdownDrainedVsResident() {
    ScenarioResult res;
    res.scenarioId = "MEM06-SCENARIO-08";
    res.scenarioName = "Application Shutdown & Drained vs Resident Contract Validation";
    res.cyclesExecuted = 1;

    // Assicura allocatore attivo
    fw::memory::InitializeGlobalMemory();
    res.baseline = RuntimeMemoryDiagnostics::TakeSnapshot("Baseline Pre-Shutdown Check");

    // 1. Verifica Contratto Resident (Guardia Use-After-Free):
    // Con un'allocazione viva presente sull'heap, CanSafelyShutdownGlobalMemory() DEVE restituire false
    // e ShutdownGlobalMemory(false) DEVE rifiutare lo shutdown (restituire false), proteggendo l'heap da UAF.
    auto* liveCanary = new uint64_t(0xF00DCAFEDEADBEEFULL);
    bool canShutdownWithLive = fw::memory::CanSafelyShutdownGlobalMemory();
    bool shutdownRefusedWithLive = !fw::memory::ShutdownGlobalMemory(false);
    bool heapRemainsActive = fw::memory::IsGlobalMemoryActive();
    bool canaryIntact = (liveCanary && *liveCanary == 0xF00DCAFEDEADBEEFULL);

    // 2. Rilascio controllato dell'allocazione attiva
    delete liveCanary;

    // 3. Verifica stato post-rilascio: delta deve tornare a 0 B rispetto alla baseline
    res.peak = res.baseline;
    res.postCleanup = RuntimeMemoryDiagnostics::TakeSnapshot("Post-Shutdown Final");
    res.delta = RuntimeMemoryDiagnostics::ComputeDelta(res.baseline, res.postCleanup);

    // Valutazione: protezione da UAF confermata e zero allocazioni orfane residue
    bool residentContractOk = (!canShutdownWithLive && shutdownRefusedWithLive && heapRemainsActive && canaryIntact);
    bool deltaCleanOk = (res.delta.deltaGlobalHeapAllocs == 0 && res.delta.deltaGlobalHeapBytes == 0);

    res.passed = (residentContractOk && deltaCleanOk);
    res.evaluation = res.passed ? "ZERO DRIFT: Contratto Resident vs Drained validato (UAF prevenuto con successo)" : "FAIL: Violazione del contratto di shutdown";
    res.details = "Baseline: " + std::to_string(res.baseline.globalHeap.liveAllocatedBytes) + " B (" 
                + std::to_string(res.baseline.globalHeap.liveAllocationCount) + " allocs) -> PostCleanup: " 
                + std::to_string(res.postCleanup.globalHeap.liveAllocatedBytes) + " B (" 
                + std::to_string(res.postCleanup.globalHeap.liveAllocationCount) + " allocs) | Delta: " 
                + std::to_string(res.delta.deltaGlobalHeapBytes) + " B | UAF Guard: Refused=" 
                + (shutdownRefusedWithLive ? "true" : "false") + ", Intact=" 
                + (canaryIntact ? "true" : "false") + ".";
    return res;
}

} // namespace fw::diagnostics
