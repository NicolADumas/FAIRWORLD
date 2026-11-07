#include "pch.h"
#include "VulkanDmaManager.h"
#include <iostream>
#include <cstring>
#include <limits>
#include "VulkanResourceTracker.h"
#include "vulkan/VulkanForensicAudit.h"

namespace fw {

VulkanDmaManager::VulkanDmaManager() = default;

VulkanDmaManager::~VulkanDmaManager() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_pendingTransfers.empty()) {
        std::cerr << "[VulkanDmaManager] ERROR: retaining timeline and command buffers with "
                  << m_pendingTransfers.size() << " unconfirmed transfer(s).\n";
        return;
    }
    if (m_device != VK_NULL_HANDLE && m_transferTimeline != VK_NULL_HANDLE) {
        if (m_dispatch.destroySemaphore) m_dispatch.destroySemaphore(m_device, m_transferTimeline, nullptr);
        else fw::forensic::InterceptDestroySemaphore("VulkanDmaManager::~VulkanDmaManager", m_device, m_transferTimeline, nullptr);
        fw::VulkanResourceTracker::Get().TrackDestroy(m_transferTimeline);
        m_transferTimeline = VK_NULL_HANDLE;
    }
}

void VulkanDmaManager::SetDispatchForTesting(VulkanDmaDispatch dispatch) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_device != VK_NULL_HANDLE || m_transferTimeline != VK_NULL_HANDLE) {
        throw std::logic_error("DMA test dispatch must be set before Initialize");
    }
    m_dispatch = std::move(dispatch);
}

void VulkanDmaManager::Initialize(VkDevice device, VkQueue transferQueue, VkCommandPool transferPool,
    VkBuffer stagingBuffer, VkDeviceMemory stagingMemory, void* mappedStaging, uint64_t stagingSize,
    VkBuffer deprecatedGlobalVramBuffer, std::mutex* queueMutex, std::mutex* commandPoolMutex) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_device = device;
    m_transferQueue = transferQueue;
    m_transferPool = transferPool;
    m_stagingVkBuffer = stagingBuffer;
    m_stagingVkDeviceMemory = stagingMemory;
    m_mappedStagingBuffer = mappedStaging;
    m_stagingBufferSize = stagingSize;
    m_globalVramBuffer = deprecatedGlobalVramBuffer;
    m_queueMutex = queueMutex;
    m_commandPoolMutex = commandPoolMutex;
    m_deviceLost = false;

    VkSemaphoreTypeCreateInfo timelineCreateInfo{};
    timelineCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    createInfo.pNext = &timelineCreateInfo;
    VkResult result = m_dispatch.createSemaphore
        ? m_dispatch.createSemaphore(m_device, &createInfo, nullptr, &m_transferTimeline)
        : fw::forensic::InterceptCreateSemaphore("VulkanDmaManager::Initialize", "DMATransferTimeline", "VulkanDmaManager", m_device, &createInfo, nullptr, &m_transferTimeline);
    if (result != VK_SUCCESS || m_transferTimeline == VK_NULL_HANDLE) {
        m_transferTimeline = VK_NULL_HANDLE;
        std::cerr << "[Vulkan DMA] Timeline creation failed: " << result << "\n";
        if (result == VK_ERROR_DEVICE_LOST) m_deviceLost = true;
        return;
    }
    fw::VulkanResourceTracker::Get().TrackCreate(m_transferTimeline, "DMATransferTimeline", "VulkanDmaManager");
}

size_t VulkanDmaManager::GetPendingTransferCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pendingTransfers.size();
}

uint64_t VulkanDmaManager::GetSubmittedTimelineValue() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentTimelineValue;
}

void VulkanDmaManager::FreeUnsubmittedCommandBuffer(VkCommandBuffer cmd) {
    if (cmd == VK_NULL_HANDLE) return;
    if (m_commandPoolMutex) {
        std::lock_guard<std::mutex> poolLock(*m_commandPoolMutex);
        if (m_dispatch.freeCommandBuffers) m_dispatch.freeCommandBuffers(m_device, m_transferPool, 1, &cmd);
        else vkFreeCommandBuffers(m_device, m_transferPool, 1, &cmd);
    } else if (m_dispatch.freeCommandBuffers) m_dispatch.freeCommandBuffers(m_device, m_transferPool, 1, &cmd);
    else vkFreeCommandBuffers(m_device, m_transferPool, 1, &cmd);
    --m_liveCommandBuffers;
}

