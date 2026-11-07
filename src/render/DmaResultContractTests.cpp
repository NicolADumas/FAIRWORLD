#include "VulkanDmaManager.h"
#include <array>
#include <iostream>

namespace {
template<class T> T Handle(uintptr_t value) { return reinterpret_cast<T>(value); }

struct FakeVulkan {
    VkResult counterResult = VK_SUCCESS;
    VkResult allocateResult = VK_SUCCESS;
    VkResult beginResult = VK_SUCCESS;
    VkResult flushResult = VK_SUCCESS;
    VkResult endResult = VK_SUCCESS;
    VkResult submitResult = VK_SUCCESS;
    VkResult waitResult = VK_SUCCESS;
    VkResult queueIdleResult = VK_SUCCESS;
    uint64_t counter = 0;
    uint32_t allocCalls = 0, freeCalls = 0, submitCalls = 0, copyCalls = 0;
    bool semaphoreDestroyed = false;
    uintptr_t nextCmd = 10, nextSemaphore = 100;

    fw::VulkanDmaDispatch Dispatch() {
        fw::VulkanDmaDispatch d;
        d.createSemaphore = [this](VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore* out) {
            *out = Handle<VkSemaphore>(nextSemaphore++); return VK_SUCCESS;
        };
        d.destroySemaphore = [this](VkDevice, VkSemaphore, const VkAllocationCallbacks*) { semaphoreDestroyed = true; };
        d.allocateCommandBuffers = [this](VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer* out) {
            ++allocCalls; if (allocateResult == VK_SUCCESS) *out = Handle<VkCommandBuffer>(nextCmd++); return allocateResult;
        };
        d.freeCommandBuffers = [this](VkDevice, VkCommandPool, uint32_t count, const VkCommandBuffer*) { freeCalls += count; };
        d.beginCommandBuffer = [this](VkCommandBuffer, const VkCommandBufferBeginInfo*) { return beginResult; };
        d.cmdCopyBuffer = [this](VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy*) { ++copyCalls; };
        d.endCommandBuffer = [this](VkCommandBuffer) { return endResult; };
        d.flushMappedMemoryRanges = [this](VkDevice, uint32_t, const VkMappedMemoryRange*) { return flushResult; };
        d.queueSubmit = [this](VkQueue, uint32_t, const VkSubmitInfo*, VkFence) { ++submitCalls; return submitResult; };
        d.waitSemaphores = [this](VkDevice, const VkSemaphoreWaitInfo*, uint64_t) {
            if (waitResult == VK_SUCCESS) ++counter;
            return waitResult;
        };
        d.getSemaphoreCounterValue = [this](VkDevice, VkSemaphore, uint64_t* out) {
            if (counterResult == VK_SUCCESS) *out = counter;
            return counterResult;
        };
        d.queueWaitIdle = [this](VkQueue) { if (queueIdleResult == VK_SUCCESS) counter = UINT64_MAX; return queueIdleResult; };
        return d;
    }
};

struct Fixture {
    FakeVulkan fake;
    fw::VulkanDmaManager dma;
    std::array<unsigned char, 128> staging{};
    Fixture() {
        dma.SetDispatchForTesting(fake.Dispatch());
        dma.Initialize(Handle<VkDevice>(1), Handle<VkQueue>(2), Handle<VkCommandPool>(3),
            Handle<VkBuffer>(4), Handle<VkDeviceMemory>(5), staging.data(), staging.size(), VK_NULL_HANDLE, nullptr);
    }
    fw::VramAllocationInfo Allocation() const { return {0, 0, 64, 0, true}; }
    fw::DmaUploadResult Upload() {
        const std::array<unsigned char, 16> src{};
        return dma.UploadMeshAsync(src.data(), static_cast<uint32_t>(src.size()), Allocation(), Handle<VkBuffer>(6));
    }
};

bool Check(bool condition, const char* name) {
    if (!condition) std::cerr << "FAIL " << name << "\n";
    else std::cout << "PASS " << name << "\n";
    return condition;
}
}

