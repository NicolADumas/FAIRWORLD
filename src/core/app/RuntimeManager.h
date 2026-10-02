#pragma once
#include <memory>
#include <future>
#include <atomic>
#include <climits>

struct SharedContext;

namespace fw {

#include <cstdint>

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

    // Avvia l'attivazione di specifiche feature in background (se non già attive).
    void RequireFeaturesAsync(RuntimeFeature featureMask);
    
    // Controlla se l'operazione in background è terminata
    bool IsReady() const;
    
    void ShutdownGpuRuntime();

    RuntimeFeature GetActiveFeatures() const { return m_activeFeatures; }

    // C4 Diagnostic getters — read-only
    bool IsShutdownComplete() const { return m_gpuRuntimeShutdown; }
    const RuntimeShutdownResult& GetShutdownResult() const { return m_shutdownResult; }

private:
    void EnsureGlobalVRAM();
    void EnsureJobSystem();
    void EnsurePBRTextures();

    SharedContext* m_context = nullptr;
    RuntimeFeature m_activeFeatures = RuntimeFeature::None;
    
    std::future<void> m_asyncLoadTask;
    std::atomic<bool> m_isLoading{false};
    bool m_gpuRuntimeShutdown = false;
    bool m_jobSystemWasCreated = false;
    bool m_dmaManagerWasCreated = false;
    RuntimeShutdownResult m_shutdownResult;
};

} // namespace fw