VkResult VulkanDmaManager::GetCompletedTimelineValue(uint64_t& value) const {
    if (m_dispatch.getSemaphoreCounterValue) return m_dispatch.getSemaphoreCounterValue(m_device, m_transferTimeline, &value);
    return vkGetSemaphoreCounterValue(m_device, m_transferTimeline, &value);
}

VkResult VulkanDmaManager::Drain() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_device == VK_NULL_HANDLE || m_deviceLost) return VK_ERROR_DEVICE_LOST;
    if (m_pendingTransfers.empty()) return VK_SUCCESS;
    if (m_transferQueue == VK_NULL_HANDLE) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result;
    if (m_queueMutex) {
        std::lock_guard<std::mutex> queueLock(*m_queueMutex);
        result = m_dispatch.queueWaitIdle ? m_dispatch.queueWaitIdle(m_transferQueue) : vkQueueWaitIdle(m_transferQueue);
    } else {
        result = m_dispatch.queueWaitIdle ? m_dispatch.queueWaitIdle(m_transferQueue) : vkQueueWaitIdle(m_transferQueue);
    }
    if (result != VK_SUCCESS) {
        if (result == VK_ERROR_DEVICE_LOST) m_deviceLost = true;
        std::cerr << "[VulkanDmaManager] Drain could not confirm queue idle: " << result << "\n";
        return result;
    }
    for (const auto& transfer : m_pendingTransfers) FreeUnsubmittedCommandBuffer(transfer.cmd);
    m_pendingTransfers.clear();
    m_headOffset = 0;
    return VK_SUCCESS;
}

VkResult VulkanDmaManager::QueryTimelineCompletion(uint64_t targetValue, bool& completed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    completed = false;
    if (m_deviceLost) return VK_ERROR_DEVICE_LOST;
    if (m_device == VK_NULL_HANDLE || m_transferTimeline == VK_NULL_HANDLE) return VK_ERROR_INITIALIZATION_FAILED;
    uint64_t counter = 0;
    const VkResult result = GetCompletedTimelineValue(counter);
    if (result != VK_SUCCESS) {
        if (result == VK_ERROR_DEVICE_LOST) m_deviceLost = true;
        return result;
    }
    completed = counter >= targetValue;
    if (!completed) return VK_SUCCESS;
    auto it = m_pendingTransfers.begin();
    while (it != m_pendingTransfers.end()) {
        if (counter >= it->timelineId) {
            FreeUnsubmittedCommandBuffer(it->cmd);
            it = m_pendingTransfers.erase(it);
        } else ++it;
    }
    if (m_pendingTransfers.empty()) m_headOffset = 0;
    return VK_SUCCESS;
}

void VulkanDmaManager::Cleanup() {
    const VkResult drainResult = Drain();
    if (drainResult != VK_SUCCESS) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_device != VK_NULL_HANDLE && m_transferTimeline != VK_NULL_HANDLE) {
        if (m_dispatch.destroySemaphore) m_dispatch.destroySemaphore(m_device, m_transferTimeline, nullptr);
        else fw::forensic::InterceptDestroySemaphore("VulkanDmaManager::Cleanup", m_device, m_transferTimeline, nullptr);
        fw::VulkanResourceTracker::Get().TrackDestroy(m_transferTimeline);
    }
    m_transferTimeline = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
}