int main() {
    int passed = 0, failed = 0;
    auto run = [&](bool ok, const char* name) { (ok ? passed : failed)++; Check(ok, name); };

    { Fixture f; auto a = f.Allocation(); auto r = f.dma.UploadMeshAsync(nullptr, 16, a, Handle<VkBuffer>(6));
      run(r.status == fw::DmaUploadStatus::InvalidInput && f.fake.allocCalls == 0, "invalid input rejected before Vulkan work"); }
    { Fixture f; f.fake.allocateResult = VK_ERROR_OUT_OF_HOST_MEMORY; auto r = f.Upload();
      run(r.status == fw::DmaUploadStatus::CommandBufferAllocationFailed && f.dma.GetLiveCommandBufferCount() == 0, "command buffer allocation failure"); }
    { Fixture f; f.fake.beginResult = VK_ERROR_UNKNOWN; auto r = f.Upload();
      run(r.status == fw::DmaUploadStatus::CommandBufferBeginFailed && f.fake.freeCalls == 1 && f.dma.GetLiveCommandBufferCount() == 0, "begin failure releases command buffer"); }
    { Fixture f; f.fake.endResult = VK_ERROR_UNKNOWN; auto r = f.Upload();
      run(r.status == fw::DmaUploadStatus::CommandBufferEndFailed && f.fake.freeCalls == 1 && f.fake.submitCalls == 0, "end failure releases without submit"); }
    { Fixture f; f.fake.flushResult = VK_ERROR_MEMORY_MAP_FAILED; auto r = f.Upload();
      run(r.status == fw::DmaUploadStatus::FlushFailed && f.fake.allocCalls == 0, "flush failure stops before command allocation"); }
    { Fixture f; f.fake.submitResult = VK_ERROR_OUT_OF_HOST_MEMORY; auto r = f.Upload();
      run(r.status == fw::DmaUploadStatus::SubmitFailed && r.submission == fw::DmaSubmissionState::SubmitFailed && f.fake.freeCalls == 1 && f.dma.GetPendingTransferCount() == 0, "submit failure is not completion and releases non-pending command"); }
    { Fixture f; f.fake.submitResult = VK_ERROR_DEVICE_LOST; auto r = f.Upload();
      run(r.status == fw::DmaUploadStatus::DeviceLost && !f.dma.IsAcceptingSubmissions() && f.fake.freeCalls == 0, "device lost submission is separately retained"); }
    { Fixture f; f.fake.waitResult = VK_ERROR_UNKNOWN; auto r = f.Upload();
      auto blocked = f.Upload();
      bool complete = true;
      const VkResult earlyQuery = f.dma.QueryTimelineCompletion(r.timelineTarget, complete);
      run(r.status == fw::DmaUploadStatus::WaitFailed && r.mayStillBeInUse() && f.dma.GetPendingTransferCount() == 1 &&
          blocked.status == fw::DmaUploadStatus::PendingTransferNotConfirmed && f.fake.freeCalls == 0 &&
          earlyQuery == VK_SUCCESS && !complete, "wait failure retains transfer and prevents staging reuse until timeline confirmation");
      f.fake.waitResult = VK_SUCCESS;
      run(f.dma.Drain() == VK_SUCCESS && f.fake.freeCalls == 1 && f.dma.GetPendingTransferCount() == 0, "confirmed drain releases pending command buffer"); }
    { Fixture f; f.fake.counterResult = VK_ERROR_UNKNOWN; auto r = f.Upload();
      run(r.status == fw::DmaUploadStatus::CounterQueryFailed && f.fake.allocCalls == 0, "timeline counter query failure is propagated"); }
    { Fixture f; auto r = f.Upload();
      run(r.completed() && r.timelineTarget == 1 && f.fake.freeCalls == 1 && f.dma.GetPendingTransferCount() == 0, "completed upload requires successful wait"); }
    { Fixture f; f.fake.waitResult = VK_ERROR_DEVICE_LOST; auto r = f.Upload();
      run(r.status == fw::DmaUploadStatus::DeviceLost && r.mayStillBeInUse() && f.fake.freeCalls == 0, "wait device lost retains possibly pending command buffer"); }
    { Fixture f; f.fake.waitResult = VK_ERROR_UNKNOWN; auto r = f.Upload(); (void)r;
      f.fake.queueIdleResult = VK_ERROR_UNKNOWN;
      run(f.dma.Drain() == VK_ERROR_UNKNOWN && f.fake.freeCalls == 0 && f.dma.GetPendingTransferCount() == 1, "failed queue idle does not release pending command buffer"); }
    { Fixture f; f.fake.waitResult = VK_ERROR_UNKNOWN; auto r = f.Upload(); (void)r;
      f.fake.queueIdleResult = VK_ERROR_DEVICE_LOST;
      run(f.dma.Drain() == VK_ERROR_DEVICE_LOST && !f.dma.IsAcceptingSubmissions() &&
          f.fake.freeCalls == 0 && f.dma.GetPendingTransferCount() == 1, "queue idle device lost is distinct and retains pending command buffer"); }

    std::cout << "DMA RESULT CONTRACT: " << passed << "/" << (passed + failed) << " PASS\n";
    return failed == 0 ? 0 : 1;
}
