#include "pch.h"
#include "VulkanMemory.h"
#include "VulkanCore.h"
#include "RenderManager.h" // Per struct UniformBufferObject, ecc.
#include <iostream>

namespace fw {

VulkanMemory::VulkanMemory(VulkanCore* core) : m_core(core) {}

VulkanMemory::~VulkanMemory() { Cleanup(); }

bool VulkanMemory::Initialize() {
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = m_core->GetPhysicalDevice();
    allocatorInfo.device = m_core->GetDevice();
    allocatorInfo.instance = m_core->GetInstance();
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_1;
    if (vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] Impossibile inizializzare VmaAllocator!" << std::endl;
        return false;
    }

    QueueFamilyIndices indices = m_core->FindQueueFamilies(m_core->GetPhysicalDevice());

    // --- 2. CREATE CHUNK VMA POOL ---
    VkBufferCreateInfo dummyBufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    dummyBufInfo.size = 1024;
    dummyBufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo dummyAllocInfo = {};
    dummyAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    uint32_t memTypeIndex = 0;
    vmaFindMemoryTypeIndexForBufferInfo(m_vmaAllocator, &dummyBufInfo, &dummyAllocInfo, &memTypeIndex);

    VmaPoolCreateInfo vmaPoolInfo = {};
    vmaPoolInfo.memoryTypeIndex = memTypeIndex;
    if (vmaCreatePool(m_vmaAllocator, &vmaPoolInfo, &m_chunkVmaPool) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] failed to create VMA Pool for chunks!" << std::endl;
        return false;
    }

    // --- 3. CREATE RING BUFFER (STAGING PERSISTENTE) ---
    VkBufferCreateInfo stagingBufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    stagingBufInfo.size = 128 * 1024 * 1024; // 128 MB (Assumendo STAGING_BUFFER_SIZE)
    stagingBufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    
    VmaAllocationCreateInfo stagingAllocInfo = {};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    
    if (vmaCreateBuffer(m_vmaAllocator, &stagingBufInfo, &stagingAllocInfo, &m_stagingRingBuffer, &m_stagingAllocation, nullptr) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] Impossibile creare lo Staging Ring Buffer!\n";
        return false;
    }
    
    VmaAllocationInfo vmaRingInfo;
    vmaGetAllocationInfo(m_vmaAllocator, m_stagingAllocation, &vmaRingInfo);
    m_mappedStagingData = vmaRingInfo.pMappedData;

    // --- 4. CREATE GLOBAL VRAM BUFFER (512 MB per i chunk) ---
    VkBufferCreateInfo vramBufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    vramBufInfo.size = 512 * 1024 * 1024; // 512 MB
    vramBufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    
    std::vector<uint32_t> uniqueQueueFamilies;
    if (indices.graphicsFamily.has_value()) {
        uniqueQueueFamilies.push_back(indices.graphicsFamily.value());
    }
    if (indices.transferFamily.has_value() && indices.transferFamily.value() != indices.graphicsFamily.value()) {
        uniqueQueueFamilies.push_back(indices.transferFamily.value());
    }

    if (uniqueQueueFamilies.size() > 1) {
        vramBufInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        vramBufInfo.queueFamilyIndexCount = static_cast<uint32_t>(uniqueQueueFamilies.size());
        vramBufInfo.pQueueFamilyIndices = uniqueQueueFamilies.data();
    } else {
        vramBufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    
    VmaAllocationCreateInfo vramAllocInfo = {};
    vramAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    
    if (vmaCreateBuffer(m_vmaAllocator, &vramBufInfo, &vramAllocInfo, &m_globalVramBuffer, &m_globalVramAllocation, nullptr) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] Impossibile creare il Global VRAM Buffer da 512MB!\n";
        return false;
    }

    std::cout << "[VMA] VmaAllocator e Global VRAM Buffer (512MB) inizializzati con successo." << std::endl;
    return true;
}

