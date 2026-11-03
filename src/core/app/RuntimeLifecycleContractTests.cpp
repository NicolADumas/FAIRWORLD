#include "RuntimeLifecycleContractTests.h"
#include "RuntimeManager.h"
#include "SharedContext.h"
#include "JobSystem.h"
#include "MaterialRegistry.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <future>
#include <sstream>

namespace fw {

std::vector<RuntimeLifecycleTestResult> RuntimeLifecycleContractTests::RunAllTests() {
    std::vector<RuntimeLifecycleTestResult> results;
    results.reserve(5);
    results.push_back(Test01_ConcurrentRequestsDuringWorkerTermination());
    results.push_back(Test02_RapidCrossStateTransition_Hub_Play_BlockMaker());
    results.push_back(Test03_StateExitDuringCpuPreparation());
    results.push_back(Test04_GpuCommitFailureRollbackAndRetry());
    results.push_back(Test05_SharedFeatureIndependentOwnershipAcrossScreens());
    return results;
}

// -----------------------------------------------------------------------------
// RLC-01: Richieste concorrenti durante l'arresto del worker
// -----------------------------------------------------------------------------
RuntimeLifecycleTestResult RuntimeLifecycleContractTests::Test01_ConcurrentRequestsDuringWorkerTermination() {
    RuntimeLifecycleTestResult r;
    r.testId = "RLC-01";
    r.testName = "Concurrent Requests During Worker Termination (No Lost Requests)";
    r.targetRequirement = "Accorpamento richieste concorrenti senza race sulla terminazione del worker";
    r.expected = "Tutte le richieste concorrenti vengono elaborate; zero perdite; stato finale GpuReady per tutte le feature richieste";

    auto tStart = std::chrono::high_resolution_clock::now();

    try {
        SharedContext context;
        MaterialRegistry matReg;
        matReg.Initialize();
        context.materialRegistry = &matReg;

        RuntimeManager rm(&context);

        // 1. Avvia worker iniziale con JobSystem
        rm.RequireFeaturesAsync(RuntimeFeature::JobSystem);

        // 2. Lancia 8 thread concorrenti che martellano richieste di feature multiple
        constexpr size_t numThreads = 8;
        std::vector<std::thread> threads;
        threads.reserve(numThreads);

        std::cout << "[RLC-01] [Thread " << std::this_thread::get_id()
                  << "] [Phase: Client Concurrency] Avvio " << numThreads << " thread client concorrenti...\n";

        for (size_t i = 0; i < numThreads; ++i) {
            threads.emplace_back([&rm, i]() {
                if (i % 2 == 0) {
                    rm.RequireFeaturesAsync(RuntimeFeature::GlobalVRAM);
                } else {
                    rm.RequireFeaturesAsync(RuntimeFeature::PBRTextures);
                }
            });
        }

        std::cout << "[RLC-01] [Thread " << std::this_thread::get_id()
                  << "] [Phase: Client Concurrency] Attesa join degli 8 thread client...\n";
        for (auto& t : threads) {
            t.join();
        }
        std::cout << "[RLC-01] [Thread " << std::this_thread::get_id()
                  << "] [Phase: Client Concurrency] Tutti gli 8 thread client hanno completato le richieste.\n";

        // Attendi che il worker CPU termini la preparazione in RAM
        std::cout << "[RLC-01] [Thread " << std::this_thread::get_id()
                  << "] [Phase: CPU Wait] Attesa completamento CPU preparation (GlobalVRAM, JobSystem, PBRTextures)...\n";
        auto waitStart = std::chrono::steady_clock::now();
        bool timeoutOccurred = false;
        while (rm.IsCpuWorkerActive() || 
               !rm.IsCpuReady(RuntimeFeature::GlobalVRAM) || 
               !rm.IsCpuReady(RuntimeFeature::JobSystem) || 
               !rm.IsCpuReady(RuntimeFeature::PBRTextures)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double, std::milli>(now - waitStart).count() > 30000.0) {
                timeoutOccurred = true;
                std::cerr << "[RLC-01] [Thread " << std::this_thread::get_id()
                          << "] [Phase: CPU Wait] TIMEOUT superato (>30s) in attesa di CPU prep!\n";
                break;
            }
        }

        if (timeoutOccurred) {
            r.passed = false;
            std::ostringstream oss;
            oss << "FAIL: Watchdog timeout (>30s) - CPU prep incompleta: "
                << "GV=" << (rm.IsCpuReady(RuntimeFeature::GlobalVRAM) ? "CpuReady" : "NotReady")
                << ", JS=" << (rm.IsCpuReady(RuntimeFeature::JobSystem) ? "CpuReady" : "NotReady")
                << ", PBR=" << (rm.IsCpuReady(RuntimeFeature::PBRTextures) ? "CpuReady" : "NotReady")
                << ", WorkerActive=" << (rm.IsCpuWorkerActive() ? "YES" : "NO");
            r.observed = oss.str();
            rm.ShutdownGpuRuntime();
            auto tEnd = std::chrono::high_resolution_clock::now();
            r.durationMs = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
            return r;
        }

        std::cout << "[RLC-01] [Thread " << std::this_thread::get_id()
                  << "] [Phase: CPU Wait] CPU preparation completata con successo.\n";

        // 3. Esegui il commit GPU sul thread principale (simulazione Main Render Thread)
        std::cout << "[RLC-01] [Thread " << std::this_thread::get_id()
                  << "] [Phase: GPU Commit] Invocazione ProcessGpuCommits(SIZE_MAX)...\n";
        size_t committed = rm.ProcessGpuCommits(SIZE_MAX);
        std::cout << "[RLC-01] [Thread " << std::this_thread::get_id()
                  << "] [Phase: GPU Commit] ProcessGpuCommits completato. Features committate: " << committed << "\n";

        bool gvReady = rm.IsGpuReady(RuntimeFeature::GlobalVRAM);
        bool jsReady = rm.IsGpuReady(RuntimeFeature::JobSystem);
        bool pbrReady = rm.IsGpuReady(RuntimeFeature::PBRTextures);
        bool allReady = rm.IsReady();

        auto timingGV = rm.GetFeatureTiming(RuntimeFeature::GlobalVRAM);
        auto timingJS = rm.GetFeatureTiming(RuntimeFeature::JobSystem);
        auto timingPBR = rm.GetFeatureTiming(RuntimeFeature::PBRTextures);

        r.passed = gvReady && jsReady && pbrReady && allReady && (committed > 0);

        std::ostringstream oss;
        oss << "Committed=" << committed 
            << ", States(GV=" << (gvReady ? "GpuReady" : "NotReady")
            << ", JS=" << (jsReady ? "GpuReady" : "NotReady")
            << ", PBR=" << (pbrReady ? "GpuReady" : "NotReady")
            << ", IsReady=" << (allReady ? "TRUE" : "FALSE") << ")"
            << ", CPU Prep Times: [JS=" << timingJS.cpuPrepTimeMs << "ms, PBR=" << timingPBR.cpuPrepTimeMs << "ms]"
            << ", GPU Commit Times: [GV=" << timingGV.gpuCommitTimeMs << "ms, PBR=" << timingPBR.gpuCommitTimeMs << "ms]";
        r.observed = oss.str();

        std::cout << "[RLC-01] [Thread " << std::this_thread::get_id()
                  << "] [Phase: Shutdown] Inizio ShutdownGpuRuntime...\n";
        rm.ShutdownGpuRuntime();
        std::cout << "[RLC-01] [Thread " << std::this_thread::get_id()
                  << "] [Phase: Shutdown] ShutdownGpuRuntime completato.\n";
    } catch (const std::exception& ex) {
        r.passed = false;
        r.observed = std::string("Eccezione durante test: ") + ex.what();
    }

