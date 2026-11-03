#include "pch.h"
#include "RuntimeManager.h"
#include "SharedContext.h"
#include "RenderManager.h"
#include "TexturePacker.h"
#include "VulkanDmaManager.h"
#include "VramSlabAllocator.h"
#include "JobSystem.h"
#include "FAIRWORLD.h"
#include "MaterialRegistry.h"
#include <iostream>
#include <chrono>
#include <thread>

namespace fw {

RuntimeManager::RuntimeManager(SharedContext* context) : m_context(context) {
    for (size_t i = 0; i < kMaxRuntimeFeatures; ++i) {
        m_featureStates[i] = FeatureState::Uninitialized;
        m_featureRefCounts[i] = 0;
        m_simulatedFailures[i] = false;
        m_featureTimings[i] = FeatureTimingStats{};
    }
    m_workerThread = std::thread([this]() { WorkerLoop(); });
    std::cout << "[RuntimeManager] Creato. Architettura a due fasi (CPU Prep -> GPU Commit) attiva.\n";
}

RuntimeManager::~RuntimeManager() {
    ShutdownGpuRuntime();
    std::cout << "[RuntimeManager] Distrutto.\n";
}

void RuntimeManager::ShutdownGpuRuntime() {
    if (m_gpuRuntimeShutdown) return;
    
    auto tShutdownStart = std::chrono::high_resolution_clock::now();

    // 1. Richiedi cancellazione al worker asincrono e attendine la terminazione pulita fuori dai lock
    m_cancelRequested.store(true);
    m_workerCv.notify_all();

    double workerJoinMs = 0.0;
    if (m_workerThread.joinable()) {
        std::cout << "[RuntimeManager] [Thread " << std::this_thread::get_id()
                  << "] [Phase: Shutdown] Richiesta cancellazione worker e attesa join...\n";
        auto tJoinStart = std::chrono::high_resolution_clock::now();
        m_workerThread.join();
        auto tJoinEnd = std::chrono::high_resolution_clock::now();
        workerJoinMs = std::chrono::duration<double, std::milli>(tJoinEnd - tJoinStart).count();
        std::cout << "[RuntimeManager] [Thread " << std::this_thread::get_id()
                  << "] [Phase: Shutdown] m_workerThread terminato (tempo join worker: "
                  << workerJoinMs << " ms).\n";
    }
    m_workerActive.store(false);

    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    m_gpuRuntimeShutdown = true;

    // 2. Rilascia payload CPU pendenti
    m_pendingPbrData.reset();

    for (size_t i = 0; i < kMaxRuntimeFeatures; ++i) {
        m_featureStates[i] = FeatureState::Uninitialized;
        m_featureRefCounts[i] = 0;
    }
    m_activeFeatures = RuntimeFeature::None;

    m_shutdownResult.jobSystemWasCreated = m_jobSystemWasCreated;
    m_shutdownResult.dmaManagerWasCreated = m_dmaManagerWasCreated;

    // 3. Sequenza di distruzione controllata dei sottosistemi GPU e JobSystem
    double jobSystemShutdownMs = 0.0;
    if (m_context) {
        if (m_context->jobSystem) {
            std::cout << "\n[RuntimeShutdown]\nStop accepting jobs\n";
            m_context->jobSystem->StopAcceptingJobs();
            m_shutdownResult.cpuStopCalled = true;

            std::cout << "[RuntimeManager] [Thread " << std::this_thread::get_id()
                      << "] [Phase: Shutdown] Attesa terminazione thread pool JobSystem (Shutdown join)...\n";
            auto tJsStart = std::chrono::high_resolution_clock::now();
            m_context->jobSystem->Shutdown(); // JOIN: blocca fino a workers = 0
            auto tJsEnd = std::chrono::high_resolution_clock::now();
            jobSystemShutdownMs = std::chrono::duration<double, std::milli>(tJsEnd - tJsStart).count();
            std::cout << "[RuntimeManager] [Thread " << std::this_thread::get_id()
                      << "] [Phase: Shutdown] JobSystem workers terminati con successo (tempo arresto JobSystem: "
                      << jobSystemShutdownMs << " ms).\n";
            
            m_shutdownResult.pendingCpuJobs = m_context->jobSystem->GetPendingJobCount();
            m_shutdownResult.activeWorkers  = m_context->jobSystem->GetActiveWorkerCount();

            std::cout << "\n[RuntimeShutdown]\nWorkers joined\n"
                      << "Pending jobs   : " << m_shutdownResult.pendingCpuJobs << "\n"
                      << "Active workers : " << m_shutdownResult.activeWorkers  << "\n";

            delete m_context->jobSystem;
            m_context->jobSystem = nullptr;
        }
        
        if (m_context->dmaManager) {
            std::cout << "[RuntimeManager] [Thread " << std::this_thread::get_id()
                      << "] [Phase: Shutdown] Attesa svuotamento comandi DMA (Drain)...\n";
            m_context->dmaManager->Drain();
            std::cout << "[RuntimeManager] [Thread " << std::this_thread::get_id()
                      << "] [Phase: Shutdown] VulkanDmaManager Drain completato.\n";
            m_shutdownResult.dmaDrainCalled = true;

            m_shutdownResult.pendingDmaTransfers   = m_context->dmaManager->GetPendingTransferCount();
            m_shutdownResult.liveDmaCommandBuffers = m_context->dmaManager->GetLiveCommandBufferCount();

            std::cout << "\n[RuntimeShutdown]\nDMA drained\n"
                      << "Pending DMA transfers     : " << m_shutdownResult.pendingDmaTransfers   << "\n"
                      << "Live DMA command buffers  : " << m_shutdownResult.liveDmaCommandBuffers << "\n";

            delete m_context->dmaManager;
            m_context->dmaManager = nullptr;
        }

        if (m_context->vramAllocator) {
            delete m_context->vramAllocator;
            m_context->vramAllocator = nullptr;
        }
    }

    m_shutdownResult.completed = true;
    auto tShutdownEnd = std::chrono::high_resolution_clock::now();
    double totalShutdownMs = std::chrono::duration<double, std::milli>(tShutdownEnd - tShutdownStart).count();
    std::cout << "[RuntimeShutdown] GPU resources destroyed. Tempi di Shutdown: [WorkerJoin=" 
              << workerJoinMs << " ms, JobSystemStop=" << jobSystemShutdownMs 
              << " ms, Totale=" << totalShutdownMs << " ms]\n";
}

void RuntimeManager::RequireFeaturesAsync(RuntimeFeature featureMask) {
    if (m_gpuRuntimeShutdown || m_cancelRequested.load()) return;
    uint32_t req = static_cast<uint32_t>(featureMask);
    if (req == 0) return;

    {
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);

        // Se tutte le feature richieste sono già CpuReady o GpuReady, non c'è lavoro CPU da svolgere
        bool needsCpuWork = false;
        for (size_t i = 0; i < kMaxRuntimeFeatures; ++i) {
            RuntimeFeature f = IndexToFeature(i);
            if (HasFeature(req, f)) {
                if (m_featureStates[i] != FeatureState::CpuReady && m_featureStates[i] != FeatureState::GpuReady) {
                    needsCpuWork = true;
                    break;
                }
            }
        }
        if (!needsCpuWork) return;
    }

