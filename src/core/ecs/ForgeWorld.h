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
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

// Gestione Stato Editor FORGE
enum class EditorMode {
    PLACEMENT_MODE,
    SELECTION_MODE,
    DELETE_MODE
};

// Struttura blocco isolata per FORGE
struct ForgeBlock {
    int type;
    glm::vec4 color;
};

// Funzione Hash per glm::ivec3 nel caso in cui gtx/hash non sia sufficiente
struct Ivec3Hash {
    size_t operator()(const glm::ivec3& v) const {
        return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 1) ^ (std::hash<int>()(v.z) << 2);
    }
};

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
    
    // Imposta la directory di salvataggio ("saves/world" per PlayState, "saves/forge" per ForgeState)
    void SetSaveDirectory(const std::string& dir) { m_saveDir = dir; }
    const std::string& GetSaveDirectory() const { return m_saveDir; }

    // Pulisce tutti i chunk attuali per preparare un nuovo stato
    void ClearWorld(bool saveToDisk = false);

    // Aggiornamento principale della FORGE (ECS Systems & Jobs)
    void Update(float dt);
    
    // Genera un blocco "Chunk" allocandolo tramite il PoolAllocator
#ifdef _MSC_VER
    __declspec(noinline) entt::entity CreateChunkEntity(const std::string& name, const Vec3& position);
#else
    __attribute__((noinline)) entt::entity CreateChunkEntity(const std::string& name, const Vec3& position);
#endif
    
    // Generatore utility convertito in entity
    entt::entity CreatePrimitive(const std::string& name, const Vec3& position, const std::string& type);
    
    // Crea un'entità vuota pre-registrata (utile per promesse di caricamento asincrono)
    entt::entity CreateEmptyEntity(const std::string& name);

    // Accesso globale ai blocchi
    BlockType GetBlock(int x, int y, int z) const;
    void SetBlock(int x, int y, int z, BlockType type);
    bool IsChunkReady(int cx, int cz) const;

    // FluidSystem event-driven
    void ProcessFluidUpdate(int x, int y, int z);

    // Aggiunge asincronamente un mesh chunk per il rendering differito
    void EnqueueDeferredMesh(const std::string& name, glm::vec3 position, fw::MeshComponent mesh, std::shared_ptr<VoxelChunkComponent> chunkData = nullptr, entt::entity targetEntity = entt::null, bool newlyGen = false);
    
    // Distrugge in modo sicuro un'entità, liberando la VRAM se contiene una MeshComponent
    void DestroyEntity(entt::entity e);

    // Ritorna il registro EnTT per interrogarlo
    entt::registry& GetRegistry() { return m_registry; }
    
    // Accesso alla Palette Materiali della Forge (256 slot)
    ForgeMaterialPalette& GetPalette() { return m_palette; }
    const ForgeMaterialPalette& GetPalette() const { return m_palette; }

    // Forza un chunk ad essere ricostruito asincronamente
    void MarkChunkDirty(entt::entity chunkEntity);

    // Genera i dati di un chunk usando PerlinNoise
    void GenerateChunkData(VoxelChunkComponent& chunk, int cx, int cz);

    // Salvataggio/Caricamento Chunks
    bool SaveChunk(int cx, int cz) const;
    bool LoadChunk(int cx, int cz, VoxelChunkComponent& chunkData) const;
    // Salvataggio di tutta l'area in memoria su disco
    void SaveAllChunks() const;

    // --- STRUTTURE (FORMATO .FWBLOCK) ---
    bool SaveStructure(const std::string& name, uint8_t placementMode = 0, int pivotX = 0, int pivotY = 0, int pivotZ = 0);
    entt::entity LoadStructureAsPrefab(const std::string& filepath, const fw::Vec3& position);
    bool LoadStructureAsVoxels(const std::string& filepath, int startX, int startY, int startZ);

    // --- STRUTTURE (FORMATO JSON) ---
    bool SaveStructureJSON(const std::string& name, uint8_t placementMode = 0, int pivotX = 0, int pivotY = 0, int pivotZ = 0);
    bool LoadStructureJSON(const std::string& name);

    // --- EDITOR STATE MACHINE ---
    EditorMode GetEditorMode() const { return m_editorMode; }
    void SetEditorMode(EditorMode mode) { m_editorMode = mode; }

    // --- DATI SPAZIALI ISOLATI FORGE ---
    void PlaceForgeBlock(const glm::ivec3& pos, const ForgeBlock& block) {
        m_forgeBlocks[pos] = block;
    }
    bool RemoveForgeBlock(const glm::ivec3& pos) {
        return m_forgeBlocks.erase(pos) > 0;
    }
    const ForgeBlock* GetForgeBlock(const glm::ivec3& pos) const {
        auto it = m_forgeBlocks.find(pos);
        if (it != m_forgeBlocks.end()) return &it->second;
        return nullptr;
    }

private:
    entt::registry m_registry;
    SharedContext* m_context = nullptr;
    PerlinNoise m_noiseGen;
    ForgeMaterialPalette m_palette;

    // Mappa hash dei chunk attivi: uint64_t(cx, cz) -> entity
    std::unordered_map<uint64_t, entt::entity> m_activeChunks;

    // Architettura Dati Isolati FORGE
    EditorMode m_editorMode = EditorMode::PLACEMENT_MODE;
    std::unordered_map<glm::ivec3, ForgeBlock, Ivec3Hash> m_forgeBlocks;

    // --- TIER MEMORY SYSTEM ---
    // 1. Persistent Memory
    std::unique_ptr<fw::memory::FreeListAllocator> m_persistentAllocator;
    // 2. Component/Chunk Pool Memory
    std::unique_ptr<fw::memory::PoolAllocator> m_chunkPoolAllocator;
    // 3. Transient Frame Memory (azzerata a ogni frame)
    std::unique_ptr<fw::memory::StackAllocator> m_frameAllocator;
    
    void* m_masterMemoryBlock = nullptr;
    std::string m_saveDir = "saves/world"; // Directory di salvataggio (cambia per Forge vs Play)
    
    // -- DEFERRED COMMANDS (Thread-Safe) --
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

// Funzioni generatrici statiche per le primitive
class MeshGenerators {
public:
    static MeshComponent MakeCube(float size = 1.0f);
    static MeshComponent MakeVoxelPreview(int colorIndex, const ForgeMaterialPalette& palette);
    static MeshComponent MakeSphere(int segs = 16, int rings = 8, float r = 1.0f);
    static MeshComponent MakeGridBox(int width, int height, int depth, float thickness = 0.05f);
};

} // namespace fw
