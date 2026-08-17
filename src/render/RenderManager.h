#pragma once
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include <vector>
#include <map>
#include <optional>
#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include "vulkan/VulkanCore.h"
#include "vulkan/VulkanMemory.h"
#include "World.h"  // Per la struct Vertex
#include "TexturePacker.h"
#include "apps/BlockMakerRenderer.h"
#include "apps/MapRenderer.h"
#include "apps/ForgeRenderer.h"
#include "apps/PlayRenderer.h"
#include "apps/PhysicsLabRenderer.h"
#include "apps/ChunkEditorRenderer.h"
#include "apps/PlanetMapperRenderer.h"
#include "apps/SolarSystemRenderer.h"
#include "vulkan/TerrainPipelineSystem.h"

class XrManager;
struct BlockDef;

namespace fw {
    class ForgeWorld;
}

struct VulkanTextureArray {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    int layerCount = 0;
    
    // Per pulizia post-esecuzione
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
};



struct ForgePushConstantData {
    glm::mat4 mvp;
    glm::vec4 colorOverride;
    int useColorOverride;
    float seasonProgress;
    uint32_t grid_width;        // NEW
    uint32_t debug_lens_active; // NEW
    glm::vec4 lightDir;
    glm::vec4 cameraPos;
};

class RenderManager {
public:
    RenderManager();
    ~RenderManager();

    // Aggiornata per ricevere l'handle della finestra (HWND) e l'istanza (HINSTANCE)
    bool Init(bool isVRMode, XrManager* xrManager, void* hwnd, void* hinstance);
    void RenderStereo(XrManager* xrManager);
    void Shutdown();

    void InitImGui(void* hwnd);
    void UpdateTextureLayerSolidColor(VkImage image, uint32_t layerIndex, uint32_t width, uint32_t height, const glm::vec4& color);
    void UpdateMaterialFallback(uint32_t layerIndex, const glm::vec3& baseColor, float roughness, float metallic);

    VkInstance GetVulkanInstance() const { return m_core ? m_core->GetInstance() : VK_NULL_HANDLE; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_core ? m_core->GetPhysicalDevice() : VK_NULL_HANDLE; }
    VkDevice GetDevice() const { return m_core ? m_core->GetDevice() : VK_NULL_HANDLE; }
    VkQueue GetTransferQueue() const { return m_core ? m_core->GetTransferQueue() : VK_NULL_HANDLE; }
    VkCommandPool GetTransferCommandPool() const { return m_transferCommandPool; }
    VkBuffer GetStagingRingBuffer() const { return m_memory ? m_memory->GetStagingRingBuffer() : VK_NULL_HANDLE; }
    VkDeviceMemory GetStagingDeviceMemory() const;
    void* GetMappedStagingData() const { return m_memory ? m_memory->GetMappedStagingData() : nullptr; }
    uint64_t GetStagingBufferSize() const { return STAGING_BUFFER_SIZE; }
    VkBuffer GetGlobalVramBuffer() const { return m_memory ? m_memory->GetGlobalVramBuffer() : VK_NULL_HANDLE; }
    std::mutex* GetQueueMutex() { return m_core ? m_core->GetQueueMutex() : nullptr; }

    // Notifica che la finestra è stata ridimensionata: ricrea la Swapchain
    // Ignorato se l'init non è ancora completato
    void NotifyResize() { if (m_isFullyInitialized) RecreateSwapchain(); }

private:
    std::unique_ptr<fw::VulkanCore> m_core;
    std::unique_ptr<fw::VulkanMemory> m_memory;

    
    bool m_isVRMode;
    bool m_isFullyInitialized{ false }; // Protegge RecreateSwapchain durante l'init
    void* m_hwnd{ nullptr };
    
    

    // --- NUOVI COMPONENTI FASE 3 ---
    
    
    
    
     // Coda asincrona per i Voxel
    
    // --- VMA ---
    
    
    
    
    
    
    
    
    

    // --- NUOVI COMPONENTI (FASE 3 - ULTIMA PARTE) ---
    VkRenderPass m_renderPass{ VK_NULL_HANDLE };
    std::vector<VkFramebuffer> m_framebuffers;
    
    

    // --- NUOVI COMPONENTI (FASE 4) ---

    uint32_t m_currentFrame = 0;

    // Struttura che rispecchia esattamente quella nello shader
    struct UniformBufferObject {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec4 globalLightDir; // w serve per l'allineamento o possiamo usare isBlockMakerMode
        float seasonProgress;
        int debugColorMode; // 0=Normale, 1=Inverti, 2=Swap RB
        int isBlockMakerMode;
        float padding;
    };

    // --- COMPONENTI FASE 5 (UBO & Descriptors) ---
    VkDescriptorSetLayout m_descriptorSetLayout{ VK_NULL_HANDLE };
    
    

    
    
     // Puntatori per scrivere direttamente nella RAM

    VkPipelineLayout m_pipelineLayout{ VK_NULL_HANDLE };
    VkPipeline m_graphicsPipeline{ VK_NULL_HANDLE };