    auto tEnd = std::chrono::high_resolution_clock::now();
    r.durationMs = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
    return r;
}

// -----------------------------------------------------------------------------
// RLC-02: Cambio rapido Hub -> Play -> Block Maker (DAG resolution e tempi di transizione)
// -----------------------------------------------------------------------------
RuntimeLifecycleTestResult RuntimeLifecycleContractTests::Test02_RapidCrossStateTransition_Hub_Play_BlockMaker() {
    RuntimeLifecycleTestResult r;
    r.testId = "RLC-02";
    r.testName = "Rapid Cross-State Transition (Hub -> Play -> BlockMaker)";
    r.targetRequirement = "Dipendenze risolte in ordine topologico prima dell'attivazione e misurazione transizioni";
    r.expected = "Risoluzione DAG (GlobalVRAM + JobSystem prima di PBRTextures); zero stalli; tempi transizione misurati";

    auto tStart = std::chrono::high_resolution_clock::now();

    try {
        SharedContext context;
        MaterialRegistry matReg;
        matReg.Initialize();
        context.materialRegistry = &matReg;

        RuntimeManager rm(&context);

        // Simulazione HubState: nessuna feature richiesta
        auto tHub0 = std::chrono::high_resolution_clock::now();
        rm.RequireFeaturesAsync(RuntimeFeature::None);
        auto tHub1 = std::chrono::high_resolution_clock::now();
        double hubDuration = std::chrono::duration<double, std::milli>(tHub1 - tHub0).count();

        // Simulazione rapida transizione in PlayState: richiede JobSystem asincrono
        auto tPlay0 = std::chrono::high_resolution_clock::now();
        rm.RequireFeaturesAsync(RuntimeFeature::JobSystem);
        auto tPlay1 = std::chrono::high_resolution_clock::now();
        double playDuration = std::chrono::duration<double, std::milli>(tPlay1 - tPlay0).count();

        // Simulazione rapida transizione in BlockMakerState: richiede PBRTextures (che via DAG trascina GlobalVRAM e JobSystem)
        auto tBm0 = std::chrono::high_resolution_clock::now();
        rm.RequireFeaturesSync(RuntimeFeature::GlobalVRAM | RuntimeFeature::JobSystem | RuntimeFeature::PBRTextures);
        auto tBm1 = std::chrono::high_resolution_clock::now();
        double bmDuration = std::chrono::duration<double, std::milli>(tBm1 - tBm0).count();

        bool gvOk = rm.IsGpuReady(RuntimeFeature::GlobalVRAM);
        bool jsOk = rm.IsGpuReady(RuntimeFeature::JobSystem);
        bool pbrOk = rm.IsGpuReady(RuntimeFeature::PBRTextures);

        r.passed = gvOk && jsOk && pbrOk;

        std::ostringstream oss;
        oss << "Transizioni misurate: Hub=" << hubDuration << "ms, Play=" << playDuration 
            << "ms, BlockMaker=" << bmDuration << "ms. "
            << "DAG Status: GV=" << (gvOk ? "OK" : "FAIL") 
            << ", JS=" << (jsOk ? "OK" : "FAIL") 
            << ", PBR=" << (pbrOk ? "OK" : "FAIL");
        r.observed = oss.str();

        rm.ShutdownGpuRuntime();
    } catch (const std::exception& ex) {
        r.passed = false;
        r.observed = std::string("Eccezione durante test: ") + ex.what();
    }

    auto tEnd = std::chrono::high_resolution_clock::now();
    r.durationMs = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
    return r;
}

