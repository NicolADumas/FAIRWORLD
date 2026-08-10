#include "pch.h"
#include "GameWorld.h"
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
#include "BlockRegistry.h"
#include "MaterialRegistry.h"
#include "MapWorldGenerator.h"
#include "../app/AssetManager.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include "Systems.h"
#include <cmath>
#include <vector>
#include "BiomeSystems.h"

namespace fw {

GameWorld::GameWorld() {
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
        
        std::cout << "[GameWorld] Arena di memoria di 128MB allocata e partizionata con successo.\n";
    }
}

GameWorld::~GameWorld() {
    ClearWorld(false);
    EventManager::Get().UnsubscribeAll<Event_BlockUpdated>();
    if (m_masterMemoryBlock) {
        free(m_masterMemoryBlock);
        m_masterMemoryBlock = nullptr;
    }
    std::cout << "[GameWorld] O(1) Memory Destruction. Tutta l'arena liberata.\n";
}

void GameWorld::Initialize(SharedContext* context) {
    m_context = context;
    if (m_context) {
        if (!m_context->gameWorld) {
            m_context->gameWorld = this;
        }
        m_context->forgeWorld = this;
        m_context->activeRegistry = &m_registry;

        // Registrazione per i fluidi (Event-Driven) - solo per il mondo di gioco reale
        if (!m_context->isMapBuilderMode) {
            EventManager::Get().Subscribe<Event_BlockUpdated>([this](const Event_BlockUpdated& e) {
                this->ProcessFluidUpdate(e.position.x, e.position.y, e.position.z);
            });
        }
    }
}

void GameWorld::ClearWorld(bool saveToDisk) {
    if (saveToDisk) {
        SaveAllChunks();
    }
    
    if (m_context && m_context->jobSystem) {
        m_context->jobSystem->WaitAll();
    }
    
    {
        std::lock_guard<std::mutex> lock(m_deferredMutex);
        for (auto& def : m_deferredMeshes) {
            if (def.mesh.vramAlloc.valid && m_context && m_context->vramAllocator) {
                m_context->vramAllocator->Free(def.mesh.vramAlloc);
                def.mesh.vramAlloc.valid = false;
            }
        }
        m_deferredMeshes.clear();
    }
    
    auto view = m_registry.view<MeshComponent>();
    for (auto entity : view) {
        auto& mesh = view.get<MeshComponent>(entity);
        if (mesh.vramAlloc.valid && m_context && m_context->vramAllocator) {
            m_context->vramAllocator->Free(mesh.vramAlloc);
            mesh.vramAlloc.valid = false;
        }
    }
    
    m_registry.clear();
    m_chunkManager.Clear();
    m_forgeBlocks.clear();
    
    if (m_frameAllocator) m_frameAllocator->Reset();
}

