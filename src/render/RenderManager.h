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
#include "World.h"  // Per la struct Vertex

class XrManager;
struct BlockDef;

// Struttura di supporto per le code della GPU
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    std::optional<uint32_t> transferFamily;

    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value() && transferFamily.has_value();
    }
};

struct ForgePushConstantData {
    glm::mat4 mvp;
    glm::vec4 colorOverride;
    int useColorOverride;
    float seasonProgress;
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

    VkInstance GetVulkanInstance() const { return m_instance; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }
    VkDevice GetDevice() const { return m_device; }
    VkQueue GetTransferQueue() const { return m_transferQueue; }
    VkCommandPool GetTransferCommandPool() const { return m_transferCommandPool; }
    VkBuffer GetStagingRingBuffer() const { return m_stagingRingBuffer; }
    VkDeviceMemory GetStagingDeviceMemory() const;
    void* GetMappedStagingData() const { return m_mappedStagingData; }
    uint64_t GetStagingBufferSize() const { return STAGING_BUFFER_SIZE; }
    VkBuffer GetGlobalVramBuffer() const { return m_globalVramBuffer; }
    std::mutex* GetQueueMutex() { return &m_queueMutex; }

    // Notifica che la finestra è stata ridimensionata: ricrea la Swapchain
    void NotifyResize() { RecreateSwapchain(); }

private:
    bool m_isVRMode;
    void* m_hwnd{ nullptr };
    VkInstance m_instance{ VK_NULL_HANDLE };
    VkPhysicalDevice m_physicalDevice{ VK_NULL_HANDLE };

    // --- NUOVI COMPONENTI FASE 3 ---
    VkDevice m_device{ VK_NULL_HANDLE };
    std::mutex m_queueMutex;
    VkQueue m_graphicsQueue{ VK_NULL_HANDLE };
    VkQueue m_presentQueue{ VK_NULL_HANDLE };
    VkQueue m_transferQueue{ VK_NULL_HANDLE }; // Coda asincrona per i Voxel
    
    // --- VMA ---
    VmaAllocator m_vmaAllocator{ VK_NULL_HANDLE };
    
    VkSurfaceKHR m_surface{ VK_NULL_HANDLE };
    VkSwapchainKHR m_swapchain{ VK_NULL_HANDLE };
    
    std::vector<VkImage> m_swapchainImages;
    VkFormat m_swapchainImageFormat;
    VkExtent2D m_swapchainExtent;
    std::vector<VkImageView> m_swapchainImageViews;

    // --- NUOVI COMPONENTI (FASE 3 - ULTIMA PARTE) ---
    VkRenderPass m_renderPass{ VK_NULL_HANDLE };
    std::vector<VkFramebuffer> m_framebuffers;
    
    VkDescriptorPool m_imguiDescriptorPool{ VK_NULL_HANDLE };

    // --- NUOVI COMPONENTI (FASE 4) ---
    const int MAX_FRAMES_IN_FLIGHT = 2;
    uint32_t m_currentFrame = 0;

    // Struttura che rispecchia esattamente quella nello shader
    struct UniformBufferObject {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
        float seasonProgress;
        float padding[3]; // Allineamento a 16 byte (std140)
    };

    // --- COMPONENTI FASE 5 (UBO & Descriptors) ---
    VkDescriptorSetLayout m_descriptorSetLayout{ VK_NULL_HANDLE };
    VkDescriptorPool m_descriptorPool{ VK_NULL_HANDLE };
    std::vector<VkDescriptorSet> m_descriptorSets;

    std::vector<VkBuffer> m_uniformBuffers;
    std::vector<VmaAllocation> m_uniformBuffersAllocation;
    std::vector<void*> m_uniformBuffersMapped; // Puntatori per scrivere direttamente nella RAM

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

    // --- TEXTURE ARRAY (In-Game Pixel Editor) ---
    VkImage m_textureImage{ VK_NULL_HANDLE };
    VmaAllocation m_textureImageAllocation{ VK_NULL_HANDLE };
    VkImageView m_textureImageView{ VK_NULL_HANDLE };
    VkSampler m_textureSampler{ VK_NULL_HANDLE };

    // --- DEPTH BUFFER (risolve il problema delle facce trasparenti) ---
    VkImage m_depthImage{ VK_NULL_HANDLE };
    VmaAllocation m_depthImageAllocation{ VK_NULL_HANDLE };
    VkImageView m_depthImageView{ VK_NULL_HANDLE };