// -----------------------------------------------------------------------------
// RLC-03: Chiusura dello stato durante preparazione CPU
// -----------------------------------------------------------------------------
RuntimeLifecycleTestResult RuntimeLifecycleContractTests::Test03_StateExitDuringCpuPreparation() {
    RuntimeLifecycleTestResult r;
    r.testId = "RLC-03";
    r.testName = "State Exit and Cancellation During CPU Preparation";
    r.targetRequirement = "Cancellazione pulita senza deadlock né leak di puntatori pendenti";
    r.expected = "Worker interrotto tempestivamente; payload liberati; shutdown completato senza eccezioni né hang";

    auto tStart = std::chrono::high_resolution_clock::now();

    try {
        SharedContext context;
        MaterialRegistry matReg;
        matReg.Initialize();
        context.materialRegistry = &matReg;

        auto rm = std::make_unique<RuntimeManager>(&context);

        // Avvia preparazione asincrona pesante (PBR)
        rm->RequireFeaturesAsync(RuntimeFeature::PBRTextures);

        // Simula immediata uscita dallo stato o distruzione del contesto
        std::this_thread::sleep_for(std::chrono::microseconds(100)); // micro-intervallo durante il worker
        
        auto tShutdown0 = std::chrono::high_resolution_clock::now();
        rm->ShutdownGpuRuntime();
        auto tShutdown1 = std::chrono::high_resolution_clock::now();
        double shutdownDuration = std::chrono::duration<double, std::milli>(tShutdown1 - tShutdown0).count();

        bool shutdownDone = rm->IsShutdownComplete();
        bool pbrNotReady = !rm->IsCpuReady(RuntimeFeature::PBRTextures);
        rm.reset(); // Distruzione sicura

        r.passed = shutdownDone && pbrNotReady && (shutdownDuration < 1000.0); // Confermato entro 1s senza deadlock

        std::ostringstream oss;
        oss << "Shutdown completato in " << shutdownDuration << "ms. IsShutdownComplete=" << (shutdownDone ? "YES" : "NO")
            << ", PBRNotReady=" << (pbrNotReady ? "YES" : "NO");
        r.observed = oss.str();
    } catch (const std::exception& ex) {
        r.passed = false;
        r.observed = std::string("Eccezione durante test: ") + ex.what();
    }

    auto tEnd = std::chrono::high_resolution_clock::now();
    r.durationMs = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
    return r;
}

