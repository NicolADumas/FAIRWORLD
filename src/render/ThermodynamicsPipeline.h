#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include "EventBridge.h" // Contiene CellData e ReadbackEvent

class ThermodynamicsPipeline {
public:
    // Struttura per i Push Constants (matcha il .comp)
    struct alignas(16) PushConstants {
        uint32_t grid_width;
        uint32_t grid_height;
        float diffusion_rate;
    };

    ThermodynamicsPipeline() = default;
    ~ThermodynamicsPipeline();

    // 1. Inizializzazione
    void Initialize(VkDevice device, VmaAllocator allocator);

    // 2. Allocazione Buffer (Tramite VMA)
    void AllocateBuffers(uint32_t total_cells, uint32_t max_events);

    // 3. Registrazione del Comando
    void RecordComputeCommands(VkCommandBuffer cmdBuffer, const PushConstants& pc);

    // 4. Ping Pong e Readback
    void SwapBuffers(); 
    std::vector<fw::ReadbackEvent> FetchEvents(); 

    // Pulizia
    void Cleanup();

private:
    void CreateDescriptorSetLayout();
    void CreatePipeline();
    void UpdateDescriptorSets();
    VkShaderModule CreateShaderModule(const std::vector<uint32_t>& code);

    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;

    VkPipeline m_computePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    // Buffers per Ping-Pong (0 e 1)
    VkBuffer m_gridBuffers[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VmaAllocation m_gridAllocations[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };

    // Buffer degli eventi mappato
    VkBuffer m_eventBuffer = VK_NULL_HANDLE;
    VmaAllocation m_eventAllocation = VK_NULL_HANDLE;
    void* m_eventMappedData = nullptr;

    uint32_t m_maxEvents = 0;
    uint32_t m_totalCells = 0;

    // Indice del buffer di lettura (0 o 1)
    int m_readBufferIndex = 0; 
};
