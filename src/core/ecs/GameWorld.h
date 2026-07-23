#pragma once
#include <entt/entt.hpp>
#include "IAllocator.h"
#include "ForgeMath.h"
#include "ForgeComponents.h"
#include "DimensionsManager.h"
#include "PerlinNoise.h"
#include "WorldChunkManager.h"
#include "WorldStructureManager.h"
#include "MeshGenerators.h"
#include <memory>
#include <string>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
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

class JobSystem;

class GameWorld {
public:
    GameWorld();
    ~GameWorld();

    void Initialize(SharedContext* context);
    void SetSaveDirectory(const std::string& dir) { m_chunkManager.SetSaveDirectory(dir); }
    const std::string& GetSaveDirectory() const { return m_chunkManager.GetSaveDirectory(); }

    void ClearWorld(bool saveToDisk = false);
    void Update(float dt);
    
#ifdef _MSC_VER
    __declspec(noinline) entt::entity CreateChunkEntity(const std::string& name, const Vec3& position);
#else
    __attribute__((noinline)) entt::entity CreateChunkEntity(const std::string& name, const Vec3& position);
#endif
    
    entt::entity CreatePrimitive(const std::string& name, const Vec3& position, const std::string& type);
    entt::entity CreateEmptyEntity(const std::string& name);

    BlockType GetBlock(int x, int y, int z) const;
    void SetBlock(int x, int y, int z, BlockType type);
    bool IsChunkReady(int cx, int cz) const;

    void ProcessFluidUpdate(int x, int y, int z);
    void EnqueueDeferredMesh(const std::string& name, glm::vec3 position, fw::MeshComponent mesh, std::shared_ptr<VoxelChunkComponent> chunkData = nullptr, entt::entity targetEntity = entt::null, bool newlyGen = false);
    void DestroyEntity(entt::entity e);

    entt::registry& GetRegistry() { return m_registry; }
    void MarkChunkDirty(entt::entity chunkEntity);
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

private:
    entt::registry m_registry;
    SharedContext* m_context = nullptr;
    PerlinNoise m_noiseGen;

    WorldChunkManager m_chunkManager;
    WorldStructureManager m_structureManager;

    EditorMode m_editorMode = EditorMode::PLACEMENT_MODE;
    std::unordered_map<glm::ivec3, ForgeBlock, Ivec3Hash> m_forgeBlocks;

    std::unique_ptr<fw::memory::FreeListAllocator> m_persistentAllocator;
    std::unique_ptr<fw::memory::PoolAllocator> m_chunkPoolAllocator;
    std::unique_ptr<fw::memory::StackAllocator> m_frameAllocator;
    
    void* m_masterMemoryBlock = nullptr;
    
    struct DeferredMeshSpawn {
        std::string name;
        glm::vec3 position;
        MeshComponent mesh;
        std::shared_ptr<VoxelChunkComponent> chunkData = nullptr;
        entt::entity targetEntity = entt::null;
        bool isNewlyGenerated = false;
    };
    std::vector<DeferredMeshSpawn> m_deferredMeshes;
    std::mutex m_deferredMutex;
};

// Retrocompatibilità completa per forward declaration di ForgeWorld
class ForgeWorld : public GameWorld {
public:
    using GameWorld::GameWorld;
};

} // namespace fw
