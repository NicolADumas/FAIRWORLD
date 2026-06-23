#include "pch.h"
#include "ForgeWorld.h"
#include "FreeListAllocator.h"
#include "PoolAllocator.h"
#include "StackAllocator.h"
#include "JobSystem.h"
#include "SharedContext.h"
#include "VulkanDmaManager.h"
#include "VramSlabAllocator.h"
#include "Components.h"
#include "FAIRWORLD.h"
#include "RenderManager.h"
#include <iostream>
#include <cmath>

namespace fw {

// --- MESH GENERATORS (Tradotti dalla classe monolitica) ---
MeshComponent MeshGenerators::MakeCube(float size) {
    MeshComponent m; 
    m.name = "Cube";
    float h = size * 0.5f;
    
    // Vertices (PBR format)
    // format: {position, color, uv, texIndex, normal, ao, light}
    m.vertices = {
        {{-h,-h,-h}, {1,1,1}, {0,0}, 0.0f, {0,0,-1}, 1.0f, 1.0f}, {{h,-h,-h}, {1,1,1}, {1,0}, 0.0f, {0,0,-1}, 1.0f, 1.0f},
        {{h, h,-h}, {1,1,1}, {1,1}, 0.0f, {0,0,-1}, 1.0f, 1.0f}, {{-h, h,-h}, {1,1,1}, {0,1}, 0.0f, {0,0,-1}, 1.0f, 1.0f},
        {{-h,-h, h}, {1,1,1}, {0,0}, 0.0f, {0,0, 1}, 1.0f, 1.0f}, {{h,-h, h}, {1,1,1}, {1,0}, 0.0f, {0,0, 1}, 1.0f, 1.0f},
        {{h, h, h}, {1,1,1}, {1,1}, 0.0f, {0,0, 1}, 1.0f, 1.0f}, {{-h, h, h}, {1,1,1}, {0,1}, 0.0f, {0,0, 1}, 1.0f, 1.0f},
        {{-h,-h,-h}, {1,1,1}, {0,0}, 0.0f, {-1,0,0}, 1.0f, 1.0f}, {{-h, h,-h}, {1,1,1}, {1,0}, 0.0f, {-1,0,0}, 1.0f, 1.0f},
        {{-h, h, h}, {1,1,1}, {1,1}, 0.0f, {-1,0,0}, 1.0f, 1.0f}, {{-h,-h, h}, {1,1,1}, {0,1}, 0.0f, {-1,0,0}, 1.0f, 1.0f},
        {{h,-h,-h}, {1,1,1}, {0,0}, 0.0f, { 1,0,0}, 1.0f, 1.0f}, {{h, h,-h}, {1,1,1}, {1,0}, 0.0f, { 1,0,0}, 1.0f, 1.0f},
        {{h, h, h}, {1,1,1}, {1,1}, 0.0f, { 1,0,0}, 1.0f, 1.0f}, {{h,-h, h}, {1,1,1}, {0,1}, 0.0f, { 1,0,0}, 1.0f, 1.0f},
        {{-h,-h,-h}, {1,1,1}, {0,0}, 0.0f, {0,-1,0}, 1.0f, 1.0f}, {{h,-h,-h}, {1,1,1}, {1,0}, 0.0f, {0,-1,0}, 1.0f, 1.0f},
        {{h,-h, h}, {1,1,1}, {1,1}, 0.0f, {0,-1,0}, 1.0f, 1.0f}, {{-h,-h, h}, {1,1,1}, {0,1}, 0.0f, {0,-1,0}, 1.0f, 1.0f},
        {{-h, h,-h}, {1,1,1}, {0,0}, 0.0f, {0, 1,0}, 1.0f, 1.0f}, {{h, h,-h}, {1,1,1}, {1,0}, 0.0f, {0, 1,0}, 1.0f, 1.0f},
        {{h, h, h}, {1,1,1}, {1,1}, 0.0f, {0, 1,0}, 1.0f, 1.0f}, {{-h, h, h}, {1,1,1}, {0,1}, 0.0f, {0, 1,0}, 1.0f, 1.0f}
    };
    
    // Faces
    for(int i = 0; i < 6; i++) {
        Face f; 
        int b = i * 4;
        f.indices = {b, b+1, b+2, b+3};
        // Face normal (simplified)
        f.faceNormal = m.vertices[b].normal;
        m.faces.push_back(f);
    }
    
    return m;
}

MeshComponent MeshGenerators::MakeSphere(int segs, int rings, float r) {
    MeshComponent m; 
    m.name = "Sphere";
    const float PI = 3.14159265f;
    
    for(int ri = 0; ri <= rings; ri++) {
        float phi = PI * ri / rings;
        for(int si = 0; si <= segs; si++) {
            float theta = 2 * PI * si / segs;
            Vertex v;
            v.position = {r * std::sin(phi) * std::cos(theta),
                          r * std::cos(phi),
                          r * std::sin(phi) * std::sin(theta)};
            v.color = {1.0f, 1.0f, 1.0f};
            v.texIndex = 0.0f;
            v.normal = v.position.norm();
            v.uv = {(float)si / segs, (float)ri / rings};
            v.ao = 1.0f;
            v.light = 1.0f;
            m.vertices.push_back(v);
        }
    }
    
    for(int ri = 0; ri < rings; ri++) {
        for(int si = 0; si < segs; si++) {
            int a = ri * (segs + 1) + si;
            int b = a + 1;
            int c = a + (segs + 1);
            int d = c + 1;
            Face f; 
            f.indices = {a, b, d, c};
            m.faces.push_back(f);
        }
    }
    
    return m;
}

// --- FORGE WORLD ---
ForgeWorld::ForgeWorld() {
    std::cout << "[ForgeWorld] Istanza creata.\n";
}

ForgeWorld::~ForgeWorld() {
    // Svuotiamo il registry per sicurezza prima di liberare la memoria
    m_registry.clear();
    
    // Reset dell'allocatore a partizioni
    if (m_masterMemoryBlock) {
        free(m_masterMemoryBlock);
        m_masterMemoryBlock = nullptr;
        std::cout << "[ForgeWorld] O(1) Memory Destruction. Tutta l'arena FORGE liberata.\n";
    }

    m_registry.clear();
    m_activeChunks.clear();
    {
        std::lock_guard<std::mutex> lock(m_deferredMutex);
        m_deferredMeshes.clear();
    }
}

void ForgeWorld::Initialize(SharedContext* context) {
    m_context = context;
    
    // Pulisci stato precedente in caso di reinizializzazione
    m_registry.clear();
    m_activeChunks.clear();
    {
        std::lock_guard<std::mutex> lock(m_deferredMutex);
        m_deferredMeshes.clear();
    }

    std::cout << "[ForgeWorld] Inizializzazione Memory Partitioning per FORGE...\n";
    
    // Allocazione monolitica di 128 MB per la Forge per isolare la frammentazione
    const size_t TOTAL_FORGE_MEMORY = 128 * 1024 * 1024; 
    m_masterMemoryBlock = malloc(TOTAL_FORGE_MEMORY);
    
    // 1. Persistent Memory (32 MB)
    size_t persistentSize = 32 * 1024 * 1024;
    m_persistentAllocator = std::make_unique<fw::memory::FreeListAllocator>(persistentSize, m_masterMemoryBlock);
    
    // 2. Chunk Pool (64 MB) - Assumiamo Chunk di 16KB circa
    size_t poolSize = 64 * 1024 * 1024;
    void* poolStart = static_cast<char*>(m_masterMemoryBlock) + persistentSize;
    m_chunkPoolAllocator = std::make_unique<fw::memory::PoolAllocator>(
        poolSize,                                        // totalSize
        16384,                                           // chunkSize
        static_cast<uint8_t>(alignof(std::max_align_t)), // chunkAlignment
        poolStart);
    
    // 3. Transient Frame Memory (32 MB)
    size_t transientSize = 32 * 1024 * 1024;
    void* transientStart = static_cast<char*>(poolStart) + poolSize;
    m_frameAllocator = std::make_unique<fw::memory::StackAllocator>(transientSize, transientStart);
    
    std::cout << "[ForgeWorld] Arene di memoria dedicate pronte. ECS Registry online.\n";

    // --- INIZIALIZZAZIONE INFRASTRUTTURA ASINCRONA GLOBALE ---
    if (!m_context->jobSystem) {
        m_context->jobSystem = new fw::JobSystem();
        m_context->jobSystem->Initialize();
    }
    
    if (!m_context->vramAllocator) {
        m_context->vramAllocator = new fw::VramSlabAllocator(512 * 1024 * 1024);
    }
    
    // VulkanDmaManager richiede RenderManager dal FairWorldEngine
    if (!m_context->dmaManager) {
        m_context->dmaManager = new fw::VulkanDmaManager();
        if (m_context->engine) {
            if (auto* rm = m_context->engine->GetRenderManager()) {
                m_context->dmaManager->Initialize(
                    rm->GetDevice(),
                    rm->GetTransferQueue(),
                    rm->GetTransferCommandPool(),
                    rm->GetStagingRingBuffer(),
                    rm->GetMappedStagingData(),
                    rm->GetStagingBufferSize(),
                    rm->GetGlobalVramBuffer(),
                    rm->GetQueueMutex()
                );
            }
        }
    }
}

entt::entity ForgeWorld::CreateChunkEntity(const std::string& name, const Vec3& position) {
    auto entity = m_registry.create();
    
    m_registry.emplace<MetadataComponent>(entity, name, true, false);
    m_registry.emplace<TransformComponent>(entity, position);
    m_registry.emplace<VoxelChunkComponent>(entity, (int)position.x / 16, (int)position.z / 16);
    
    // Inizializza a zero
    auto& chunk = m_registry.get<VoxelChunkComponent>(entity);
    memset(chunk.blocks, 0, sizeof(chunk.blocks));
    memset(chunk.light, 255, sizeof(chunk.light)); // Luce massima di default

    // GenerateChunkData(chunk, chunk.cx, chunk.cz); // RIMOSSO: Spostato nel Worker Thread!
    
    // Hash (cx, cz) -> entity
    uint64_t hashKey = ((uint64_t)(uint32_t)chunk.cx << 32) | (uint32_t)chunk.cz;
    m_activeChunks[hashKey] = entity;

    // Mark dirty forcing a mesh generation
    MarkChunkDirty(entity);
    
    return entity;
}

void ForgeWorld::GenerateChunkData(VoxelChunkComponent& chunk, int cx, int cz) {
    // Generazione Procedurale con Perlin Noise
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            // Coordinate globali
            double worldX = cx * 16.0 + x;
            double worldZ = cz * 16.0 + z;

            // FBM (Fractal Brownian Motion)
            // Parametri: scala x/z, octaves, persistence
            double scale = 0.02; 
            double noiseVal = m_noiseGen.octaveNoise(worldX * scale, 0.0, worldZ * scale, 4, 0.5);
            
            // Mappa il noise (0.0 - 1.0) all'altezza (es. 20 a 60 blocchi)
            int height = 20 + (int)(noiseVal * 40.0);
            
            // Limita l'altezza per evitare overflow
            if (height >= 128) height = 127;
            if (height < 1) height = 1;

            // Riempi la colonna
            for (int y = 0; y < height; ++y) {
                if (y == height - 1) {
                    chunk.blocks[x][y][z] = 1; // Erba in cima
                } else if (y > height - 4) {
                    chunk.blocks[x][y][z] = 2; // Terra sotto
                } else {
                    chunk.blocks[x][y][z] = 3; // Pietra in profondità
                }
            }
        }
    }
}

