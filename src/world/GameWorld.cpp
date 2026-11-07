#include "pch.h"
#include "GameWorld.h"
#include "FreeListAllocator.h"
#include "PoolAllocator.h"
#include "StackAllocator.h"
#include "JobSystem.h"
#include "SharedContext.h"
#include "VulkanDmaManager.h"
#include "VramSlabAllocator.h"
#include "../components/PlanetComponents.h"
#include "../simulation/PlanetTimeSystem.h"
#include "../simulation/AstronomySystem.h"
#include "FAIRWORLD.h"
#include "RenderManager.h"
#include "EventManager.h"
#include "BlockRegistry.h"
#include "MaterialRegistry.h"
#include "VoxelFaceUV.h"
#include "VoxelChunkMesher.h"
#include "MapWorldGenerator.h"
#include "PlanetaryMeshGenerator.h"
#include "PlanetRadialMapping.h"
#include "PlanetaryChunkNeighborhood.h"
#include "AssetManager.h"
#include "systems/TerrainSolverSystem.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include "Systems.h"
#include <cmath>
#include <array>
#include <limits>
#include <vector>
#include <cstdlib>
#include <chrono>
#include "systems/BiomeSystems.h"
#include "systems/TerrainSolverSystem.h"

#include <atomic>
std::atomic<int> g_d74f6l_gen_nonzero(0);
std::atomic<int> g_d74f6l_dma_completed(0);
std::atomic<int> g_d74f6l_deferred_queued(0);
std::atomic<int> g_d74f6l_ecs_committed(0);

namespace {
bool MeshJobTraceEnabled() {
    const char* value = std::getenv("FAIRWORLD_JOB_TRACE");
    return value && value[0] != '\0' && value[0] != '0';
}

void TraceMeshJob(const char* state, uint64_t requestId, entt::entity entity,
    const fw::VoxelChunkComponent* chunk, uint64_t revision, const char* detail = nullptr) {
    if (!MeshJobTraceEnabled()) return;
    const auto ticks = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    std::cout << "[MeshJobTrace] t_us=" << ticks << " request=" << requestId
              << " entity=" << entt::to_integral(entity)
              << " key=" << (chunk ? chunk->cx : 0) << ',' << (chunk ? chunk->cz : 0)
              << " planet=" << (chunk ? chunk->planetCoord.planet.value : fw::PlanetID::Invalid().value)
              << ':' << (chunk ? static_cast<unsigned>(chunk->planetCoord.face) : 0)
              << ':' << (chunk ? chunk->planetCoord.col : 0) << ',' << (chunk ? chunk->planetCoord.row : 0)
              << ':' << (chunk ? chunk->planetCoord.layer : 0)
              << " revision=" << revision << " state=" << state;
    if (detail) std::cout << " detail=" << detail;
    std::cout << '\n';
}

void RetireMeshAllocation(SharedContext* context, fw::VramAllocationHandle handle) {
    if (!context || !context->vramAllocator || handle == fw::INVALID_VRAM_HANDLE) return;
    auto* renderManager = context->engine ? context->engine->GetRenderManager() : nullptr;
    if (!renderManager || renderManager->IsDeviceLost() || renderManager->GetDevice() == VK_NULL_HANDLE) {
        // Without a live graphics timeline, conservatively prevent reuse. The
        // allocator can be released only after shutdown confirms device idle.
        if (!context->vramAllocator->Retire(handle, UINT64_MAX)) {
            std::cerr << "[GameWorld] Could not conservatively retire VRAM handle " << handle << ".\n";
        }
        return;
    }
    const uint64_t lastGraphicsUse = renderManager->GetLastSubmittedGraphicsSerial();
    if (lastGraphicsUse == 0 || lastGraphicsUse <= renderManager->GetCompletedGraphicsSerial()) {
        context->vramAllocator->Free(handle);
    } else if (!context->vramAllocator->Retire(handle, lastGraphicsUse)) {
        std::cerr << "[GameWorld] Could not retire stale/non-active VRAM handle " << handle << ".\n";
    }
}

}

