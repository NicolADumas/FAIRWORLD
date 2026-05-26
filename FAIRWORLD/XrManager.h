#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#define XR_USE_PLATFORM_WIN32_KHR
#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

class XrManager {
public:
    XrManager();
    ~XrManager();

    bool Init();
    bool CreateSession(VkInstance instance, VkDevice device);
    void PollEvents(bool& isRunning);
    
    bool BeginFrame();
    void EndFrame();
    void Shutdown();

    // Ritorna i requisiti grafici che OpenXR impone a Vulkan
    void* GetVulkanGraphicsRequirements() { return nullptr; } // TODO: Implementare XrGraphicsRequirementsVulkanKHR

private:
    XrInstance m_xrInstance{XR_NULL_HANDLE};
    XrSession m_xrSession{XR_NULL_HANDLE};
    XrSpace m_appSpace{XR_NULL_HANDLE}; // Spazio di riferimento per la scala 1:1
};
