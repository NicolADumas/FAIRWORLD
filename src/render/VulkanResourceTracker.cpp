#include "pch.h"
#include "VulkanResourceTracker.h"
#include <iomanip>
#include <iostream>

namespace fw {

VulkanResourceTracker& VulkanResourceTracker::Get() {
    static VulkanResourceTracker instance;
    return instance;
}

void VulkanResourceTracker::Initialize(VkDevice device) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_device = device;
    m_pfnSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT");
    if (!m_pfnSetDebugUtilsObjectNameEXT) {
        std::cout << "[VulkanResourceTracker] vkSetDebugUtilsObjectNameEXT not found (Debug utils not enabled?). Names won't be visible in external debuggers.\n";
    }
}

void VulkanResourceTracker::Shutdown() {
    PrintReport();
}

void VulkanResourceTracker::TrackCreate(VkSemaphore semaphore, const std::string& name, const std::string& owner) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_semaphores[semaphore] = { name, owner };
    m_semaphoresCreated++;
    m_ownerStats[owner].created++;
    
    if (m_device && m_pfnSetDebugUtilsObjectNameEXT) {
        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_SEMAPHORE;
        nameInfo.objectHandle = (uint64_t)semaphore;
        nameInfo.pObjectName = name.c_str();
        m_pfnSetDebugUtilsObjectNameEXT(m_device, &nameInfo);
    }
}

void VulkanResourceTracker::TrackDestroy(VkSemaphore semaphore) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_semaphores.find(semaphore);
    if (it != m_semaphores.end()) {
        m_ownerStats[it->second.owner].destroyed++;
        m_semaphores.erase(it);
        m_semaphoresDestroyed++;
    } else {
        std::cout << "[VulkanResourceTracker] WARNING: Untracked semaphore destroyed: 0x" << std::hex << (uint64_t)semaphore << std::dec << "\n";
    }
}

void VulkanResourceTracker::PrintReport() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::cout << "\n====================================================\n";
    std::cout << " [VulkanResourceTracker][Semaphore]\n";
    std::cout << "====================================================\n";
    std::cout << "CREATED   : " << m_semaphoresCreated << "\n";
    std::cout << "DESTROYED : " << m_semaphoresDestroyed << "\n";
    std::cout << "LIVE      : " << m_semaphores.size() << "\n\n";
    
    std::cout << "BY OWNER:\n";
    for (const auto& pair : m_ownerStats) {
        uint32_t live = pair.second.created - pair.second.destroyed;
        std::cout << "  " << pair.first << "\n";
        std::cout << "    Created   : " << pair.second.created << "\n";
        std::cout << "    Destroyed : " << pair.second.destroyed << "\n";
        std::cout << "    Live      : " << live << "\n\n";
    }

    if (!m_semaphores.empty()) {
        std::cout << "LIVE OBJECTS:\n";
        for (const auto& pair : m_semaphores) {
            std::cout << "  Handle : 0x" << std::hex << (uint64_t)pair.first << std::dec << "\n";
            std::cout << "  Owner  : " << pair.second.owner << "\n";
            std::cout << "  Name   : " << pair.second.name << "\n\n";
        }
    }
    std::cout << "====================================================\n\n";
}

} // namespace fw
