#pragma once

#include <vulkan/vulkan.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>
#include <functional>
#include "VramSlabAllocator.h"

namespace fw {

enum class DmaUploadStatus : uint8_t {
    Completed,
    InvalidInput,
    CommandBufferAllocationFailed,
    CommandBufferBeginFailed,
    CommandBufferEndFailed,
    FlushFailed,
    SubmitFailed,
    WaitFailed,
    CounterQueryFailed,
    PendingTransferNotConfirmed,
    DeviceLost
};

enum class DmaSubmissionState : uint8_t {
    NotSubmitted,
    SubmitFailed,
    SubmittedButNotConfirmed,
    Completed,
    DeviceLost
};

struct DmaUploadResult {
    DmaUploadStatus status = DmaUploadStatus::InvalidInput;
    DmaSubmissionState submission = DmaSubmissionState::NotSubmitted;
    VkResult vkResult = VK_SUCCESS;
    uint64_t timelineTarget = 0;

    bool completed() const { return status == DmaUploadStatus::Completed; }
    bool mayStillBeInUse() const {
        return submission == DmaSubmissionState::SubmittedButNotConfirmed ||
               submission == DmaSubmissionState::DeviceLost;
    }
};

// Narrow injectable boundary used only by deterministic CPU fault tests.
// Empty callbacks always dispatch to Vulkan in normal production operation.
struct VulkanDmaDispatch {
    std::function<VkResult(VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore*)> createSemaphore;
    std::function<void(VkDevice, VkSemaphore, const VkAllocationCallbacks*)> destroySemaphore;
    std::function<VkResult(VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*)> allocateCommandBuffers;
    std::function<void(VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer*)> freeCommandBuffers;
    std::function<VkResult(VkCommandBuffer, const VkCommandBufferBeginInfo*)> beginCommandBuffer;
    std::function<void(VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy*)> cmdCopyBuffer;
    std::function<VkResult(VkCommandBuffer)> endCommandBuffer;
    std::function<VkResult(VkDevice, uint32_t, const VkMappedMemoryRange*)> flushMappedMemoryRanges;
    std::function<VkResult(VkQueue, uint32_t, const VkSubmitInfo*, VkFence)> queueSubmit;
    std::function<VkResult(VkDevice, const VkSemaphoreWaitInfo*, uint64_t)> waitSemaphores;
    std::function<VkResult(VkDevice, VkSemaphore, uint64_t*)> getSemaphoreCounterValue;
    std::function<VkResult(VkQueue)> queueWaitIdle;
};

class VulkanDmaManager {
public:
    VulkanDmaManager();
    ~VulkanDmaManager();

    void SetDispatchForTesting(VulkanDmaDispatch dispatch);
    void Initialize(VkDevice device, VkQueue transferQueue, VkCommandPool commandPool,
        VkBuffer stagingBuffer, VkDeviceMemory stagingMemory, void* mappedData,
        uint64_t stagingSize, VkBuffer deprecatedGlobalVramBuffer, std::mutex* queueMutex,
        std::mutex* commandPoolMutex = nullptr);

    void UpdateStagingBuffer(VkBuffer stagingBuffer, VkDeviceMemory stagingMemory, void* mappedData) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stagingVkBuffer = stagingBuffer;
        m_stagingVkDeviceMemory = stagingMemory;
        m_mappedStagingBuffer = mappedData;
    }

    void Cleanup();
    VkResult Drain();
    size_t GetPendingTransferCount() const;
    bool IsAcceptingSubmissions() const { return m_device != VK_NULL_HANDLE && !m_deviceLost; }
    size_t GetLiveCommandBufferCount() const { return m_liveCommandBuffers.load(); }
    DmaUploadResult UploadMeshAsync(const void* meshData, uint32_t sizeInBytes,
        const VramAllocationInfo& destination, VkBuffer destBuffer);
    VkResult QueryTimelineCompletion(uint64_t targetValue, bool& completed);
    VkSemaphore GetTimelineSemaphore() const { return m_transferTimeline; }
    uint64_t GetSubmittedTimelineValue() const;

private:
    struct PendingTransfer { uint64_t timelineId; VkCommandBuffer cmd; };
    void FreeUnsubmittedCommandBuffer(VkCommandBuffer cmd);
    VkResult GetCompletedTimelineValue(uint64_t& value) const;

    mutable std::mutex m_mutex;
    uint64_t m_stagingBufferSize = 0;
    uint64_t m_headOffset = 0;
    void* m_mappedStagingBuffer = nullptr;
    VkDevice m_device = VK_NULL_HANDLE;
    VkBuffer m_stagingVkBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingVkDeviceMemory = VK_NULL_HANDLE;
    VkQueue m_transferQueue = VK_NULL_HANDLE;
    VkCommandPool m_transferPool = VK_NULL_HANDLE;
    VkBuffer m_globalVramBuffer = VK_NULL_HANDLE;
    std::mutex* m_queueMutex = nullptr;
    VkSemaphore m_transferTimeline = VK_NULL_HANDLE;
    std::mutex* m_commandPoolMutex = nullptr;
    uint64_t m_currentTimelineValue = 0;
    std::vector<PendingTransfer> m_pendingTransfers;
    std::atomic<uint32_t> m_liveCommandBuffers{0};
    VulkanDmaDispatch m_dispatch;
    bool m_deviceLost = false;
};

} // namespace fw
