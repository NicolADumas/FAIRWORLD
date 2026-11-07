#pragma once
#include <entt/entt.hpp>
#include "IAllocator.h"
#include "ForgeMath.h"
#include "ForgeComponents.h"
#include "PlanetComponents.h"
#include "DimensionsManager.h"
#include "PerlinNoise.h"
#include "WorldChunkManager.h"
#include "WorldStructureManager.h"
#include "MeshGenerators.h"
#include "EventManager.h"
#include <memory>
#include <string>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <glm/glm.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/hash.hpp>

enum class EditorMode {
    PLACEMENT_MODE,
    SELECTION_MODE,
    DELETE_MODE
};

struct ForgeBlock {
    int type;
    glm::vec4 color;
};

struct Ivec3Hash {
    size_t operator()(const glm::ivec3& v) const {
        return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 1) ^ (std::hash<int>()(v.z) << 2);
    }
};

namespace fw::memory {
    class FreeListAllocator;
    class PoolAllocator;
    class StackAllocator;
}

struct SharedContext;

namespace fw {

enum class PlanetarySpawnResolveStatus : uint8_t {
    Ready,
    TerrainPending,
    NoSolidSurface,
    InvalidInput
};

enum class WorldVoxelQueryStatus : uint8_t {
    SolidVoxel,
    Air,
    PassableVoxel,
    OutOfBounds,
    TerrainPending,
    InvalidInput
};

struct WorldVoxelQueryResult {
    WorldVoxelQueryStatus status = WorldVoxelQueryStatus::InvalidInput;
    BlockType block = BlockType::OutOfBounds;
    glm::ivec3 voxelCoord{0};
};

class JobSystem;

class GameWorld {
public:
    GameWorld();
    ~GameWorld();

    void Initialize(SharedContext* context);
    void InitializePhysics();
    void SetSaveDirectory(const std::string& dir) { m_chunkManager.SetSaveDirectory(dir); }
    const std::string& GetSaveDirectory() const { return m_chunkManager.GetSaveDirectory(); }

    void ClearWorld(bool saveToDisk = false);
    void CancelJobs();
    void TeardownResources();
    bool IsAcceptingMeshJobs() const { return m_meshJobsAccepting.load(); }
    void Update(float dt);
    std::shared_ptr<std::atomic<bool>> GetCancelToken() const { return m_cancelToken; }
    
#ifdef _MSC_VER
    __declspec(noinline) entt::entity CreateChunkEntity(const std::string& name, const Vec3& position);
    __declspec(noinline) entt::entity CreateChunkEntity(const std::string& name, const Vec3& position, int chunkX, int chunkZ);
#else
    __attribute__((noinline)) entt::entity CreateChunkEntity(const std::string& name, const Vec3& position);
    __attribute__((noinline)) entt::entity CreateChunkEntity(const std::string& name, const Vec3& position, int chunkX, int chunkZ);
#endif
    
    entt::entity CreatePrimitive(const std::string& name, const Vec3& position, const std::string& type);
    void UploadMeshToVram(entt::entity e);
    entt::entity CreateEmptyEntity(const std::string& name);

    BlockType GetBlockCartesian(const glm::vec3& worldPos) const;
    WorldVoxelQueryResult QueryBlockCartesian(const glm::vec3& worldPos) const;
    PlanetarySpawnResolveStatus TryResolvePlanetarySpawn(
        const glm::vec3& unitDirection, float heightOffset, float colliderRadius,
        glm::vec3& outBodyPosition, float* outSurfaceRadius = nullptr) const;
    BlockType GetBlock(int x, int y, int z) const;
    BlockType GetBlockFlat(int flatX, int y, int flatZ) const;
    void SetBlockFlat(int flatX, int y, int flatZ, BlockType type);
    void SetBlock(int x, int y, int z, BlockType type);
    bool IsChunkReady(int cx, int cz) const;

    void ProcessFluidUpdate(int x, int y, int z);
    void EnqueueDeferredMesh(const std::string& name, glm::vec3 position, fw::MeshComponent mesh, std::shared_ptr<VoxelChunkComponent> chunkData = nullptr, entt::entity targetEntity = entt::null, bool newlyGen = false);
    void DestroyEntity(entt::entity e);

    entt::registry& GetRegistry() { return m_registry; }
    const entt::registry& GetRegistry() const { return m_registry; }
    
    // Ritorna l'entità centrale del Pianeta (Singleton Entity)
    entt::entity GetPlanetEntity() const { return m_planetEntity; }
    
    void MarkTerrainDirty(entt::entity chunkEntity);
    void MarkMeshDirty(entt::entity chunkEntity);
    void MarkAllChunksDirty();
    void GenerateChunkData(VoxelChunkComponent& chunk, int cx, int cz);

    bool SaveChunk(int cx, int cz) const { return m_chunkManager.SaveChunk(cx, cz, VoxelChunkComponent()); }
    bool LoadChunk(int cx, int cz, VoxelChunkComponent& chunkData) const { return m_chunkManager.LoadChunk(cx, cz, chunkData); }
    void SaveAllChunks() const { m_chunkManager.SaveAllChunks(const_cast<entt::registry&>(m_registry)); }