void GameWorld::Update(float dt) {
    // 1. Processa la coda delle mesh differite da Worker Threads
    std::vector<DeferredMeshSpawn> meshBatch;
    {
        std::lock_guard<std::mutex> lock(m_deferredMutex);
        meshBatch.swap(m_deferredMeshes);
    }
    
    for (auto& def : meshBatch) {
        if (def.targetEntity != entt::null && !m_registry.valid(def.targetEntity)) {
            if (def.mesh.vramAlloc.valid && m_context && m_context->vramAllocator) {
                m_context->vramAllocator->Free(def.mesh.vramAlloc);
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
            if (!def.mesh.vertices.empty()) {
                if (!def.mesh.vramAlloc.valid && m_context && m_context->vramAllocator && m_context->dmaManager) {
                    uint32_t meshSizeBytes = (uint32_t)(def.mesh.vertices.size() * sizeof(fw::Vertex));
                    def.mesh.vramAlloc = m_context->vramAllocator->Allocate(meshSizeBytes);
                    if (def.mesh.vramAlloc.valid) {
                        m_context->dmaManager->UploadMeshAsync(def.mesh.vertices.data(), meshSizeBytes, def.mesh.vramAlloc);
                    }
                }

                if (m_registry.all_of<MeshComponent>(def.targetEntity)) {
                    auto& oldMesh = m_registry.get<MeshComponent>(def.targetEntity);
                    if (oldMesh.vramAlloc.valid && m_context && m_context->vramAllocator) {
                        m_context->vramAllocator->Free(oldMesh.vramAlloc);
                    }
                }
                m_registry.emplace_or_replace<MeshComponent>(def.targetEntity, std::move(def.mesh));
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
            auto newEntity = m_registry.create();
            if (!def.mesh.vramAlloc.valid && !def.mesh.vertices.empty() && m_context && m_context->vramAllocator && m_context->dmaManager) {
                uint32_t meshSizeBytes = (uint32_t)(def.mesh.vertices.size() * sizeof(fw::Vertex));
                def.mesh.vramAlloc = m_context->vramAllocator->Allocate(meshSizeBytes);
                if (def.mesh.vramAlloc.valid) {
                    m_context->dmaManager->UploadMeshAsync(def.mesh.vertices.data(), meshSizeBytes, def.mesh.vramAlloc);
                }
            }

            m_registry.emplace<MetadataComponent>(newEntity, def.name, true, false);
            m_registry.emplace<MeshComponent>(newEntity, std::move(def.mesh));

            fw::TransformComponent trans;
            trans.location = {def.position.x, def.position.y, def.position.z};
            trans.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
            trans.scale = {1.0f, 1.0f, 1.0f};
            m_registry.emplace<TransformComponent>(newEntity, trans);
            m_registry.emplace<PBRMaterialComponent>(newEntity);
        }
    }

    // 2. Pipeline Biomi
    int maxBatch = (m_context && m_context->engine && m_context->engine->GetGameMode() == GameMode::Map) ? 250 : 15;
    fw::BiomeTerrainSystem::Update(m_registry, maxBatch, GetBlockRegistry());
    fw::BiomeDecoratorSystem::Update(m_registry, maxBatch, GetBlockRegistry());

    // 3. Chunk System: Rigenerazione asincrona per chunk Dirty
    if (m_context && m_context->jobSystem) {
        auto dirtyChunks = m_registry.view<VoxelChunkComponent, ChunkDirtyComponent>();
        int jobsDispatchedThisFrame = 0;
        int maxJobsPerFrame = 15;

        for (auto entity : dirtyChunks) {
            if (jobsDispatchedThisFrame >= maxJobsPerFrame) break;

            auto& dirty = dirtyChunks.get<ChunkDirtyComponent>(entity);
            if (dirty.pendingJob) continue;

            dirty.pendingJob = true;
            auto& chunk = dirtyChunks.get<VoxelChunkComponent>(entity);
            std::string chunkName = "Chunk_" + std::to_string(chunk.cx) + "_" + std::to_string(chunk.cz);
            auto chunkData = std::shared_ptr<VoxelChunkComponent>(new VoxelChunkComponent());
            *chunkData = chunk;
            SharedContext* ctx = m_context;

            m_context->jobSystem->Execute([this, entity, chunkName, chunkData, ctx]() {
                bool newlyGen = false;
                if (!chunkData->isGenerated) {
                    if (ctx && ctx->isForgeMode) {
                        // In Forge Mode partiamo con un chunk vuoto
                    } else {
                        if (!LoadChunk(chunkData->cx, chunkData->cz, *chunkData)) {
                            GenerateChunkData(*chunkData, chunkData->cx, chunkData->cz);
                        }
                    }
                    chunkData->isGenerated = true;
                    newlyGen = true;
                }

                std::vector<Vertex> vertices;
                vertices.reserve(16384);

                auto getBlock = [&](int x, int y, int z) -> uint8_t {
                    if (y < 0 || y >= CHUNK_HEIGHT) return 0;
                    if (x >= 0 && x < CHUNK_SIZE && z >= 0 && z < CHUNK_SIZE) {
                        return chunkData->blocks[x][y][z];
                    }
                    int wx = chunkData->cx * CHUNK_SIZE + x;
                    int wz = chunkData->cz * CHUNK_SIZE + z;
                    fw::BlockType neighborBlock = GetBlock(wx, y, wz);
                    if (neighborBlock == fw::BlockType::OutOfBounds) return 0;
                    return static_cast<uint8_t>(neighborBlock);
                };

                auto getLight = [&](int x, int y, int z) -> float {
                    if (y < 0 || y >= CHUNK_HEIGHT) return 1.0f;
                    if (x >= 0 && x < CHUNK_SIZE && z >= 0 && z < CHUNK_SIZE) {
                        return chunkData->light[x][y][z] / 255.0f;
                    }
                    return 1.0f;
                };

                auto calcAO = [&](bool side1, bool side2, bool corner) -> float {
                    if (side1 && side2) return 0.25f;
                    return 1.0f - (side1 + side2 + corner) * 0.25f;
                };

                auto shouldDrawFace = [&](uint8_t thisBlock, uint8_t neighborBlock) -> bool {
                    if (neighborBlock == 0) return true;
                    if (thisBlock == neighborBlock) return false;
                    if (neighborBlock == 6 || neighborBlock == 8 || neighborBlock == 13) return true; // Acqua, Foglie, Ghiaccio
                    return false;
                };

                for (int x = 0; x < CHUNK_SIZE; x++) {
                    for (int y = 0; y < CHUNK_HEIGHT; y++) {
                        for (int z = 0; z < CHUNK_SIZE; z++) {
                            uint8_t block = chunkData->blocks[x][y][z];
                            if (block == 0) continue;

                            float px = x; float py = y; float pz = z;
                            auto& mat = ctx->materialRegistry->GetMaterial(block);

                            fw::Vec4 color = {mat.baseColorFallback.x, mat.baseColorFallback.y, mat.baseColorFallback.z, 1.0f};
                            float rough = mat.roughnessFallback;
                            float metal = mat.metallicFallback;
                            float emissive = mat.emissiveStrength;
                            uint32_t materialID = block;

                            // Top (+Y)
                            if (shouldDrawFace(block, getBlock(x, y + 1, z))) {
                                float light = getLight(x, y + 1, z);
                                float ao00 = calcAO(getBlock(x-1, y+1, z), getBlock(x, y+1, z-1), getBlock(x-1, y+1, z-1));
                                float ao10 = calcAO(getBlock(x+1, y+1, z), getBlock(x, y+1, z-1), getBlock(x+1, y+1, z-1));
                                float ao11 = calcAO(getBlock(x+1, y+1, z), getBlock(x, y+1, z+1), getBlock(x+1, y+1, z+1));
                                float ao01 = calcAO(getBlock(x-1, y+1, z), getBlock(x, y+1, z+1), getBlock(x-1, y+1, z+1));
                                vertices.push_back({{px-0.5f, py+0.5f, pz-0.5f}, color, {rough, metal}, materialID, {0,1,0}, ao00, light, emissive});
                                vertices.push_back({{px+0.5f, py+0.5f, pz-0.5f}, color, {rough, metal}, materialID, {0,1,0}, ao10, light, emissive});
                                vertices.push_back({{px+0.5f, py+0.5f, pz+0.5f}, color, {rough, metal}, materialID, {0,1,0}, ao11, light, emissive});
                                vertices.push_back({{px-0.5f, py+0.5f, pz-0.5f}, color, {rough, metal}, materialID, {0,1,0}, ao00, light, emissive});
                                vertices.push_back({{px+0.5f, py+0.5f, pz+0.5f}, color, {rough, metal}, materialID, {0,1,0}, ao11, light, emissive});
                                vertices.push_back({{px-0.5f, py+0.5f, pz+0.5f}, color, {rough, metal}, materialID, {0,1,0}, ao01, light, emissive});
                            }
                            // Bottom (-Y)
                            if (shouldDrawFace(block, getBlock(x, y - 1, z))) {
                                float light = getLight(x, y - 1, z);
                                vertices.push_back({{px-0.5f, py-0.5f, pz+0.5f}, color, {rough, metal}, materialID, {0,-1,0}, 1.0f, light, emissive});
                                vertices.push_back({{px+0.5f, py-0.5f, pz+0.5f}, color, {rough, metal}, materialID, {0,-1,0}, 1.0f, light, emissive});
                                vertices.push_back({{px+0.5f, py-0.5f, pz-0.5f}, color, {rough, metal}, materialID, {0,-1,0}, 1.0f, light, emissive});
                                vertices.push_back({{px-0.5f, py-0.5f, pz+0.5f}, color, {rough, metal}, materialID, {0,-1,0}, 1.0f, light, emissive});
                                vertices.push_back({{px+0.5f, py-0.5f, pz-0.5f}, color, {rough, metal}, materialID, {0,-1,0}, 1.0f, light, emissive});
                                vertices.push_back({{px-0.5f, py-0.5f, pz-0.5f}, color, {rough, metal}, materialID, {0,-1,0}, 1.0f, light, emissive});
                            }
                            // Left (-X)
                            if (shouldDrawFace(block, getBlock(x - 1, y, z))) {
                                float light = getLight(x - 1, y, z);
                                vertices.push_back({{px-0.5f, py-0.5f, pz-0.5f}, color, {rough, metal}, materialID, {-1,0,0}, 1.0f, light, emissive});
                                vertices.push_back({{px-0.5f, py+0.5f, pz-0.5f}, color, {rough, metal}, materialID, {-1,0,0}, 1.0f, light, emissive});
                                vertices.push_back({{px-0.5f, py+0.5f, pz+0.5f}, color, {rough, metal}, materialID, {-1,0,0}, 1.0f, light, emissive});
                                vertices.push_back({{px-0.5f, py-0.5f, pz-0.5f}, color, {rough, metal}, materialID, {-1,0,0}, 1.0f, light, emissive});
                                vertices.push_back({{px-0.5f, py+0.5f, pz+0.5f}, color, {rough, metal}, materialID, {-1,0,0}, 1.0f, light, emissive});
                                vertices.push_back({{px-0.5f, py-0.5f, pz+0.5f}, color, {rough, metal}, materialID, {-1,0,0}, 1.0f, light, emissive});
                            }
                            // Right (+X)
                            if (shouldDrawFace(block, getBlock(x + 1, y, z))) {
                                float light = getLight(x + 1, y, z);
                                vertices.push_back({{px+0.5f, py-0.5f, pz+0.5f}, color, {rough, metal}, materialID, {1,0,0}, 1.0f, light, emissive});
                                vertices.push_back({{px+0.5f, py+0.5f, pz+0.5f}, color, {rough, metal}, materialID, {1,0,0}, 1.0f, light, emissive});
                                vertices.push_back({{px+0.5f, py+0.5f, pz-0.5f}, color, {rough, metal}, materialID, {1,0,0}, 1.0f, light, emissive});
                                vertices.push_back({{px+0.5f, py-0.5f, pz+0.5f}, color, {rough, metal}, materialID, {1,0,0}, 1.0f, light, emissive});
                                vertices.push_back({{px+0.5f, py+0.5f, pz-0.5f}, color, {rough, metal}, materialID, {1,0,0}, 1.0f, light, emissive});
                                vertices.push_back({{px+0.5f, py-0.5f, pz-0.5f}, color, {rough, metal}, materialID, {1,0,0}, 1.0f, light, emissive});
                            }
                            // Front (+Z)
                            if (shouldDrawFace(block, getBlock(x, y, z + 1))) {
                                float light = getLight(x, y, z + 1);
                                vertices.push_back({{px-0.5f, py-0.5f, pz+0.5f}, color, {rough, metal}, materialID, {0,0,1}, 1.0f, light, emissive});
                                vertices.push_back({{px+0.5f, py-0.5f, pz+0.5f}, color, {rough, metal}, materialID, {0,0,1}, 1.0f, light, emissive});
                                vertices.push_back({{px+0.5f, py+0.5f, pz+0.5f}, color, {rough, metal}, materialID, {0,0,1}, 1.0f, light, emissive});
                                vertices.push_back({{px-0.5f, py-0.5f, pz+0.5f}, color, {rough, metal}, materialID, {0,0,1}, 1.0f, light, emissive});
                                vertices.push_back({{px+0.5f, py+0.5f, pz+0.5f}, color, {rough, metal}, materialID, {0,0,1}, 1.0f, light, emissive});
                                vertices.push_back({{px-0.5f, py+0.5f, pz+0.5f}, color, {rough, metal}, materialID, {0,0,1}, 1.0f, light, emissive});
                            }
                            // Back (-Z)
                            if (shouldDrawFace(block, getBlock(x, y, z - 1))) {
                                float light = getLight(x, y, z - 1);
                                vertices.push_back({{px+0.5f, py-0.5f, pz-0.5f}, color, {rough, metal}, materialID, {0,0,-1}, 1.0f, light, emissive});
                                vertices.push_back({{px-0.5f, py-0.5f, pz-0.5f}, color, {rough, metal}, materialID, {0,0,-1}, 1.0f, light, emissive});
                                vertices.push_back({{px-0.5f, py+0.5f, pz-0.5f}, color, {rough, metal}, materialID, {0,0,-1}, 1.0f, light, emissive});
                                vertices.push_back({{px+0.5f, py-0.5f, pz-0.5f}, color, {rough, metal}, materialID, {0,0,-1}, 1.0f, light, emissive});
                                vertices.push_back({{px-0.5f, py+0.5f, pz-0.5f}, color, {rough, metal}, materialID, {0,0,-1}, 1.0f, light, emissive});
                                vertices.push_back({{px+0.5f, py+0.5f, pz-0.5f}, color, {rough, metal}, materialID, {0,0,-1}, 1.0f, light, emissive});
                            }
                        }
                    }
                }

                if (vertices.empty()) {
                    std::lock_guard<std::mutex> lock(m_deferredMutex);
                    m_deferredMeshes.push_back({
                        chunkName + "_Empty", 
                        {(float)chunkData->cx * 16.0f, 0.0f, (float)chunkData->cz * 16.0f}, 
                        MeshComponent{},
                        chunkData,
                        entity,
                        newlyGen
                    });
                    return;
                }

                uint32_t meshSizeBytes = (uint32_t)(vertices.size() * sizeof(Vertex));
                auto vramAlloc = ctx->vramAllocator->Allocate(meshSizeBytes);
                if (!vramAlloc.valid) return;

                ctx->dmaManager->UploadMeshAsync(vertices.data(), meshSizeBytes, vramAlloc);

                MeshComponent newMesh;
                newMesh.name = chunkName + "_Mesh";
                newMesh.type = MeshType::Chunk;
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

            jobsDispatchedThisFrame++;
        }
    }

    // 4. Reset frame allocator
    if (m_frameAllocator) {
        m_frameAllocator->Reset();
    }
}

entt::entity GameWorld::CreateChunkEntity(const std::string& name, const Vec3& position) {
    auto entity = m_registry.create();
    
    m_registry.emplace<MetadataComponent>(entity, name, true, false);
    fw::TransformComponent trans;
    trans.location = position;
    trans.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    trans.scale = {1.0f, 1.0f, 1.0f};
    m_registry.emplace<TransformComponent>(entity, trans);
    
    int cx = (int)position.x / 16;
    int cz = (int)position.z / 16;
    m_registry.emplace<VoxelChunkComponent>(entity, cx, cz);
    
    auto& chunk = m_registry.get<VoxelChunkComponent>(entity);
    memset(chunk.blocks, 0, sizeof(chunk.blocks));
    memset(chunk.light, 255, sizeof(chunk.light));

    if (m_context && m_context->isForgeMode) {
        chunk.isGenerated = true;
    }
    
    m_chunkManager.RegisterChunkEntity(cx, cz, entity);
    MarkChunkDirty(entity);
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
    }
    
    return entity;
}

entt::entity GameWorld::CreateEmptyEntity(const std::string& name) {
    auto entity = m_registry.create();
    m_registry.emplace<MetadataComponent>(entity, name, true, false);
    return entity;
}

BlockType GameWorld::GetBlock(int x, int y, int z) const {
    if (y < 0 || y >= 128) return BlockType::OutOfBounds;
    int cx = x >= 0 ? x / 16 : (x - 15) / 16;
    int cz = z >= 0 ? z / 16 : (z - 15) / 16;
    int lx = x - (cx * 16);
    int lz = z - (cz * 16);
    
    entt::entity chunkEnt = m_chunkManager.GetChunkEntity(cx, cz);
    if (chunkEnt != entt::null && m_registry.valid(chunkEnt)) {
        const auto& chunk = m_registry.get<VoxelChunkComponent>(chunkEnt);
        return static_cast<BlockType>(chunk.blocks[lx][y][lz]);
    }
    return BlockType::OutOfBounds;
}

void GameWorld::SetBlock(int x, int y, int z, BlockType type) {
    if (y < 0 || y >= 128) return;
    int cx = x >= 0 ? x / 16 : (x - 15) / 16;
    int cz = z >= 0 ? z / 16 : (z - 15) / 16;
    int lx = x - (cx * 16);
    int lz = z - (cz * 16);

    entt::entity chunkEnt = m_chunkManager.GetChunkEntity(cx, cz);
    if (chunkEnt == entt::null || !m_registry.valid(chunkEnt)) {
        chunkEnt = const_cast<GameWorld*>(this)->CreateChunkEntity("Chunk_" + std::to_string(cx) + "_" + std::to_string(cz), {(float)cx * 16.0f, 0.0f, (float)cz * 16.0f});
    }

    if (m_registry.valid(chunkEnt)) {
        auto& chunk = m_registry.get<VoxelChunkComponent>(chunkEnt);
        uint8_t oldType = chunk.blocks[lx][y][lz];
        if (oldType != static_cast<uint8_t>(type)) {
            chunk.blocks[lx][y][lz] = static_cast<uint8_t>(type);
            MarkChunkDirty(chunkEnt);

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
            if (mesh->vramAlloc.valid && m_context && m_context->vramAllocator) {
                m_context->vramAllocator->Free(mesh->vramAlloc);
            }
        }
        m_registry.destroy(e);
    }
}

void GameWorld::MarkChunkDirty(entt::entity chunkEntity) {
    if (m_registry.valid(chunkEntity)) {
        if (m_registry.all_of<ChunkDirtyComponent>(chunkEntity)) {
            auto& dirty = m_registry.get<ChunkDirtyComponent>(chunkEntity);
            if (dirty.pendingJob) dirty.needsRebuild = true;
        } else {
            m_registry.emplace<ChunkDirtyComponent>(chunkEntity);
        }
    }
}

void GameWorld::MarkAllChunksDirty() {
    auto view = m_registry.view<VoxelChunkComponent>();
    for (auto entity : view) {
        MarkChunkDirty(entity);
    }
}

void GameWorld::GenerateChunkData(VoxelChunkComponent& chunk, int cx, int cz) {
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