    // --- SKY PIPELINE ---
    VkPipelineLayout m_skyPipelineLayout{ VK_NULL_HANDLE };
    VkPipeline m_skyPipeline{ VK_NULL_HANDLE };

    VkPipeline m_portalPipeline{ VK_NULL_HANDLE };      // Pipeline per scrivere nello Stencil Buffer
    VkPipeline m_otherWorldPipeline{ VK_NULL_HANDLE };  // Pipeline per disegnare dove Stencil == 1

    // --- FORGE PIPELINE ---
    VkPipelineLayout m_forgePipelineLayout{ VK_NULL_HANDLE };
    VkPipeline m_forgePipeline{ VK_NULL_HANDLE };

    // --- RENDERERS ---
    std::unique_ptr<fw::BlockMakerRenderer> m_blockMakerRenderer;
    std::unique_ptr<fw::MapRenderer> m_mapRenderer;
    std::unique_ptr<fw::ForgeRenderer> m_forgeRenderer;
    std::unique_ptr<fw::PlayRenderer> m_playRenderer;
    std::unique_ptr<fw::PhysicsLabRenderer> m_physicsLabRenderer;
    std::unique_ptr<fw::ChunkEditorRenderer> m_chunkEditorRenderer;
    std::unique_ptr<fw::PlanetMapperRenderer> m_planetMapperRenderer;
    std::unique_ptr<fw::SolarSystemRenderer> m_solarSystemRenderer;
    std::unique_ptr<TerrainPipelineSystem> m_terrainPipeline;

    // --- FORGE DESCRIPTOR SETS ---
    VkDescriptorSetLayout m_forgeDescriptorSetLayout{ VK_NULL_HANDLE };
    
    

    // --- PBR TEXTURE ARRAYS ---
    VkImage m_albedoImage{ VK_NULL_HANDLE };
    VmaAllocation m_albedoImageAllocation{ VK_NULL_HANDLE };
    VkImageView m_albedoImageView{ VK_NULL_HANDLE };

    VkImage m_normalImage{ VK_NULL_HANDLE };
    VmaAllocation m_normalImageAllocation{ VK_NULL_HANDLE };
    VkImageView m_normalImageView{ VK_NULL_HANDLE };

    VkImage m_ormImage{ VK_NULL_HANDLE };
    VmaAllocation m_ormImageAllocation{ VK_NULL_HANDLE };
    VkImageView m_ormImageView{ VK_NULL_HANDLE };

    VkSampler m_textureSampler{ VK_NULL_HANDLE };

    // --- DEPTH BUFFER (risolve il problema delle facce trasparenti) ---
    VkImage m_depthImage{ VK_NULL_HANDLE };
    VmaAllocation m_depthImageAllocation{ VK_NULL_HANDLE };
    VkImageView m_depthImageView{ VK_NULL_HANDLE };

    bool CreateDepthResources();
    VkFormat FindDepthFormat();
    VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer);
    void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layerCount);
    void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount);
    void CreateImage(uint32_t width, uint32_t height, uint32_t layerCount, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VmaMemoryUsage vmaUsage, VkImage& image, VmaAllocation& imageAllocation);
    
    // --- FASE 9: CREAZIONE TEXTURE ARRAY DA MEGA-BUFFER CPU ---
    VulkanTextureArray CreateTextureArray(
        VkDevice device, 
        VmaAllocator allocator, 
        VkCommandBuffer cmdBuffer, 
        const std::vector<uint8_t>& pixelData, 
        uint32_t width, 
        uint32_t height, 
        uint32_t layerCount, 
        VkFormat format
    );
    
    float m_fov = 45.0f;

    void CleanupSwapchain();
    void RecreateSwapchain();
    void DefragmentVRAM();
    
