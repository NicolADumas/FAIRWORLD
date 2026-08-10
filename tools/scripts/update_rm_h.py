import re

file_path = "D:/FAIRWORLD/FAIRWORLD/src/render/RenderManager.h"
with open(file_path, "r") as f:
    content = f.read()

# Add VulkanCore include
content = content.replace('#include "World.h"', '#include "vulkan/VulkanCore.h"\n#include "World.h"')

# Add m_core
if 'std::unique_ptr<fw::VulkanCore> m_core;' not in content:
    content = content.replace('private:', 'private:\n    std::unique_ptr<fw::VulkanCore> m_core;\n')

# Remove duplicate fields and methods that are now in VulkanCore
fields_to_remove = [
    r'VkInstance m_instance\{ VK_NULL_HANDLE \};',
    r'VkPhysicalDevice m_physicalDevice\{ VK_NULL_HANDLE \};',
    r'VkDevice m_device\{ VK_NULL_HANDLE \};',
    r'std::mutex m_queueMutex;',
    r'VkQueue m_graphicsQueue\{ VK_NULL_HANDLE \};',
    r'VkQueue m_presentQueue\{ VK_NULL_HANDLE \};',
    r'VkQueue m_transferQueue\{ VK_NULL_HANDLE \};',
    r'VkSurfaceKHR m_surface\{ VK_NULL_HANDLE \};',
    r'VkSwapchainKHR m_swapchain\{ VK_NULL_HANDLE \};',
    r'std::vector<VkImage> m_swapchainImages;',
    r'VkFormat m_swapchainImageFormat;',
    r'VkExtent2D m_swapchainExtent;',
    r'std::vector<VkImageView> m_swapchainImageViews;',
    r'const bool enableValidationLayers =.*?;',
    r'#ifdef NDEBUG',
    r'#else',
    r'#endif',
    r'const std::vector<const char\*> validationLayers = \{.*?\}'
]

methods_to_remove = [
    r'bool CheckValidationLayerSupport\(\);',
    r'std::vector<const char\*> GetRequiredExtensions\(XrManager\* xrManager\);',
    r'bool CreateVulkanInstance\(XrManager\* xrManager\);',
    r'bool PickPhysicalDevice\(XrManager\* xrManager\);',
    r'int RateDeviceSuitability\(VkPhysicalDevice device\);',
    r'bool CreateLogicalDevice\(\);',
    r'bool CreateSurface\(void\* hwnd, void\* hinstance\);',
    r'bool CreateSwapchain\(void\* hwnd\);',
    r'bool CreateImageViews\(\);',
    r'QueueFamilyIndices FindQueueFamilies\(VkPhysicalDevice device\);'
]

for field in fields_to_remove:
    content = re.sub(field, '', content, flags=re.DOTALL)

for method in methods_to_remove:
    content = re.sub(method, '', content)

# Fix Getters in RenderManager.h
getter_replacements = {
    r'VkInstance GetVulkanInstance\(\) const \{ return m_instance; \}': 'VkInstance GetVulkanInstance() const { return m_core ? m_core->GetInstance() : VK_NULL_HANDLE; }',
    r'VkPhysicalDevice GetPhysicalDevice\(\) const \{ return m_physicalDevice; \}': 'VkPhysicalDevice GetPhysicalDevice() const { return m_core ? m_core->GetPhysicalDevice() : VK_NULL_HANDLE; }',
    r'VkDevice GetDevice\(\) const \{ return m_device; \}': 'VkDevice GetDevice() const { return m_core ? m_core->GetDevice() : VK_NULL_HANDLE; }',
    r'VkQueue GetTransferQueue\(\) const \{ return m_transferQueue; \}': 'VkQueue GetTransferQueue() const { return m_core ? m_core->GetTransferQueue() : VK_NULL_HANDLE; }',
    r'std::mutex\* GetQueueMutex\(\) \{ return &m_queueMutex; \}': 'std::mutex* GetQueueMutex() { return m_core ? m_core->GetQueueMutex() : nullptr; }'
}

for old, new_ in getter_replacements.items():
    content = re.sub(old, new_, content)

with open(file_path, "w") as f:
    f.write(content)
