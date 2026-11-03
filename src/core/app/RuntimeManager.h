#pragma once
#include <memory>
#include <future>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <mutex>
#include <climits>
#include <cstdint>
#include <array>
#include <vector>
#include <chrono>

struct SharedContext;

namespace fw {

struct PackedTextureData;

enum class RuntimeFeature : uint32_t {
    None           = 0,
    GlobalVRAM     = 1 << 0, // Mega-Buffer da 2GB e Staging Ring
    JobSystem      = 1 << 1, // Pool di Thread asincroni
    PBRTextures    = 1 << 2, // Esecuzione TexturePacker
    PhysicsEngine  = 1 << 3, // Inizializzazione Mondo Fisico (Futuro)
    WorldSim       = 1 << 4  // Simulazione Meteo, AI Globale (Futuro)
};

inline RuntimeFeature operator|(RuntimeFeature a, RuntimeFeature b) {
    return static_cast<RuntimeFeature>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline RuntimeFeature operator&(RuntimeFeature a, RuntimeFeature b) {
    return static_cast<RuntimeFeature>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool HasFeature(uint32_t mask, RuntimeFeature feature) {
    return (mask & static_cast<uint32_t>(feature)) != 0;
}

// Macchina a stati per singola feature (Fase 2 Lifecycle: CPU Preparation -> GPU Commit)
enum class FeatureState : uint8_t {
    Uninitialized = 0,
    PreparingCpu  = 1,
    CpuReady      = 2,
    CommittingGpu = 3,
    GpuReady      = 4,
    Failed        = 5
};

constexpr size_t kMaxRuntimeFeatures = 5;

inline size_t FeatureToIndex(RuntimeFeature f) {
    switch (f) {
        case RuntimeFeature::GlobalVRAM:    return 0;
        case RuntimeFeature::JobSystem:     return 1;
        case RuntimeFeature::PBRTextures:   return 2;
        case RuntimeFeature::PhysicsEngine: return 3;
        case RuntimeFeature::WorldSim:      return 4;
        default: return 0;
    }
}

inline RuntimeFeature IndexToFeature(size_t idx) {
    switch (idx) {
        case 0: return RuntimeFeature::GlobalVRAM;
        case 1: return RuntimeFeature::JobSystem;
        case 2: return RuntimeFeature::PBRTextures;
        case 3: return RuntimeFeature::PhysicsEngine;
        case 4: return RuntimeFeature::WorldSim;
        default: return RuntimeFeature::None;
    }
}

// Metriche temporali misurate (CPU Prep vs GPU Commit)
struct FeatureTimingStats {
    double cpuPrepTimeMs = 0.0;
    double gpuCommitTimeMs = 0.0;
};

// Risultato verificato della sequenza di shutdown GPU.
// Ogni campo e' catturato dal sottosistema reale PRIMA della sua distruzione,
// non dedotto dalla sua assenza (nullptr != successo verificato).
struct RuntimeShutdownResult
{
    // CPU: catturato dopo StopAcceptingJobs() + Shutdown() (join workers)
    bool   jobSystemWasCreated = false;
    bool   cpuStopCalled      = false;   // StopAcceptingJobs e' stato chiamato
    size_t pendingCpuJobs     = SIZE_MAX; // SIZE_MAX = non catturato
    size_t activeWorkers      = SIZE_MAX;

    // DMA: catturato dopo Drain(), prima di delete
    bool   dmaManagerWasCreated = false;
    bool   dmaDrainCalled     = false;
    size_t pendingDmaTransfers     = SIZE_MAX;
    size_t liveDmaCommandBuffers   = SIZE_MAX;

    bool completed = false; // true solo se l'intera sequenza e' terminata
};

class RuntimeManager {
public:
    RuntimeManager(SharedContext* context);
    ~RuntimeManager();

    // Avvia l'attivazione di specifiche feature in background (CPU preparation asincrona).
    void RequireFeaturesAsync(RuntimeFeature featureMask);

    // Attiva specifiche feature in modo sincrono (CPU prep + GPU commit sul thread chiamante).
    void RequireFeaturesSync(RuntimeFeature featureMask);
    
    // Esegue i commit GPU per tutte le feature che hanno completato la CPU preparation.
    // Deve essere invocato dal thread di rendering (Main Render Thread).
    size_t ProcessGpuCommits(size_t maxCommits = SIZE_MAX);

    // Controlla se tutte le feature richieste sono in stato GpuReady
    bool IsReady() const;
    
    // Query granulari sulla macchina a stati per singola feature
    FeatureState GetFeatureState(RuntimeFeature feature) const;
    bool IsCpuReady(RuntimeFeature feature) const;
    bool IsGpuReady(RuntimeFeature feature) const;

    // Controllo attività worker asincrono CPU (preparazione in RAM)
    bool IsCpuWorkerActive() const {
        return m_workerActive.load() || (m_pendingCpuRequestsMask.load() != 0);
    }

    // Gestione indipendente della ownership (reference counting per schermata)
    void AcquireFeature(RuntimeFeature feature);
    void ReleaseFeature(RuntimeFeature feature);
    uint32_t GetFeatureRefCount(RuntimeFeature feature) const;

    // Metriche e tempi di preparazione/commit
    FeatureTimingStats GetFeatureTiming(RuntimeFeature feature) const;

    // Fault injection per collaudo di rollback e retry (RLC-04)
    void SimulateGpuCommitFailure(RuntimeFeature feature, bool enable);

    // Shutdown deterministico e privo di deadlock
    void ShutdownGpuRuntime();

    RuntimeFeature GetActiveFeatures() const { 
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);
        return m_activeFeatures; 
    }

    // C4 Diagnostic getters — read-only
    bool IsShutdownComplete() const { return m_gpuRuntimeShutdown; }
    const RuntimeShutdownResult& GetShutdownResult() const { return m_shutdownResult; }

private:
    // Helper CPU Preparation (eseguiti su worker thread senza Vulkan)
    void PrepareCpuGlobalVRAM();
    void PrepareCpuJobSystem();
    void PrepareCpuPBRTextures();

    // Helper GPU Commit (eseguiti rigorosamente sul Render Thread)
    bool CommitGpuGlobalVRAM();
    bool CommitGpuJobSystem();
    bool CommitGpuPBRTextures();

    // Rollback per feature fallita
    void RollbackFeature(size_t idx);

    // Worker loop asincrono
    void WorkerLoop();

    SharedContext* m_context = nullptr;
    mutable std::mutex m_lifecycleMutex;
    RuntimeFeature m_activeFeatures = RuntimeFeature::None;

    // Macchina a stati e conteggi ownership
    std::array<FeatureState, kMaxRuntimeFeatures> m_featureStates;
    std::array<uint32_t, kMaxRuntimeFeatures> m_featureRefCounts;
    std::array<FeatureTimingStats, kMaxRuntimeFeatures> m_featureTimings;
    std::array<bool, kMaxRuntimeFeatures> m_simulatedFailures;

    // Payload CPU in attesa di GPU commit
    std::unique_ptr<PackedTextureData> m_pendingPbrData;

    // Concorrenza e accorpamento richieste (worker dedicato permanente con condition variable)
    mutable std::mutex m_workerMutex;
    std::condition_variable m_workerCv;
    std::thread m_workerThread;
    std::atomic<uint32_t> m_pendingCpuRequestsMask{0};
    std::atomic<bool> m_workerActive{false};
    std::atomic<bool> m_cancelRequested{false};

    bool m_gpuRuntimeShutdown = false;
    bool m_jobSystemWasCreated = false;
    bool m_dmaManagerWasCreated = false;
    RuntimeShutdownResult m_shutdownResult;
};

} // namespace fw
