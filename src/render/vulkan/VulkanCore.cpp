#include "pch.h"
#include "VulkanCore.h"
#include "XrManager.h"
#include <iostream>
#include <set>
#include <algorithm>
#include <map>

namespace fw {

VulkanCore::VulkanCore() {}
VulkanCore::~VulkanCore() { Cleanup(); }

bool VulkanCore::Initialize(bool isVRMode, XrManager* xrManager, void* hwnd, void* hinstance) {
    m_isVRMode = isVRMode;
    m_hwnd = hwnd;
    
    if (enableValidationLayers && !CheckValidationLayerSupport()) {
        std::cerr << "[VULKAN] Validation layers richiesti non disponibili!\n";
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

bool VulkanCore::CheckValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) {
        bool layerFound = false;
        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }
        if (!layerFound) return false;
    }
    return true;
}
std::vector<const char*> VulkanCore::GetRequiredExtensions(XrManager* xrManager) {
    std::vector<const char*> extensions;

    // Se siamo in VR, OpenXR DEVE fornirci le sue estensioni obbligatorie
    if (m_isVRMode && xrManager) {
        // TODO: Chiamare il metodo di XrManager che incapsula xrGetVulkanInstanceExtensionsKHR
    } else {
        // Modalita Desktop: aggiungiamo le estensioni standard per disegnare su Windows
        extensions.push_back("VK_KHR_surface");
        extensions.push_back("VK_KHR_win32_surface");
    }

    // Estensione per i messaggi di debug dei Validation Layers
    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}