public:
    float GetFov() const { return m_fov; }
    void SetFov(float fov) { m_fov = fov; }
    uint32_t GetWindowWidth() const { return m_core ? m_core->GetSwapchainExtent().width : 0; }
    uint32_t GetWindowHeight() const { return m_core ? m_core->GetSwapchainExtent().height : 0; }

    // Chiamata dall'Editor per aggiornare la texture in real-time
    enum class PBRTextureType {
        ALBEDO,
        NORMAL,
        ORM
    };

    void UpdateTextureLayer(uint32_t layerIndex, const void* pixelData, uint32_t width, uint32_t height, PBRTextureType type);
    void LoadBlockTextures(const std::string& baseDir, const std::vector<BlockDef>& blocks);
    bool LoadPBRTextureFromFile(const std::string& filePath, uint32_t layerIndex, PBRTextureType type);

    // --- CHUNK BUFFERS (Mappa RPG Veloce) ---
    struct VulkanChunkBuffer {
        VkBuffer vertexBuffer{ VK_NULL_HANDLE };
        VmaAllocation vertexBufferAllocation{ VK_NULL_HANDLE };
        VkBuffer indexBuffer{ VK_NULL_HANDLE };
        VmaAllocation indexBufferAllocation{ VK_NULL_HANDLE };
        uint32_t indexCount{ 0 };
    };
    std::unordered_map<ChunkCoord, VulkanChunkBuffer, ChunkHash> m_chunkBuffers;

    // --- VERTEX e INDEX BUFFER (ologrammi AI) ---
    VkBuffer m_ghostVertexBuffer{ VK_NULL_HANDLE };
    VmaAllocation m_ghostVertexBufferAllocation{ VK_NULL_HANDLE };
    VkBuffer m_ghostIndexBuffer{ VK_NULL_HANDLE };
    VmaAllocation m_ghostIndexBufferAllocation{ VK_NULL_HANDLE };
    uint32_t m_ghostIndexCount = 0;

    // --- MOB MESH (Dynamic Registry) ---
    struct VoxelMesh {
        VkBuffer vertexBuffer{ VK_NULL_HANDLE };
        VmaAllocation vertexBufferAllocation{ VK_NULL_HANDLE };
        VkBuffer indexBuffer{ VK_NULL_HANDLE };
        VmaAllocation indexBufferAllocation{ VK_NULL_HANDLE };
        uint32_t indexCount{ 0 };
    };
    std::map<std::string, VoxelMesh> m_mobMeshes;
    
    void LoadAllMobMeshes(class AssetManager& assets);
    void LoadMobMesh(const std::string& filepath);

    VkCommandPool m_commandPool{ VK_NULL_HANDLE };
    // --- Variabili per il VMA Staging Ring Buffer ---
    
    
     // Puntatore fisso alla RAM
    const uint64_t STAGING_BUFFER_SIZE = 32 * 1024 * 1024; // 32 MB
    uint64_t m_currentOffset = 0; // Il cursore 'Head'
    VkCommandBuffer m_transferCommandBuffer{ VK_NULL_HANDLE };
    VkCommandPool m_transferCommandPool{ VK_NULL_HANDLE };
    
    // --- VmaPool dedicato per Chunk ---
    

    // --- GLOBAL VRAM BUFFER (Slab Allocator) ---
    
    

    inline uint64_t AlignMemory(uint64_t offset, uint64_t alignment = 256) {
        return (offset + alignment - 1) & ~(alignment - 1);
    }
    void FlushTransferBatch();

    // Trasformati in Vettori per supportare i MAX_FRAMES_IN_FLIGHT
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;

    // Gestione Validation Layers (Attivi solo in configurazione Debug)

    

    


    ;

    
    

    
    
    

    // --- METODI FASE 3 ---
    
    
    
    
    

    // --- NUOVI METODI (FASE 3 - ULTIMA PARTE) ---
    bool CreateRenderPass();
    bool CreateDescriptorSetLayout();
    bool CreateGraphicsPipeline();
    bool CreateForgePipeline();
    bool CreateFramebuffers();
    bool CreateCommandPoolAndBuffer();
    
    void CreatePBRTextures(const fw::PackedTextureData& pbrData);
    
    
    bool CreateSyncObjects();

    // --- NUOVI METODI (FASE 4) ---
    static std::vector<char> ReadFile(const std::string& filename);
    VkShaderModule CreateShaderModule(const std::vector<char>& code);
    void UpdateUniformBuffer(uint32_t currentImage, glm::mat4 viewMatrix, glm::mat4 projMatrix, float seasonProgress, struct SharedContext* context);
    

    // --- VERTEX/INDEX BUFFER ---
    bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VmaMemoryUsage vmaUsage,
                      VkBuffer& buffer, VmaAllocation& bufferAllocation, VmaAllocationCreateFlags flags = 0);
    void CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
    bool CreateVertexBuffer(const std::vector<Vertex>& vertices);
    bool CreateIndexBuffer(const std::vector<uint32_t>& indices);

public:
    // Carica (o ricarica) la geometria di un singolo chunk
    void UploadChunkMesh(ChunkCoord coord, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    
    // Rimuove i buffer di un chunk se viene distrutto
    void DestroyChunkBuffer(ChunkCoord coord);
    
    // Carica la geometria dei blocchi fantasma sulla GPU
    void UploadGhostMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    // Invalida la cache di rendering del mondo procedurale (es. Chunk, buffer pendenti)
    void InvalidateForgeCache();

private:
    
    void RenderFairworld(VkCommandBuffer cmd, glm::mat4 viewMatrix, glm::vec3 skyColor, SharedContext* context, AssetManager* assets, MobManager* mobManager, Player* player, fw::ForgeWorld* overrideWorld = nullptr);

public:
    // Metodo da chiamare nel Game Loop
    void RenderDesktop(glm::mat4 viewMatrix, glm::vec3 skyColor, struct SharedContext* context = nullptr, class AssetManager* assets = nullptr, class MobManager* mobManager = nullptr, class Player* player = nullptr);
    void RenderForge(VkCommandBuffer cmd, const glm::mat4& viewProjMatrix, glm::vec3 cameraPos, struct SharedContext* context);
};
