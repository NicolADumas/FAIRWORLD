#pragma once
// ============================================================
//  FAIRWORLD -- PHASE C4: LIFECYCLE FREEZE GATE
// ============================================================
//
//  Due gate separati, ciascuno con la propria ownership:
//
//  RUNTIME LIFECYCLE GATE
//  - Posizione: DOPO ShutdownGpuRuntime(), PRIMA di RenderManager::Shutdown()
//  - Legge RuntimeShutdownResult (stato catturato prima dei delete)
//  - NON verifica i semaphore del RenderManager (ancora legittimamente vivi)
//  - Verifica: CPU stopped/joined/drained + DMA drained + runtime complete
//
//  FINAL VULKAN OWNERSHIP GATE
//  - Posizione: DOPO RenderManager::Shutdown(), PRIMA di VulkanResourceTracker::Shutdown()
//  - Verifica: liveSemaphores == 0 (tutte le risorse Vulkan owner da FAIRWORLD distrutte)
//
//  Filosofia: real internal state -> snapshot -> condition evaluation -> PASS/FAIL
//  NO nullptr == successo implicito.
//  NO [PASS] senza stato realmente osservato.
// ============================================================

#include <cstdint>
#include <cstddef>

struct SharedContext;

namespace fw {

struct RuntimeShutdownResult;  // forward — definito in RuntimeManager.h

// ─────────────────────────────────────────────────────────────
//  RuntimeLifecycleSnapshot
//  Stato del Runtime Gate, letto da RuntimeShutdownResult.
//  Tutti i valori CPU e DMA sono catturati prima della distruzione.
// ─────────────────────────────────────────────────────────────
struct RuntimeLifecycleSnapshot
{
    // [1] PRODUCERS — dallo stato reale catturato
    bool   cpuStopCalled             = false;  // StopAcceptingJobs fu chiamato
    bool   dmaDrainCalled            = false;  // Drain() fu chiamato

    // [2] ASYNC SUBSYSTEMS — valori osservati dopo join/drain, prima di delete
    size_t pendingCpuJobs            = SIZE_MAX;  // SIZE_MAX = non catturato
    size_t activeWorkers             = SIZE_MAX;
    size_t pendingDmaTransfers       = SIZE_MAX;
    size_t liveDmaCommandBuffers     = SIZE_MAX;

    // [3] RUNTIME STATE
    bool   runtimeShutdownComplete   = false;
    bool   gameWorldReleased         = false;

    // [4] NON MISURABILI — esposti come WARN
    bool   cacheInvalidated          = false;
    bool   cacheInvalidatedKnown     = false;  // false -> [WARN] nel report

    // Meta: la cattura e' affidabile solo se shutdownResultAvailable == true
    bool   shutdownResultAvailable   = false;
};

// ─────────────────────────────────────────────────────────────
//  VulkanFinalSnapshot
//  Stato del Final Vulkan Gate, letto dopo RenderManager::Shutdown().
// ─────────────────────────────────────────────────────────────
struct VulkanFinalSnapshot
{
    uint32_t liveSemaphores = 0;
    // In futuro: altri handle Vulkan tracciati (pipeline, descriptor pool, ecc.)
};

// ─────────────────────────────────────────────────────────────
//  LifecycleFreezeGate
// ─────────────────────────────────────────────────────────────
class LifecycleFreezeGate
{
public:
    // ── RUNTIME LIFECYCLE GATE ────────────────────────────────
    // Chiamato DOPO ShutdownGpuRuntime(), PRIMA di RenderManager::Shutdown()
    static RuntimeLifecycleSnapshot CollectRuntime(SharedContext* context);
    static bool EvaluateRuntime(const RuntimeLifecycleSnapshot& snap);
    static void PrintRuntimeReport(const RuntimeLifecycleSnapshot& snap);
    // Helper combinato: Collect + Evaluate + Print
    static bool RunRuntimeGate(SharedContext* context);

    // ── FINAL VULKAN OWNERSHIP GATE ───────────────────────────
    // Chiamato DOPO RenderManager::Shutdown(), PRIMA di VulkanResourceTracker::Shutdown()
    static VulkanFinalSnapshot CollectVulkanFinal();
    static bool EvaluateVulkanFinal(const VulkanFinalSnapshot& snap);
    static void PrintVulkanFinalReport(const VulkanFinalSnapshot& snap);
    // Helper combinato: Collect + Evaluate + Print
    static bool RunVulkanFinalGate();

private:
    LifecycleFreezeGate() = delete;
};

// ─────────────────────────────────────────────────────────────
//  LifecycleStressMode (Step C4.7)
// ─────────────────────────────────────────────────────────────
enum class LifecycleStressMode
{
    None,
    CpuLoad,          // Genera solo carico CPU (job sintetici)
    DmaLoad,          // Genera carico CPU + DMA upload
    FullRuntimeLoad   // CpuLoad + DmaLoad + verifica stato reale prima dello shutdown
};

} // namespace fw
