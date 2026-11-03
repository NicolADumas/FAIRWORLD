#pragma once
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <string>

namespace fw::forensic {

enum class Phase {
    Startup,
    Runtime,
    Resize,
    Shutdown
};

void SetPhase(Phase phase);
const char* GetPhaseName(Phase phase);
Phase GetPhase();

std::string CaptureCallstack(int skipFrames = 1, int maxFrames = 12);

VkResult InterceptCreateFramebuffer(
    const char* caller,
    VkDevice device,
    const VkFramebufferCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkFramebuffer* pFramebuffer
);

void InterceptDestroyFramebuffer(
    const char* caller,
    VkDevice device,
    VkFramebuffer framebuffer,
    const VkAllocationCallbacks* pAllocator
);

VkResult InterceptCreateSemaphore(
    const char* caller,
    const char* name,
    const char* owner,
    VkDevice device,
    const VkSemaphoreCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSemaphore* pSemaphore
);

void InterceptDestroySemaphore(
    const char* caller,
    VkDevice device,
    VkSemaphore semaphore,
    const VkAllocationCallbacks* pAllocator
);

void InterceptDestroyDevice(
    const char* caller,
    VkDevice device,
    const VkAllocationCallbacks* pAllocator
);

void DumpForensicReport();

void SetupDebugMessenger(VkInstance instance);
void DestroyDebugMessenger(VkInstance instance);
uint32_t GetValidationErrorCount();
void ResetValidationErrorCount();

} // namespace fw::forensic