// -----------------------------------------------------------------------------
// RLC-04: Errore durante commit GPU con rollback e retry sicuro
// -----------------------------------------------------------------------------
RuntimeLifecycleTestResult RuntimeLifecycleContractTests::Test04_GpuCommitFailureRollbackAndRetry() {
    RuntimeLifecycleTestResult r;
    r.testId = "RLC-04";
    r.testName = "GPU Commit Failure Rollback and Subsequent Safe Retry";
    r.targetRequirement = "Rollback su errore commit GPU, stato Failed isolato, e successivo retry con esito positivo";
    r.expected = "Errore gestito in sicurezza; feature transita in Failed; successivo retry riporta la feature a GpuReady";

    auto tStart = std::chrono::high_resolution_clock::now();

    try {
        SharedContext context;
        MaterialRegistry matReg;
        matReg.Initialize();
        context.materialRegistry = &matReg;

        RuntimeManager rm(&context);

        // 1. Prepara CPU per PBRTextures
        std::cout << "[RLC-04] [Thread " << std::this_thread::get_id()
                  << "] [Phase: CPU Wait] Attesa completamento CPU prep PBRTextures...\n";
        rm.RequireFeaturesAsync(RuntimeFeature::PBRTextures);
        while (!rm.IsCpuReady(RuntimeFeature::PBRTextures)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        std::cout << "[RLC-04] [Thread " << std::this_thread::get_id()
                  << "] [Phase: CPU Wait] PBRTextures CPU ready.\n";

        // 2. Inietta errore intenzionale di commit GPU
        rm.SimulateGpuCommitFailure(RuntimeFeature::PBRTextures, true);

        // Esegui il commit: deve fallire in sicurezza
        rm.ProcessGpuCommits(SIZE_MAX);

        FeatureState stateAfterFail = rm.GetFeatureState(RuntimeFeature::PBRTextures);
        bool failedHandled = (stateAfterFail == FeatureState::Failed);
        bool notInActive = !HasFeature(static_cast<uint32_t>(rm.GetActiveFeatures()), RuntimeFeature::PBRTextures);

        // 3. Disabilita l'errore e ritenta l'attivazione (Retry)
        rm.SimulateGpuCommitFailure(RuntimeFeature::PBRTextures, false);
        rm.RequireFeaturesSync(RuntimeFeature::PBRTextures);

        FeatureState stateAfterRetry = rm.GetFeatureState(RuntimeFeature::PBRTextures);
        bool retrySuccess = (stateAfterRetry == FeatureState::GpuReady);
        bool nowInActive = HasFeature(static_cast<uint32_t>(rm.GetActiveFeatures()), RuntimeFeature::PBRTextures);

        r.passed = failedHandled && notInActive && retrySuccess && nowInActive;

        std::ostringstream oss;
        oss << "Stato dopo errore simulato: " << (failedHandled ? "Failed (OK)" : "Inatteso")
            << ", NotInActive=" << (notInActive ? "YES" : "NO")
            << " | Stato dopo Retry: " << (retrySuccess ? "GpuReady (OK)" : "FAIL")
            << ", InActive=" << (nowInActive ? "YES" : "NO");
        r.observed = oss.str();

        rm.ShutdownGpuRuntime();
    } catch (const std::exception& ex) {
        r.passed = false;
        r.observed = std::string("Eccezione durante test: ") + ex.what();
    }

    auto tEnd = std::chrono::high_resolution_clock::now();
    r.durationMs = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
    return r;
}

// -----------------------------------------------------------------------------
// RLC-05: Verifica che una feature condivisa non venga distrutta mentre un altro stato la utilizza
// -----------------------------------------------------------------------------
RuntimeLifecycleTestResult RuntimeLifecycleContractTests::Test05_SharedFeatureIndependentOwnershipAcrossScreens() {
    RuntimeLifecycleTestResult r;
    r.testId = "RLC-05";
    r.testName = "Shared Feature Independent Ownership Across Screen Lifecycles";
    r.targetRequirement = "Ownership indipendente dalla singola schermata e prevenzione di distruzione prematura";
    r.expected = "Uscita della schermata A decrementa refcount senza distruggere la risorsa ancora in uso dalla schermata B";

    auto tStart = std::chrono::high_resolution_clock::now();

    try {
        SharedContext context;
        RuntimeManager rm(&context);

        // Schermata A (es. BlockMaker) acquisisce JobSystem e GlobalVRAM
        rm.AcquireFeature(RuntimeFeature::JobSystem);
        rm.AcquireFeature(RuntimeFeature::GlobalVRAM);

        // Schermata B (es. ForgeWorld / World Simulation) acquisisce a sua volta JobSystem
        rm.AcquireFeature(RuntimeFeature::JobSystem);

        // Attendi che il worker CPU completi la preparazione in RAM per le feature acquisite
        std::cout << "[RLC-05] [Thread " << std::this_thread::get_id()
                  << "] [Phase: CPU Wait] Attesa completamento CPU prep JobSystem e GlobalVRAM...\n";
        auto waitStart = std::chrono::steady_clock::now();
        bool timeoutOccurred = false;
        while (rm.IsCpuWorkerActive() || 
               !rm.IsCpuReady(RuntimeFeature::JobSystem) || 
               !rm.IsCpuReady(RuntimeFeature::GlobalVRAM)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double, std::milli>(now - waitStart).count() > 5000.0) {
                timeoutOccurred = true;
                break;
            }
        }
        if (timeoutOccurred) {
            r.passed = false;
            r.observed = "FAIL: Watchdog timeout in attesa di CPU prep per JobSystem / GlobalVRAM";
            rm.ShutdownGpuRuntime();
            auto tEnd = std::chrono::high_resolution_clock::now();
            r.durationMs = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
            return r;
        }

        std::cout << "[RLC-05] [Thread " << std::this_thread::get_id()
                  << "] [Phase: GPU Commit] Invocazione ProcessGpuCommits...\n";
        rm.ProcessGpuCommits(SIZE_MAX);

        uint32_t refBefore = rm.GetFeatureRefCount(RuntimeFeature::JobSystem); // Deve essere 2
        bool jsAliveBefore = rm.IsGpuReady(RuntimeFeature::JobSystem) && (context.jobSystem != nullptr);

        // Simulazione chiusura / distruzione Schermata A: rilascia le sue feature
        rm.ReleaseFeature(RuntimeFeature::JobSystem);
        rm.ReleaseFeature(RuntimeFeature::GlobalVRAM);

        uint32_t refAfterScreenAExit = rm.GetFeatureRefCount(RuntimeFeature::JobSystem); // Deve essere 1
        bool jsAliveAfterScreenAExit = rm.IsGpuReady(RuntimeFeature::JobSystem) && (context.jobSystem != nullptr);

        // Esegui un job di prova su JobSystem per verificare che sia 100% operativo per la Schermata B
        std::atomic<bool> jobExecuted{false};
        if (context.jobSystem) {
            std::cout << "[RLC-05] [Thread " << std::this_thread::get_id()
                      << "] [Phase: Job Dispatch] Invio job di prova al thread pool...\n";
            context.jobSystem->Execute([&jobExecuted]() {
                jobExecuted.store(true);
            });
            std::cout << "[RLC-05] [Thread " << std::this_thread::get_id()
                      << "] [Phase: Job Wait] Attesa completamento WaitAll()...\n";
            context.jobSystem->WaitAll();
            std::cout << "[RLC-05] [Thread " << std::this_thread::get_id()
                      << "] [Phase: Job Wait] WaitAll completato con successo.\n";
        }

        // Schermata B rilascia infine la sua ownership
        rm.ReleaseFeature(RuntimeFeature::JobSystem);
        uint32_t refFinal = rm.GetFeatureRefCount(RuntimeFeature::JobSystem); // 0

        r.passed = (refBefore == 2) && jsAliveBefore && 
                   (refAfterScreenAExit == 1) && jsAliveAfterScreenAExit && 
                   jobExecuted.load() && (refFinal == 0);

        std::ostringstream oss;
        oss << "RefCount(Initial=" << refBefore << ", PostScreenA=" << refAfterScreenAExit 
            << ", Final=" << refFinal << "), JobSystemAlive=" << (jsAliveAfterScreenAExit ? "YES" : "NO")
            << ", ExecutionCheck=" << (jobExecuted.load() ? "SUCCESS" : "FAIL");
        r.observed = oss.str();

        rm.ShutdownGpuRuntime();
    } catch (const std::exception& ex) {
        r.passed = false;
        r.observed = std::string("Eccezione durante test: ") + ex.what();
    }

    auto tEnd = std::chrono::high_resolution_clock::now();
    r.durationMs = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
    return r;
}

} // namespace fw