    // Accorpamento atomico delle richieste e risveglio del worker dedicato (senza lock annidati)
    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        m_pendingCpuRequestsMask.fetch_or(req);
        m_workerActive.store(true);
        std::cout << "[RuntimeManager] [Thread " << std::this_thread::get_id()
                  << "] [RequireFeaturesAsync] Registrate richieste CPU: 0x" << std::hex << req << std::dec
                  << " -> pendingMask=0x" << std::hex << m_pendingCpuRequestsMask.load() << std::dec
                  << " (m_workerActive=true).\n";
    }
    m_workerCv.notify_one();
}

void RuntimeManager::WorkerLoop() {
    std::cout << "[RuntimeManager] [Thread " << std::this_thread::get_id()
              << "] [Phase: Dedicated Worker] Worker thread avviato e in ascolto.\n";

    while (!m_cancelRequested.load()) {
        uint32_t currentBatch = 0;
        {
            std::unique_lock<std::mutex> lock(m_workerMutex);
            m_workerCv.wait(lock, [this]() {
                return m_cancelRequested.load() || (m_pendingCpuRequestsMask.load() != 0);
            });

            if (m_cancelRequested.load()) {
                break;
            }

            currentBatch = m_pendingCpuRequestsMask.exchange(0);
            m_workerActive.store(true);
            std::cout << "[RuntimeManager] [Thread " << std::this_thread::get_id()
                      << "] [Phase: Dedicated Worker] Estratto batch: 0x" << std::hex << currentBatch << std::dec
                      << " (m_workerActive=true).\n";
        }

        // Risoluzione topologica delle dipendenze: PBRTextures richiede JobSystem e GlobalVRAM
        if (HasFeature(currentBatch, RuntimeFeature::PBRTextures)) {
            currentBatch |= static_cast<uint32_t>(RuntimeFeature::JobSystem);
            currentBatch |= static_cast<uint32_t>(RuntimeFeature::GlobalVRAM);
        }

        // CPU Preparation: eseguita rigorosamente FUORI dai lock per non bloccare i client concorrenti
        if (HasFeature(currentBatch, RuntimeFeature::GlobalVRAM)) {
            PrepareCpuGlobalVRAM();
        }
        if (m_cancelRequested.load()) break;

        if (HasFeature(currentBatch, RuntimeFeature::JobSystem)) {
            PrepareCpuJobSystem();
        }
        if (m_cancelRequested.load()) break;

        if (HasFeature(currentBatch, RuntimeFeature::PBRTextures)) {
            PrepareCpuPBRTextures();
        }

        {
            std::lock_guard<std::mutex> lock(m_workerMutex);
            if (m_pendingCpuRequestsMask.load() == 0) {
                m_workerActive.store(false);
                std::cout << "[RuntimeManager] [Thread " << std::this_thread::get_id()
                          << "] [Phase: Dedicated Worker] Batch completato, nessun lavoro pendente (m_workerActive=false).\n";
            } else {
                std::cout << "[RuntimeManager] [Thread " << std::this_thread::get_id()
                          << "] [Phase: Dedicated Worker] Batch completato, ulteriori richieste pendenti: 0x"
                          << std::hex << m_pendingCpuRequestsMask.load() << std::dec
                          << " (m_workerActive resta true).\n";
            }
        }
    }

    m_workerActive.store(false);
    std::cout << "[RuntimeManager] [Thread " << std::this_thread::get_id()
              << "] [Phase: Dedicated Worker] Worker thread terminato.\n";
}