namespace fw {

GameWorld::GameWorld() {
    m_cancelToken = std::make_shared<std::atomic<bool>>(false);
}

void GameWorld::InitializePhysics() {
    if (m_masterMemoryBlock) return;
    
    size_t totalMemorySize = 128 * 1024 * 1024; // 128MB Arena
    m_masterMemoryBlock = malloc(totalMemorySize);
    
    if (m_masterMemoryBlock) {
        uint8_t* basePtr = static_cast<uint8_t*>(m_masterMemoryBlock);
        
        size_t persistentSize = 32 * 1024 * 1024;
        m_persistentAllocator = std::make_unique<fw::memory::FreeListAllocator>(
            persistentSize, basePtr
        );
        
        size_t poolSize = 64 * 1024 * 1024;
        m_chunkPoolAllocator = std::make_unique<fw::memory::PoolAllocator>(
            poolSize, sizeof(VoxelChunkComponent), (uint8_t)alignof(VoxelChunkComponent), basePtr + persistentSize
        );
        
        size_t frameSize = 32 * 1024 * 1024;
        m_frameAllocator = std::make_unique<fw::memory::StackAllocator>(
            frameSize, basePtr + persistentSize + poolSize
        );
        
        static bool s_firstArenaLogged = false;
        if (!s_firstArenaLogged) {
            std::cout << "[GameWorld] Arena di memoria di 128MB allocata e partizionata con successo.\n";
            s_firstArenaLogged = true;
        }
    }
}

size_t GameWorld::GetPersistentUsedBytes() const {
    return m_persistentAllocator ? m_persistentAllocator->GetUsed() : 0;
}

size_t GameWorld::GetChunkPoolUsedBytes() const {
    return m_chunkPoolAllocator ? m_chunkPoolAllocator->GetUsed() : 0;
}

size_t GameWorld::GetFrameStackUsedBytes() const {
    return m_frameAllocator ? m_frameAllocator->GetUsed() : 0;
}

GameWorld::~GameWorld() {
    // MEM07-F05: Segnala cancellazione immediata di tutti i job in volo (m_cancelToken rimane true!)
    if (m_cancelToken) {
        *m_cancelToken = true;
    }
    // Rilascia risorse VRAM, mesh e registry senza ricreare entità singleton o riallocare memoria
    TeardownResources();

    if (m_blockUpdatedSubId != INVALID_SUBSCRIPTION_ID) {
        EventManager::Get().Unsubscribe(m_blockUpdatedSubId);
        m_blockUpdatedSubId = INVALID_SUBSCRIPTION_ID;
    }
    if (m_context) {
        if (m_context->gameWorld == this) {
            m_context->gameWorld = nullptr;
        }
        if (m_context->forgeWorld == this) {
            m_context->forgeWorld = nullptr;
        }
        if (m_context->activeRegistry == &m_registry) {
            m_context->activeRegistry = nullptr;
        }
    }
    if (m_masterMemoryBlock) {
        free(m_masterMemoryBlock);
        m_masterMemoryBlock = nullptr;
    }
    static bool s_firstDestructionLogged = false;
    if (!s_firstDestructionLogged) {
        std::cout << "[GameWorld] O(1) Memory Destruction. Tutta l'arena liberata.\n";
        s_firstDestructionLogged = true;
    }
}

void GameWorld::InitPlanetSingleton() {
    if (!m_registry.valid(m_planetEntity)) {
        m_planetEntity = m_registry.create();
    }
    m_registry.emplace_or_replace<PlanetTag>(m_planetEntity);
    m_registry.emplace_or_replace<PlanetTimeComponent>(m_planetEntity);
    m_registry.emplace_or_replace<PlanetAstronomyComponent>(m_planetEntity);
    m_registry.emplace_or_replace<PlanetGeometryComponent>(m_planetEntity);
    m_registry.emplace_or_replace<PlanetEnvironmentComponent>(m_planetEntity);
    m_registry.emplace_or_replace<PlanetAtmosphereComponent>(m_planetEntity);
    m_registry.emplace_or_replace<PlanetClimateComponent>(m_planetEntity);
    m_registry.emplace_or_replace<PlanetOceanComponent>(m_planetEntity);
    m_registry.emplace_or_replace<PlanetGeologyComponent>(m_planetEntity);
    m_registry.emplace_or_replace<PlanetEcologyComponent>(m_planetEntity);
}

void GameWorld::Initialize(SharedContext* context) {
    m_meshJobsAccepting.store(true);
    if (!m_cancelToken || m_cancelToken->load()) m_cancelToken = std::make_shared<std::atomic<bool>>(false);
    m_context = context;
    if (m_context) {
        if (!m_context->gameWorld) {
            m_context->gameWorld = this;
        }
        m_context->forgeWorld = this;
        m_context->activeRegistry = &m_registry;

        // Registrazione per i fluidi (Event-Driven) - solo per il mondo di gioco reale
        if (!m_context->isMapBuilderMode) {
            if (m_blockUpdatedSubId != INVALID_SUBSCRIPTION_ID) {
                EventManager::Get().Unsubscribe(m_blockUpdatedSubId);
                m_blockUpdatedSubId = INVALID_SUBSCRIPTION_ID;
            }
            m_blockUpdatedSubId = EventManager::Get().Subscribe<Event_BlockUpdated>([this](const Event_BlockUpdated& e) {
                this->ProcessFluidUpdate(e.position.x, e.position.y, e.position.z);
            });
        }
    }
    
    // Inizializzazione Entità Pianeta Centrale (Singleton ECS)
    InitPlanetSingleton();
}

void GameWorld::CancelJobs() {
    if (m_cancelToken) {
        *m_cancelToken = true;
    }
}

void GameWorld::TeardownResources() {
    m_meshJobsAccepting.store(false);
    if (m_cancelToken) m_cancelToken->store(true);
    if (m_context && m_context->jobSystem) {
        m_context->jobSystem->WaitAll();
    }
    
    {
        std::lock_guard<std::mutex> lock(m_deferredMutex);
        for (auto& def : m_deferredMeshes) {
            if (def.mesh.vramAlloc != fw::INVALID_VRAM_HANDLE && m_context && m_context->vramAllocator) {
                if (def.dmaPendingUnconfirmed) {
                    QuarantineVramAllocation(def.mesh.vramAlloc, def.dmaTimelineTarget);
                }
                else m_context->vramAllocator->Free(def.mesh.vramAlloc);
                def.mesh.vramAlloc = fw::INVALID_VRAM_HANDLE;
            }
        }
        m_deferredMeshes.clear();
    }
    
    auto view = m_registry.view<MeshComponent>();
    for (auto entity : view) {
        auto& mesh = view.get<MeshComponent>(entity);
        if (mesh.vramAlloc != fw::INVALID_VRAM_HANDLE && m_context && m_context->vramAllocator) {
            RetireMeshAllocation(m_context, mesh.vramAlloc);
            mesh.vramAlloc = fw::INVALID_VRAM_HANDLE;
        }
    }
    
    m_registry.clear();
    m_planetEntity = entt::null;
    m_chunkManager.Clear();
    m_forgeBlocks.clear();
    
    if (m_frameAllocator) m_frameAllocator->Reset();
}

void GameWorld::ClearWorld(bool saveToDisk) {
    if (saveToDisk) {
        SaveAllChunks();
    }
    
    TeardownResources();
    
    // B5.4D3.1P2: Reset the cancellation token so that subsequent async meshing jobs are not aborted immediately
    m_cancelToken = std::make_shared<std::atomic<bool>>(false);
    m_meshJobsAccepting.store(true);
    
    // Re-inizializza l'entità singleton del pianeta per il ciclo di vita riutilizzabile
    InitPlanetSingleton();
}

void GameWorld::Update(float dt) {
    if (!m_meshJobsAccepting.load()) return;
    if (m_context && m_context->dmaManager && m_context->vramAllocator) {
        auto it = m_unconfirmedDmaAllocations.begin();
        while (it != m_unconfirmedDmaAllocations.end()) {
            if (it->timelineTarget == 0) { ++it; continue; }
            bool complete = false;
            const VkResult query = m_context->dmaManager->QueryTimelineCompletion(it->timelineTarget, complete);
            if (query == VK_SUCCESS && complete) {
                if (!m_context->vramAllocator->ReleaseQuarantined(it->handle, true)) {
                    std::cerr << "[GameWorld] Quarantined VRAM handle no longer resolves: " << it->handle << ".\n";
                }
                it = m_unconfirmedDmaAllocations.erase(it);
            } else ++it;
        }
    }
    // 0. Aggiorna lo stack di simulazione planetaria (ECS)
    if (m_registry.valid(m_planetEntity)) {
        fw::PlanetTimeSystem::Update(m_registry, dt);
        fw::AstronomySystem::Update(m_registry, dt);
        
        // Sincronizziamo la luce di shading dei blocchi con il Sole dinamico calcolato dall'Astronomia!
        if (m_context && m_registry.all_of<PlanetEnvironmentComponent>(m_planetEntity)) {
            m_context->previewLightDir = m_registry.get<PlanetEnvironmentComponent>(m_planetEntity).sunDirection;
        }
    }

    // 1. Processa la coda delle mesh differite da Worker Threads
    std::vector<DeferredMeshSpawn> meshBatch;
    {
        std::lock_guard<std::mutex> lock(m_deferredMutex);
        meshBatch.swap(m_deferredMeshes);
    }
    
    static int s_d74f6l_r1_gw_queue = 0;
    if (s_d74f6l_r1_gw_queue < 5 && !meshBatch.empty()) {
        std::cout << "[D7.4F.6L-R1] GameWorld consuming deferred queue of size " << meshBatch.size() << " (Snapshot " << (s_d74f6l_r1_gw_queue + 1) << "/5)\n";
        s_d74f6l_r1_gw_queue++;
    }
    
    for (auto& def : meshBatch) {
        if (def.trackedJob) {
            const bool targetValid = def.targetEntity != entt::null && m_registry.valid(def.targetEntity);
            if (!targetValid) {
                if (def.mesh.vramAlloc != fw::INVALID_VRAM_HANDLE && m_context && m_context->vramAllocator) {
                    if (def.dmaPendingUnconfirmed) {
                        QuarantineVramAllocation(def.mesh.vramAlloc, def.dmaTimelineTarget);
                    }
                    else m_context->vramAllocator->Free(def.mesh.vramAlloc);
                }
                TraceMeshJob("discarded-invalid-entity", def.jobRequestId, def.targetEntity,
                    def.chunkData.get(), def.sourceMeshRevision);
                continue;
            }

            auto* currentDirty = m_registry.try_get<ChunkDirtyComponent>(def.targetEntity);
            const bool ownsPendingRequest = currentDirty && currentDirty->pendingJob &&
                currentDirty->activeRequestId == def.jobRequestId;
            auto* currentChunk = m_registry.try_get<VoxelChunkComponent>(def.targetEntity);
            const bool identityMatches = currentChunk && def.chunkData &&
                currentChunk->cx == def.sourceCx && currentChunk->cz == def.sourceCz &&
                currentChunk->planetCoord == def.sourcePlanetCoord;
            const bool versionMatches = identityMatches &&
                currentChunk->meshRevision == def.sourceMeshRevision;

            if (!ownsPendingRequest) {
                if (def.mesh.vramAlloc != fw::INVALID_VRAM_HANDLE && m_context && m_context->vramAllocator) {
                    if (def.dmaPendingUnconfirmed) {
                        QuarantineVramAllocation(def.mesh.vramAlloc, def.dmaTimelineTarget);
                    }
                    else m_context->vramAllocator->Free(def.mesh.vramAlloc);
                }
                TraceMeshJob("discarded-superseded-request", def.jobRequestId, def.targetEntity,
                    def.chunkData.get(), def.sourceMeshRevision);
                continue;
            }

            if (!identityMatches || !versionMatches || currentDirty->needsRebuild) {
                if (def.mesh.vramAlloc != fw::INVALID_VRAM_HANDLE && m_context && m_context->vramAllocator) {
                    if (def.dmaPendingUnconfirmed) {
                        QuarantineVramAllocation(def.mesh.vramAlloc, def.dmaTimelineTarget);
                    }
                    else m_context->vramAllocator->Free(def.mesh.vramAlloc);
                }
                currentDirty->pendingJob = false;
                currentDirty->activeRequestId = 0;
                currentDirty->needsRebuild = false;
                if (currentChunk && m_meshJobsAccepting.load()) MarkMeshDirty(def.targetEntity);
                TraceMeshJob("discarded-stale-version", def.jobRequestId, def.targetEntity,
                    def.chunkData.get(), def.sourceMeshRevision);
                continue;
            }

            if (def.jobOutcome != DeferredMeshSpawn::JobOutcome::Completed) {
                if (def.mesh.vramAlloc != fw::INVALID_VRAM_HANDLE && m_context && m_context->vramAllocator) {
                    if (def.dmaPendingUnconfirmed) {
                        QuarantineVramAllocation(def.mesh.vramAlloc, def.dmaTimelineTarget);
                    }
                    else m_context->vramAllocator->Free(def.mesh.vramAlloc);
                }
                currentDirty->pendingJob = false;
                currentDirty->activeRequestId = 0;
                const bool rebuildWasRequested = currentDirty->needsRebuild;
                currentDirty->needsRebuild = false;
                if (rebuildWasRequested && m_meshJobsAccepting.load()) {
                    if (std::find(m_dirtyMeshChunks.begin(), m_dirtyMeshChunks.end(), def.targetEntity) == m_dirtyMeshChunks.end()) {
                        m_dirtyMeshChunks.push_back(def.targetEntity);
                    }
                }
                const char* outcome = def.jobOutcome == DeferredMeshSpawn::JobOutcome::Cancelled ? "cancelled" : "failed";
                TraceMeshJob(outcome, def.jobRequestId, def.targetEntity, currentChunk,
                    currentChunk->meshRevision);
                continue;
            }

            currentDirty->pendingJob = false;
            currentDirty->activeRequestId = 0;
            TraceMeshJob("completion-ready-for-ecs-commit", def.jobRequestId, def.targetEntity,
                currentChunk, currentChunk->meshRevision);
        }

        if (def.targetEntity != entt::null && !m_registry.valid(def.targetEntity)) {
            if (def.mesh.vramAlloc != fw::INVALID_VRAM_HANDLE && m_context && m_context->vramAllocator) {
                if (def.dmaPendingUnconfirmed) {
                    QuarantineVramAllocation(def.mesh.vramAlloc, def.dmaTimelineTarget);
                }
                else m_context->vramAllocator->Free(def.mesh.vramAlloc);
            }
            continue;
        }

        if (def.targetEntity != entt::null) {
            if (def.isNewlyGenerated && def.chunkData != nullptr) {
                if (m_registry.all_of<VoxelChunkComponent>(def.targetEntity)) {
                    auto& chunk = m_registry.get<VoxelChunkComponent>(def.targetEntity);
                    memcpy(chunk.blocks, def.chunkData->blocks, sizeof(chunk.blocks));
                    memcpy(chunk.light, def.chunkData->light, sizeof(chunk.light));
                    chunk.isGenerated = true;
                }
            }

            m_registry.emplace_or_replace<PBRMaterialComponent>(def.targetEntity);
            if (!m_registry.all_of<fw::TransformComponent>(def.targetEntity)) {
                fw::TransformComponent tc;
                tc.location = {def.position.x, def.position.y, def.position.z};
                m_registry.emplace<fw::TransformComponent>(def.targetEntity, tc);
            }
            if (!def.mesh.vertices.empty()) {
                bool gpuReady = true;
                if (def.mesh.vramAlloc == fw::INVALID_VRAM_HANDLE && m_context && m_context->vramAllocator && m_context->dmaManager) {
                    uint32_t meshSizeBytes = (uint32_t)(def.mesh.vertices.size() * sizeof(fw::Vertex));
                    def.mesh.vramAlloc = m_context->vramAllocator->Allocate(meshSizeBytes);
                    if (def.mesh.vramAlloc != fw::INVALID_VRAM_HANDLE) {
                        auto allocInfo = m_context->vramAllocator->GetAllocation(def.mesh.vramAlloc);
                        auto* renderManager = m_context->engine ? m_context->engine->GetRenderManager() : nullptr;
                        const auto* compartments = renderManager ? &renderManager->GetVramCompartments() : nullptr;
                        DmaUploadResult upload{DmaUploadStatus::InvalidInput, DmaSubmissionState::NotSubmitted, VK_ERROR_INITIALIZATION_FAILED, 0};
                        if (allocInfo.valid && compartments && allocInfo.compartmentIdx < compartments->size() &&
                            (*compartments)[allocInfo.compartmentIdx] != VK_NULL_HANDLE) {
                            upload = m_context->dmaManager->UploadMeshAsync(def.mesh.vertices.data(), meshSizeBytes,
                                allocInfo, (*compartments)[allocInfo.compartmentIdx]);
                        }
                        if (!upload.completed()) {
                            if (upload.mayStillBeInUse()) {
                                def.dmaPendingUnconfirmed = true;
                                def.dmaTimelineTarget = upload.timelineTarget;
                                QuarantineVramAllocation(def.mesh.vramAlloc, upload.timelineTarget);
                            } else m_context->vramAllocator->Free(def.mesh.vramAlloc);
                            def.mesh.vramAlloc = fw::INVALID_VRAM_HANDLE;
                            gpuReady = false;
                        }
                    } else gpuReady = false;
                }

                if (gpuReady && m_registry.all_of<MeshComponent>(def.targetEntity)) {
                    auto& oldMesh = m_registry.get<MeshComponent>(def.targetEntity);
                    if (oldMesh.vramAlloc != fw::INVALID_VRAM_HANDLE && m_context && m_context->vramAllocator) {
                        RetireMeshAllocation(m_context, oldMesh.vramAlloc);
                    }
                }
                if (gpuReady) m_registry.emplace_or_replace<MeshComponent>(def.targetEntity, std::move(def.mesh));
                
                if (m_registry.all_of<VoxelChunkComponent>(def.targetEntity)) {
                    if (m_registry.get<VoxelChunkComponent>(def.targetEntity).planetCoord.planet.IsValid()) {
                        g_d74f6l_ecs_committed++;
                    }
                }
            }

            auto* dirty = m_registry.try_get<ChunkDirtyComponent>(def.targetEntity);
            if (dirty) {
                if (dirty->needsRebuild) {
                    dirty->pendingJob = false;
                    dirty->needsRebuild = false;
                } else {
                    m_registry.remove<ChunkDirtyComponent>(def.targetEntity);
                }
            }
        } else {
            entt::entity newEntity = def.targetEntity;
            if (newEntity == entt::null || !m_registry.valid(newEntity)) {
                newEntity = m_registry.create();
            }
            bool gpuReady = true;
            if (def.mesh.vramAlloc == fw::INVALID_VRAM_HANDLE && !def.mesh.vertices.empty() && m_context && m_context->vramAllocator && m_context->dmaManager) {
                uint32_t meshSizeBytes = (uint32_t)(def.mesh.vertices.size() * sizeof(fw::Vertex));
                def.mesh.vramAlloc = m_context->vramAllocator->Allocate(meshSizeBytes);
                if (def.mesh.vramAlloc != fw::INVALID_VRAM_HANDLE) {
                    auto allocInfo = m_context->vramAllocator->GetAllocation(def.mesh.vramAlloc);
                    auto* renderManager = m_context->engine ? m_context->engine->GetRenderManager() : nullptr;
                    const auto* compartments = renderManager ? &renderManager->GetVramCompartments() : nullptr;
                    DmaUploadResult upload{DmaUploadStatus::InvalidInput, DmaSubmissionState::NotSubmitted, VK_ERROR_INITIALIZATION_FAILED, 0};
                    if (allocInfo.valid && compartments && allocInfo.compartmentIdx < compartments->size() &&
                        (*compartments)[allocInfo.compartmentIdx] != VK_NULL_HANDLE) {
                        upload = m_context->dmaManager->UploadMeshAsync(def.mesh.vertices.data(), meshSizeBytes,
                            allocInfo, (*compartments)[allocInfo.compartmentIdx]);
                    }
                    if (!upload.completed()) {
                        if (upload.mayStillBeInUse()) {
                            def.dmaPendingUnconfirmed = true;
                            def.dmaTimelineTarget = upload.timelineTarget;
                            QuarantineVramAllocation(def.mesh.vramAlloc, upload.timelineTarget);
                        } else m_context->vramAllocator->Free(def.mesh.vramAlloc);
                        def.mesh.vramAlloc = fw::INVALID_VRAM_HANDLE;
                        gpuReady = false;
                    }
                } else gpuReady = false;
            }

            m_registry.emplace_or_replace<MetadataComponent>(newEntity, def.name, true, false);
            
            if (gpuReady && m_registry.all_of<MeshComponent>(newEntity)) {
                auto& oldMesh = m_registry.get<MeshComponent>(newEntity);
                if (oldMesh.vramAlloc != fw::INVALID_VRAM_HANDLE && m_context && m_context->vramAllocator) {
                    RetireMeshAllocation(m_context, oldMesh.vramAlloc);
                }
            }
            if (gpuReady) m_registry.emplace_or_replace<MeshComponent>(newEntity, std::move(def.mesh));

            fw::TransformComponent trans;
            trans.location = {def.position.x, def.position.y, def.position.z};
            trans.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
            trans.scale = {1.0f, 1.0f, 1.0f};
            m_registry.emplace_or_replace<TransformComponent>(newEntity, trans);
            m_registry.emplace_or_replace<PBRMaterialComponent>(newEntity);
        }
    }

    // 2. Pipeline Terreno (Unico Voxel Writer autorizzato)
    int maxBatch = (m_context && m_context->engine && m_context->engine->GetGameMode() == GameMode::Map) ? 250 : 15;
    
    // Fase 5 Freeze: Il nuovo TerrainSolver è l'esclusivo generatore procedurale per tutte le modalità.
    // Nessun legacy BiomeSystem è autorizzato a sovrascrivere `output.blocks`.
    fw::TerrainSolverSystem::Update(m_registry, m_dirtyTerrainChunks, maxBatch, [this](entt::entity e) {
        this->MarkMeshDirty(e);
    }, GetBlockRegistry());

    // 3. Chunk System: Rigenerazione asincrona per chunk Dirty
    if (m_meshJobsAccepting.load() && m_cancelToken && !m_cancelToken->load() &&
        m_context && m_context->jobSystem && m_context->jobSystem->IsAcceptingJobs()) {
        int jobsDispatchedThisFrame = 0;
        int maxJobsPerFrame = 15;
        
        std::vector<entt::entity> remainingMesh;

        for (auto entity : m_dirtyMeshChunks) {
            if (!m_registry.valid(entity) || !m_registry.all_of<VoxelChunkComponent, ChunkDirtyComponent>(entity)) {
                continue;
            }

            if (jobsDispatchedThisFrame >= maxJobsPerFrame) {
                remainingMesh.push_back(entity);
                continue;
            }

            auto& dirty = m_registry.get<ChunkDirtyComponent>(entity);
            if (dirty.pendingJob) {
                remainingMesh.push_back(entity);
                continue;
            }

            // In PlanetMapper mode, we only need CPU data for raycasting, NOT for rendering.
            // So we remove ChunkDirtyComponent and skip mesh generation entirely to save VRAM and CPU time.
            if (m_context && m_context->engine && m_context->engine->GetGameMode() == GameMode::PlanetMapper) {
                m_registry.remove<ChunkDirtyComponent>(entity);
                continue;
            }

            auto& chunk = m_registry.get<VoxelChunkComponent>(entity);
            std::string chunkName = "Chunk_" + std::to_string(chunk.cx) + "_" + std::to_string(chunk.cz);
            auto chunkData = std::shared_ptr<VoxelChunkComponent>(new VoxelChunkComponent());
            *chunkData = chunk;
            SharedContext* ctx = m_context;
            const uint64_t sourceRevision = chunk.meshRevision;

            std::shared_ptr<VoxelChunkComponent> neighbors[3][3];
            bool planetaryNeighborPending = false;
            bool planetaryNeighborInvalid = false;
            const bool planetaryChunk = chunk.planetCoord.planet.IsValid();
            fw::PlanetSize neighborPlanetSize = fw::PlanetSize::Medium;
            bool neighborPlanetSizeKnown = false;
            if (planetaryChunk && m_context && m_context->activeRegistry) {
                const auto planetView = m_context->activeRegistry->view<fw::PlanetGeometryComponent>();
                auto planetIt = planetView.begin();
                if (planetIt != planetView.end()) {
                    const auto onlyPlanet = *planetIt++;
                    if (planetIt == planetView.end()) {
                        neighborPlanetSize = planetView.get<fw::PlanetGeometryComponent>(onlyPlanet).planetSize;
                        neighborPlanetSizeKnown = true;
                    }
                }
            }

            for (int dx = -1; dx <= 1; ++dx) {
                for (int dz = -1; dz <= 1; ++dz) {
                    if (dx == 0 && dz == 0) continue;

                    if (planetaryChunk) {
                        if (!neighborPlanetSizeKnown) {
                            planetaryNeighborInvalid = true;
                            continue;
                        }

                        fw::planetary_neighborhood::Address address;
                        bool addressValid = false;
                        const bool cardinal = (dx == -1 && dz == 0) || (dx == 1 && dz == 0) ||
                            (dx == 0 && dz == -1) || (dx == 0 && dz == 1);
                        if (cardinal) {
                            addressValid = fw::planetary_neighborhood::TryResolveAddress(
                                chunk.planetCoord, dx, dz, neighborPlanetSize, address);
                        } else {
                            // Resolve same-face AO diagonals only. Cube corners have no
                            // canonical diagonal contract in PlanetMath::ResolveNeighbor.
                            const int resolution = static_cast<int>(
                                fw::PlanetMath::GetFaceResolution(neighborPlanetSize));
                            address.coordinate = chunk.planetCoord;
                            address.coordinate.col += dx;
                            address.coordinate.row -= dz;
                            addressValid = fw::planetary_neighborhood::IsValidCoordinate(
                                address.coordinate, resolution) &&
                                fw::planet_grid_compat::TryPlanetChunkCoordToLegacyNet(
                                    address.coordinate, resolution,
                                    address.storageCx, address.storageCz);
                        }
                        if (!addressValid) {
                            if (cardinal) planetaryNeighborInvalid = true;
                            continue;
                        }

                        const entt::entity neighborEntity = m_chunkManager.GetChunkEntity(
                            address.storageCx, address.storageCz);
                        const bool found = neighborEntity != entt::null;
                        const bool entityValid = found && m_registry.valid(neighborEntity);
                        const bool hasChunk = entityValid && m_registry.all_of<VoxelChunkComponent>(neighborEntity);
                        const VoxelChunkComponent* targetChunk = hasChunk
                            ? &m_registry.get<VoxelChunkComponent>(neighborEntity) : nullptr;
                        const bool identityMatches = targetChunk &&
                            targetChunk->planetCoord == address.coordinate;
                        const auto availability = fw::planetary_neighborhood::Classify(
                            found, entityValid, hasChunk, identityMatches,
                            targetChunk && targetChunk->isGenerated);
                        if (availability == fw::planetary_neighborhood::Availability::Pending) {
                            planetaryNeighborPending = true;
                            continue;
                        }
                        if (availability == fw::planetary_neighborhood::Availability::Invalid) {
                            planetaryNeighborInvalid = true;
                            continue;
                        }
                        if (availability != fw::planetary_neighborhood::Availability::Available) continue;

                        std::shared_ptr<VoxelChunkComponent> snapshot;
                        if (cardinal && address.coordinate.face != chunk.planetCoord.face) {
                            snapshot = std::make_shared<VoxelChunkComponent>();
                            if (!fw::planetary_neighborhood::TryOrientCrossFaceSnapshot(
                                    chunk.planetCoord, dx, dz, neighborPlanetSize,
                                    *targetChunk, *snapshot)) {
                                planetaryNeighborInvalid = true;
                                continue;
                            }
                        } else {
                            snapshot = std::make_shared<VoxelChunkComponent>(*targetChunk);
                        }
                        neighbors[dx + 1][dz + 1] = std::move(snapshot);
                    } else {
                        const entt::entity neighborEntity = m_chunkManager.GetChunkEntity(
                            chunk.cx + dx, chunk.cz + dz);
                        if (neighborEntity != entt::null && m_registry.valid(neighborEntity) &&
                            m_registry.all_of<VoxelChunkComponent>(neighborEntity)) {
                            neighbors[dx + 1][dz + 1] =
                                std::make_shared<VoxelChunkComponent>(m_registry.get<VoxelChunkComponent>(neighborEntity));
                        }
                    }
                }
            }

            // Do not bake an open seam against a neighbor that exists but has
            // not finished terrain generation. The dirty chunk stays queued and
            // is retried after the neighbor becomes available.
            if (planetaryNeighborPending || planetaryNeighborInvalid) {
                remainingMesh.push_back(entity);
                if (planetaryNeighborInvalid) {
                    TraceMeshJob("planetary-neighbor-invalid", 0, entity, &chunk, chunk.meshRevision,
                        "mesh deferred because planet identity/topology is ambiguous");
                } else {
                    TraceMeshJob("planetary-neighbor-pending", 0, entity, &chunk, chunk.meshRevision,
                        "mesh deferred until adjacent voxel data is generated");
                }
                continue;
            }

            GameWorld* self = this;
            auto cancelToken = m_cancelToken;
            auto payload = std::make_shared<DeferredMeshSpawn>();
            payload->targetEntity = entity;
            payload->chunkData = chunkData;
            payload->sourceMeshRevision = sourceRevision;
            payload->sourceCx = chunk.cx;
            payload->sourceCz = chunk.cz;
            payload->sourcePlanetCoord = chunk.planetCoord;

            dirty.pendingJob = true;
            const auto submission = m_context->jobSystem->ExecuteTracked(
                [self, entity, chunkName, chunkData, ctx, cancelToken, payload, sourceRevision,
                 n00=neighbors[0][0], n10=neighbors[1][0], n20=neighbors[2][0],
                 n01=neighbors[0][1],                      n21=neighbors[2][1],
                 n02=neighbors[0][2], n12=neighbors[1][2], n22=neighbors[2][2]](uint64_t requestId) {
                if (cancelToken->load()) return JobExecutionStatus::Cancelled;

                bool newlyGen = false;
                if (!chunkData->isGenerated) {
                    if (ctx && ctx->isForgeMode) {
                        // In Forge Mode partiamo con un chunk vuoto
                    } else {
                        if (!self->LoadChunk(chunkData->cx, chunkData->cz, *chunkData)) {
                            self->GenerateChunkData(*chunkData, chunkData->cx, chunkData->cz);
                        }
                    }
                    chunkData->isGenerated = true;
                    newlyGen = true;
                }

                if (cancelToken->load()) return JobExecutionStatus::Cancelled;

                bool isSpherical = false;
                fw::PlanetSize pSize = fw::PlanetSize::Medium;
                if (ctx && ctx->activeRegistry) {
                    auto planetView = ctx->activeRegistry->view<fw::PlanetGeometryComponent>();
                    if (!planetView.empty()) {
                        isSpherical = planetView.get<fw::PlanetGeometryComponent>(*planetView.begin()).isLogicalSphere;
                        pSize = planetView.get<fw::PlanetGeometryComponent>(*planetView.begin()).planetSize;
                    }
                }

                const std::array<const VoxelChunkComponent*, 9> neighborData = {
                    n00.get(), n01.get(), n02.get(),
                    n10.get(), nullptr,   n12.get(),
                    n20.get(), n21.get(), n22.get()
                };
                auto cpuMesh = fw::VoxelChunkMesher::Build(
                    *chunkData, neighborData, *ctx->materialRegistry, isSpherical, pSize);
                std::vector<Vertex> vertices = std::move(cpuMesh.vertices);
                payload->name = chunkName + (vertices.empty() ? "_Empty" : "_Ready");
                payload->position = {(float)chunkData->cx * 16.0f, 0.0f, (float)chunkData->cz * 16.0f};
                payload->isNewlyGenerated = newlyGen;
                if (MeshJobTraceEnabled()) {
                    TraceMeshJob("builder-completed", requestId, entity, chunkData.get(), sourceRevision,
                        vertices.empty() ? "vertices=0" : ("vertices=" + std::to_string(vertices.size())).c_str());
                }
                if (vertices.empty()) {
                    payload->mesh = MeshComponent{};
                    return JobExecutionStatus::Completed;
                }

                if (cancelToken->load()) return JobExecutionStatus::Cancelled;

                uint32_t meshSizeBytes = (uint32_t)(vertices.size() * sizeof(Vertex));
                auto vramAlloc = ctx->vramAllocator->Allocate(meshSizeBytes);
                if (vramAlloc == fw::INVALID_VRAM_HANDLE) return JobExecutionStatus::Failed;
                payload->mesh.vramAlloc = vramAlloc;

                if (cancelToken->load()) return JobExecutionStatus::Cancelled;

                if (chunkData->planetCoord.planet.IsValid()) {
                    g_d74f6l_gen_nonzero++;
                }

                auto allocInfo = ctx->vramAllocator->GetAllocation(vramAlloc);
                auto* renderManager = ctx->engine ? ctx->engine->GetRenderManager() : nullptr;
                const auto* compartments = renderManager ? &renderManager->GetVramCompartments() : nullptr;
                if (!allocInfo.valid || !compartments || allocInfo.compartmentIdx >= compartments->size() ||
                    (*compartments)[allocInfo.compartmentIdx] == VK_NULL_HANDLE || !ctx->dmaManager) {
                    return JobExecutionStatus::Failed;
                }
                const DmaUploadResult dmaResult = ctx->dmaManager->UploadMeshAsync(
                    vertices.data(), meshSizeBytes, allocInfo, (*compartments)[allocInfo.compartmentIdx]);
                if (!dmaResult.completed()) {
                    payload->dmaPendingUnconfirmed = dmaResult.mayStillBeInUse();
                    payload->dmaTimelineTarget = dmaResult.timelineTarget;
                    TraceMeshJob("dma-upload-failed", requestId, entity, chunkData.get(), sourceRevision,
                        ("status=" + std::to_string(static_cast<unsigned>(dmaResult.status)) +
                         " vk=" + std::to_string(dmaResult.vkResult) +
                         " timeline=" + std::to_string(dmaResult.timelineTarget)).c_str());
                    return JobExecutionStatus::Failed;
                }

                if (chunkData->planetCoord.planet.IsValid()) {
                g_d74f6l_dma_completed++;
                }

                MeshComponent newMesh;
                newMesh.name = chunkName + "_Mesh";
                newMesh.type = MeshType::Chunk;
                newMesh.vertices = std::move(vertices);
                newMesh.vramAlloc = vramAlloc;
                auto b = newMesh.bounds();

                if (fw::TerrainSolverSystem::s_enableVisualGateLog) {
                    std::cout << "\n[TerrainVisualGate][MESH]\n";
                    std::cout << "Vertices: " << newMesh.vertices.size() << "\n";
                    std::cout << "Indices: 0 (non-indexed chunk mesh)\n";
                    std::cout << "BoundsMin: {" << b.min.x << ", " << b.min.y << ", " << b.min.z << "}\n";
                    std::cout << "BoundsMax: {" << b.max.x << ", " << b.max.y << ", " << b.max.z << "}\n";
                    
                    std::cout << "\n[GPU]\n";
                    std::cout << "Upload size: " << meshSizeBytes << " bytes\n";
                    std::cout << "VRAM offset: " << allocInfo.offset << "\n";
                    std::cout << "Compartment: " << allocInfo.compartmentIdx << "\n";
                    std::cout << "=======================================================\n";
                }

                if (chunkData->planetCoord.planet.IsValid()) {
                    g_d74f6l_deferred_queued++;
                }
                payload->mesh = std::move(newMesh);
                return cancelToken->load() ? JobExecutionStatus::Cancelled : JobExecutionStatus::Completed;
            }, [self, payload, chunkData, entity, sourceRevision](const JobExecutionResult& result) {
                payload->trackedJob = true;
                payload->jobRequestId = result.requestId;
                payload->sourceMeshRevision = sourceRevision;
                payload->sourceCx = chunkData->cx;
                payload->sourceCz = chunkData->cz;
                payload->sourcePlanetCoord = chunkData->planetCoord;
                payload->targetEntity = entity;
                payload->jobOutcome = result.status == JobExecutionStatus::Completed
                    ? DeferredMeshSpawn::JobOutcome::Completed
                    : result.status == JobExecutionStatus::Cancelled
                        ? DeferredMeshSpawn::JobOutcome::Cancelled
                        : DeferredMeshSpawn::JobOutcome::Failed;
                TraceMeshJob(result.status == JobExecutionStatus::Completed ? "worker-completed" :
                    result.status == JobExecutionStatus::Cancelled ? "worker-cancelled" : "worker-failed",
                    result.requestId, entity, chunkData.get(), sourceRevision);
                std::lock_guard<std::mutex> lock(self->m_deferredMutex);
                self->m_deferredMeshes.push_back(std::move(*payload));
            });

            if (submission.accepted()) {
                dirty.activeRequestId = submission.requestId;
                ++jobsDispatchedThisFrame;
                TraceMeshJob("accepted", submission.requestId, entity, chunkData.get(), sourceRevision);
            } else {
                dirty.pendingJob = false;
                dirty.activeRequestId = 0;
                if (std::find(remainingMesh.begin(), remainingMesh.end(), entity) == remainingMesh.end()) {
                    remainingMesh.push_back(entity);
                }
                TraceMeshJob(submission.status == JobSubmissionStatus::RejectedShutdown ?
                    "rejected-shutdown" : "rejected-invalid", 0, entity, chunkData.get(), sourceRevision);
            }
        }
        
        m_dirtyMeshChunks = std::move(remainingMesh);
    }

    // 4. Reset frame allocator
    if (m_frameAllocator) {
        m_frameAllocator->Reset();
    }
}

entt::entity GameWorld::CreateChunkEntity(const std::string& name, const Vec3& position) {
    const int chunkX = static_cast<int>(position.x) / 16;
    const int chunkZ = static_cast<int>(position.z) / 16;
    return CreateChunkEntityWithCoordinates(name, position, chunkX, chunkZ);
}

entt::entity GameWorld::CreateChunkEntity(const std::string& name, const Vec3& position, int chunkX, int chunkZ) {
    return CreateChunkEntityWithCoordinates(name, position, chunkX, chunkZ);
}

entt::entity GameWorld::CreateChunkEntityWithCoordinates(
    const std::string& name, const Vec3& position, int chunkX, int chunkZ) {
    auto entity = m_registry.create();
    
    m_registry.emplace<MetadataComponent>(entity, name, true, false);
    fw::TransformComponent trans;
    trans.location = position;
    trans.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    trans.scale = {1.0f, 1.0f, 1.0f};
    const int cx = chunkX;
    const int cz = chunkZ;

    if (m_registry.valid(m_planetEntity) && m_registry.all_of<PlanetGeometryComponent>(m_planetEntity)) {
        auto& geom = m_registry.get<PlanetGeometryComponent>(m_planetEntity);
        if (!geom.isLogicalSphere) {
            // Flat mode non ancora supportato per render sferico
        } else {
            glm::vec3 sphPos;
            glm::quat sphRot;
            if (fw::MapWorldGenerator::GetSphericalChunkTransform(geom.planetSize, cx, cz, sphPos, sphRot)) {
                // PlanetaryMeshGenerator outputs absolute planet-local vertices.
                // The chunk transform must be exactly the planet center (identity translation and rotation).
                trans.location = {0.0f, 0.0f, 0.0f};
                trans.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
            } else {
                // Se il chunk è fuori dai limiti della mappa sferica (pianeta finito), 
                // lo scaliamo a zero per non renderizzarlo e dopo lo marchiamo come vuoto
                trans.scale = {0.0f, 0.0f, 0.0f};
            }
        }
    }

    m_registry.emplace<TransformComponent>(entity, trans);
    
    m_registry.emplace<VoxelChunkComponent>(entity, cx, cz);
    
    auto& chunk = m_registry.get<VoxelChunkComponent>(entity);
    memset(chunk.blocks, 0, sizeof(chunk.blocks));
    memset(chunk.light, 255, sizeof(chunk.light));

    if (m_context && m_context->isForgeMode) {
        chunk.isGenerated = true;
    }
    
    m_chunkManager.RegisterChunkEntity(cx, cz, entity);
    MarkTerrainDirty(entity);
    return entity;
}

entt::entity GameWorld::CreatePrimitive(const std::string& name, const Vec3& position, const std::string& type) {
    auto entity = m_registry.create();
    
    m_registry.emplace<MetadataComponent>(entity, name, true, true);
    
    fw::TransformComponent trans;
    trans.location = position;
    trans.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    trans.scale = {1.0f, 1.0f, 1.0f};
    m_registry.emplace<TransformComponent>(entity, trans);
    
    m_registry.emplace<PBRMaterialComponent>(entity);
    
    if (type == "Cube" || type == "cube") {
        m_registry.emplace<MeshComponent>(entity, MeshGenerators::MakeCube());
    } else if (type == "Sphere" || type == "sphere") {
        m_registry.emplace<MeshComponent>(entity, MeshGenerators::MakeSphere());
    } else if (type == "SuperSphere" || type == "supersphere") {
        m_registry.emplace<MeshComponent>(entity, MeshGenerators::MakeSuperSphere(2.0f, 1.0f, 24));
    } else if (type == "Obelisk" || type == "obelisk") {
        m_registry.emplace<MeshComponent>(entity, MeshGenerators::MakeObelisk());
    }
    
    return entity;
}

entt::entity GameWorld::CreateEmptyEntity(const std::string& name) {
    auto entity = m_registry.create();
    m_registry.emplace<MetadataComponent>(entity, name, true, false);
    return entity;
}

void GameWorld::QuarantineVramAllocation(VramAllocationHandle handle, uint64_t timelineTarget) {
    if (!m_context || !m_context->vramAllocator || handle == INVALID_VRAM_HANDLE) return;
    if (!m_context->vramAllocator->Quarantine(handle)) {
        std::cerr << "[GameWorld] Could not quarantine stale/non-active VRAM handle " << handle << ".\n";
        return;
    }
    try { m_unconfirmedDmaAllocations.push_back({handle, timelineTarget}); }
    catch (...) {
        // The allocator record itself remains in Quarantined state, so this
        // metadata failure cannot make the suballocation reusable.
        std::cerr << "[GameWorld] Timeline polling metadata unavailable; handle remains allocator-quarantined: " << handle << ".\n";
    }
}

void GameWorld::UploadMeshToVram(entt::entity e) {
    if (auto* mesh = m_registry.try_get<MeshComponent>(e)) {
        if (mesh->vramAlloc == fw::INVALID_VRAM_HANDLE && !mesh->vertices.empty() && m_context && m_context->vramAllocator && m_context->dmaManager) {
            uint32_t meshSizeBytes = (uint32_t)(mesh->vertices.size() * sizeof(fw::Vertex));
            mesh->vramAlloc = m_context->vramAllocator->Allocate(meshSizeBytes);
            if (mesh->vramAlloc != fw::INVALID_VRAM_HANDLE) {
                auto allocInfo = m_context->vramAllocator->GetAllocation(mesh->vramAlloc);
                auto* renderManager = m_context->engine ? m_context->engine->GetRenderManager() : nullptr;
                const auto* compartments = renderManager ? &renderManager->GetVramCompartments() : nullptr;
                DmaUploadResult dmaResult{DmaUploadStatus::InvalidInput, DmaSubmissionState::NotSubmitted, VK_ERROR_INITIALIZATION_FAILED, 0};
                if (allocInfo.valid && compartments && allocInfo.compartmentIdx < compartments->size() &&
                    (*compartments)[allocInfo.compartmentIdx] != VK_NULL_HANDLE) {
                    dmaResult = m_context->dmaManager->UploadMeshAsync(
                        mesh->vertices.data(), meshSizeBytes, allocInfo, (*compartments)[allocInfo.compartmentIdx]);
                }
                if (!dmaResult.completed()) {
                    if (dmaResult.mayStillBeInUse()) {
                        QuarantineVramAllocation(mesh->vramAlloc, dmaResult.timelineTarget);
                    }
                    else m_context->vramAllocator->Free(mesh->vramAlloc);
                    mesh->vramAlloc = fw::INVALID_VRAM_HANDLE;
                    std::cerr << "[GameWorld] Mesh DMA upload failed; mesh not GPU-ready (status="
                              << static_cast<unsigned>(dmaResult.status) << ", VkResult=" << dmaResult.vkResult << ").\n";
                }
            }
        }
    }
}

BlockType GameWorld::GetBlockCartesian(const glm::vec3& worldPos) const {
    return QueryBlockCartesian(worldPos).block;
}

WorldVoxelQueryResult GameWorld::QueryBlockCartesian(const glm::vec3& worldPos) const {
    WorldVoxelQueryResult result;
    if (!std::isfinite(worldPos.x) || !std::isfinite(worldPos.y) || !std::isfinite(worldPos.z)) {
        result.status = WorldVoxelQueryStatus::InvalidInput;
        return result;
    }

    fw::PlanetSize pSize = fw::PlanetSize::Medium;
    bool isSpherical = false;
    if (m_registry.valid(m_planetEntity) && m_registry.all_of<fw::PlanetGeometryComponent>(m_planetEntity)) {
        const auto& geom = m_registry.get<fw::PlanetGeometryComponent>(m_planetEntity);
        isSpherical = geom.isLogicalSphere;
        pSize = geom.planetSize;
    }

    if (isSpherical) {
        float flatX, localY, flatZ;
        fw::MapWorldGenerator::WorldToVoxelCoord(pSize, false, worldPos, flatX, localY, flatZ);
        if (!std::isfinite(flatX) || !std::isfinite(localY) || !std::isfinite(flatZ)) {
            result.status = WorldVoxelQueryStatus::InvalidInput;
            return result;
        }

        // The radial space above the 128 voxel terrain column is empty space,
        // not a collision surface. The inner side below voxel row zero is OOB.
        if (localY < 0.0f) {
            result.status = WorldVoxelQueryStatus::OutOfBounds;
            return result;
        }
        if (localY >= static_cast<float>(fw::ChunkDimensions::VoxelsY)) {
            result.status = WorldVoxelQueryStatus::Air;
            result.block = BlockType::Air;
            return result;
        }

        const int faceResolution = static_cast<int>(fw::PlanetMath::GetFaceResolution(pSize));
        const int64_t maxFlatX = static_cast<int64_t>(faceResolution) * 3 * fw::ChunkDimensions::VoxelsX;
        const int64_t maxFlatZ = static_cast<int64_t>(faceResolution) * 2 * fw::ChunkDimensions::VoxelsZ;
        if (flatX < 0.0f || flatZ < 0.0f || flatX >= static_cast<float>(maxFlatX) ||
            flatZ >= static_cast<float>(maxFlatZ)) {
            result.status = WorldVoxelQueryStatus::OutOfBounds;
            return result;
        }

        const int voxelX = static_cast<int>(std::floor(flatX));
        const int voxelY = static_cast<int>(std::floor(localY));
        const int voxelZ = static_cast<int>(std::floor(flatZ));
        result.voxelCoord = {voxelX, voxelY, voxelZ};

        const int chunkX = voxelX / fw::ChunkDimensions::VoxelsX;
        const int chunkZ = voxelZ / fw::ChunkDimensions::VoxelsZ;
        const entt::entity chunkEntity = m_chunkManager.GetChunkEntity(chunkX, chunkZ);
        if (chunkEntity == entt::null || !m_registry.valid(chunkEntity) ||
            !m_registry.all_of<VoxelChunkComponent>(chunkEntity)) {
            result.status = WorldVoxelQueryStatus::TerrainPending;
            return result;
        }

        const auto& chunk = m_registry.get<VoxelChunkComponent>(chunkEntity);
        if (!chunk.isGenerated) {
            result.status = WorldVoxelQueryStatus::TerrainPending;
            return result;
        }

        const int localX = voxelX - chunkX * fw::ChunkDimensions::VoxelsX;
        const int localZ = voxelZ - chunkZ * fw::ChunkDimensions::VoxelsZ;
        if (localX < 0 || localX >= fw::ChunkDimensions::VoxelsX ||
            localZ < 0 || localZ >= fw::ChunkDimensions::VoxelsZ) {
            result.status = WorldVoxelQueryStatus::OutOfBounds;
            return result;
        }

        result.block = static_cast<BlockType>(chunk.blocks[localX][voxelY][localZ]);
    } else {
        const float voxelXf = std::floor(worldPos.x + 0.5f);
        const float voxelYf = std::floor(worldPos.y + 0.1f);
        const float voxelZf = std::floor(worldPos.z + 0.5f);
        if (voxelXf < static_cast<float>(std::numeric_limits<int>::min()) ||
            voxelXf > static_cast<float>(std::numeric_limits<int>::max()) ||
            voxelYf < static_cast<float>(std::numeric_limits<int>::min()) ||
            voxelYf > static_cast<float>(std::numeric_limits<int>::max()) ||
            voxelZf < static_cast<float>(std::numeric_limits<int>::min()) ||
            voxelZf > static_cast<float>(std::numeric_limits<int>::max())) {
            result.status = WorldVoxelQueryStatus::OutOfBounds;
            return result;
        }
        const int voxelX = static_cast<int>(voxelXf);
        const int voxelY = static_cast<int>(voxelYf);
        const int voxelZ = static_cast<int>(voxelZf);
        result.voxelCoord = {voxelX, voxelY, voxelZ};
        if (voxelY < 0 || voxelY >= fw::ChunkDimensions::VoxelsY) {
            result.status = WorldVoxelQueryStatus::OutOfBounds;
            return result;
        }

        const int chunkX = voxelX >= 0 ? voxelX / fw::ChunkDimensions::VoxelsX
            : (voxelX - (fw::ChunkDimensions::VoxelsX - 1)) / fw::ChunkDimensions::VoxelsX;
        const int chunkZ = voxelZ >= 0 ? voxelZ / fw::ChunkDimensions::VoxelsZ
            : (voxelZ - (fw::ChunkDimensions::VoxelsZ - 1)) / fw::ChunkDimensions::VoxelsZ;
        const entt::entity chunkEntity = m_chunkManager.GetChunkEntity(chunkX, chunkZ);
        if (chunkEntity == entt::null || !m_registry.valid(chunkEntity) ||
            !m_registry.all_of<VoxelChunkComponent>(chunkEntity)) {
            result.status = WorldVoxelQueryStatus::TerrainPending;
            return result;
        }
        const auto& chunk = m_registry.get<VoxelChunkComponent>(chunkEntity);
        if (!chunk.isGenerated) {
            result.status = WorldVoxelQueryStatus::TerrainPending;
            return result;
        }
        const int localX = voxelX - chunkX * fw::ChunkDimensions::VoxelsX;
        const int localZ = voxelZ - chunkZ * fw::ChunkDimensions::VoxelsZ;
        result.block = static_cast<BlockType>(chunk.blocks[localX][voxelY][localZ]);
    }

    if (result.block == BlockType::OutOfBounds) {
        result.status = WorldVoxelQueryStatus::OutOfBounds;
    } else if (result.block == BlockType::Air) {
        result.status = WorldVoxelQueryStatus::Air;
    } else if (result.block == BlockType::Water || result.block == BlockType::Lava ||
               result.block == BlockType::StargatePortal) {
        result.status = WorldVoxelQueryStatus::PassableVoxel;
    } else {
        result.status = WorldVoxelQueryStatus::SolidVoxel;
    }
    return result;
}

PlanetarySpawnResolveStatus GameWorld::TryResolvePlanetarySpawn(
    const glm::vec3& unitDirection, float heightOffset, float colliderRadius,
    glm::vec3& outBodyPosition, float* outSurfaceRadius) const {
    const float directionLength = glm::length(unitDirection);
    if (!std::isfinite(directionLength) || directionLength < 1.0e-5f ||
        !std::isfinite(heightOffset) || !std::isfinite(colliderRadius) || colliderRadius < 0.0f ||
        !m_registry.valid(m_planetEntity) ||
        !m_registry.all_of<fw::PlanetGeometryComponent>(m_planetEntity)) {
        return PlanetarySpawnResolveStatus::InvalidInput;
    }

    const auto& geometry = m_registry.get<fw::PlanetGeometryComponent>(m_planetEntity);
    if (!geometry.isLogicalSphere) return PlanetarySpawnResolveStatus::InvalidInput;

    const glm::vec3 up = unitDirection / directionLength;
    const glm::vec3 planetNorth(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::cross(up, planetNorth);
    if (glm::length(right) < 0.01f) right = glm::cross(up, glm::vec3(1.0f, 0.0f, 0.0f));
    right = glm::normalize(right);
    const glm::vec3 forward = glm::normalize(glm::cross(right, up));

    // Match the collision cylinder's nine horizontal sample offsets. Resolve the
    // highest solid voxel below each sample so the whole collider clears terrain.
    const std::array<glm::vec2, 9> footprint = {{
        {0.0f, 0.0f}, {colliderRadius, 0.0f}, {-colliderRadius, 0.0f},
        {0.0f, colliderRadius}, {0.0f, -colliderRadius},
        {colliderRadius * 0.707f, colliderRadius * 0.707f},
        {-colliderRadius * 0.707f, colliderRadius * 0.707f},
        {colliderRadius * 0.707f, -colliderRadius * 0.707f},
        {-colliderRadius * 0.707f, -colliderRadius * 0.707f}
    }};

    const float planetRadius = fw::PlanetMath::GetPlanetRadius(geometry.planetSize);
    if (!std::isfinite(planetRadius) || planetRadius <= 0.0f) return PlanetarySpawnResolveStatus::InvalidInput;
    float highestSurfaceRadius = -std::numeric_limits<float>::infinity();

    for (const glm::vec2& offset : footprint) {
        const glm::vec3 sampleDirection = glm::normalize(
            up * planetRadius + right * offset.x + forward * offset.y);
        float flatX = 0.0f, localY = 0.0f, flatZ = 0.0f;
        fw::MapWorldGenerator::WorldToVoxelCoord(
            geometry.planetSize, false, sampleDirection * planetRadius, flatX, localY, flatZ);

        const int voxelX = static_cast<int>(std::floor(flatX));
        const int voxelZ = static_cast<int>(std::floor(flatZ));
        const int chunkX = static_cast<int>(std::floor(flatX / fw::ChunkDimensions::VoxelsX));
        const int chunkZ = static_cast<int>(std::floor(flatZ / fw::ChunkDimensions::VoxelsZ));
        const entt::entity chunkEntity = m_chunkManager.GetChunkEntity(chunkX, chunkZ);
        if (chunkEntity == entt::null || !m_registry.valid(chunkEntity) ||
            !m_registry.all_of<VoxelChunkComponent>(chunkEntity) ||
            !m_registry.get<VoxelChunkComponent>(chunkEntity).isGenerated) {
            return PlanetarySpawnResolveStatus::TerrainPending;
        }

        bool foundSolid = false;
        for (int y = fw::ChunkDimensions::VoxelsY - 1; y >= 0; --y) {
            const BlockType block = GetBlockFlat(voxelX, y, voxelZ);
            if (block == BlockType::OutOfBounds) return PlanetarySpawnResolveStatus::TerrainPending;
            if (block == BlockType::Air || block == BlockType::Water || block == BlockType::Lava ||
                block == BlockType::StargatePortal) {
                continue;
            }

            // TerrainSolver and the planetary mesher treat integer localY as
            // the voxel center; the outer face is at localY + half a cell.
            const float surfaceRadius = fw::PlanetRadialMapping::GetCellOuterRadius(
                0, y, planetRadius);
            highestSurfaceRadius = std::max(highestSurfaceRadius, surfaceRadius);
            foundSolid = true;
            break;
        }
        if (!foundSolid) return PlanetarySpawnResolveStatus::NoSolidSurface;
    }

    // Physics samples the collider from 0.1 world units above its base. Keep a
    // further 0.01 numerical margin; authored heightOffset remains clearance
    // above the actual solid surface, not above the reference sphere.
    constexpr float colliderProbeClearance = 0.11f;
    const float clearance = std::max(heightOffset, colliderProbeClearance);
    outBodyPosition = up * (highestSurfaceRadius + clearance);
    if (outSurfaceRadius) *outSurfaceRadius = highestSurfaceRadius;
    return PlanetarySpawnResolveStatus::Ready;
}

BlockType GameWorld::GetBlock(int x, int y, int z) const {
    if (y < 0) return BlockType::OutOfBounds;
    
    int cx = x >= 0 ? x / 16 : (x - 15) / 16;
    int cz = z >= 0 ? z / 16 : (z - 15) / 16;
    
    fw::PlanetSize pSize = fw::PlanetSize::Medium;
    bool isSpherical = false;
    if (m_registry.valid(m_planetEntity) && m_registry.all_of<fw::PlanetGeometryComponent>(m_planetEntity)) {
        const auto& geom = m_registry.get<fw::PlanetGeometryComponent>(m_planetEntity);
        isSpherical = geom.isLogicalSphere;
        pSize = geom.planetSize;
    }

    if (isSpherical) {
        float flatX, localY, flatZ;
        // Mappa il punto centrale del blocco nello spazio sferico
        fw::MapWorldGenerator::WorldToVoxelCoord(pSize, false, glm::vec3(x + 0.5f, y + 0.5f, z + 0.5f), flatX, localY, flatZ);
        return GetBlockFlat((int)std::floor(flatX), (int)std::floor(localY), (int)std::floor(flatZ));
    }
    
    entt::entity chunkEnt = m_chunkManager.GetChunkEntity(cx, cz);
    if (chunkEnt != entt::null && m_registry.valid(chunkEnt)) {
        int lx = x - (cx * 16);
        int lz = z - (cz * 16);
        int ly = y;

        if (lx < 0 || lx >= 16 || lz < 0 || lz >= 16 || ly < 0 || ly >= 128) {
            return BlockType::OutOfBounds;
        }

        const auto& chunk = m_registry.get<VoxelChunkComponent>(chunkEnt);
        if (!chunk.isGenerated) return BlockType::OutOfBounds; // Tratta i chunk in generazione come limiti del mondo per bloccare la fisica
        return static_cast<BlockType>(chunk.blocks[lx][ly][lz]);
    }
    return BlockType::OutOfBounds;
}

BlockType GameWorld::GetBlockFlat(int flatX, int y, int flatZ) const {
    if (y < 0 || y >= 128) return BlockType::OutOfBounds;
    
    int cx = (int)std::floor(flatX / 16.0f);
    int cz = (int)std::floor(flatZ / 16.0f);
    
    entt::entity chunkEnt = m_chunkManager.GetChunkEntity(cx, cz);
    if (chunkEnt != entt::null && m_registry.valid(chunkEnt)) {
        int lx = flatX - (cx * 16);
        int lz = flatZ - (cz * 16);

        if (lx < 0 || lx >= 16 || lz < 0 || lz >= 16 || y < 0 || y >= 128) {
            return BlockType::OutOfBounds;
        }

        const auto& chunk = m_registry.get<VoxelChunkComponent>(chunkEnt);
        if (!chunk.isGenerated) return BlockType::OutOfBounds;
        return static_cast<BlockType>(chunk.blocks[lx][y][lz]);
    }
    return BlockType::OutOfBounds;
}

void GameWorld::SetBlockFlat(int flatX, int y, int flatZ, BlockType type) {
    if (y < 0) return;
    
    int cx = (int)std::floor(flatX / 16.0f);
    int cz = (int)std::floor(flatZ / 16.0f);

    entt::entity chunkEnt = m_chunkManager.GetChunkEntity(cx, cz);
    if (chunkEnt == entt::null || !m_registry.valid(chunkEnt)) {
        chunkEnt = const_cast<GameWorld*>(this)->CreateChunkEntity("Chunk_" + std::to_string(cx) + "_" + std::to_string(cz), {(float)cx * 16.0f, 0.0f, (float)cz * 16.0f});
    }

    if (m_registry.valid(chunkEnt)) {
        int lx = flatX - (cx * 16);
        int lz = flatZ - (cz * 16);
        int ly = y;

        if (lx < 0 || lx >= 16 || lz < 0 || lz >= 16 || ly < 0 || ly >= 128) {
            return;
        }

        auto& chunk = m_registry.get<VoxelChunkComponent>(chunkEnt);
        uint8_t oldType = chunk.blocks[lx][ly][lz];
        if (oldType != static_cast<uint8_t>(type)) {
            chunk.blocks[lx][ly][lz] = static_cast<uint8_t>(type);
            MarkTerrainDirty(chunkEnt);

            // Per ora usiamo posizioni flat per gli aggiornamenti fluido
            EventManager::Get().QueueEvent(Event_BlockUpdated(glm::ivec3(flatX, y, flatZ)));
            EventManager::Get().QueueEvent(Event_BlockUpdated(glm::ivec3(flatX+1, y, flatZ)));
            EventManager::Get().QueueEvent(Event_BlockUpdated(glm::ivec3(flatX-1, y, flatZ)));
            EventManager::Get().QueueEvent(Event_BlockUpdated(glm::ivec3(flatX, y+1, flatZ)));
            EventManager::Get().QueueEvent(Event_BlockUpdated(glm::ivec3(flatX, y-1, flatZ)));
            EventManager::Get().QueueEvent(Event_BlockUpdated(glm::ivec3(flatX, y, flatZ+1)));
            EventManager::Get().QueueEvent(Event_BlockUpdated(glm::ivec3(flatX, y, flatZ-1)));
        }
    }
}

void GameWorld::SetBlock(int x, int y, int z, BlockType type) {
    if (y < 0) return;
    
    int cx = x >= 0 ? x / 16 : (x - 15) / 16;
    int cz = z >= 0 ? z / 16 : (z - 15) / 16;

    fw::PlanetSize pSize = fw::PlanetSize::Medium;
    bool isSpherical = false;
    if (m_registry.valid(m_planetEntity) && m_registry.all_of<fw::PlanetGeometryComponent>(m_planetEntity)) {
        const auto& geom = m_registry.get<fw::PlanetGeometryComponent>(m_planetEntity);
        isSpherical = geom.isLogicalSphere;
        pSize = geom.planetSize;
    }

    if (isSpherical) {
        float flatX, localY, flatZ;
        fw::MapWorldGenerator::WorldToVoxelCoord(pSize, false, glm::vec3(x + 0.5f, y + 0.5f, z + 0.5f), flatX, localY, flatZ);
        SetBlockFlat((int)std::floor(flatX), (int)std::floor(localY), (int)std::floor(flatZ), type);
        return;
    }

    entt::entity chunkEnt = m_chunkManager.GetChunkEntity(cx, cz);
    if (chunkEnt == entt::null || !m_registry.valid(chunkEnt)) {
        chunkEnt = const_cast<GameWorld*>(this)->CreateChunkEntity("Chunk_" + std::to_string(cx) + "_" + std::to_string(cz), {(float)cx * 16.0f, 0.0f, (float)cz * 16.0f});
    }

    if (m_registry.valid(chunkEnt)) {
        int lx = x - (cx * 16);
        int lz = z - (cz * 16);
        int ly = y;

        if (lx < 0 || lx >= 16 || lz < 0 || lz >= 16 || ly < 0 || ly >= 128) {
            return;
        }

        auto& chunk = m_registry.get<VoxelChunkComponent>(chunkEnt);
        uint8_t oldType = chunk.blocks[lx][ly][lz];
        if (oldType != static_cast<uint8_t>(type)) {
            chunk.blocks[lx][ly][lz] = static_cast<uint8_t>(type);
            MarkTerrainDirty(chunkEnt);

            EventManager::Get().QueueEvent(Event_BlockUpdated(glm::ivec3(x, y, z)));
            EventManager::Get().QueueEvent(Event_BlockUpdated(glm::ivec3(x+1, y, z)));
            EventManager::Get().QueueEvent(Event_BlockUpdated(glm::ivec3(x-1, y, z)));
            EventManager::Get().QueueEvent(Event_BlockUpdated(glm::ivec3(x, y+1, z)));
            EventManager::Get().QueueEvent(Event_BlockUpdated(glm::ivec3(x, y-1, z)));
            EventManager::Get().QueueEvent(Event_BlockUpdated(glm::ivec3(x, y, z+1)));
            EventManager::Get().QueueEvent(Event_BlockUpdated(glm::ivec3(x, y, z-1)));
        }
    }
}

void GameWorld::ProcessFluidUpdate(int x, int y, int z) {
    if (y < 0 || y >= 128) return;
    BlockType b = GetBlock(x, y, z);
    
    if (b == BlockType::Water || b == BlockType::Lava) {
        BlockType below = GetBlock(x, y - 1, z);
        if (below == BlockType::Air) {
            SetBlock(x, y - 1, z, b);
        } else if (below != BlockType::Water && below != BlockType::Lava && below != BlockType::OutOfBounds) {
            if (GetBlock(x+1, y, z) == BlockType::Air) SetBlock(x+1, y, z, b);
            if (GetBlock(x-1, y, z) == BlockType::Air) SetBlock(x-1, y, z, b);
            if (GetBlock(x, y, z+1) == BlockType::Air) SetBlock(x, y, z+1, b);
            if (GetBlock(x, y, z-1) == BlockType::Air) SetBlock(x, y, z-1, b);
        }

        if (b == BlockType::Water) {
            if (GetBlock(x, y-1, z) == BlockType::Lava || 
                GetBlock(x+1, y, z) == BlockType::Lava || GetBlock(x-1, y, z) == BlockType::Lava ||
                GetBlock(x, y, z+1) == BlockType::Lava || GetBlock(x, y, z-1) == BlockType::Lava) {
                SetBlock(x, y, z, BlockType::Stone);
            }
        } else if (b == BlockType::Lava) {
            if (GetBlock(x, y-1, z) == BlockType::Water || 
                GetBlock(x+1, y, z) == BlockType::Water || GetBlock(x-1, y, z) == BlockType::Water ||
                GetBlock(x, y, z+1) == BlockType::Water || GetBlock(x, y, z-1) == BlockType::Water) {
                SetBlock(x, y, z, BlockType::Obsidian);
            }
        }
    }
    
    if (b == BlockType::Ice) {
        if (GetBlock(x, y-1, z) == BlockType::Lava || GetBlock(x, y-1, z) == BlockType::LightSource ||
            GetBlock(x+1, y, z) == BlockType::Lava || GetBlock(x+1, y, z) == BlockType::LightSource ||
            GetBlock(x-1, y, z) == BlockType::Lava || GetBlock(x-1, y, z) == BlockType::LightSource ||
            GetBlock(x, y, z+1) == BlockType::Lava || GetBlock(x, y, z+1) == BlockType::LightSource ||
            GetBlock(x, y, z-1) == BlockType::Lava || GetBlock(x, y, z-1) == BlockType::LightSource ||
            GetBlock(x, y+1, z) == BlockType::Lava || GetBlock(x, y+1, z) == BlockType::LightSource) {
            SetBlock(x, y, z, BlockType::Water);
        }
    }
}

bool GameWorld::IsChunkReady(int cx, int cz) const {
    entt::entity e = m_chunkManager.GetChunkEntity(cx, cz);
    if (e != entt::null && m_registry.valid(e)) {
        if (m_registry.all_of<ChunkDirtyComponent>(e)) {
            return false;
        }
        return true;
    }
    return false;
}

void GameWorld::EnqueueDeferredMesh(const std::string& name, glm::vec3 position, fw::MeshComponent mesh, std::shared_ptr<VoxelChunkComponent> chunkData, entt::entity targetEntity, bool newlyGen) {
    std::lock_guard<std::mutex> lock(m_deferredMutex);
    m_deferredMeshes.push_back({name, position, std::move(mesh), chunkData, targetEntity, newlyGen});
}

void GameWorld::DestroyEntity(entt::entity e) {
    if (m_registry.valid(e)) {
        if (auto* mesh = m_registry.try_get<MeshComponent>(e)) {
            if (mesh->vramAlloc != fw::INVALID_VRAM_HANDLE && m_context && m_context->vramAllocator) {
                RetireMeshAllocation(m_context, mesh->vramAlloc);
            }
        }
        m_registry.destroy(e);
    }
}

void GameWorld::MarkTerrainDirty(entt::entity chunkEntity) {
    if (m_registry.valid(chunkEntity)) {
        if (std::find(m_dirtyTerrainChunks.begin(), m_dirtyTerrainChunks.end(), chunkEntity) == m_dirtyTerrainChunks.end()) {
            m_dirtyTerrainChunks.push_back(chunkEntity);
        }
    }
}

void GameWorld::MarkMeshDirty(entt::entity chunkEntity) {
    if (m_meshJobsAccepting.load() && m_registry.valid(chunkEntity)) {
        if (m_cancelToken && m_cancelToken->load()) {
            // A new explicit invalidation starts a fresh cancellation epoch; in-flight
            // workers keep their old, permanently-cancelled shared token.
            m_cancelToken = std::make_shared<std::atomic<bool>>(false);
        }
        if (auto* chunk = m_registry.try_get<VoxelChunkComponent>(chunkEntity)) {
            ++chunk->meshRevision;
        }
        if (m_registry.all_of<ChunkDirtyComponent>(chunkEntity)) {
            auto& dirty = m_registry.get<ChunkDirtyComponent>(chunkEntity);
            if (dirty.pendingJob) dirty.needsRebuild = true;
        } else {
            m_registry.emplace<ChunkDirtyComponent>(chunkEntity);
        }
        
        if (std::find(m_dirtyMeshChunks.begin(), m_dirtyMeshChunks.end(), chunkEntity) == m_dirtyMeshChunks.end()) {
            m_dirtyMeshChunks.push_back(chunkEntity);
        }
    }
}

void GameWorld::MarkAllChunksDirty() {
    auto view = m_registry.view<VoxelChunkComponent>();
    for (auto entity : view) {
        MarkTerrainDirty(entity);
    }
}

void GameWorld::GenerateChunkData(VoxelChunkComponent& chunk, int cx, int cz) {
    std::cerr << "[GameWorld] WARNING: Legacy GenerateChunkData invoked for chunk (" << cx << ", " << cz << "). This should be handled by TerrainSolverSystem!\n";
    
    chunk.cx = cx;
    chunk.cz = cz;
    
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            double worldX = cx * 16.0 + x;
            double worldZ = cz * 16.0 + z;
            
            double scale = 0.01;
            double noiseVal = m_noiseGen.octaveNoise(worldX * scale, 0.0, worldZ * scale, 4, 0.5);
            double normalizedNoise = (noiseVal + 1.0) * 0.5;
            double shapedNoise = std::pow(normalizedNoise, 3.0);
            int height = 20 + (int)(shapedNoise * 80.0);
            
            float tempNoise = (float)m_noiseGen.octaveNoise(worldX * 0.005, 1000.0, worldZ * 0.005, 2, 0.5);
            float humNoise = (float)m_noiseGen.octaveNoise(worldX * 0.005, 2000.0, worldZ * 0.005, 2, 0.5);
            float temp = (tempNoise + 1.0f) * 0.5f;
            float hum = (humNoise + 1.0f) * 0.5f;
            float relHeight = std::clamp((float)height / 128.0f, 0.0f, 1.0f);
            
            const BiomeDef* biome = nullptr;
            if (m_context && m_context->assetManager) {
                biome = fw::MapWorldGenerator::EvaluateBiome(temp, hum, relHeight, m_context->assetManager);
            }
            
            uint8_t surfaceBlock = biome ? biome->surfaceBlockId : (uint8_t)BlockType::Grass;
            uint8_t subsurfaceBlock = biome ? biome->subsurfaceBlockId : (uint8_t)BlockType::Dirt;
            
            for (int y = 0; y < height; ++y) {
                if (y == height - 1) {
                    chunk.blocks[x][y][z] = surfaceBlock;
                } else if (y > height - 4) {
                    chunk.blocks[x][y][z] = subsurfaceBlock;
                } else {
                    chunk.blocks[x][y][z] = (uint8_t)BlockType::Stone;
                }
                chunk.light[x][y][z] = 255;
            }
            
            for (int y = height; y < 128; ++y) {
                chunk.blocks[x][y][z] = (uint8_t)BlockType::Air;
                chunk.light[x][y][z] = 255; 
            }
            
            if (x > 2 && x < 13 && z > 2 && z < 13) {
                double treeNoise = m_noiseGen.noise(worldX * 0.5, 0.0, worldZ * 0.5);
                if (treeNoise > 0.8) {
                    int treeHeight = 4 + (int)(treeNoise * 5.0) % 3;
                    for (int ty = 0; ty < treeHeight; ++ty) {
                        chunk.blocks[x][height + ty][z] = (uint8_t)BlockType::Wood;
                    }
                    for (int lx = x - 2; lx <= x + 2; ++lx) {
                        for (int ly = height + treeHeight - 2; ly <= height + treeHeight + 1; ++ly) {
                            for (int lz = z - 2; lz <= z + 2; ++lz) {
                                if (lx >= 0 && lx < 16 && lz >= 0 && lz < 16 && ly < 128) {
                                    if (chunk.blocks[lx][ly][lz] == (uint8_t)BlockType::Air) {
                                        chunk.blocks[lx][ly][lz] = (uint8_t)BlockType::Leaves;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

bool GameWorld::SaveStructure(const std::string& name, uint8_t placementMode, int pivotX, int pivotY, int pivotZ) {
    std::unordered_map<glm::ivec3, StructureBlock> blocks;
    for (const auto& pair : m_forgeBlocks) {
        blocks[pair.first] = {pair.second.type, pair.second.color};
    }
    return m_structureManager.SaveStructure(name, blocks, placementMode, pivotX, pivotY, pivotZ);
}

bool GameWorld::SaveStructureJSON(const std::string& name, uint8_t placementMode, int pivotX, int pivotY, int pivotZ) {
    std::unordered_map<glm::ivec3, StructureBlock> blocks;
    for (const auto& pair : m_forgeBlocks) {
        blocks[pair.first] = {pair.second.type, pair.second.color};
    }
    return m_structureManager.SaveStructureJSON(name, blocks, placementMode, pivotX, pivotY, pivotZ);
}

entt::entity GameWorld::LoadStructureAsPrefab(const std::string& filepath, const fw::Vec3& position) {
    return m_structureManager.LoadStructureAsPrefab(m_registry, m_context, filepath, position);
}

bool GameWorld::LoadStructureAsVoxels(const std::string& filepath, int startX, int startY, int startZ) {
    return true;
}

class BlockRegistry* GameWorld::GetBlockRegistry() const {
    return m_context ? m_context->blockRegistry : nullptr;
}

class MaterialRegistry* GameWorld::GetMaterialRegistry() const {
    return m_context ? m_context->materialRegistry : nullptr;
}

} // namespace fw
