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
#include "EventManager.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include "Systems.h"
#include <cmath>
#include <vector>

namespace fw {

// --- MESH GENERATORS (Tradotti dalla classe monolitica) ---
MeshComponent MeshGenerators::MakeCube(float size) {
    MeshComponent m; 
    m.name = "Cube";
    float h = size * 0.5f;
    fw::Vec3 color = {1.0f, 1.0f, 1.0f};
    
    // Vertices (PBR format): {position, color, uv, texIndex, normal, ao, light}
    auto addFace = [&](fw::Vec3 v0, fw::Vec3 v1, fw::Vec3 v2, fw::Vec3 v3, fw::Vec3 n) {
        m.vertices.push_back({v0, color, {0,0}, -1.0f, n, 1.0f, 1.0f});
        m.vertices.push_back({v1, color, {1,0}, -1.0f, n, 1.0f, 1.0f});
        m.vertices.push_back({v2, color, {1,1}, -1.0f, n, 1.0f, 1.0f});
        m.vertices.push_back({v0, color, {0,0}, -1.0f, n, 1.0f, 1.0f});
        m.vertices.push_back({v2, color, {1,1}, -1.0f, n, 1.0f, 1.0f});
        m.vertices.push_back({v3, color, {0,1}, -1.0f, n, 1.0f, 1.0f});
    };

    // Front (-Z)
    addFace({-h,-h,-h}, { h,-h,-h}, { h, h,-h}, {-h, h,-h}, {0,0,-1});
    // Back (+Z)
    addFace({ h,-h, h}, {-h,-h, h}, {-h, h, h}, { h, h, h}, {0,0, 1});
    // Left (-X)
    addFace({-h,-h, h}, {-h,-h,-h}, {-h, h,-h}, {-h, h, h}, {-1,0,0});
    // Right (+X)
    addFace({ h,-h,-h}, { h,-h, h}, { h, h, h}, { h, h,-h}, { 1,0,0});
    // Bottom (-Y)
    addFace({-h,-h, h}, { h,-h, h}, { h,-h,-h}, {-h,-h,-h}, {0,-1,0});
    // Top (+Y)
    addFace({-h, h,-h}, { h, h,-h}, { h, h, h}, {-h, h, h}, {0, 1,0});
    
    return m;
}

MeshComponent MeshGenerators::MakeSphere(int segs, int rings, float r) {
    MeshComponent m; 
    m.name = "Sphere";
    const float PI = 3.14159265f;
    
    std::vector<Vertex> tempVerts;
    for(int ri = 0; ri <= rings; ri++) {
        float phi = PI * ri / rings;
        for(int si = 0; si <= segs; si++) {
            float theta = 2 * PI * si / segs;
            Vertex v;
            v.position = {r * std::sin(phi) * std::cos(theta),
                          r * std::cos(phi),
                          r * std::sin(phi) * std::sin(theta)};
            v.color = {1.0f, 1.0f, 1.0f};
            v.texIndex = -1.0f;
            v.normal = v.position.norm();
            v.uv = {(float)si / segs, (float)ri / rings};
            v.ao = 1.0f;
            v.light = 1.0f;
            tempVerts.push_back(v);
        }
    }
    
    for(int ri = 0; ri < rings; ri++) {
        for(int si = 0; si < segs; si++) {
            int a = ri * (segs + 1) + si;
            int b = a + 1;
            int c = a + (segs + 1);
            int d = c + 1;
            
            // Triangle 1: a, b, c
            m.vertices.push_back(tempVerts[a]);
            m.vertices.push_back(tempVerts[b]);
            m.vertices.push_back(tempVerts[c]);
            
            // Triangle 2: c, b, d
            m.vertices.push_back(tempVerts[c]);
            m.vertices.push_back(tempVerts[b]);
            m.vertices.push_back(tempVerts[d]);
        }
    }
    
    return m;
}

MeshComponent MeshGenerators::MakeGridBox(int width, int height, int depth, float thickness) {
    MeshComponent m;
    m.name = "GridBox";
    
    // Aumentiamo lo spessore per renderla estremamente visibile
    float thick = 0.15f; 

    auto addBar = [&](glm::vec3 pos, glm::vec3 size) {
        MeshComponent bar = MakeCube(1.0f);
        int vertexOffset = (int)m.vertices.size();
        for (auto& v : bar.vertices) {
            v.position.x = v.position.x * size.x + pos.x;
            v.position.y = v.position.y * size.y + pos.y;
            v.position.z = v.position.z * size.z + pos.z;
            v.color = {0.8f, 0.8f, 0.0f}; // Giallo acceso
            m.vertices.push_back(v);
        }
    };
    
    // Generiamo una griglia REALE (linee ogni 1 unità) alla base e in cima
    for (int y : {0, height}) {
        for (int x = 0; x <= width; ++x) {
            addBar({(float)x, (float)y, depth/2.0f}, {thick, thick, (float)depth}); // Linee asse Z
        }
        for (int z = 0; z <= depth; ++z) {
            addBar({width/2.0f, (float)y, (float)z}, {(float)width, thick, thick}); // Linee asse X
        }
    }
    
    // Pilastri verticali solo agli angoli per non ostruire la vista
    for (int x : {0, width}) {
        for (int z : {0, depth}) {
            addBar({(float)x, height/2.0f, (float)z}, {thick, (float)height, thick});
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
                    rm->GetStagingDeviceMemory(),
                    rm->GetMappedStagingData(),
                    rm->GetStagingBufferSize(),
                    rm->GetGlobalVramBuffer(),
                    rm->GetQueueMutex()
                );
            }
        }
    }

    // Registrazione per i fluidi (Event-Driven)
    EventManager::Get().Subscribe<Event_BlockUpdated>([this](const Event_BlockUpdated& e) {
        this->ProcessFluidUpdate(e.position.x, e.position.y, e.position.z);
    });
    // --- SETUP WORKSPACE FORGE ---
    // Creiamo l'unico chunk che funge da area di lavoro per il blocco (micro-voxel)
    CreateChunkEntity("WorkspaceBlock", {0.0f, 0.0f, 0.0f});
}

