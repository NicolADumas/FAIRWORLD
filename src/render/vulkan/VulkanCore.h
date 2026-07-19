#pragma once
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include <string>
#include <mutex>

class XrManager;

namespace fw {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    std::optional<uint32_t> transferFamily;

    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value() && transferFamily.has_value();
    }
};

class VulkanCore {
public:
    VulkanCore();
    ~VulkanCore();

    bool Initialize(bool isVRMode, XrManager* xrManager, void* hwnd, void* hinstance);
    void Cleanup();
    void RecreateSwapchain(void* hwnd);

    // Getters per l'Ape Regina (RenderManager)
    VkInstance GetInstance() const { return m_instance; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }
    VkDevice GetDevice() const { return m_device; }
    VkSurfaceKHR GetSurface() const { return m_surface; }
    VkSwapchainKHR GetSwapchain() const { return m_swapchain; }
    VkExtent2D GetSwapchainExtent() const { return m_swapchainExtent; }
    VkFormat GetSwapchainImageFormat() const { return m_swapchainImageFormat; }
    const std::vector<VkImage>& GetSwapchainImages() const { return m_swapchainImages; }
    const std::vector<VkImageView>& GetSwapchainImageViews() const { return m_swapchainImageViews; }
    
    VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }
    VkQueue GetPresentQueue() const { return m_presentQueue; }
    VkQueue GetTransferQueue() const { return m_transferQueue; }
    std::mutex* GetQueueMutex() { return &m_queueMutex; }
    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;

private:
    bool m_isVRMode{ false };
    void* m_hwnd{ nullptr };

    VkInstance m_instance{ VK_NULL_HANDLE };
    VkPhysicalDevice m_physicalDevice{ VK_NULL_HANDLE };
    VkDevice m_device{ VK_NULL_HANDLE };

    std::mutex m_queueMutex;
    VkQueue m_graphicsQueue{ VK_NULL_HANDLE };
    VkQueue m_presentQueue{ VK_NULL_HANDLE };
    VkQueue m_transferQueue{ VK_NULL_HANDLE };

    VkSurfaceKHR m_surface{ VK_NULL_HANDLE };
    VkSwapchainKHR m_swapchain{ VK_NULL_HANDLE };
    std::vector<VkImage> m_swapchainImages;
    VkFormat m_swapchainImageFormat;
    VkExtent2D m_swapchainExtent;
    std::vector<VkImageView> m_swapchainImageViews;

#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif
    const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };

    bool CheckValidationLayerSupport();
    std::vector<const char*> GetRequiredExtensions(XrManager* xrManager);
    bool CreateVulkanInstance(XrManager* xrManager);
    bool PickPhysicalDevice(XrManager* xrManager);
    int RateDeviceSuitability(VkPhysicalDevice device);
    bool CreateLogicalDevice();
    bool CreateSurface(void* hwnd, void* hinstance);
    bool CreateSwapchain(void* hwnd);
    bool CreateImageViews();
};

} // namespace fw
