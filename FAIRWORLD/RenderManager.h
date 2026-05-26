#pragma once
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
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

    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
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

private:
    bool m_isVRMode;
    void* m_hwnd{ nullptr };
    VkInstance m_instance{ VK_NULL_HANDLE };
    VkPhysicalDevice m_physicalDevice{ VK_NULL_HANDLE };

    // --- NUOVI COMPONENTI FASE 3 ---
    VkDevice m_device{ VK_NULL_HANDLE };
    VkQueue m_graphicsQueue{ VK_NULL_HANDLE };
    VkQueue m_presentQueue{ VK_NULL_HANDLE };
    
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
    };

    // --- COMPONENTI FASE 5 (UBO & Descriptors) ---
    VkDescriptorSetLayout m_descriptorSetLayout{ VK_NULL_HANDLE };
    VkDescriptorPool m_descriptorPool{ VK_NULL_HANDLE };
    std::vector<VkDescriptorSet> m_descriptorSets;

    std::vector<VkBuffer> m_uniformBuffers;
    std::vector<VkDeviceMemory> m_uniformBuffersMemory;
    std::vector<void*> m_uniformBuffersMapped; // Puntatori per scrivere direttamente nella RAM

    VkPipelineLayout m_pipelineLayout{ VK_NULL_HANDLE };
    VkPipeline m_graphicsPipeline{ VK_NULL_HANDLE };

    // --- TEXTURE ARRAY (In-Game Pixel Editor) ---
    VkImage m_textureImage{ VK_NULL_HANDLE };
    VkDeviceMemory m_textureImageMemory{ VK_NULL_HANDLE };
    VkImageView m_textureImageView{ VK_NULL_HANDLE };
    VkSampler m_textureSampler{ VK_NULL_HANDLE };

    // --- DEPTH BUFFER (risolve il problema delle facce trasparenti) ---
    VkImage m_depthImage{ VK_NULL_HANDLE };
    VkDeviceMemory m_depthImageMemory{ VK_NULL_HANDLE };
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
    void CreateImage(uint32_t width, uint32_t height, uint32_t layerCount, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
    
    float m_fov = 45.0f;

    void CleanupSwapchain();
    void RecreateSwapchain();
    
public:
    float GetFov() const { return m_fov; }
    void SetFov(float fov) { m_fov = fov; }

    // Chiamata dall'Editor per aggiornare la texture in real-time
    void UpdateTextureLayer(uint32_t layerIndex, const void* pixelData, uint32_t width, uint32_t height);
    void LoadBlockTextures(const std::string& baseDir, const std::vector<BlockDef>& blocks);
    bool LoadTextureFromFile(const std::string& filePath, uint32_t layerIndex);

    // --- CHUNK BUFFERS (Mappa RPG Veloce) ---
    struct VulkanChunkBuffer {
        VkBuffer vertexBuffer{ VK_NULL_HANDLE };
        VkDeviceMemory vertexBufferMemory{ VK_NULL_HANDLE };
        VkBuffer indexBuffer{ VK_NULL_HANDLE };
        VkDeviceMemory indexBufferMemory{ VK_NULL_HANDLE };
        uint32_t indexCount{ 0 };
    };
    std::unordered_map<ChunkCoord, VulkanChunkBuffer, ChunkHash> m_chunkBuffers;

    // --- VERTEX e INDEX BUFFER (ologrammi AI) ---
    VkBuffer m_ghostVertexBuffer{ VK_NULL_HANDLE };
    VkDeviceMemory m_ghostVertexBufferMemory{ VK_NULL_HANDLE };
    VkBuffer m_ghostIndexBuffer{ VK_NULL_HANDLE };
    VkDeviceMemory m_ghostIndexBufferMemory{ VK_NULL_HANDLE };
    uint32_t m_ghostIndexCount = 0;

    // --- MOB MESH (Dynamic Registry) ---
    struct VoxelMesh {
        VkBuffer vertexBuffer{ VK_NULL_HANDLE };
        VkDeviceMemory vertexBufferMemory{ VK_NULL_HANDLE };
        VkBuffer indexBuffer{ VK_NULL_HANDLE };
        VkDeviceMemory indexBufferMemory{ VK_NULL_HANDLE };
        uint32_t indexCount{ 0 };
    };
    std::map<std::string, VoxelMesh> m_mobMeshes;
    
    void LoadAllMobMeshes(class AssetManager& assets);
    void LoadMobMesh(const std::string& filepath);

    VkCommandPool m_commandPool{ VK_NULL_HANDLE };

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
    bool CreateDescriptorSetLayout();
    bool CreateUniformBuffers();
    bool CreateDescriptorPoolAndSets();
    void UpdateUniformBuffer(uint32_t currentImage, glm::mat4 viewMatrix);
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    // --- VERTEX/INDEX BUFFER ---
    bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& bufferMemory);
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
    
    // Metodo da chiamare nel Game Loop
    void RenderDesktop(glm::mat4 viewMatrix, glm::vec3 skyColor, class AssetManager* assets = nullptr, class MobManager* mobManager = nullptr, class Player* player = nullptr);
};