entt::entity ForgeWorld::CreateChunkEntity(const std::string& name, const Vec3& position) {
    auto entity = m_registry.create();
    
    m_registry.emplace<MetadataComponent>(entity, name, true, false);
    fw::TransformComponent trans;
    trans.location = position;
    trans.rotation = {0.0f, 0.0f, 0.0f};
    trans.scale = {1.0f, 1.0f, 1.0f};
    m_registry.emplace<TransformComponent>(entity, trans);
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
    chunk.cx = cx;
    chunk.cz = cz;
    
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            double worldX = cx * 16.0 + x;
            double worldZ = cz * 16.0 + z;
            
            // FBM (Fractal Brownian Motion)
            double scale = 0.02; 
            double noiseVal = m_noiseGen.octaveNoise(worldX * scale, 0.0, worldZ * scale, 4, 0.5);
            int height = 20 + (int)(noiseVal * 40.0);
            
            for (int y = 0; y < height; ++y) {
                if (y == height - 1) {
                    chunk.blocks[x][y][z] = (uint8_t)BlockType::Grass;
                } else if (y > height - 4) {
                    chunk.blocks[x][y][z] = (uint8_t)BlockType::Dirt;
                } else {
                    chunk.blocks[x][y][z] = (uint8_t)BlockType::Stone;
                }
                chunk.light[x][y][z] = 255;
            }
            
            for (int y = height; y < 128; ++y) {
                chunk.blocks[x][y][z] = (uint8_t)BlockType::Air;
                chunk.light[x][y][z] = 255; 
            }
            
            // Generazione procedurale alberi
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

bool ForgeWorld::SaveChunk(int cx, int cz) const {
    uint64_t hashKey = ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz;
    auto it = m_activeChunks.find(hashKey);
    if (it == m_activeChunks.end() || !m_registry.valid(it->second)) return false;

    const auto& chunk = m_registry.get<VoxelChunkComponent>(it->second);
    if (!chunk.isGenerated) return false;

    std::filesystem::create_directories("saves/world");
    std::string filename = "saves/world/chunk_" + std::to_string(cx) + "_" + std::to_string(cz) + ".bin";
    
    std::ofstream file(filename, std::ios::binary);
    if (!file) return false;

    // Salviamo solo i blocchi e l'illuminazione per risparmiare spazio (32KB + 32KB = 64KB per chunk)
    file.write(reinterpret_cast<const char*>(chunk.blocks), sizeof(chunk.blocks));
    file.write(reinterpret_cast<const char*>(chunk.light), sizeof(chunk.light));
    file.close();
    
    return true;
}

bool ForgeWorld::LoadChunk(int cx, int cz, VoxelChunkComponent& chunkData) const {
    std::string filename = "saves/world/chunk_" + std::to_string(cx) + "_" + std::to_string(cz) + ".bin";
    if (!std::filesystem::exists(filename)) return false;

    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;

    file.read(reinterpret_cast<char*>(chunkData.blocks), sizeof(chunkData.blocks));
    file.read(reinterpret_cast<char*>(chunkData.light), sizeof(chunkData.light));
    file.close();
    
    chunkData.cx = cx;
    chunkData.cz = cz;
    chunkData.isGenerated = true;
    
    return true;
}

void ForgeWorld::SaveAllChunks() const {
    int count = 0;
    for (auto it : m_activeChunks) {
        int cx = (int)(it.first >> 32);
        int cz = (int)(it.first & 0xFFFFFFFF);
        if (SaveChunk(cx, cz)) count++;
    }
    std::cout << "[ForgeWorld] Salvati " << count << " chunk su disco in saves/world/." << std::endl;
}

entt::entity ForgeWorld::CreatePrimitive(const std::string& name, const Vec3& position, const std::string& type) {
    auto entity = m_registry.create();
    
    m_registry.emplace<MetadataComponent>(entity, name, true, true); // Interattivo
    
    fw::TransformComponent trans;
    trans.location = position;
    trans.rotation = {0.0f, 0.0f, 0.0f};
    trans.scale = {1.0f, 1.0f, 1.0f};
    m_registry.emplace<TransformComponent>(entity, trans);
    
    m_registry.emplace<PBRMaterialComponent>(entity);
    
    if (type == "Cube") {
        m_registry.emplace<MeshComponent>(entity, MeshGenerators::MakeCube());
    } else if (type == "Sphere") {
        m_registry.emplace<MeshComponent>(entity, MeshGenerators::MakeSphere());
    }
    
    return entity;
}

void ForgeWorld::MarkChunkDirty(entt::entity chunkEntity) {
    if (m_registry.all_of<ChunkDirtyComponent>(chunkEntity)) {
        auto& dirty = m_registry.get<ChunkDirtyComponent>(chunkEntity);
        if (dirty.pendingJob) dirty.needsRebuild = true;
    } else {
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
            std::cout << "[DEBUG ProcessDeferred] Elaborazione " << m_deferredMeshes.size() << " mesh differiti dalla coda asincrona.\n";
        }
        for (auto& def : m_deferredMeshes) {
            // Aggiorna l'entità originaria del chunk invece di crearne una nuova
            if (m_registry.valid(def.targetEntity)) {
                // Sincronizza i dati procedurali generati in background SOLO se sono stati creati da zero
                if (def.isNewlyGenerated) {
                    auto& chunk = m_registry.get<VoxelChunkComponent>(def.targetEntity);
                    memcpy(chunk.blocks, def.chunkData->blocks, sizeof(chunk.blocks));
                    memcpy(chunk.light, def.chunkData->light, sizeof(chunk.light));
                    chunk.isGenerated = true; // BUG FIX CRITICO: Evita che il chunk venga rigenerato!
                }
                
                m_registry.emplace_or_replace<PBRMaterialComponent>(def.targetEntity);
                std::cout << "[DEBUG ProcessDeferred] Aggiornando entity, mesh vertices=" << def.mesh.vertices.size() << "\n";
                if (!def.mesh.vertices.empty()) {
                    if (m_registry.all_of<MeshComponent>(def.targetEntity)) {
                        auto& oldMesh = m_registry.get<MeshComponent>(def.targetEntity);
                        if (oldMesh.vramAlloc.valid && m_context && m_context->vramAllocator) {
                            m_context->vramAllocator->Free(oldMesh.vramAlloc);
                        }
                    }
                    m_registry.emplace_or_replace<MeshComponent>(def.targetEntity, std::move(def.mesh));
                }
                
                // Controlliamo se durante la generazione il chunk è stato modificato di nuovo (es. il giocatore ha spaccato un altro blocco)
                auto& dirty = m_registry.get<ChunkDirtyComponent>(def.targetEntity);
                if (dirty.needsRebuild) {
                    dirty.pendingJob = false;
                    dirty.needsRebuild = false;
                } else {
                    // Rimuoviamo il tag Dirty SOLO ORA, indicando che il chunk è completato e pronto per la fisica
                    m_registry.remove<ChunkDirtyComponent>(def.targetEntity);
                }
                // std::cout << "[ForgeWorld ECS] " << def.name << " aggiornato nell'ECS con successo!\n";
            } else {
                // Se non c'è una targetEntity valida, creiamo una nuova entità (es. Primitive asincrone)
                auto newEntity = m_registry.create();
                
                // Allocazione VRAM e DMA per mesh fisse come la Griglia (PRIMA del move!)
                if (!def.mesh.vramAlloc.valid && !def.mesh.vertices.empty() && m_context->vramAllocator && m_context->dmaManager) {
                    uint32_t meshSizeBytes = (uint32_t)(def.mesh.vertices.size() * sizeof(fw::Vertex));
                    def.mesh.vramAlloc = m_context->vramAllocator->Allocate(meshSizeBytes);
                    if (def.mesh.vramAlloc.valid) {
                        m_context->dmaManager->UploadMeshAsync(
                            def.mesh.vertices.data(), 
                            meshSizeBytes, 
                            def.mesh.vramAlloc
                        );
                    }
                }
                
                m_registry.emplace<MetadataComponent>(newEntity, def.name, true, false);
                m_registry.emplace<MeshComponent>(newEntity, std::move(def.mesh));
                
                fw::TransformComponent trans;
                trans.location = def.position;
                trans.rotation = {0.0f, 0.0f, 0.0f};
                trans.scale = {1.0f, 1.0f, 1.0f}; // Fissa il bug della scala a zero!
                m_registry.emplace<TransformComponent>(newEntity, trans);
                
                m_registry.emplace<PBRMaterialComponent>(newEntity);
            }
        }
        m_deferredMeshes.clear();
    }

    // ECS Systems: Iterazione iper-veloce O(1) cache-friendly
    // [NOTA FORGE]: Chunk Streaming infinito e Portali rimossi. 
    // La Forge opera su un'area di lavoro statica o guidata dagli strumenti, non dallo spostamento del giocatore.

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
            // Creiamo uno shared_ptr allocando l'heap vuoto, poi copiamo i dati per evitare temporanei sullo stack MSVC
            auto chunkData = std::shared_ptr<VoxelChunkComponent>(new VoxelChunkComponent());
            *chunkData = chunk;
            SharedContext* ctx = m_context;
            ForgeMaterialPalette paletteCopy = m_palette;
            
            // Sottomette il job
            m_context->jobSystem->Execute([this, entity, chunkName, chunkData, ctx, paletteCopy]() {
                bool newlyGen = false;
                // 1. CARICAMENTO O GENERAZIONE DATI
                if (!chunkData->isGenerated) {
                    if (ctx && ctx->isForgeMode) {
                        // In Forge Mode partiamo con un chunk vuoto (tutto Air)
                        // I dati sono già a 0 (Air) grazie al memset in CreateChunkEntity
                    } else {
                        // Prova a caricare da disco prima di generare
                        if (!LoadChunk(chunkData->cx, chunkData->cz, *chunkData)) {
                            GenerateChunkData(*chunkData, chunkData->cx, chunkData->cz);
                        }
                    }
                    chunkData->isGenerated = true;
                    newlyGen = true;
                }
                
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
                            
                            fw::Vec3 color = paletteCopy.materials[block].baseColor;

                            // Top (+Y)
                            if (getBlock(x, y + 1, z) == 0) {
                                float light = getLight(x, y + 1, z);
                                float ao00 = calcAO(getBlock(x-1, y+1, z), getBlock(x, y+1, z-1), getBlock(x-1, y+1, z-1));
                                float ao10 = calcAO(getBlock(x+1, y+1, z), getBlock(x, y+1, z-1), getBlock(x+1, y+1, z-1));
                                float ao11 = calcAO(getBlock(x+1, y+1, z), getBlock(x, y+1, z+1), getBlock(x+1, y+1, z+1));
                                float ao01 = calcAO(getBlock(x-1, y+1, z), getBlock(x, y+1, z+1), getBlock(x-1, y+1, z+1));
                                vertices.push_back({{px-0.5f, py+0.5f, pz-0.5f}, color, {0,0}, (float)block, {0,1,0}, ao00, light});
                                vertices.push_back({{px+0.5f, py+0.5f, pz-0.5f}, color, {1,0}, (float)block, {0,1,0}, ao10, light});
                                vertices.push_back({{px+0.5f, py+0.5f, pz+0.5f}, color, {1,1}, (float)block, {0,1,0}, ao11, light});
                                vertices.push_back({{px-0.5f, py+0.5f, pz-0.5f}, color, {0,0}, (float)block, {0,1,0}, ao00, light});
                                vertices.push_back({{px+0.5f, py+0.5f, pz+0.5f}, color, {1,1}, (float)block, {0,1,0}, ao11, light});
                                vertices.push_back({{px-0.5f, py+0.5f, pz+0.5f}, color, {0,1}, (float)block, {0,1,0}, ao01, light});
                            }
                            // Bottom (-Y)
                            if (getBlock(x, y - 1, z) == 0) {
                                float light = getLight(x, y - 1, z);
                                vertices.push_back({{px-0.5f, py-0.5f, pz+0.5f}, color, {0,1}, (float)block, {0,-1,0}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py-0.5f, pz+0.5f}, color, {1,1}, (float)block, {0,-1,0}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py-0.5f, pz-0.5f}, color, {1,0}, (float)block, {0,-1,0}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py-0.5f, pz+0.5f}, color, {0,1}, (float)block, {0,-1,0}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py-0.5f, pz-0.5f}, color, {1,0}, (float)block, {0,-1,0}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py-0.5f, pz-0.5f}, color, {0,0}, (float)block, {0,-1,0}, 1.0f, light});
                            }
                            // Left (-X)
                            if (getBlock(x - 1, y, z) == 0) {
                                float light = getLight(x - 1, y, z);
                                vertices.push_back({{px-0.5f, py-0.5f, pz-0.5f}, color, {0,0}, (float)block, {-1,0,0}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py+0.5f, pz-0.5f}, color, {0,1}, (float)block, {-1,0,0}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py+0.5f, pz+0.5f}, color, {1,1}, (float)block, {-1,0,0}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py-0.5f, pz-0.5f}, color, {0,0}, (float)block, {-1,0,0}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py+0.5f, pz+0.5f}, color, {1,1}, (float)block, {-1,0,0}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py-0.5f, pz+0.5f}, color, {1,0}, (float)block, {-1,0,0}, 1.0f, light});
                            }
                            // Right (+X)
                            if (getBlock(x + 1, y, z) == 0) {
                                float light = getLight(x + 1, y, z);
                                vertices.push_back({{px+0.5f, py-0.5f, pz+0.5f}, color, {0,0}, (float)block, {1,0,0}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py+0.5f, pz+0.5f}, color, {0,1}, (float)block, {1,0,0}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py+0.5f, pz-0.5f}, color, {1,1}, (float)block, {1,0,0}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py-0.5f, pz+0.5f}, color, {0,0}, (float)block, {1,0,0}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py+0.5f, pz-0.5f}, color, {1,1}, (float)block, {1,0,0}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py-0.5f, pz-0.5f}, color, {1,0}, (float)block, {1,0,0}, 1.0f, light});
                            }
                            // Front (+Z)
                            if (getBlock(x, y, z + 1) == 0) {
                                float light = getLight(x, y, z + 1);
                                vertices.push_back({{px-0.5f, py-0.5f, pz+0.5f}, color, {0,0}, (float)block, {0,0,1}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py-0.5f, pz+0.5f}, color, {1,0}, (float)block, {0,0,1}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py+0.5f, pz+0.5f}, color, {1,1}, (float)block, {0,0,1}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py-0.5f, pz+0.5f}, color, {0,0}, (float)block, {0,0,1}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py+0.5f, pz+0.5f}, color, {1,1}, (float)block, {0,0,1}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py+0.5f, pz+0.5f}, color, {0,1}, (float)block, {0,0,1}, 1.0f, light});
                            }
                            // Back (-Z)
                            if (getBlock(x, y, z - 1) == 0) {
                                float light = getLight(x, y, z - 1);
                                vertices.push_back({{px+0.5f, py-0.5f, pz-0.5f}, color, {0,0}, (float)block, {0,0,-1}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py-0.5f, pz-0.5f}, color, {1,0}, (float)block, {0,0,-1}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py+0.5f, pz-0.5f}, color, {1,1}, (float)block, {0,0,-1}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py-0.5f, pz-0.5f}, color, {0,0}, (float)block, {0,0,-1}, 1.0f, light});
                                vertices.push_back({{px-0.5f, py+0.5f, pz-0.5f}, color, {1,1}, (float)block, {0,0,-1}, 1.0f, light});
                                vertices.push_back({{px+0.5f, py+0.5f, pz-0.5f}, color, {0,1}, (float)block, {0,0,-1}, 1.0f, light});
                            }
                        }
                    }
                }
                
                // Se la chunk è completamente vuota (solo aria), notifichiamo comunque il main thread per sincronizzare l'array e sbloccare la fisica
                if (vertices.empty()) {
                    std::lock_guard<std::mutex> lock(m_deferredMutex);
                    m_deferredMeshes.push_back({
                        chunkName + "_Empty", 
                        {(float)chunkData->cx * 16.0f, 0.0f, (float)chunkData->cz * 16.0f}, 
                        MeshComponent{}, // mesh vuota
                        chunkData,
                        entity,
                        newlyGen
                    });
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
                    entity,
                    newlyGen
                });
            });
            
            // Niente più rimozione di ChunkDirtyComponent qui! Viene rimosso dal Main Thread dopo che il DeferredCommand è elaborato.
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
    if (y >= 128) return BlockType::Air; // Sky libero
    if (y < 0) return BlockType::OutOfBounds; // Void solido
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
    return BlockType::OutOfBounds; // Chunk non ancora caricato agisce da muro solido
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
            uint8_t oldType = chunk.blocks[lx][y][lz];
            if (oldType != static_cast<uint8_t>(type)) {
                chunk.blocks[lx][y][lz] = static_cast<uint8_t>(type);
                std::cout << "[DEBUG SetBlock] Blocco (" << x << "," << y << "," << z << ") cambiato da " << (int)oldType << " a " << (int)type << " nel chunk cx=" << cx << " cz=" << cz << "\n";
                if (m_registry.all_of<ChunkDirtyComponent>(it->second)) {
                    auto& dirty = m_registry.get<ChunkDirtyComponent>(it->second);
                    if (dirty.pendingJob) dirty.needsRebuild = true;
                } else {
                    m_registry.emplace<ChunkDirtyComponent>(it->second);
                }

                // Emette eventi di aggiornamento fisico ai blocchi adiacenti e a sé stesso
                // Usiamo QueueEvent per non sfasciare lo stack con chiamate ricorsive.
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
}