void RuntimeManager::PrepareCpuGlobalVRAM() {
    size_t idx = FeatureToIndex(RuntimeFeature::GlobalVRAM);
    {
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);
        if (m_featureStates[idx] == FeatureState::CpuReady || m_featureStates[idx] == FeatureState::GpuReady) {
            return;
        }
        m_featureStates[idx] = FeatureState::PreparingCpu;
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    // Validazione parametri CPU per GlobalVRAM (l'allocazione Vulkan avviene rigorosamente nel Commit GPU)
    auto t1 = std::chrono::high_resolution_clock::now();

    {
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);
        m_featureStates[idx] = FeatureState::CpuReady;
        m_featureTimings[idx].cpuPrepTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    }
}

void RuntimeManager::PrepareCpuJobSystem() {
    size_t idx = FeatureToIndex(RuntimeFeature::JobSystem);
    {
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);
        if (m_featureStates[idx] == FeatureState::CpuReady || m_featureStates[idx] == FeatureState::GpuReady) {
            return;
        }
        m_featureStates[idx] = FeatureState::PreparingCpu;
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    std::cout << "[RuntimeManager] [CPU Prep] Inizializzazione Pool Thread Asincroni...\n";
    if (m_context && !m_context->jobSystem) {
        m_jobSystemWasCreated = true;
        m_context->jobSystem = new fw::JobSystem();
        m_context->jobSystem->Initialize();
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    {
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);
        m_featureStates[idx] = FeatureState::CpuReady;
        m_featureTimings[idx].cpuPrepTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    }
}

