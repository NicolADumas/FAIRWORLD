import os

src_file = "D:/FAIRWORLD/FAIRWORLD/src/render/RenderManager.cpp"
dst_file = "D:/FAIRWORLD/FAIRWORLD/src/render/vulkan/VulkanCore.cpp"

with open(src_file, "r") as f:
    lines = f.readlines()

# Extract VulkanCore methods
core_methods = [
    "CheckValidationLayerSupport()",
    "GetRequiredExtensions(XrManager*",
    "CreateVulkanInstance(XrManager*",
    "CreateSurface(void*",
    "FindQueueFamilies(VkPhysicalDevice",
    "PickPhysicalDevice(XrManager*",
    "RateDeviceSuitability(VkPhysicalDevice",
    "CreateLogicalDevice()",
    "CreateSwapchain(void*",
    "CreateImageViews()"
]

vulkan_core_content = """#include "pch.h"
#include "VulkanCore.h"
#include "vr/XrManager.h"
#include <iostream>
#include <set>
#include <algorithm>

namespace fw {

VulkanCore::VulkanCore() {}
VulkanCore::~VulkanCore() { Cleanup(); }

bool VulkanCore::Initialize(bool isVRMode, XrManager* xrManager, void* hwnd, void* hinstance) {
    m_isVRMode = isVRMode;
    m_hwnd = hwnd;
    
    if (enableValidationLayers && !CheckValidationLayerSupport()) {
        std::cerr << "[VULKAN] Validation layers richiesti non disponibili!\\n";
        return false;
    }

    if (!CreateVulkanInstance(xrManager)) return false;
    if (!CreateSurface(hwnd, hinstance)) return false;
    if (!PickPhysicalDevice(xrManager)) return false;
    if (!CreateLogicalDevice()) return false;
    if (!CreateSwapchain(hwnd)) return false;
    if (!CreateImageViews()) return false;
    
    return true;
}

void VulkanCore::Cleanup() {
    for (auto imageView : m_swapchainImageViews) {
        vkDestroyImageView(m_device, imageView, nullptr);
    }
    m_swapchainImageViews.clear();
    
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

void VulkanCore::RecreateSwapchain(void* hwnd) {
    vkDeviceWaitIdle(m_device);
    for (auto imageView : m_swapchainImageViews) {
        vkDestroyImageView(m_device, imageView, nullptr);
    }
    m_swapchainImageViews.clear();
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
    CreateSwapchain(hwnd);
    CreateImageViews();
}

"""

new_rm_lines = []
skip = False

extracted_code = ""

brace_count = 0
in_target_method = False

for line in lines:
    if not in_target_method:
        is_target = False
        for m in core_methods:
            if f"RenderManager::{m}" in line or (f"QueueFamilyIndices RenderManager::" in line and "FindQueueFamilies" in m):
                is_target = True
                break
        
        if is_target:
            in_target_method = True
            brace_count = line.count("{") - line.count("}")
            extracted_code += line.replace("RenderManager::", "VulkanCore::")
            continue
        
        # Don't add to new RM lines if it's one of the targeted ones
        new_rm_lines.append(line)
    else:
        extracted_code += line.replace("RenderManager::", "VulkanCore::")
        brace_count += line.count("{") - line.count("}")
        if brace_count <= 0:
            in_target_method = False

with open(dst_file, "w") as f:
    f.write(vulkan_core_content + extracted_code + "\\n} // namespace fw\\n")

with open(src_file, "w") as f:
    f.writelines(new_rm_lines)