// FluidSystem: Processa gli update di adiacenza per acqua e lava
void ForgeWorld::ProcessFluidUpdate(int x, int y, int z) {
    if (y < 0 || y >= 128) return;
    BlockType b = GetBlock(x, y, z);
    
    // Solo acqua e lava "reagiscono" alla gravità/vuoto
    if (b == BlockType::Water || b == BlockType::Lava) {
        // 1. Cade in basso
        BlockType below = GetBlock(x, y - 1, z);
        if (below == BlockType::Air) {
            SetBlock(x, y - 1, z, b);
        } else if (below != BlockType::Water && below != BlockType::Lava && below != BlockType::OutOfBounds) {
            // Se c'è un solido sotto, si espande lateralmente
            if (GetBlock(x+1, y, z) == BlockType::Air) SetBlock(x+1, y, z, b);
            if (GetBlock(x-1, y, z) == BlockType::Air) SetBlock(x-1, y, z, b);
            if (GetBlock(x, y, z+1) == BlockType::Air) SetBlock(x, y, z+1, b);
            if (GetBlock(x, y, z-1) == BlockType::Air) SetBlock(x, y, z-1, b);
        }

        // Reazioni termiche (Ossidiana, Pietra)
        if (b == BlockType::Water) {
            if (GetBlock(x, y-1, z) == BlockType::Lava || 
                GetBlock(x+1, y, z) == BlockType::Lava || GetBlock(x-1, y, z) == BlockType::Lava ||
                GetBlock(x, y, z+1) == BlockType::Lava || GetBlock(x, y, z-1) == BlockType::Lava) {
                SetBlock(x, y, z, BlockType::Stone); // Acqua che tocca lava diventa pietra
            }
        } else if (b == BlockType::Lava) {
            if (GetBlock(x, y-1, z) == BlockType::Water || 
                GetBlock(x+1, y, z) == BlockType::Water || GetBlock(x-1, y, z) == BlockType::Water ||
                GetBlock(x, y, z+1) == BlockType::Water || GetBlock(x, y, z-1) == BlockType::Water) {
                SetBlock(x, y, z, BlockType::Obsidian); // Lava che tocca acqua diventa Ossidiana
            }
        }
    }
    
    // Termodinamica: Il ghiaccio fonde se adiacente a fonti di calore (Lava, Torcia)
    if (b == BlockType::Ice) {
        if (GetBlock(x, y-1, z) == BlockType::Lava || GetBlock(x, y-1, z) == BlockType::LightSource ||
            GetBlock(x+1, y, z) == BlockType::Lava || GetBlock(x+1, y, z) == BlockType::LightSource ||
            GetBlock(x-1, y, z) == BlockType::Lava || GetBlock(x-1, y, z) == BlockType::LightSource ||
            GetBlock(x, y, z+1) == BlockType::Lava || GetBlock(x, y, z+1) == BlockType::LightSource ||
            GetBlock(x, y, z-1) == BlockType::Lava || GetBlock(x, y, z-1) == BlockType::LightSource ||
            GetBlock(x, y+1, z) == BlockType::Lava || GetBlock(x, y+1, z) == BlockType::LightSource) {
            
            SetBlock(x, y, z, BlockType::Water); // Il ghiaccio diventa acqua
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
