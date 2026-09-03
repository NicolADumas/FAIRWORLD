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
                                  VkBuffer stagingBuffer, VkDeviceMemory stagingDeviceMemory, void* mappedStaging, uint64_t stagingSize,
                                  VkBuffer deprecatedGlobalVramBuffer, std::mutex* queueMutex) {
    m_device = device;
    m_transferQueue = transferQueue;
    m_transferPool = transferPool;
    m_stagingVkBuffer = stagingBuffer;
    m_stagingVkDeviceMemory = stagingDeviceMemory;
    m_mappedStagingBuffer = mappedStaging;
    m_stagingBufferSize = stagingSize;
    m_queueMutex = queueMutex;
    
    // m_globalVramBuffer non più usato qui, passato per parametro

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

uint64_t VulkanDmaManager::UploadMeshAsync(const void* meshData, uint32_t sizeInBytes, const VramAllocationInfo& destination, VkBuffer destBuffer) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 0. Garbage Collection dei Command Buffer completati
    uint64_t completedValue = 0;
    if (m_transferTimeline) {
        // Query del valore attuale del semaforo sulla GPU
        vkGetSemaphoreCounterValue(m_device, m_transferTimeline, &completedValue);
    }
    
    auto it = m_pendingTransfers.begin();
    while (it != m_pendingTransfers.end()) {
        if (completedValue >= it->timelineId) {
            vkFreeCommandBuffers(m_device, m_transferPool, 1, &it->cmd);
            it = m_pendingTransfers.erase(it);
        } else {
            ++it;
        }
    }

    // 1. Alloca spazio nello staging buffer (O(1))
    uint32_t stagingOffset = AllocateStagingSpace(sizeInBytes);

    // 2. Burst Copy in RAM. 
    void* dstPtr = static_cast<char*>(m_mappedStagingBuffer) + stagingOffset;
    std::memcpy(dstPtr, meshData, sizeInBytes);
    
    // Flush the mapped memory so the GPU sees the new data
    if (m_stagingVkDeviceMemory != VK_NULL_HANDLE) {
        VkMappedMemoryRange flushRange{};
        flushRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        flushRange.memory = m_stagingVkDeviceMemory;
        flushRange.offset = 0;
        flushRange.size = VK_WHOLE_SIZE;
        vkFlushMappedMemoryRanges(m_device, 1, &flushRange);
    }
    
    // 3. Registrazione del comando Vulkan
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_transferPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkResult resAlloc = vkAllocateCommandBuffers(m_device, &allocInfo, &cmd);
    if (resAlloc != VK_SUCCESS || cmd == VK_NULL_HANDLE) {
        std::cerr << "[VulkanDmaManager] ERROR: vkAllocateCommandBuffers fallito con codice " << resAlloc << "!\n";
        return 0;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        std::cerr << "[VulkanDmaManager] ERROR: vkBeginCommandBuffer fallito!\n";
        return 0;
    }

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = stagingOffset;
    copyRegion.dstOffset = destination.offset;
    copyRegion.size = sizeInBytes;
    
    if (destBuffer == VK_NULL_HANDLE) {
        std::cerr << "[VulkanDmaManager] ERROR: destBuffer IS NULL! Cannot copy buffer.\n";
    } else if (m_stagingVkBuffer == VK_NULL_HANDLE) {
        std::cerr << "[VulkanDmaManager] ERROR: m_stagingVkBuffer IS NULL! Cannot copy buffer.\n";
    } else {
        vkCmdCopyBuffer(cmd, m_stagingVkBuffer, destBuffer, 1, &copyRegion);
    }
    
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

    VkResult resSubmit = VK_SUCCESS;
    if (m_queueMutex) {
        std::lock_guard<std::mutex> qLock(*m_queueMutex);
        resSubmit = vkQueueSubmit(m_transferQueue, 1, &submitInfo, VK_NULL_HANDLE);
    } else {
        resSubmit = vkQueueSubmit(m_transferQueue, 1, &submitInfo, VK_NULL_HANDLE);
    }
    
    if (resSubmit != VK_SUCCESS) {
        std::cerr << "[VulkanDmaManager] ERROR: vkQueueSubmit fallito con codice " << resSubmit << "!\n";
    } else {
        // Aspetta che il trasferimento sia completo PRIMA di segnare la mesh come pronta.
        // Questo elimina la race condition: senza questo, il RenderManager potrebbe disegnare
        // dalla VRAM ancora non aggiornata dal DMA.
        // NOTA: m_queueMutex è già stato usato nella submit, non rilocchiamo.
        vkQueueWaitIdle(m_transferQueue);
    }

    if (sizeInBytes > 1024 * 1024) {
        std::cout << "[DMA Transfer] Mesh da " << (sizeInBytes / 1024) << "KB sparata sul PCIe. "
                  << "VRAM Offset: " << destination.offset << ". Timeline attesa: " << m_currentTimelineValue << "\n";
    }

    // 6. Registriamo il Command Buffer per la pulizia futura
    m_pendingTransfers.push_back({m_currentTimelineValue, cmd});
    
    return m_currentTimelineValue;
}

} // namespace fw
