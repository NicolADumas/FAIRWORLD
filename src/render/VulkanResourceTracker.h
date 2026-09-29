#pragma once
#include <vulkan/vulkan.h>
#include <mutex>
#include <unordered_map>
#include <string>

namespace fw {

class VulkanResourceTracker {
public:
    static VulkanResourceTracker& Get();

    void Initialize(VkDevice device);
    void Shutdown();

    // Semaphores
    void TrackCreate(VkSemaphore semaphore, const std::string& name, const std::string& owner);
    void TrackDestroy(VkSemaphore semaphore);

    void PrintReport() const;

private:
    VulkanResourceTracker() = default;
    ~VulkanResourceTracker() = default;

    VkDevice m_device = VK_NULL_HANDLE;
    PFN_vkSetDebugUtilsObjectNameEXT m_pfnSetDebugUtilsObjectNameEXT = nullptr;

    mutable std::mutex m_mutex;
    struct SemaphoreInfo {
        std::string name;
        std::string owner;
    };
    std::unordered_map<VkSemaphore, SemaphoreInfo> m_semaphores;
    
    struct OwnerStats {
        uint32_t created = 0;
        uint32_t destroyed = 0;
    };
    std::unordered_map<std::string, OwnerStats> m_ownerStats;

    uint32_t m_semaphoresCreated = 0;
    uint32_t m_semaphoresDestroyed = 0;
};

} // namespace fw