void VulkanMemory::Cleanup() {
    if (m_stagingRingBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_vmaAllocator, m_stagingRingBuffer, m_stagingAllocation);
        m_stagingRingBuffer = VK_NULL_HANDLE;
    }
    
    if (m_globalVramBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_vmaAllocator, m_globalVramBuffer, m_globalVramAllocation);
        m_globalVramBuffer = VK_NULL_HANDLE;
    }

    for (size_t i = 0; i < m_uniformBuffers.size(); i++) {
        if (m_uniformBuffers[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_vmaAllocator, m_uniformBuffers[i], m_uniformBuffersAllocation[i]);
        }
    }
    m_uniformBuffers.clear();
    m_uniformBuffersAllocation.clear();
    m_uniformBuffersMapped.clear();

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_core->GetDevice(), m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    if (m_imguiDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_core->GetDevice(), m_imguiDescriptorPool, nullptr);
        m_imguiDescriptorPool = VK_NULL_HANDLE;
    }
    if (m_forgeDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_core->GetDevice(), m_forgeDescriptorPool, nullptr);
        m_forgeDescriptorPool = VK_NULL_HANDLE;
    }

    if (m_chunkVmaPool != VK_NULL_HANDLE) {
        vmaDestroyPool(m_vmaAllocator, m_chunkVmaPool);
        m_chunkVmaPool = VK_NULL_HANDLE;
    }

    if (m_vmaAllocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_vmaAllocator);
        m_vmaAllocator = VK_NULL_HANDLE;
        std::cout << "[VMA] VmaAllocator distrutto." << std::endl;
    }
}

uint32_t VulkanMemory::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_core->GetPhysicalDevice(), &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    std::cerr << "[VULKAN ERROR] Impossibile trovare un tipo di memoria adatto!" << std::endl;
    return 0;
}
bool VulkanMemory::CreateUniformBuffers(size_t uniformBufferSize) {
    VkDeviceSize bufferSize = uniformBufferSize;

    m_uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersAllocation.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        
        if (vmaCreateBuffer(m_vmaAllocator, &bufferInfo, &allocInfo, &m_uniformBuffers[i], &m_uniformBuffersAllocation[i], nullptr) != VK_SUCCESS) {
            return false;
        }

        VmaAllocationInfo vmaAllocInfo;
        vmaGetAllocationInfo(m_vmaAllocator, m_uniformBuffersAllocation[i], &vmaAllocInfo);
        m_uniformBuffersMapped[i] = vmaAllocInfo.pMappedData;
    }
    return true;
}
bool VulkanMemory::CreateDescriptorPoolAndSets(VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSetLayout forgeDescriptorSetLayout, size_t uniformBufferSize) {
    // Questo Ã¨ il legacy descriptor pool e set! Serve UBO e 1 Sampler (dummy)
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    if (vkCreateDescriptorPool(m_core->GetDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) return false;

    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    m_descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(m_core->GetDevice(), &allocInfo, m_descriptorSets.data()) != VK_SUCCESS) return false;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = uniformBufferSize;

        // Usiamo il sampler e una imageView dummy o niente se non usati. 
        // Per evitare crash usiamo m_albedoImageView se disponibile, altrimenti puo fallire il legacy.
        // Assumiamo che m_albedoImageView sia creato. Ma CreateDescriptorPoolAndSets 
        // viene richiamato PRIMA di CreatePBRTextures.
        // Cosi non funziona. Spostiamo CreateDescriptorPoolAndSets IN Init?
        // Se non abbiamo l'image view, il vecchio rendering crasherÃ  se lo attiviamo.
    }
    return true;
}
bool VulkanMemory::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VmaMemoryUsage vmaUsage,
                                 VkBuffer& buffer, VmaAllocation& bufferAllocation, VmaAllocationCreateFlags flags) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size  = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = vmaUsage;
    allocInfo.flags = flags;

    if (vmaCreateBuffer(m_vmaAllocator, &bufferInfo, &allocInfo, &buffer, &bufferAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }
    return true;
}

VkDeviceMemory VulkanMemory::GetStagingDeviceMemory() const {
    if (m_vmaAllocator != VK_NULL_HANDLE && m_stagingAllocation != VK_NULL_HANDLE) {
        VmaAllocationInfo allocInfo;
        vmaGetAllocationInfo(m_vmaAllocator, m_stagingAllocation, &allocInfo);
        return allocInfo.deviceMemory;
    }
    return VK_NULL_HANDLE;
}

} // namespace fw
