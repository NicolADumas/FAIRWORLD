#pragma once
#include <entt/entt.hpp>
#include "IAllocator.h"
#include "ForgeMath.h"
#include "ForgeComponents.h"
#include "PerlinNoise.h"
#include <memory>
#include <string>
#include <mutex>
#include <vector>
#include <unordered_map>

// Forward declarations per gli allocatori (già esistenti nel progetto)
namespace fw::memory {
    class FreeListAllocator;
    class PoolAllocator;
    class StackAllocator;
}

struct SharedContext;

namespace fw {

class JobSystem;

class ForgeWorld {
public:
    ForgeWorld();
    ~ForgeWorld();

    // Inizializza il mondo della FORGE allocando la memoria dedicata e passandogli l'infrastruttura asincrona
    void Initialize(SharedContext* context);
    
    // Aggiornamento principale della FORGE (ECS Systems & Jobs)
    void Update(float dt);
    
    // Genera un blocco "Chunk" allocandolo tramite il PoolAllocator
    entt::entity CreateChunkEntity(const std::string& name, const Vec3& position);
    
    // Generatore utility convertito in entity
    entt::entity CreatePrimitive(const std::string& name, const Vec3& position, const std::string& type);

    // Accesso globale ai blocchi
    BlockType GetBlock(int x, int y, int z) const;
    void SetBlock(int x, int y, int z, BlockType type);
    bool IsChunkReady(int cx, int cz) const;

    // Accoda la creazione di una mesh da un thread separato (Thread-safe)
    void EnqueueDeferredMesh(const std::string& name, const Vec3& position, MeshComponent&& mesh);

    // Ritorna il registro EnTT per interrogarlo
    entt::registry& GetRegistry() { return m_registry; }

    // Forza un chunk ad essere ricostruito asincronamente
    void MarkChunkDirty(entt::entity chunkEntity);

    // Genera i dati di un chunk usando PerlinNoise
    void GenerateChunkData(VoxelChunkComponent& chunk, int cx, int cz);

private:
    entt::registry m_registry;
    SharedContext* m_context = nullptr;
    PerlinNoise m_noiseGen;

    // Mappa hash dei chunk attivi: uint64_t(cx, cz) -> entity
    std::unordered_map<uint64_t, entt::entity> m_activeChunks;

    // --- TIER MEMORY SYSTEM ---
    // 1. Persistent Memory
    std::unique_ptr<fw::memory::FreeListAllocator> m_persistentAllocator;
    // 2. Component/Chunk Pool Memory
    std::unique_ptr<fw::memory::PoolAllocator> m_chunkPoolAllocator;
    // 3. Transient Frame Memory (azzerata a ogni frame)
    std::unique_ptr<fw::memory::StackAllocator> m_frameAllocator;
    
    void* m_masterMemoryBlock = nullptr;
    
    // -- DEFERRED COMMANDS (Thread-Safe) --
    struct DeferredMeshSpawn {
        std::string name;
        Vec3 position;
        MeshComponent mesh;
        std::shared_ptr<VoxelChunkComponent> chunkData;
        entt::entity targetEntity;
    };
    std::vector<DeferredMeshSpawn> m_deferredMeshes;
    std::mutex m_deferredMutex;
};

// Funzioni generatrici statiche per le primitive
class MeshGenerators {
public:
    static MeshComponent MakeCube(float size = 1.0f);
    static MeshComponent MakeSphere(int segs = 16, int rings = 8, float r = 1.0f);
};

} // namespace fw
