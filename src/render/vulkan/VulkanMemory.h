#pragma once
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include <vector>

#define MAX_FRAMES_IN_FLIGHT 2

namespace fw {

class VulkanCore;

class VulkanMemory {
public:
    VulkanMemory(VulkanCore* core);
    ~VulkanMemory();

    bool Initialize();
    void Cleanup();
    void AddVramCompartment();
    
    VmaAllocator GetAllocator() const { return m_vmaAllocator; }
    VkBuffer& GetStagingRingBuffer() { return m_stagingRingBuffer; }
    void* GetMappedStagingData() { return m_mappedStagingData; }
    VkDeviceMemory GetStagingDeviceMemory() const;
    VmaPool GetChunkVmaPool() const { return m_chunkVmaPool; }
    
    const std::vector<VkBuffer>& GetVramCompartments() const { return m_vramCompartments; }
    VkBuffer GetVramCompartment(uint32_t idx) const { return idx < m_vramCompartments.size() ? m_vramCompartments[idx] : VK_NULL_HANDLE; }
    
    const std::vector<VkBuffer>& GetUniformBuffers() const { return m_uniformBuffers; }
    const std::vector<void*>& GetUniformBuffersMapped() const { return m_uniformBuffersMapped; }
    
    VkDescriptorPool& GetDescriptorPool() { return m_descriptorPool; }
    std::vector<VkDescriptorSet>& GetDescriptorSets() { return m_descriptorSets; }

    VkDescriptorPool& GetImguiDescriptorPool() { return m_imguiDescriptorPool; }
    VkDescriptorPool& GetForgeDescriptorPool() { return m_forgeDescriptorPool; }
    std::vector<VkDescriptorSet>& GetForgeDescriptorSets() { return m_forgeDescriptorSets; }

    // Helpers
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage vmaUsage, VkBuffer& buffer, VmaAllocation& bufferAllocation, VmaAllocationCreateFlags flags = 0);
    bool CreateUniformBuffers(size_t uniformBufferSize);
    bool CreateDescriptorPoolAndSets(VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSetLayout forgeDescriptorSetLayout, size_t uniformBufferSize);

private:
    VulkanCore* m_core;

    VmaAllocator m_vmaAllocator{ VK_NULL_HANDLE };

    VkBuffer m_stagingRingBuffer{ VK_NULL_HANDLE };
    VmaAllocation m_stagingAllocation{ VK_NULL_HANDLE };
    void* m_mappedStagingData = nullptr;

    VmaPool m_chunkVmaPool{ VK_NULL_HANDLE };

    std::vector<VkBuffer> m_vramCompartments;
    std::vector<VmaAllocation> m_vramCompartmentAllocations;

    std::vector<VkBuffer> m_uniformBuffers;
    std::vector<VmaAllocation> m_uniformBuffersAllocation;
    std::vector<void*> m_uniformBuffersMapped;

    VkDescriptorPool m_descriptorPool{ VK_NULL_HANDLE };
    std::vector<VkDescriptorSet> m_descriptorSets;

    VkDescriptorPool m_imguiDescriptorPool{ VK_NULL_HANDLE };
    VkDescriptorPool m_forgeDescriptorPool{ VK_NULL_HANDLE };
    std::vector<VkDescriptorSet> m_forgeDescriptorSets;
};

} // namespace fw
