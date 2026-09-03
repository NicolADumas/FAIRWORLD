#pragma once
#include <vector>
#include <cstdint>
#include <mutex>
#include "VramSlabAllocator.h"

// Forward declaration per i tipi Vulkan
typedef struct VkDevice_T* VkDevice;
typedef struct VkBuffer_T* VkBuffer;
typedef struct VkQueue_T* VkQueue;
typedef struct VkSemaphore_T* VkSemaphore;
typedef struct VkCommandPool_T* VkCommandPool;
typedef struct VkDeviceMemory_T* VkDeviceMemory;

typedef struct VkCommandBuffer_T* VkCommandBuffer;

namespace fw {

class VulkanDmaManager {
public:
    VulkanDmaManager();
    ~VulkanDmaManager();

    // Inizializza il Dma Manager con gli handle di Vulkan generati da RenderManager
    void Initialize(
        VkDevice device, 
        VkQueue transferQueue, 
        VkCommandPool commandPool,
        VkBuffer stagingBuffer, 
        VkDeviceMemory stagingMemory, 
        void* mappedData, 
        uint64_t stagingSize,
        VkBuffer deprecatedGlobalVramBuffer,
        std::mutex* queueMutex
    );
    
    void UpdateStagingBuffer(VkBuffer stagingBuffer, VkDeviceMemory stagingMemory, void* mappedData) {
        m_stagingVkBuffer = stagingBuffer;
        m_stagingVkDeviceMemory = stagingMemory;
        m_mappedStagingBuffer = mappedData;
    }

    void Cleanup();

    // Funzione chiamata dal Worker Thread (Job System).
    // Esegue una copia Zero-Copy in RAM (Write-Combine burst) verso lo Staging Buffer,
    // e accoda un Job di trasferimento DMA sulla Transfer Queue di Vulkan.
    // Ritorna il valore del Timeline Semaphore a cui la Graphics Queue dovrà attendere.
    uint64_t UploadMeshAsync(const void* meshData, uint32_t sizeInBytes, const VramAllocationInfo& destination, VkBuffer destBuffer);

    // Ritorna il Timeline Semaphore da passare al vkQueueSubmit grafico
    VkSemaphore GetTimelineSemaphore() const { return m_transferTimeline; }

private:
    std::mutex m_mutex;

    // -- Staging Ring Buffer --
    uint32_t m_stagingBufferSize;
    uint32_t m_headOffset = 0; // Dove scriviamo
    uint32_t m_tailOffset = 0; // Cosa ha finito di leggere la GPU
    
    // Puntatore MAPPATO persistente in memoria HOST_VISIBLE | HOST_COHERENT
    void* m_mappedStagingBuffer = nullptr; 
    
    VkDevice m_device = nullptr;
    VkBuffer m_stagingVkBuffer = nullptr;
    VkDeviceMemory m_stagingVkDeviceMemory = nullptr;
    VkQueue m_transferQueue = nullptr;
    VkCommandPool m_transferPool = nullptr;
    VkBuffer m_globalVramBuffer = nullptr;
    std::mutex* m_queueMutex = nullptr;

    // -- Timeline Semaphore --
    VkSemaphore m_transferTimeline = nullptr;
    uint64_t m_currentTimelineValue = 0;

    // Helper interno per allocare spazio nel ring buffer.
    // Se non c'è spazio, in una VERA implementazione dovrebbe attendere (wait)
    // l'avanzamento della m_tailOffset verificando il valore del Timeline Semaphore.
    uint32_t AllocateStagingSpace(uint32_t size);

    struct PendingTransfer {
        uint64_t timelineId;
        VkCommandBuffer cmd;
    };
    std::vector<PendingTransfer> m_pendingTransfers;
};

} // namespace fw
