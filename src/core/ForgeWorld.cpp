#include "pch.h"
#include "ForgeWorld.h"
#include "FreeListAllocator.h"
#include "PoolAllocator.h"
#include "StackAllocator.h"
#include "JobSystem.h"
#include "SharedContext.h"
#include "VramSlabAllocator.h"
#include "VulkanDmaManager.h"
#include <iostream>
#include <cmath>

namespace fw {

// --- MESH GENERATORS (Tradotti dalla classe monolitica) ---
MeshComponent MeshGenerators::MakeCube(float size) {
    MeshComponent m; 
    m.name = "Cube";
    float h = size * 0.5f;
    
    // Vertices (PBR format)
    m.vertices = {
        {{-h,-h,-h}, {0,0,-1}, {0,0}}, {{h,-h,-h}, {0,0,-1}, {1,0}},
        {{h, h,-h}, {0,0,-1}, {1,1}}, {{-h, h,-h}, {0,0,-1}, {0,1}},
        {{-h,-h, h}, {0,0, 1}, {0,0}}, {{h,-h, h}, {0,0, 1}, {1,0}},
        {{h, h, h}, {0,0, 1}, {1,1}}, {{-h, h, h}, {0,0, 1}, {0,1}},
        {{-h,-h,-h}, {-1,0,0}, {0,0}}, {{-h, h,-h}, {-1,0,0}, {1,0}},
        {{-h, h, h}, {-1,0,0}, {1,1}}, {{-h,-h, h}, {-1,0,0}, {0,1}},
        {{h,-h,-h}, { 1,0,0}, {0,0}}, {{h, h,-h}, { 1,0,0}, {1,0}},
        {{h, h, h}, { 1,0,0}, {1,1}}, {{h,-h, h}, { 1,0,0}, {0,1}},
        {{-h,-h,-h}, {0,-1,0}, {0,0}}, {{h,-h,-h}, {0,-1,0}, {1,0}},
        {{h,-h, h}, {0,-1,0}, {1,1}}, {{-h,-h, h}, {0,-1,0}, {0,1}},
        {{-h, h,-h}, {0, 1,0}, {0,0}}, {{h, h,-h}, {0, 1,0}, {1,0}},
        {{h, h, h}, {0, 1,0}, {1,1}}, {{-h, h, h}, {0, 1,0}, {0,1}}
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
            v.normal = v.position.norm();
            v.uv = {(float)si / segs, (float)ri / rings};
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
    
    // Liberiamo la memoria contigua globale
    if (m_masterMemoryBlock) {
        free(m_masterMemoryBlock);
        m_masterMemoryBlock = nullptr;
        std::cout << "[ForgeWorld] O(1) Memory Destruction. Tutta l'arena FORGE liberata.\n";
    }
}

void ForgeWorld::Initialize(SharedContext* context) {
    m_context = context;
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
}

entt::entity ForgeWorld::CreateChunkEntity(const std::string& name, const Vec3& position) {
    auto entity = m_registry.create();
    
    m_registry.emplace<MetadataComponent>(entity, name, true, false);
    m_registry.emplace<TransformComponent>(entity, position);
    m_registry.emplace<VoxelChunkComponent>(entity, (int)position.x / 16, (int)position.z / 16);
    
    // In futuro: i dati dei blocchi verranno allocati usando m_chunkPoolAllocator
    // Esempio: mettiamo un blocco sporco finto
    auto& chunk = m_registry.get<VoxelChunkComponent>(entity);
    chunk.blocks[0][0][0] = 3; // Pietra
    
    // Mark dirty forcing a mesh generation
    MarkChunkDirty(entity);
    
    return entity;
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
        for (auto& def : m_deferredMeshes) {
            auto entity = m_registry.create();
            m_registry.emplace<MetadataComponent>(entity, def.name, true, false);
            m_registry.emplace<TransformComponent>(entity, def.position);
            m_registry.emplace<PBRMaterialComponent>(entity);
            m_registry.emplace<MeshComponent>(entity, std::move(def.mesh));
            std::cout << "[ForgeWorld ECS] Sfera procedurale asincrona registrata nell'ECS con successo!\n";
        }
        m_deferredMeshes.clear();
    }

    // ECS Systems: Iterazione iper-veloce O(1) cache-friendly
    
    // 2. Chunk System: Rileva i chunk "Dirty" e innesca la rigenerazione in background
    if (m_context && m_context->jobSystem) {
        auto dirtyChunks = m_registry.view<VoxelChunkComponent, ChunkDirtyComponent>();
        for (auto entity : dirtyChunks) {
            auto& dirty = dirtyChunks.get<ChunkDirtyComponent>(entity);
            if (dirty.pendingJob) continue;
            
            dirty.pendingJob = true;
            auto& chunk = dirtyChunks.get<VoxelChunkComponent>(entity);
            std::string chunkName = "Chunk_" + std::to_string(chunk.cx) + "_" + std::to_string(chunk.cz);
            
            VoxelChunkComponent chunkCopy = chunk;
            SharedContext* ctx = m_context;
            
            // Sottomette il job
            m_context->jobSystem->Execute([this, entity, chunkName, chunkCopy, ctx]() {
                // Generazione della Mesh nel buffer RAM transitorio del Worker (StackAllocator non thread-safe, usiamo allocazione locale per ora)
                std::vector<Vertex> vertices(1000); // Dummy vertices per test
                uint32_t meshSizeBytes = (uint32_t)(vertices.size() * sizeof(Vertex));
                
                std::this_thread::sleep_for(std::chrono::milliseconds(20)); // Simula calcolo
                
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
                // newMesh contiene l'allocazione vram (da implementare nel componente reale)
                
                std::lock_guard<std::mutex> lock(m_deferredMutex);
                m_deferredMeshes.push_back({chunkName + "_Ready", {0,0,0}, std::move(newMesh)});
            });
            
            // Rimuoviamo il tag così non lo reinvia al prossimo frame
            m_registry.remove<ChunkDirtyComponent>(entity);
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
    
    auto view = m_registry.view<VoxelChunkComponent>();
    for (auto entity : view) {
        const auto& chunk = view.get<VoxelChunkComponent>(entity);
        if (chunk.cx == cx && chunk.cz == cz) {
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

    auto view = m_registry.view<VoxelChunkComponent>();
    for (auto entity : view) {
        auto& chunk = view.get<VoxelChunkComponent>(entity);
        if (chunk.cx == cx && chunk.cz == cz) {
            chunk.blocks[lx][y][lz] = static_cast<uint8_t>(type);
            m_registry.emplace_or_replace<ChunkDirtyComponent>(entity);
            return;
        }
    }
}

} // namespace fw