bool VulkanCore::CreateVulkanInstance(XrManager* xrManager) {
    if (enableValidationLayers && !CheckValidationLayerSupport()) {
        std::cerr << "[VULKAN ERROR] Validation layers richiesti, ma non disponibili!" << std::endl;
        return false;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "FAIRWORLD Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "FAIRWORLD";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2; // Usiamo Vulkan 1.2 per i Timeline Semaphore!

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = GetRequiredExtensions(xrManager);
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] vkCreateInstance fallito con codice di errore: " << result << std::endl;
        if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
            std::cerr << "[VULKAN ERROR] -> Il tuo driver grafico non supporta Vulkan 1.2 (richiesto per Timeline Semaphores). Aggiorna i driver o cambia GPU!" << std::endl;
        } else if (result == VK_ERROR_EXTENSION_NOT_PRESENT) {
            std::cerr << "[VULKAN ERROR] -> Un'estensione richiesta non e' supportata!" << std::endl;
        } else if (result == VK_ERROR_LAYER_NOT_PRESENT) {
            std::cerr << "[VULKAN ERROR] -> Un validation layer richiesto non e' presente!" << std::endl;
        }
        return false;
    }

    std::cout << "[VULKAN] Istanza creata con successo (Validation Layers: " 
              << (enableValidationLayers ? "ATTIVI" : "DISATTIVI") << ")." << std::endl;
    return true;
}
bool VulkanCore::CreateSurface(void* hwnd, void* hinstance) {
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hwnd = (HWND)hwnd;
    createInfo.hinstance = (HINSTANCE)hinstance;

    if (vkCreateWin32SurfaceKHR(m_instance, &createInfo, nullptr, &m_surface) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] Impossibile creare la Window Surface!" << std::endl;
        return false;
    }
    std::cout << "[VULKAN] Surface Win32 creata con successo." << std::endl;
    return true;
}
QueueFamilyIndices VulkanCore::FindQueueFamilies(VkPhysicalDevice device) const {
    QueueFamilyIndices indices;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        // Cerca una coda dedicata esclusivamente ai trasferimenti (ottimale per DMA asincrono sui Voxel)
        if ((queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) && !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            indices.transferFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }
        if (indices.isComplete()) break;
        i++;
    }

    // Fallback: se la GPU non ha una coda dedicata (es. vecchie GPU o Intel HD), usiamo la grafica
    if (!indices.transferFamily.has_value() && indices.graphicsFamily.has_value()) {
        indices.transferFamily = indices.graphicsFamily.value();
    }
    
    return indices;
}
bool VulkanCore::PickPhysicalDevice(XrManager* xrManager) {
    if (m_isVRMode && xrManager) {
        // In VR, non scegliamo la GPU.
    } 
    
    if (m_physicalDevice == VK_NULL_HANDLE) {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

        if (deviceCount == 0) {
            std::cerr << "[VULKAN ERROR] Impossibile trovare una GPU con supporto Vulkan!" << std::endl;
            return false;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

        std::multimap<int, VkPhysicalDevice> candidates;

        for (const auto& device : devices) {
            int score = RateDeviceSuitability(device);
            // Non scegliamo GPU che non supportano la surface che abbiamo creato
            if (FindQueueFamilies(device).isComplete()) {
                candidates.insert(std::make_pair(score, device));
            }
        }

        if (!candidates.empty() && candidates.rbegin()->first > 0) {
            m_physicalDevice = candidates.rbegin()->second;
        } else {
            return false;
        }
    }

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &deviceProperties);
    std::cout << "[VULKAN] GPU Selezionata: " << deviceProperties.deviceName << std::endl;

    return true;
}
int VulkanCore::RateDeviceSuitability(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties deviceProperties;
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    int score = 0;
    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    }
    score += deviceProperties.limits.maxImageDimension2D;
    return score;
}
bool VulkanCore::CreateLogicalDevice() {
    QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value(), indices.transferFamily.value() };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{}; // Nessuna feature extra per ora

    // Aggiungiamo i Timeline Semaphores a pNext
    VkPhysicalDeviceTimelineSemaphoreFeatures timelineSemaphoreFeatures{};
    timelineSemaphoreFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    timelineSemaphoreFeatures.timelineSemaphore = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &timelineSemaphoreFeatures;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;

    // Abilitiamo l'estensione Swapchain necessaria per mostrare immagini a schermo e i Timeline Semaphores
    const std::vector<const char*> deviceExtensions = { 
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME
    };
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    // Device Layers sono deprecati da Vulkan 1.0: enabledLayerCount DEVE essere 0
    createInfo.enabledLayerCount = 0;

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] Impossibile creare il Logical Device!" << std::endl;
        return false;
    }

    vkGetDeviceQueue(m_device, indices.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, indices.presentFamily.value(), 0, &m_presentQueue);
    vkGetDeviceQueue(m_device, indices.transferFamily.value(), 0, &m_transferQueue);
    
    std::cout << "[VULKAN] Logical Device e Code (Graphics, Present, Transfer) configurate." << std::endl;
    return true;
}
bool VulkanCore::CreateSwapchain(void* hwnd) {
    RECT rect;
    GetClientRect((HWND)hwnd, &rect);
    VkExtent2D extent = { static_cast<uint32_t>(rect.right - rect.left), static_cast<uint32_t>(rect.bottom - rect.top) };
    
    if (extent.width == 0 || extent.height == 0) {
        extent = { 800, 600 }; 
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = 2; // Double Buffering
    createInfo.imageFormat = VK_FORMAT_B8G8R8A8_SRGB; // Formato standard dei pixel
    createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // V-Sync attivo
    createInfo.clipped = VK_TRUE;

    std::cout << "[DEBUG] Chiamata a vkCreateSwapchainKHR in corso... (Se l'errore di validazione appare qui sotto, è colpa di un overlay esterno come Steam/Discord/OBS!)" << std::endl;
    
    if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] Impossibile creare la Swapchain!" << std::endl;
        return false;
    }

    uint32_t imageCount;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());

    m_swapchainImageFormat = VK_FORMAT_B8G8R8A8_SRGB;
    m_swapchainExtent = extent;

    std::cout << "[VULKAN] Swapchain creata (" << imageCount << " immagini)." << std::endl;
    return true;
}
bool VulkanCore::CreateImageViews() {
    m_swapchainImageViews.resize(m_swapchainImages.size());

    for (size_t i = 0; i < m_swapchainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_swapchainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_swapchainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_device, &createInfo, nullptr, &m_swapchainImageViews[i]) != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}
} // namespace fw