DmaUploadResult VulkanDmaManager::UploadMeshAsync(const void* meshData, uint32_t sizeInBytes,
    const VramAllocationInfo& destination, VkBuffer destBuffer) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto result = [](DmaUploadStatus status, DmaSubmissionState submission, VkResult vkResult, uint64_t target = 0) {
        return DmaUploadResult{status, submission, vkResult, target};
    };
    if (m_deviceLost) return result(DmaUploadStatus::DeviceLost, DmaSubmissionState::DeviceLost, VK_ERROR_DEVICE_LOST);
    if (!meshData || sizeInBytes == 0 || !destination.valid || destination.size == 0 ||
        sizeInBytes > destination.size || destination.offset > UINT32_MAX - sizeInBytes ||
        static_cast<uint64_t>(destination.offset) + sizeInBytes > 256ULL * 1024ULL * 1024ULL ||
        m_device == VK_NULL_HANDLE || m_transferQueue == VK_NULL_HANDLE || m_transferPool == VK_NULL_HANDLE ||
        m_transferTimeline == VK_NULL_HANDLE || m_stagingVkBuffer == VK_NULL_HANDLE ||
        m_stagingVkDeviceMemory == VK_NULL_HANDLE || !m_mappedStagingBuffer ||
        destBuffer == VK_NULL_HANDLE || m_stagingBufferSize == 0 || sizeInBytes > m_stagingBufferSize) {
        return result(DmaUploadStatus::InvalidInput, DmaSubmissionState::NotSubmitted, VK_ERROR_INITIALIZATION_FAILED);
    }

    uint64_t completedValue = 0;
    VkResult vkResult = GetCompletedTimelineValue(completedValue);
    if (vkResult != VK_SUCCESS) {
        if (vkResult == VK_ERROR_DEVICE_LOST) {
            m_deviceLost = true;
            return result(DmaUploadStatus::DeviceLost, DmaSubmissionState::DeviceLost, vkResult);
        }
        return result(DmaUploadStatus::CounterQueryFailed, DmaSubmissionState::NotSubmitted, vkResult);
    }
    auto it = m_pendingTransfers.begin();
    while (it != m_pendingTransfers.end()) {
        if (completedValue >= it->timelineId) {
            FreeUnsubmittedCommandBuffer(it->cmd);
            it = m_pendingTransfers.erase(it);
        } else ++it;
    }
    // Until every prior transfer is confirmed, the single staging range is owned by it.
    if (!m_pendingTransfers.empty()) {
        return result(DmaUploadStatus::PendingTransferNotConfirmed, DmaSubmissionState::SubmittedButNotConfirmed, VK_NOT_READY,
            m_pendingTransfers.front().timelineId);
    }
    m_headOffset = 0;
    if (sizeInBytes > m_stagingBufferSize - m_headOffset) {
        return result(DmaUploadStatus::InvalidInput, DmaSubmissionState::NotSubmitted, VK_ERROR_OUT_OF_DEVICE_MEMORY);
    }
    const uint64_t stagingOffset = m_headOffset;
    std::memcpy(static_cast<unsigned char*>(m_mappedStagingBuffer) + stagingOffset, meshData, sizeInBytes);

    VkMappedMemoryRange flushRange{};
    flushRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    flushRange.memory = m_stagingVkDeviceMemory;
    flushRange.offset = 0;
    flushRange.size = VK_WHOLE_SIZE;
    vkResult = m_dispatch.flushMappedMemoryRanges
        ? m_dispatch.flushMappedMemoryRanges(m_device, 1, &flushRange)
        : vkFlushMappedMemoryRanges(m_device, 1, &flushRange);
    if (vkResult != VK_SUCCESS) {
        if (vkResult == VK_ERROR_DEVICE_LOST) {
            m_deviceLost = true;
            return result(DmaUploadStatus::DeviceLost, DmaSubmissionState::DeviceLost, vkResult);
        }
        return result(DmaUploadStatus::FlushFailed, DmaSubmissionState::NotSubmitted, vkResult);
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_transferPool;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (m_commandPoolMutex) {
        std::lock_guard<std::mutex> poolLock(*m_commandPoolMutex);
        vkResult = m_dispatch.allocateCommandBuffers
            ? m_dispatch.allocateCommandBuffers(m_device, &allocInfo, &cmd)
            : vkAllocateCommandBuffers(m_device, &allocInfo, &cmd);
    } else {
        vkResult = m_dispatch.allocateCommandBuffers
            ? m_dispatch.allocateCommandBuffers(m_device, &allocInfo, &cmd)
            : vkAllocateCommandBuffers(m_device, &allocInfo, &cmd);
    }
    if (vkResult != VK_SUCCESS || cmd == VK_NULL_HANDLE) {
        if (vkResult == VK_ERROR_DEVICE_LOST) {
            m_deviceLost = true;
            return result(DmaUploadStatus::DeviceLost, DmaSubmissionState::DeviceLost, vkResult);
        }
        return result(DmaUploadStatus::CommandBufferAllocationFailed, DmaSubmissionState::NotSubmitted,
            vkResult == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : vkResult);
    }
    ++m_liveCommandBuffers;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResult = m_dispatch.beginCommandBuffer ? m_dispatch.beginCommandBuffer(cmd, &beginInfo) : vkBeginCommandBuffer(cmd, &beginInfo);
    if (vkResult != VK_SUCCESS) {
        FreeUnsubmittedCommandBuffer(cmd);
        if (vkResult == VK_ERROR_DEVICE_LOST) {
            m_deviceLost = true;
            return result(DmaUploadStatus::DeviceLost, DmaSubmissionState::DeviceLost, vkResult);
        }
        return result(DmaUploadStatus::CommandBufferBeginFailed, DmaSubmissionState::NotSubmitted, vkResult);
    }
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = stagingOffset;
    copyRegion.dstOffset = destination.offset;
    copyRegion.size = sizeInBytes;
    if (m_dispatch.cmdCopyBuffer) m_dispatch.cmdCopyBuffer(cmd, m_stagingVkBuffer, destBuffer, 1, &copyRegion);
    else vkCmdCopyBuffer(cmd, m_stagingVkBuffer, destBuffer, 1, &copyRegion);

    vkResult = m_dispatch.endCommandBuffer ? m_dispatch.endCommandBuffer(cmd) : vkEndCommandBuffer(cmd);
    if (vkResult != VK_SUCCESS) {
        FreeUnsubmittedCommandBuffer(cmd);
        if (vkResult == VK_ERROR_DEVICE_LOST) {
            m_deviceLost = true;
            return result(DmaUploadStatus::DeviceLost, DmaSubmissionState::DeviceLost, vkResult);
        }
        return result(DmaUploadStatus::CommandBufferEndFailed, DmaSubmissionState::NotSubmitted, vkResult);
    }

    // Reserve before submission so host allocation failure cannot lose ownership of a pending command buffer.
    try { m_pendingTransfers.reserve(m_pendingTransfers.size() + 1); }
    catch (...) {
        FreeUnsubmittedCommandBuffer(cmd);
        return result(DmaUploadStatus::CommandBufferAllocationFailed, DmaSubmissionState::NotSubmitted, VK_ERROR_OUT_OF_HOST_MEMORY);
    }
    const uint64_t target = m_currentTimelineValue + 1;
    if (target == 0) {
        FreeUnsubmittedCommandBuffer(cmd);
        return result(DmaUploadStatus::InvalidInput, DmaSubmissionState::NotSubmitted, VK_ERROR_TOO_MANY_OBJECTS);
    }
    VkTimelineSemaphoreSubmitInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.signalSemaphoreValueCount = 1;
    timelineInfo.pSignalSemaphoreValues = &target;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = &timelineInfo;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_transferTimeline;
    if (m_queueMutex) {
        std::lock_guard<std::mutex> queueLock(*m_queueMutex);
        vkResult = m_dispatch.queueSubmit ? m_dispatch.queueSubmit(m_transferQueue, 1, &submitInfo, VK_NULL_HANDLE)
                                          : vkQueueSubmit(m_transferQueue, 1, &submitInfo, VK_NULL_HANDLE);
    } else {
        vkResult = m_dispatch.queueSubmit ? m_dispatch.queueSubmit(m_transferQueue, 1, &submitInfo, VK_NULL_HANDLE)
                                          : vkQueueSubmit(m_transferQueue, 1, &submitInfo, VK_NULL_HANDLE);
    }
    if (vkResult != VK_SUCCESS) {
        if (vkResult == VK_ERROR_DEVICE_LOST) {
            // DEVICE_LOST does not prove that the driver retired the submitted command.
            m_pendingTransfers.push_back({target, cmd});
            m_deviceLost = true;
            return result(DmaUploadStatus::DeviceLost, DmaSubmissionState::DeviceLost, vkResult, target);
        }
        FreeUnsubmittedCommandBuffer(cmd);
        return result(DmaUploadStatus::SubmitFailed, DmaSubmissionState::SubmitFailed, vkResult, target);
    }
    m_currentTimelineValue = target;
    m_pendingTransfers.push_back({target, cmd});

    VkSemaphoreWaitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &m_transferTimeline;
    waitInfo.pValues = &target;
    vkResult = m_dispatch.waitSemaphores ? m_dispatch.waitSemaphores(m_device, &waitInfo, UINT64_MAX)
                                         : vkWaitSemaphores(m_device, &waitInfo, UINT64_MAX);
    if (vkResult == VK_SUCCESS) {
        FreeUnsubmittedCommandBuffer(cmd);
        m_pendingTransfers.pop_back();
        m_headOffset = 0;
        return result(DmaUploadStatus::Completed, DmaSubmissionState::Completed, vkResult, target);
    }
    if (vkResult == VK_ERROR_DEVICE_LOST) {
        m_deviceLost = true;
        return result(DmaUploadStatus::DeviceLost, DmaSubmissionState::DeviceLost, vkResult, target);
    }
    return result(DmaUploadStatus::WaitFailed, DmaSubmissionState::SubmittedButNotConfirmed, vkResult, target);
}

} // namespace fw