entt::entity ForgeWorld::CreatePrimitive(const std::string& name, const Vec3& position, const std::string& type) {
    auto entity = m_registry.create();
    
    m_registry.emplace<MetadataComponent>(entity, name, true, false);
    m_registry.emplace<TransformComponent>(entity, position);
    m_registry.emplace<PBRMaterialComponent>(entity);
    
    if (type == "Cube") {
        m_registry.emplace<MeshComponent>(entity, MeshGenerators::MakeCube());
    } else if (type == "Sphere") {
        m_registry.emplace<MeshComponent>(entity, MeshGenerators::MakeSphere());
    }
    
    return entity;
}

void ForgeWorld::MarkChunkDirty(entt::entity chunkEntity) {
    if (!m_registry.all_of<ChunkDirtyComponent>(chunkEntity)) {
        m_registry.emplace<ChunkDirtyComponent>(chunkEntity);
    }
}

void ForgeWorld::EnqueueDeferredMesh(const std::string& name, const Vec3& position, MeshComponent&& mesh) {
    std::lock_guard<std::mutex> lock(m_deferredMutex);
    m_deferredMeshes.push_back({name, position, std::move(mesh)});
}

void ForgeWorld::Update(float dt) {
    // 1. Processa la coda dei comandi differiti (es. mesh generate dai Worker Threads)
    {
        std::lock_guard<std::mutex> lock(m_deferredMutex);
        if (!m_deferredMeshes.empty()) {
            std::cout << "[ForgeWorld::Update] Elaborazione " << m_deferredMeshes.size() << " mesh differiti dalla coda asincrona.\n";
        }
        for (auto& def : m_deferredMeshes) {
            // Aggiorna l'entità originaria del chunk invece di crearne una nuova
            if (m_registry.valid(def.targetEntity)) {
                // Sincronizza i dati procedurali generati in background
                auto& chunk = m_registry.get<VoxelChunkComponent>(def.targetEntity);
                memcpy(chunk.blocks, def.chunkData->blocks, sizeof(chunk.blocks));
                memcpy(chunk.light, def.chunkData->light, sizeof(chunk.light));
                
                m_registry.emplace_or_replace<PBRMaterialComponent>(def.targetEntity);
                m_registry.emplace_or_replace<MeshComponent>(def.targetEntity, std::move(def.mesh));
                // std::cout << "[ForgeWorld ECS] " << def.name << " aggiornato nell'ECS con successo!\n";
            }
        }
        m_deferredMeshes.clear();
    }

    // ECS Systems: Iterazione iper-veloce O(1) cache-friendly
    
    // 2. Chunk Streaming System: Carica i chunk attorno alla telecamera
    int viewDistance = 3; // Raggio di chunk
    
    if (m_context) {
        float px = m_context->activeCameraView.cameraPosition.x;
        float py = m_context->activeCameraView.cameraPosition.y;
        float pz = m_context->activeCameraView.cameraPosition.z;
        
        static int frameCounter = 0;
        if (frameCounter++ % 60 == 0) {
            std::cout << "[ForgeWorld::Update] Camera a (" << px << ", " << py << ", " << pz << ")\n";
        }
        
        int pcx = (int)px / 16;
        int pcz = (int)pz / 16;
        
        for (int dx = -viewDistance; dx <= viewDistance; dx++) {
            for (int dz = -viewDistance; dz <= viewDistance; dz++) {
                int cx = pcx + dx;
                int cz = pcz + dz;
                uint64_t hashKey = ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz;
                
                if (m_activeChunks.find(hashKey) == m_activeChunks.end()) {
                    // Crea il chunk!
                    std::string name = "Chunk_" + std::to_string(cx) + "_" + std::to_string(cz);
                    // std::cout << "[ForgeWorld] Triggering Creation for " << name << std::endl;
                    CreateChunkEntity(name, {cx * 16.0f, 0, cz * 16.0f});
                }
            }
        }
        
        // --- 2.b Caricamento Predittivo tramite Portali ---
        auto portalView = m_registry.view<PortalComponent, VolumeComponent, TransformComponent>();
        
        struct PortalTarget { float px; float py; float pz; float radius; Mat4 mTel; };
        std::vector<PortalTarget> activePortals;

        for (auto pEntity : portalView) {
            const auto& portal = portalView.get<PortalComponent>(pEntity);
            const auto& vol = portalView.get<VolumeComponent>(pEntity);
            const auto& pTrans = portalView.get<TransformComponent>(pEntity);
            if (portal.isActive) {
                activePortals.push_back({pTrans.location.x, pTrans.location.y, pTrans.location.z, vol.radius, portal.mTeleport});
            }
        }
        
        for (const auto& pt : activePortals) {
            // Distanza dal portale
            Vec3 diff = {px - pt.px, py - pt.py, pz - pt.pz};
            float distSq = diff.dot(diff);
            
            if (distSq < pt.radius * pt.radius) {
                // Il giocatore è nell'area del portale. Calcoliamo la posizione virtuale!
                Mat4 mTel = pt.mTel;
                Vec3 virtualPos = {
                    mTel.m[0][0]*px + mTel.m[0][1]*py + mTel.m[0][2]*pz + mTel.m[0][3],
                    mTel.m[1][0]*px + mTel.m[1][1]*py + mTel.m[1][2]*pz + mTel.m[1][3],
                    mTel.m[2][0]*px + mTel.m[2][1]*py + mTel.m[2][2]*pz + mTel.m[2][3]
                };
                
                int vcx = (int)virtualPos.x / 16;
                int vcz = (int)virtualPos.z / 16;
                
                for (int dx = -viewDistance; dx <= viewDistance; dx++) {
                    for (int dz = -viewDistance; dz <= viewDistance; dz++) {
                        int cx = vcx + dx;
                        int cz = vcz + dz;
                        uint64_t hashKey = ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz;
                        
                        if (m_activeChunks.find(hashKey) == m_activeChunks.end()) {
                            std::string name = "PortalDestChunk_" + std::to_string(cx) + "_" + std::to_string(cz);
                            CreateChunkEntity(name, {cx * 16.0f, virtualPos.y, cz * 16.0f});
                        }
                    }
                }
            }
        }
    }

    // 3. Chunk System: Rileva i chunk "Dirty" e innesca la rigenerazione in background
    if (m_context && m_context->jobSystem) {
        auto dirtyChunks = m_registry.view<VoxelChunkComponent, ChunkDirtyComponent>();
        std::vector<entt::entity> toRemove;
        for (auto entity : dirtyChunks) {
            auto& dirty = dirtyChunks.get<ChunkDirtyComponent>(entity);
            if (dirty.pendingJob) continue;
            
            dirty.pendingJob = true;
            auto& chunk = dirtyChunks.get<VoxelChunkComponent>(entity);
            std::string chunkName = "Chunk_" + std::to_string(chunk.cx) + "_" + std::to_string(chunk.cz);
            
            VoxelChunkComponent chunkCopy = chunk;
            // Creiamo uno shared_ptr per i dati del chunk, isolati per il thread
            auto chunkData = std::make_shared<VoxelChunkComponent>(chunkCopy);
            SharedContext* ctx = m_context;
            
            // Sottomette il job
            m_context->jobSystem->Execute([this, entity, chunkName, chunkData, ctx]() {
                // 1. GENERAZIONE DATI PROCEDURALI IN BACKGROUND (Niente più CPU stall sul Main Thread!)
                GenerateChunkData(*chunkData, chunkData->cx, chunkData->cz);
                
                // 2. GENERAZIONE MESH
                std::vector<Vertex> vertices;
                vertices.reserve(16384); // Riserva sufficiente memoria per evitare riallocazioni lente
                
                auto getBlock = [&](int x, int y, int z) -> uint8_t {
                    if (static_cast<unsigned>(x) >= CHUNK_SIZE || static_cast<unsigned>(y) >= CHUNK_HEIGHT || static_cast<unsigned>(z) >= CHUNK_SIZE) return 0;
                    return chunkData->blocks[x][y][z];
                };
                
                auto getLight = [&](int x, int y, int z) -> float {
                    if (static_cast<unsigned>(x) >= CHUNK_SIZE || static_cast<unsigned>(y) >= CHUNK_HEIGHT || static_cast<unsigned>(z) >= CHUNK_SIZE) return 1.0f;
                    return chunkData->light[x][y][z] / 255.0f;
                };

                auto calcAO = [&](bool side1, bool side2, bool corner) -> float {
                    if (side1 && side2) return 0.25f;
                    return 1.0f - (side1 + side2 + corner) * 0.25f;
                };

                // Offset dei vertici per ogni faccia (0=Top, 1=Bottom, 2=Left, 3=Right, 4=Front, 5=Back)
                // Usiamo un mesher Naive che controlla i 6 vicini
                for (int x = 0; x < CHUNK_SIZE; x++) {
                    for (int y = 0; y < CHUNK_HEIGHT; y++) {
                        for (int z = 0; z < CHUNK_SIZE; z++) {
                            uint8_t block = chunkData->blocks[x][y][z];
                            if (block == 0) continue; // Aria
                            
                            float px = x;
                            float py = y;
                            float pz = z;

                            // Top (+Y)
                            if (getBlock(x, y + 1, z) == 0) {
                                float light = getLight(x, y + 1, z);
                                float ao00 = calcAO(getBlock(x-1, y+1, z), getBlock(x, y+1, z-1), getBlock(x-1, y+1, z-1));
                                float ao10 = calcAO(getBlock(x+1, y+1, z), getBlock(x, y+1, z-1), getBlock(x+1, y+1, z-1));
                                float ao11 = calcAO(getBlock(x+1, y+1, z), getBlock(x, y+1, z+1), getBlock(x+1, y+1, z+1));
                                float ao01 = calcAO(getBlock(x-1, y+1, z), getBlock(x, y+1, z+1), getBlock(x-1, y+1, z+1));
                                vertices.push_back({{px-0.5f, py+0.5f, pz-0.5f}, {1,1,1}, {0,0}, (float)block, {0,1,0}, ao00, light});
                                vertices.push_back({{px+0.5f, py+0.5f, pz-0.5f}, {1,1,1}, {1,0}, (float)block, {0,1,0}, ao10, light});
                                vertices.push_back({{px+0.5f, py+0.5f, pz+0.5f}, {1,1,1}, {1,1}, (float)block, {0,1,0}, ao11, light});
                                vertices.push_back({{px-0.5f, py+0.5f, pz-0.5f}, {1,1,1}, {0,0}, (float)block, {0,1,0}, ao00, light});
                                vertices.push_back({{px+0.5f, py+0.5f, pz+0.5f}, {1,1,1}, {1,1}, (float)block, {0,1,0}, ao11, light});
                                vertices.push_back({{px-0.5f, py+0.5f, pz+0.5f}, {1,1,1}, {0,1}, (float)block, {0,1,0}, ao01, light});
                            }
                            // Bottom (-Y)
                            if (getBlock(x, y - 1, z) == 0) {
                                float light = getLight(x, y - 1, z);
                                vertices.push_back({{px-0.5f, py-0.5f, pz+0.5f}, {1,1,1}, {0,1}, (float)block, {0,-1,0}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py-0.5f, pz+0.5f}, {1,1,1}, {1,1}, (float)block, {0,-1,0}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py-0.5f, pz-0.5f}, {1,1,1}, {1,0}, (float)block, {0,-1,0}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py-0.5f, pz+0.5f}, {1,1,1}, {0,1}, (float)block, {0,-1,0}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py-0.5f, pz-0.5f}, {1,1,1}, {1,0}, (float)block, {0,-1,0}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py-0.5f, pz-0.5f}, {1,1,1}, {0,0}, (float)block, {0,-1,0}, 1.0f, light});
                            }
                            // Left (-X)
                            if (getBlock(x - 1, y, z) == 0) {
                                float light = getLight(x - 1, y, z);
                                vertices.push_back({{px-0.5f, py-0.5f, pz-0.5f}, {1,1,1}, {0,0}, (float)block, {-1,0,0}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py+0.5f, pz-0.5f}, {1,1,1}, {0,1}, (float)block, {-1,0,0}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py+0.5f, pz+0.5f}, {1,1,1}, {1,1}, (float)block, {-1,0,0}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py-0.5f, pz-0.5f}, {1,1,1}, {0,0}, (float)block, {-1,0,0}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py+0.5f, pz+0.5f}, {1,1,1}, {1,1}, (float)block, {-1,0,0}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py-0.5f, pz+0.5f}, {1,1,1}, {1,0}, (float)block, {-1,0,0}, 1.0f, light});
                            }
                            // Right (+X)
                            if (getBlock(x + 1, y, z) == 0) {
                                float light = getLight(x + 1, y, z);
                                vertices.push_back({{px+0.5f, py-0.5f, pz+0.5f}, {1,1,1}, {0,0}, (float)block, {1,0,0}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py+0.5f, pz+0.5f}, {1,1,1}, {0,1}, (float)block, {1,0,0}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py+0.5f, pz-0.5f}, {1,1,1}, {1,1}, (float)block, {1,0,0}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py-0.5f, pz+0.5f}, {1,1,1}, {0,0}, (float)block, {1,0,0}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py+0.5f, pz-0.5f}, {1,1,1}, {1,1}, (float)block, {1,0,0}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py-0.5f, pz-0.5f}, {1,1,1}, {1,0}, (float)block, {1,0,0}, 1.0f, light});
                            }
                            // Front (+Z)
                            if (getBlock(x, y, z + 1) == 0) {
                                float light = getLight(x, y, z + 1);
                                vertices.push_back({{px-0.5f, py-0.5f, pz+0.5f}, {1,1,1}, {0,0}, (float)block, {0,0,1}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py-0.5f, pz+0.5f}, {1,1,1}, {1,0}, (float)block, {0,0,1}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py+0.5f, pz+0.5f}, {1,1,1}, {1,1}, (float)block, {0,0,1}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py-0.5f, pz+0.5f}, {1,1,1}, {0,0}, (float)block, {0,0,1}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py+0.5f, pz+0.5f}, {1,1,1}, {1,1}, (float)block, {0,0,1}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py+0.5f, pz+0.5f}, {1,1,1}, {0,1}, (float)block, {0,0,1}, 1.0f, light});
                            }
                            // Back (-Z)
                            if (getBlock(x, y, z - 1) == 0) {
                                float light = getLight(x, y, z - 1);
                                vertices.push_back({{px+0.5f, py-0.5f, pz-0.5f}, {1,1,1}, {0,0}, (float)block, {0,0,-1}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py-0.5f, pz-0.5f}, {1,1,1}, {1,0}, (float)block, {0,0,-1}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py+0.5f, pz-0.5f}, {1,1,1}, {1,1}, (float)block, {0,0,-1}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py-0.5f, pz-0.5f}, {1,1,1}, {0,0}, (float)block, {0,0,-1}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py+0.5f, pz-0.5f}, {1,1,1}, {1,1}, (float)block, {0,0,-1}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py+0.5f, pz-0.5f}, {1,1,1}, {0,1}, (float)block, {0,0,-1}, 1.0f, light});
                            }
                        }
                    }
                }
                
                // Se la chunk è completamente vuota (solo aria), non facciamo l'upload
                if (vertices.empty()) {
                    // std::cout << "[ForgeWorld Worker] " << chunkName << " e' vuoto (0 vertici). Scartato.\n";
                    return;
                }
                
                // std::cout << "[ForgeWorld Worker] " << chunkName << " generato: " << vertices.size() << " vertici. Procedo al DMA.\n";
                
                uint32_t meshSizeBytes = (uint32_t)(vertices.size() * sizeof(Vertex));
                
                // Richiede un blocco dalla VRAM
                auto vramAlloc = ctx->vramAllocator->Allocate(meshSizeBytes);
                if (!vramAlloc.valid) {
                    std::cerr << "[JobSystem] Errore: impossibile allocare VRAM per " << chunkName << "!\n";
                    return;
                }
                
                // Trasferimento DMA Asincrono Zero-Copy (Burst PCIe)
                uint64_t expectedTimeline = ctx->dmaManager->UploadMeshAsync(vertices.data(), meshSizeBytes, vramAlloc);
                
                // Accoda l'aggiornamento dei componenti nell'ECS per il frame successivo
                MeshComponent newMesh;
                newMesh.name = chunkName + "_Mesh";
                newMesh.vertices = std::move(vertices);
                newMesh.vramAlloc = vramAlloc;
                
                std::lock_guard<std::mutex> lock(m_deferredMutex);
                m_deferredMeshes.push_back({
                    chunkName + "_Ready", 
                    {(float)chunkData->cx * 16.0f, 0.0f, (float)chunkData->cz * 16.0f}, 
                    std::move(newMesh),
                    chunkData,
                    entity
                });
            });
            
            // Segnamo l'entità per la rimozione del tag, ma lo facciamo in modo sicuro dopo il loop
            toRemove.push_back(entity);
        }
        
        // Rimuoviamo il tag così non lo reinvia al prossimo frame (Fuori dal loop della view)
        for (auto e : toRemove) {
            m_registry.remove<ChunkDirtyComponent>(e);
        }
    }
    
    // 3. Transient Memory Reset (azzerata a ogni frame)
    if (m_frameAllocator) {
        m_frameAllocator->Reset();
    }
    
    // 2. Job System Dispatch: (Simulato)
    // Se ci sono task asincroni, li inviamo al Fiber scheduler.
    // std::cout << "Dispatching Jobs for Frame...\n";
    
    // Esempio iterazione ECS: Aggiorna fisiche o matrici
    auto view = m_registry.view<TransformComponent>();
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        // Eventuale logica o animazione su array contiguo
        // transform.rotation.y += dt; 
    }
}