void RuntimeManager::PrepareCpuPBRTextures() {
    size_t idx = FeatureToIndex(RuntimeFeature::PBRTextures);
    {
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);
        if (m_featureStates[idx] == FeatureState::CpuReady || m_featureStates[idx] == FeatureState::GpuReady) {
            return;
        }
        m_featureStates[idx] = FeatureState::PreparingCpu;
    }

    if (m_cancelRequested.load()) {
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);
        if (m_featureStates[idx] == FeatureState::PreparingCpu) {
            m_featureStates[idx] = FeatureState::Uninitialized;
        }
        return;
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    std::cout << "[RuntimeManager] [CPU Prep] Esecuzione TexturePacker PBR in RAM...\n";

    std::unique_ptr<PackedTextureData> packedData = std::make_unique<PackedTextureData>();
    if (m_context && m_context->materialRegistry) {
        fw::TexturePacker packer(512);
        *packedData = packer.PackMaterials(m_context->materialRegistry->GetAllMaterials(), &m_cancelRequested);
    } else {
        // Fallback robusto in memoria CPU se il registry non è inizializzato
        packedData->width = 1;
        packedData->height = 1;
        packedData->layerCount = 256;
        packedData->albedoData.assign(256 * 4, 255);
        packedData->normalData.assign(256 * 4, 128);
        packedData->ormData.assign(256 * 4, 255);
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    // Se la cancellazione è stata richiesta o intercettata durante il packing:
    if (m_cancelRequested.load() || packedData->isCancelled) {
        std::cout << "[RuntimeManager] [CPU Prep] Preparazione PBR interrotta cooperativamente (fase: "
                  << (packedData->isCancelled ? packedData->cancellationPhase : "Pre/Post PackMaterials")
                  << "). Feature NON promossa a CpuReady.\n";
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);
        if (m_featureStates[idx] == FeatureState::PreparingCpu) {
            m_featureStates[idx] = FeatureState::Uninitialized;
        }
        m_pendingPbrData.reset();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);
        m_pendingPbrData = std::move(packedData);
        m_featureStates[idx] = FeatureState::CpuReady;
        m_featureTimings[idx].cpuPrepTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "[RuntimeManager] [CPU Prep] Texture PBR preparate in CPU RAM ("
                  << m_featureTimings[idx].cpuPrepTimeMs << " ms).\n";
    }
}

