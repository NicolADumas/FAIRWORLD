#include "pch.h"
#include "LifecycleFreezeGate.h"
#include "SharedContext.h"
#include "RuntimeManager.h"
#include "VulkanResourceTracker.h"
#include <iostream>
#include <iomanip>

// VulkanDmaManager e JobSystem NON includili qui:
// il Gate legge RuntimeShutdownResult, non i subsystem direttamente.
// I subsystem sono gia' stati distrutti quando il Gate viene invocato.

namespace fw {

// ─────────────────────────────────────────────────────────────
//  Helpers locali
// ─────────────────────────────────────────────────────────────
static const char* PASS  = "[PASS]";
static const char* FAIL  = "[FAIL]";
static const char* WARN  = "[WARN]";

static const char* PassFail(bool cond) { return cond ? PASS : FAIL; }

static void PrintRow(const char* label, size_t value, bool pass)
{
    // Stampa con allineamento. SIZE_MAX significa "non catturato".
    std::cout << "      " << std::left << std::setw(30) << label;
    if (value == SIZE_MAX) {
        std::cout << std::right << std::setw(5) << "N/C" << "   " << WARN << " (not captured)\n";
    } else {
        std::cout << std::right << std::setw(5) << value << "   " << PassFail(pass) << "\n";
    }
}

static void PrintRowBool(const char* label, bool value, bool passWhenTrue)
{
    std::cout << "      " << std::left << std::setw(30) << label
              << std::right << std::setw(5) << (value ? "YES" : "NO")
              << "   " << PassFail(value == passWhenTrue) << "\n";
}

// ─────────────────────────────────────────────────────────────
//  RUNTIME GATE — CollectRuntime
//
//  PRECONDIZIONE:
//   - ShutdownGpuRuntime() gia' completato (m_shutdownResult.completed == true)
//   - jobSystem e dmaManager sono nullptr (distrutti)
//   - context->gameWorld e' gia' stato nullificato da FAIRWORLD::Shutdown()
//   - RenderManager NON e' ancora stato distrutto (device Vulkan vivo)
// ─────────────────────────────────────────────────────────────
RuntimeLifecycleSnapshot LifecycleFreezeGate::CollectRuntime(SharedContext* context)
{
    RuntimeLifecycleSnapshot snap;

    if (!context) {
        // Senza context non possiamo leggere nulla.
        // I campi rimangono ai default (SIZE_MAX / false).
        return snap;
    }

    if (!context->runtimeManager) {
        // RuntimeManager distrutto prima del gate: non possiamo verificare.
        // shutdownResultAvailable rimane false -> report segnalera' il problema.
        return snap;
    }

    const RuntimeShutdownResult& result = context->runtimeManager->GetShutdownResult();

    snap.shutdownResultAvailable = result.completed;

    if (!result.completed) {
        // La sequenza di shutdown non e' terminata: valori non affidabili.
        std::cerr << "[LifecycleFreezeGate] WARNING: RuntimeShutdownResult.completed == false. "
                  << "ShutdownGpuRuntime() potrebbe non essere stato chiamato prima del Gate.\n";
        return snap;
    }

    // ── [1] PRODUCERS ──────────────────────────────────────────
    snap.cpuStopCalled  = result.cpuStopCalled;
    snap.dmaDrainCalled = result.dmaDrainCalled;

    // ── [2] ASYNC SUBSYSTEMS — valori catturati prima dei delete ─
    snap.pendingCpuJobs        = result.pendingCpuJobs;       // deve essere 0 dopo join
    snap.activeWorkers         = result.activeWorkers;        // deve essere 0 dopo join
    snap.pendingDmaTransfers   = result.pendingDmaTransfers;  // deve essere 0 dopo drain
    snap.liveDmaCommandBuffers = result.liveDmaCommandBuffers;// deve essere 0 dopo drain

    // ── [3] RUNTIME STATE ─────────────────────────────────────
    snap.runtimeShutdownComplete = context->runtimeManager->IsShutdownComplete();
    snap.gameWorldReleased       = (context->gameWorld == nullptr);

    // ── [4] NON MISURABILI ────────────────────────────────────
    // CacheManager non espone un flag di stato post-flush.
    snap.cacheInvalidated      = false;
    snap.cacheInvalidatedKnown = false;

    return snap;
}

// ─────────────────────────────────────────────────────────────
//  RUNTIME GATE — EvaluateRuntime
// ─────────────────────────────────────────────────────────────
bool LifecycleFreezeGate::EvaluateRuntime(const RuntimeLifecycleSnapshot& snap)
{
    if (!snap.shutdownResultAvailable) return false;

    return
        snap.cpuStopCalled                    &&
        snap.dmaDrainCalled                   &&
        snap.pendingCpuJobs        == 0       &&
        snap.activeWorkers         == 0       &&
        snap.pendingDmaTransfers   == 0       &&
        snap.liveDmaCommandBuffers == 0       &&
        snap.runtimeShutdownComplete          &&
        snap.gameWorldReleased;
    // cacheInvalidated escluso intenzionalmente: invariante non misurabile
}

// ─────────────────────────────────────────────────────────────
//  RUNTIME GATE — PrintRuntimeReport
// ─────────────────────────────────────────────────────────────
void LifecycleFreezeGate::PrintRuntimeReport(const RuntimeLifecycleSnapshot& snap)
{
    bool frozen = EvaluateRuntime(snap);

    std::cout << "\n====================================================\n";
    std::cout << " FAIRWORLD RUNTIME LIFECYCLE GATE\n";
    std::cout << "====================================================\n\n";

    if (!snap.shutdownResultAvailable) {
        std::cout << " [ERROR] RuntimeShutdownResult non disponibile.\n"
                  << "         ShutdownGpuRuntime() non e' stato chiamato prima del Gate.\n\n"
                  << "====================================================\n"
                  << " RUNTIME LIFECYCLE: UNSTABLE (FAIL - no capture)\n"
                  << "====================================================\n\n";
        return;
    }

    // ── [1/3] PRODUCERS
    std::cout << "[1/3] PRODUCERS\n";
    PrintRowBool("CPU StopAcceptingJobs called", snap.cpuStopCalled,  true);
    PrintRowBool("DMA Drain called",             snap.dmaDrainCalled, true);
    std::cout << "\n";

    // ── [2/3] ASYNC SUBSYSTEMS
    std::cout << "[2/3] ASYNC SUBSYSTEMS (captured before delete)\n";
    PrintRow("Pending CPU jobs",         snap.pendingCpuJobs,        snap.pendingCpuJobs == 0);
    PrintRow("Active workers",           snap.activeWorkers,         snap.activeWorkers == 0);
    PrintRow("Pending DMA transfers",    snap.pendingDmaTransfers,   snap.pendingDmaTransfers == 0);
    PrintRow("Live DMA command buffers", snap.liveDmaCommandBuffers, snap.liveDmaCommandBuffers == 0);
    std::cout << "\n";

    // ── [3/3] RUNTIME STATE
    std::cout << "[3/3] RUNTIME STATE\n";
    PrintRowBool("Runtime shutdown complete", snap.runtimeShutdownComplete, true);

    // cacheInvalidated: non misurabile -> WARN
    if (snap.cacheInvalidatedKnown) {
        PrintRowBool("Cache invalidated", snap.cacheInvalidated, true);
    } else {
        std::cout << "      " << std::left << std::setw(30) << "Cache invalidated"
                  << std::right << std::setw(5) << "N/A"
                  << "   " << WARN << " not observable (CacheManager has no flush flag)\n";
    }

    PrintRowBool("GameWorld released", snap.gameWorldReleased, true);
    std::cout << "\n";

    // ── RISULTATO
    std::cout << "====================================================\n";
    if (frozen) {
        std::cout << " RUNTIME LIFECYCLE: FROZEN (100% PASS)\n";
    } else {
        std::cout << " RUNTIME LIFECYCLE: UNSTABLE (FAIL)\n";
    }
    std::cout << "====================================================\n\n";
}

// ─────────────────────────────────────────────────────────────
//  RUNTIME GATE — Run (helper combinato)
// ─────────────────────────────────────────────────────────────
bool LifecycleFreezeGate::RunRuntimeGate(SharedContext* context)
{
    RuntimeLifecycleSnapshot snap = CollectRuntime(context);
    PrintRuntimeReport(snap);
    return EvaluateRuntime(snap);
}

// ─────────────────────────────────────────────────────────────
//  FINAL VULKAN OWNERSHIP GATE — CollectVulkanFinal
//
//  PRECONDIZIONE:
//   - RenderManager::Shutdown() gia' completato
//   - Tutti i semaphore RenderManager sono stati distrutti
//   - VulkanResourceTracker::Shutdown() NON ancora chiamato (stamperebbe solo dopo)
// ─────────────────────────────────────────────────────────────
VulkanFinalSnapshot LifecycleFreezeGate::CollectVulkanFinal()
{
    VulkanFinalSnapshot snap;
    snap.liveSemaphores = VulkanResourceTracker::Get().GetLiveSemaphoreCount();
    return snap;
}

// ─────────────────────────────────────────────────────────────
//  FINAL VULKAN OWNERSHIP GATE — EvaluateVulkanFinal
// ─────────────────────────────────────────────────────────────
bool LifecycleFreezeGate::EvaluateVulkanFinal(const VulkanFinalSnapshot& snap)
{
    return snap.liveSemaphores == 0;
}

// ─────────────────────────────────────────────────────────────
//  FINAL VULKAN OWNERSHIP GATE — PrintVulkanFinalReport
// ─────────────────────────────────────────────────────────────
void LifecycleFreezeGate::PrintVulkanFinalReport(const VulkanFinalSnapshot& snap)
{
    bool ok = EvaluateVulkanFinal(snap);

    std::cout << "\n====================================================\n";
    std::cout << " FAIRWORLD FINAL VULKAN OWNERSHIP GATE\n";
    std::cout << " (post RenderManager::Shutdown)\n";
    std::cout << "====================================================\n\n";

    PrintRow("Tracked Semaphores LIVE", snap.liveSemaphores, snap.liveSemaphores == 0);
    std::cout << "\n";

    std::cout << "====================================================\n";
    if (ok) {
        std::cout << " VULKAN OWNERSHIP: CLEAN (PASS)\n";
    } else {
        std::cout << " VULKAN OWNERSHIP: LEAK DETECTED (FAIL)\n";
    }
    std::cout << "====================================================\n\n";
}

// ─────────────────────────────────────────────────────────────
//  FINAL VULKAN OWNERSHIP GATE — Run (helper combinato)
// ─────────────────────────────────────────────────────────────
bool LifecycleFreezeGate::RunVulkanFinalGate()
{
    VulkanFinalSnapshot snap = CollectVulkanFinal();
    PrintVulkanFinalReport(snap);
    return EvaluateVulkanFinal(snap);
}

} // namespace fw
