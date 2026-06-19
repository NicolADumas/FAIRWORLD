#include "pch.h"
#include "VulkanDmaManager.h"
#include <iostream>
#include <cstring> // per memcpy

#include <vulkan/vulkan.h>

namespace fw {

VulkanDmaManager::VulkanDmaManager() {
}

VulkanDmaManager::~VulkanDmaManager() {
    if (m_device && m_transferTimeline) {
        // vkDestroySemaphore(m_device, m_transferTimeline, nullptr);
        // Defer destroying semaphore to RenderManager or destroy it here
    }
}

void VulkanDmaManager::Initialize(VkDevice device, VkQueue transferQueue, VkCommandPool transferPool, 
                                  VkBuffer stagingBuffer, void* mappedStaging, uint32_t stagingSize,
                                  VkBuffer globalVramBuffer) {
    m_device = device;
    m_transferQueue = transferQueue;
    m_transferPool = transferPool;
    m_stagingVkBuffer = stagingBuffer;
    m_mappedStagingBuffer = mappedStaging;
    m_stagingBufferSize = stagingSize;
    m_globalVramBuffer = globalVramBuffer;

    std::cout << "[Vulkan DMA] Inizializzazione Macro Staging Buffer e Timeline Semaphore...\n";
    
    // Inizializzazione Timeline Semaphore
    VkSemaphoreTypeCreateInfo timelineCreateInfo{};
    timelineCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineCreateInfo.initialValue = 0;

    VkSemaphoreCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    createInfo.pNext = &timelineCreateInfo;

    if (vkCreateSemaphore(m_device, &createInfo, nullptr, &m_transferTimeline) != VK_SUCCESS) {
        std::cerr << "[Vulkan DMA] Errore: Impossibile creare Timeline Semaphore!\n";
    } else {
        std::cout << "[Vulkan DMA] Transfer Timeline Semaphore creato e inizializzato a 0.\n";
    }
}

uint32_t VulkanDmaManager::AllocateStagingSpace(uint32_t size) {
    // Semplificazione: per un vero Ring Buffer bisognerebbe gestire il wrap-around 
    // e verificare se Head raggiunge Tail. 
    // Per il test, assumiamo che i 64MB non si esauriscano o gestiamo un wrap barbaro.
    uint32_t allocOffset = m_headOffset;
    
    if (m_headOffset + size > m_stagingBufferSize) {
        // Wrap around (in Vulkan dovresti emettere due vkCmdCopyBuffer se il blocco si spezza,
        // o scartare lo spazio a fine buffer e ricominciare da 0).
        allocOffset = 0; 
        m_headOffset = size;
    } else {
        m_headOffset += size;
    }
    
    return allocOffset;
}

uint64_t VulkanDmaManager::UploadMeshAsync(const void* meshData, uint32_t sizeInBytes, const VramAllocation& destination) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. Alloca spazio nello staging buffer (O(1))
    uint32_t stagingOffset = AllocateStagingSpace(sizeInBytes);

    // 2. Burst Copy in RAM. 
    void* dstPtr = static_cast<char*>(m_mappedStagingBuffer) + stagingOffset;
    std::memcpy(dstPtr, meshData, sizeInBytes);
    
    // 3. Registrazione del comando Vulkan
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_transferPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = stagingOffset;
    copyRegion.dstOffset = destination.offset;
    copyRegion.size = sizeInBytes;
    vkCmdCopyBuffer(cmd, m_stagingVkBuffer, m_globalVramBuffer, 1, &copyRegion);
    
    vkEndCommandBuffer(cmd);

    // 5. Incremento del Timeline Counter e Sottomissione
    m_currentTimelineValue++;
    
    VkTimelineSemaphoreSubmitInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.signalSemaphoreValueCount = 1;
    timelineInfo.pSignalSemaphoreValues = &m_currentTimelineValue;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = &timelineInfo;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_transferTimeline;

    vkQueueSubmit(m_transferQueue, 1, &submitInfo, VK_NULL_HANDLE);

    std::cout << "[DMA Transfer] Mesh da " << (sizeInBytes / 1024) << "KB sparata sul PCIe. "
              << "VRAM Offset: " << destination.offset << ". Timeline attesa: " << m_currentTimelineValue << "\n";

    // Nota: in un'implementazione reale, il command buffer andrebbe liberato 
    // DOPO che la GPU ha finito di eseguirlo (controllando il timeline semaphore).
    // Per ora ci affidiamo alla distruzione del pool globale o alla vkQueueWaitIdle altrove.
    
    return m_currentTimelineValue;
}

} // namespace fw