size_t RuntimeManager::ProcessGpuCommits(size_t maxCommits) {
    if (m_gpuRuntimeShutdown) return 0;

    size_t committedCount = 0;

    // Ordine topologico di commit GPU: GlobalVRAM -> JobSystem -> PBRTextures
    const std::array<size_t, 3> commitOrder = {
        FeatureToIndex(RuntimeFeature::GlobalVRAM),
        FeatureToIndex(RuntimeFeature::JobSystem),
        FeatureToIndex(RuntimeFeature::PBRTextures)
    };

    for (size_t idx : commitOrder) {
        if (committedCount >= maxCommits) break;

        bool shouldCommit = false;
        {
            std::lock_guard<std::mutex> lock(m_lifecycleMutex);
            if (m_featureStates[idx] == FeatureState::CpuReady) {
                shouldCommit = true;
                m_featureStates[idx] = FeatureState::CommittingGpu;
            }
        }

        if (!shouldCommit) continue;

        // Simulazione errore / fault injection (RLC-04)
        if (m_simulatedFailures[idx]) {
            std::cerr << "[RuntimeManager] [FAULT-INJECTION] Errore simulato durante commit GPU per feature idx " << idx << "\n";
            RollbackFeature(idx);
            continue;
        }

        auto t0 = std::chrono::high_resolution_clock::now();
        bool commitSuccess = false;

        RuntimeFeature feat = IndexToFeature(idx);
        if (feat == RuntimeFeature::GlobalVRAM) {
            commitSuccess = CommitGpuGlobalVRAM();
        } else if (feat == RuntimeFeature::JobSystem) {
            commitSuccess = CommitGpuJobSystem();
        } else if (feat == RuntimeFeature::PBRTextures) {
            commitSuccess = CommitGpuPBRTextures();
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double commitDuration = std::chrono::duration<double, std::milli>(t1 - t0).count();

        {
            std::lock_guard<std::mutex> lock(m_lifecycleMutex);
            m_featureTimings[idx].gpuCommitTimeMs = commitDuration;
            if (commitSuccess) {
                m_featureStates[idx] = FeatureState::GpuReady;
                m_activeFeatures = m_activeFeatures | feat;
                committedCount++;
                std::cout << "[RuntimeManager] [GPU Commit] Feature " << static_cast<uint32_t>(feat)
                          << " committata con successo (" << commitDuration << " ms).\n";
            } else {
                std::cerr << "[RuntimeManager] [GPU Commit FAIL] Commit fallito per feature " << static_cast<uint32_t>(feat) << "\n";
                RollbackFeature(idx);
            }
        }
    }

    return committedCount;
}

bool RuntimeManager::CommitGpuGlobalVRAM() {
    std::cout << "[RuntimeManager] [GPU Commit] Inizializzazione Lazy VRAM Allocator e DMA...\n";
    if (m_context && !m_context->dmaManager) {
        m_dmaManagerWasCreated = true;
        m_context->dmaManager = new fw::VulkanDmaManager();
        if (m_context->engine && m_context->engine->GetRenderManager()) {
            auto* rm = m_context->engine->GetRenderManager();
            m_context->dmaManager->Initialize(
                rm->GetDevice(),
                rm->GetTransferQueue(),
                rm->GetTransferCommandPool(),
                rm->GetStagingRingBuffer(),
                rm->GetStagingDeviceMemory(),
                rm->GetMappedStagingData(),
                rm->GetStagingBufferSize(),
                VK_NULL_HANDLE,
                rm->GetQueueMutex()
            );
        }
    }

    if (m_context && !m_context->vramAllocator) {
        m_context->vramAllocator = new fw::VramSlabAllocator(2048ULL * 1024ULL * 1024ULL); 
        m_context->vramAllocator->SetAllocateCompartmentCallback([this](uint32_t compIdx) {
            if (m_context && m_context->engine && m_context->engine->GetRenderManager()) {
                m_context->engine->GetRenderManager()->AddVramCompartment();
                if (m_context->dmaManager) {
                    m_context->dmaManager->UpdateStagingBuffer(
                        m_context->engine->GetRenderManager()->GetStagingRingBuffer(),
                        m_context->engine->GetRenderManager()->GetStagingDeviceMemory(),
                        m_context->engine->GetRenderManager()->GetMappedStagingData()
                    );
                }
            }
        });
    }

    return true;
}

bool RuntimeManager::CommitGpuJobSystem() {
    // JobSystem è una risorsa CPU; il commit GPU è immediato
    return true;
}

bool RuntimeManager::CommitGpuPBRTextures() {
    std::cout << "[RuntimeManager] [GPU Commit] Upload Texture PBR su Vulkan VRAM...\n";
    if (!m_pendingPbrData) {
        // Fallback: crea payload minimo se assente
        m_pendingPbrData = std::make_unique<PackedTextureData>();
    }

    if (m_context && m_context->engine) {
        if (auto* rm = m_context->engine->GetRenderManager()) {
            rm->CreatePBRTextures(*m_pendingPbrData);
            std::cout << "[RuntimeManager] [GPU Commit] Textures PBR caricate su VkImage con successo.\n";
        }
    }

    // Libera la memoria CPU RAM una volta completato l'upload GPU
    m_pendingPbrData.reset();
    return true;
}

void RuntimeManager::RollbackFeature(size_t idx) {
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    m_featureStates[idx] = FeatureState::Failed;
    RuntimeFeature f = IndexToFeature(idx);
    m_activeFeatures = static_cast<RuntimeFeature>(static_cast<uint32_t>(m_activeFeatures) & ~static_cast<uint32_t>(f));

    if (f == RuntimeFeature::PBRTextures) {
        m_pendingPbrData.reset();
    }
}

void RuntimeManager::RequireFeaturesSync(RuntimeFeature featureMask) {
    if (m_gpuRuntimeShutdown || m_cancelRequested.load()) return;

    // 1. Se c'è un caricamento asincrono in corso, attendine il completamento fuori dai lock
    if (IsCpuWorkerActive()) {
        std::cout << "[RuntimeManager] [Thread " << std::this_thread::get_id()
                  << "] [Phase: Sync Prep] Attesa completamento worker asincrono in corso...\n";
        while (IsCpuWorkerActive()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::cout << "[RuntimeManager] [Thread " << std::this_thread::get_id()
                  << "] [Phase: Sync Prep] Worker asincrono terminato o inattivo.\n";
    }

    // 2. Assicurati che il lavoro CPU sia stato svolto
    uint32_t req = static_cast<uint32_t>(featureMask);
    if (HasFeature(req, RuntimeFeature::GlobalVRAM)) PrepareCpuGlobalVRAM();
    if (HasFeature(req, RuntimeFeature::JobSystem))  PrepareCpuJobSystem();
    if (HasFeature(req, RuntimeFeature::PBRTextures)) PrepareCpuPBRTextures();

    // 3. Esegui il commit GPU sincrono sul thread chiamante (Render Thread)
    ProcessGpuCommits(SIZE_MAX);
}

bool RuntimeManager::IsReady() const {
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    if (m_workerActive.load() || (m_pendingCpuRequestsMask.load() != 0)) return false;
    for (size_t i = 0; i < kMaxRuntimeFeatures; ++i) {
        if (m_featureStates[i] == FeatureState::PreparingCpu ||
            m_featureStates[i] == FeatureState::CpuReady ||
            m_featureStates[i] == FeatureState::CommittingGpu) {
            return false;
        }
    }
    return true;
}

FeatureState RuntimeManager::GetFeatureState(RuntimeFeature feature) const {
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    size_t idx = FeatureToIndex(feature);
    return m_featureStates[idx];
}

bool RuntimeManager::IsCpuReady(RuntimeFeature feature) const {
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    size_t idx = FeatureToIndex(feature);
    return m_featureStates[idx] == FeatureState::CpuReady || m_featureStates[idx] == FeatureState::GpuReady;
}

bool RuntimeManager::IsGpuReady(RuntimeFeature feature) const {
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    size_t idx = FeatureToIndex(feature);
    return m_featureStates[idx] == FeatureState::GpuReady;
}

void RuntimeManager::AcquireFeature(RuntimeFeature feature) {
    size_t idx = FeatureToIndex(feature);
    {
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);
        m_featureRefCounts[idx]++;
    }
    RequireFeaturesAsync(feature);
}

void RuntimeManager::ReleaseFeature(RuntimeFeature feature) {
    size_t idx = FeatureToIndex(feature);
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    if (m_featureRefCounts[idx] > 0) {
        m_featureRefCounts[idx]--;
    }
    // Nessun teardown distruttivo su un singolo decremento: ownership indipendente dalle schermate
}

uint32_t RuntimeManager::GetFeatureRefCount(RuntimeFeature feature) const {
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    size_t idx = FeatureToIndex(feature);
    return m_featureRefCounts[idx];
}

FeatureTimingStats RuntimeManager::GetFeatureTiming(RuntimeFeature feature) const {
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    size_t idx = FeatureToIndex(feature);
    return m_featureTimings[idx];
}

void RuntimeManager::SimulateGpuCommitFailure(RuntimeFeature feature, bool enable) {
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    size_t idx = FeatureToIndex(feature);
    m_simulatedFailures[idx] = enable;
}

} // namespace fw