    bool SaveStructure(const std::string& name, uint8_t placementMode = 0, int pivotX = 0, int pivotY = 0, int pivotZ = 0);
    bool SaveStructureJSON(const std::string& name, uint8_t placementMode = 0, int pivotX = 0, int pivotY = 0, int pivotZ = 0);
    bool LoadStructureJSON(const std::string& name) { return true; }
    entt::entity LoadStructureAsPrefab(const std::string& filepath, const fw::Vec3& position);
    bool LoadStructureAsVoxels(const std::string& filepath, int startX, int startY, int startZ);

    EditorMode GetEditorMode() const { return m_editorMode; }
    void SetEditorMode(EditorMode mode) { m_editorMode = mode; }

    void PlaceForgeBlock(const glm::ivec3& pos, const ForgeBlock& block) { m_forgeBlocks[pos] = block; }
    bool RemoveForgeBlock(const glm::ivec3& pos) { return m_forgeBlocks.erase(pos) > 0; }
    const ForgeBlock* GetForgeBlock(const glm::ivec3& pos) const {
        auto it = m_forgeBlocks.find(pos);
        return (it != m_forgeBlocks.end()) ? &it->second : nullptr;
    }

    WorldChunkManager& GetChunkManager() { return m_chunkManager; }
    WorldStructureManager& GetStructureManager() { return m_structureManager; }
    
    class BlockRegistry* GetBlockRegistry() const;
    class MaterialRegistry* GetMaterialRegistry() const;
    struct SharedContext* GetContext() const { return m_context; }
    SubscriptionId GetBlockUpdatedSubscriptionId() const { return m_blockUpdatedSubId; }

    // --- Diagnostica Memoria & Risorse CPU (MEM-06) ---
    size_t GetMasterBlockSizeBytes() const { return m_masterMemoryBlock ? (128ULL * 1024ULL * 1024ULL) : 0; }
    size_t GetPersistentUsedBytes() const;
    size_t GetChunkPoolUsedBytes() const;
    size_t GetFrameStackUsedBytes() const;
    size_t GetLiveEntityCount() const {
        const auto* storage = m_registry.storage<entt::entity>();
        return storage ? storage->free_list() : 0;
    }
    size_t GetLoadedChunkCount() const { return m_chunkManager.GetLoadedChunkCount(); }
    bool HasMasterMemoryBlock() const { return m_masterMemoryBlock != nullptr; }

private:
    entt::entity CreateChunkEntityWithCoordinates(const std::string& name, const Vec3& position, int chunkX, int chunkZ);
    void QuarantineVramAllocation(VramAllocationHandle handle, uint64_t timelineTarget);

    void InitPlanetSingleton();

    SubscriptionId m_blockUpdatedSubId = INVALID_SUBSCRIPTION_ID;
    entt::registry m_registry;
    entt::entity m_planetEntity = entt::null;
    
    SharedContext* m_context = nullptr;
    PerlinNoise m_noiseGen;

    std::shared_ptr<std::atomic<bool>> m_cancelToken;
    std::atomic<bool> m_meshJobsAccepting{true};

    WorldChunkManager m_chunkManager;
    WorldStructureManager m_structureManager;

    EditorMode m_editorMode = EditorMode::PLACEMENT_MODE;
    std::unordered_map<glm::ivec3, ForgeBlock, Ivec3Hash> m_forgeBlocks;

    std::unique_ptr<fw::memory::FreeListAllocator> m_persistentAllocator;
    std::unique_ptr<fw::memory::PoolAllocator> m_chunkPoolAllocator;
    std::unique_ptr<fw::memory::StackAllocator> m_frameAllocator;
    
    void* m_masterMemoryBlock = nullptr;
    
    struct DeferredMeshSpawn {
        enum class JobOutcome : uint8_t { NotTracked, Completed, Cancelled, Failed };
        std::string name;
        glm::vec3 position;
        MeshComponent mesh;
        std::shared_ptr<VoxelChunkComponent> chunkData = nullptr;
        entt::entity targetEntity = entt::null;
        bool isNewlyGenerated = false;
        bool trackedJob = false;
        uint64_t jobRequestId = 0;
        uint64_t sourceMeshRevision = 0;
        int sourceCx = 0;
        int sourceCz = 0;
        fw::PlanetChunkCoord sourcePlanetCoord{fw::PlanetID::Invalid(), fw::CubeFace::PositiveZ, 0, 0, 0};
        JobOutcome jobOutcome = JobOutcome::NotTracked;
        bool dmaPendingUnconfirmed = false;
        uint64_t dmaTimelineTarget = 0;
    };
    struct QuarantinedDmaAllocation {
        VramAllocationHandle handle = INVALID_VRAM_HANDLE;
        uint64_t timelineTarget = 0;
    };
    std::vector<DeferredMeshSpawn> m_deferredMeshes;
    // Keeps slab handles reserved when a submitted DMA could not be confirmed.
    // They are deliberately not recycled; allocator teardown owns final release.
    std::vector<QuarantinedDmaAllocation> m_unconfirmedDmaAllocations;
    std::mutex m_deferredMutex;
    
    std::vector<entt::entity> m_dirtyTerrainChunks;
    std::vector<entt::entity> m_dirtyMeshChunks;
};

// Retrocompatibilità completa per forward declaration di ForgeWorld
class ForgeWorld : public GameWorld {
public:
    using GameWorld::GameWorld;
};

} // namespace fw