BlockType ForgeWorld::GetBlock(int x, int y, int z) const {
    if (y < 0 || y >= 128) return BlockType::Air;
    int cx = x >= 0 ? x / 16 : (x - 15) / 16;
    int cz = z >= 0 ? z / 16 : (z - 15) / 16;
    int lx = x - (cx * 16);
    int lz = z - (cz * 16);
    
    uint64_t hashKey = ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz;
    auto it = m_activeChunks.find(hashKey);
    if (it != m_activeChunks.end()) {
        if (m_registry.valid(it->second)) {
            const auto& chunk = m_registry.get<VoxelChunkComponent>(it->second);
            return static_cast<BlockType>(chunk.blocks[lx][y][lz]);
        }
    }
    return BlockType::Air;
}

void ForgeWorld::SetBlock(int x, int y, int z, BlockType type) {
    if (y < 0 || y >= 128) return;
    int cx = x >= 0 ? x / 16 : (x - 15) / 16;
    int cz = z >= 0 ? z / 16 : (z - 15) / 16;
    int lx = x - (cx * 16);
    int lz = z - (cz * 16);

    uint64_t hashKey = ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz;
    auto it = m_activeChunks.find(hashKey);
    if (it != m_activeChunks.end()) {
        if (m_registry.valid(it->second)) {
            auto& chunk = m_registry.get<VoxelChunkComponent>(it->second);
            chunk.blocks[lx][y][lz] = static_cast<uint8_t>(type);
            m_registry.emplace_or_replace<ChunkDirtyComponent>(it->second);
        }
    }
}

bool ForgeWorld::IsChunkReady(int cx, int cz) const {
    uint64_t hashKey = ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz;
    auto it = m_activeChunks.find(hashKey);
    if (it != m_activeChunks.end()) {
        if (m_registry.valid(it->second)) {
            // Se ha il componente Dirty, significa che sta ancora generando o che deve ancora essere generato.
            if (m_registry.all_of<ChunkDirtyComponent>(it->second)) {
                return false;
            }
            return true;
        }
    }
    return false;
}

} // namespace fw