    bool CreateDepthResources();
    VkFormat FindDepthFormat();
    VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

    void CreateTextureImage();
    void CreateTextureImageView();
    void CreateTextureSampler();
    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer);
    void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layerCount);
    void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount);
    void CreateImage(uint32_t width, uint32_t height, uint32_t layerCount, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VmaMemoryUsage vmaUsage, VkImage& image, VmaAllocation& imageAllocation);
    
    float m_fov = 45.0f;

    void CleanupSwapchain();
    void RecreateSwapchain();
    void DefragmentVRAM();
    
public:
    float GetFov() const { return m_fov; }
    void SetFov(float fov) { m_fov = fov; }
    uint32_t GetWindowWidth() const { return m_swapchainExtent.width; }
    uint32_t GetWindowHeight() const { return m_swapchainExtent.height; }

    // Chiamata dall'Editor per aggiornare la texture in real-time
    void UpdateTextureLayer(uint32_t layerIndex, const void* pixelData, uint32_t width, uint32_t height);
    void LoadBlockTextures(const std::string& baseDir, const std::vector<BlockDef>& blocks);
    bool LoadTextureFromFile(const std::string& filePath, uint32_t layerIndex);

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
    VkBuffer m_stagingRingBuffer{ VK_NULL_HANDLE };
    VmaAllocation m_stagingAllocation{ VK_NULL_HANDLE };
    void* m_mappedStagingData = nullptr; // Puntatore fisso alla RAM
    const uint64_t STAGING_BUFFER_SIZE = 32 * 1024 * 1024; // 32 MB
    uint64_t m_currentOffset = 0; // Il cursore 'Head'
    VkCommandBuffer m_transferCommandBuffer{ VK_NULL_HANDLE };
    VkCommandPool m_transferCommandPool{ VK_NULL_HANDLE };
    
    // --- VmaPool dedicato per Chunk ---
    VmaPool m_chunkVmaPool{ VK_NULL_HANDLE };

    // --- GLOBAL VRAM BUFFER (Slab Allocator) ---
    VkBuffer m_globalVramBuffer{ VK_NULL_HANDLE };
    VmaAllocation m_globalVramAllocation{ VK_NULL_HANDLE };

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
#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif

    const std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    bool CheckValidationLayerSupport();
    std::vector<const char*> GetRequiredExtensions(XrManager* xrManager);

    bool CreateVulkanInstance(XrManager* xrManager);
    bool PickPhysicalDevice(XrManager* xrManager);
    int RateDeviceSuitability(VkPhysicalDevice device);

    // --- METODI FASE 3 ---
    bool CreateSurface(void* hwnd, void* hinstance);
    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
    bool CreateLogicalDevice();
    bool CreateSwapchain(void* hwnd);
    bool CreateImageViews();

    // --- NUOVI METODI (FASE 3 - ULTIMA PARTE) ---
    bool CreateRenderPass();
    bool CreateFramebuffers();
    bool CreateCommandPoolAndBuffer();
    bool CreateSyncObjects();

    // --- NUOVI METODI (FASE 4) ---
    static std::vector<char> ReadFile(const std::string& filename);
    VkShaderModule CreateShaderModule(const std::vector<char>& code);
    bool CreateGraphicsPipeline();
    bool CreateForgePipeline();
    bool CreateDescriptorSetLayout();
    bool CreateUniformBuffers();
    bool CreateDescriptorPoolAndSets();
    void UpdateUniformBuffer(uint32_t currentImage, glm::mat4 viewMatrix, glm::mat4 projMatrix, float seasonProgress);
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

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
    void RenderFairworld(VkCommandBuffer cmd, glm::mat4 viewMatrix, glm::vec3 skyColor, struct SharedContext* context, class AssetManager* assets, class MobManager* mobManager, class Player* player);

public:
    // Metodo da chiamare nel Game Loop
    void RenderDesktop(glm::mat4 viewMatrix, glm::vec3 skyColor, struct SharedContext* context = nullptr, class AssetManager* assets = nullptr, class MobManager* mobManager = nullptr, class Player* player = nullptr);
    void RenderForge(VkCommandBuffer cmd, const glm::mat4& viewProjMatrix, struct SharedContext* context);
};
