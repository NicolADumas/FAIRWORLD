#include "pch.h"
#include "RenderManager.h"
#include "FAIRWORLD.h"
#include "AssetManager.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "XrManager.h"
#include <map>
#include <set>
#include <fstream>
#include "json.hpp"
#include "MobManager.h"
#include "SharedContext.h"
#include "TimeManager.h"
#include "ForgeWorld.h"
#include "ForgeComponents.h"
#include <algorithm>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_vulkan.h>
#include <fstream>
#include <sstream>





RenderManager::RenderManager() : m_isVRMode(false) {}

VkDeviceMemory RenderManager::GetStagingDeviceMemory() const {
    if (m_vmaAllocator != VK_NULL_HANDLE && m_stagingAllocation != VK_NULL_HANDLE) {
        VmaAllocationInfo allocInfo;
        vmaGetAllocationInfo(m_vmaAllocator, m_stagingAllocation, &allocInfo);
        return allocInfo.deviceMemory;
    }
    return VK_NULL_HANDLE;
}

RenderManager::~RenderManager() {
    Shutdown();
}

bool RenderManager::Init(bool isVRMode, XrManager* xrManager, void* hwnd, void* hinstance) {
    m_isVRMode = isVRMode;
    m_hwnd = hwnd;

    if (!CreateVulkanInstance(xrManager)) return false;

    // FASE 3.2: Creiamo la Surface prima di scegliere la GPU definitiva e il Device
    // perché dobbiamo assicurarci che la GPU sappia disegnare su QUESTA specifica finestra
    if (!CreateSurface(hwnd, hinstance)) return false;

    if (!PickPhysicalDevice(xrManager)) return false;

    // FASE 3.1: Creazione del Logical Device
    if (!CreateLogicalDevice()) return false;

    // Inizializza VMA (Vulkan Memory Allocator)
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_1;
    if (vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] Impossibile inizializzare VmaAllocator!" << std::endl;
        return false;
    }

    // --- 1. CREATE TRANSFER COMMAND POOL E COMMAND BUFFER ---
    QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = indices.transferFamily.value();
    
    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_transferCommandPool) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] failed to create transfer command pool!" << std::endl;
        return false;
    }
    
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = m_transferCommandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &m_transferCommandBuffer) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] failed to allocate transfer command buffer!" << std::endl;
        return false;
    }
    
    // Inizia subito a registrare
    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(m_transferCommandBuffer, &beginInfo);

    // --- 2. CREATE CHUNK VMA POOL ---
    VkBufferCreateInfo dummyBufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    dummyBufInfo.size = 1024;
    dummyBufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo dummyAllocInfo = {};
    dummyAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    uint32_t memTypeIndex = 0;
    vmaFindMemoryTypeIndexForBufferInfo(m_vmaAllocator, &dummyBufInfo, &dummyAllocInfo, &memTypeIndex);

    VmaPoolCreateInfo vmaPoolInfo = {};
    vmaPoolInfo.memoryTypeIndex = memTypeIndex;
    if (vmaCreatePool(m_vmaAllocator, &vmaPoolInfo, &m_chunkVmaPool) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] failed to create VMA Pool for chunks!" << std::endl;
        return false;
    }

    // --- 3. CREATE RING BUFFER (STAGING PERSISTENTE) ---
    VkBufferCreateInfo stagingBufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    stagingBufInfo.size = STAGING_BUFFER_SIZE;
    stagingBufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    
    VmaAllocationCreateInfo stagingAllocInfo = {};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    
    if (vmaCreateBuffer(m_vmaAllocator, &stagingBufInfo, &stagingAllocInfo, &m_stagingRingBuffer, &m_stagingAllocation, nullptr) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] Impossibile creare lo Staging Ring Buffer!\n";
        return false;
    }
    
    VmaAllocationInfo vmaRingInfo;
    vmaGetAllocationInfo(m_vmaAllocator, m_stagingAllocation, &vmaRingInfo);
    m_mappedStagingData = vmaRingInfo.pMappedData;
    // --- 4. CREATE GLOBAL VRAM BUFFER (512 MB per i chunk) ---
    VkBufferCreateInfo vramBufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    vramBufInfo.size = 512 * 1024 * 1024; // 512 MB
    vramBufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    
    std::vector<uint32_t> uniqueQueueFamilies;
    if (indices.graphicsFamily.has_value()) {
        uniqueQueueFamilies.push_back(indices.graphicsFamily.value());
    }
    if (indices.transferFamily.has_value() && indices.transferFamily.value() != indices.graphicsFamily.value()) {
        uniqueQueueFamilies.push_back(indices.transferFamily.value());
    }

    if (uniqueQueueFamilies.size() > 1) {
        vramBufInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        vramBufInfo.queueFamilyIndexCount = static_cast<uint32_t>(uniqueQueueFamilies.size());
        vramBufInfo.pQueueFamilyIndices = uniqueQueueFamilies.data();
    } else {
        vramBufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    
    VmaAllocationCreateInfo vramAllocInfo = {};
    vramAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    
    if (vmaCreateBuffer(m_vmaAllocator, &vramBufInfo, &vramAllocInfo, &m_globalVramBuffer, &m_globalVramAllocation, nullptr) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] Impossibile creare il Global VRAM Buffer da 512MB!\n";
        return false;
    }

    std::cout << "[VMA] VmaAllocator e Global VRAM Buffer (512MB) inizializzati con successo." << std::endl;

    // FASE 3.3: Creazione della Swapchain
    if (!CreateSwapchain(hwnd)) return false;
    if (!CreateImageViews()) return false;

    // FASE 3.4, 4 e 5: Creazione del Render Loop, Pipeline e UBO
    if (!CreateRenderPass()) return false;
    
    // IMPORTANTE: Creiamo il Command Pool PRIMA delle texture, perché CreateTextureImage 
    // ha bisogno di eseguire comandi (BeginSingleTimeCommands)
    if (!CreateCommandPoolAndBuffer()) return false;
    
    // FASE 6: Texture (Il Texture Painter / Pixel Editor)
    CreateTextureImage();
    CreateTextureImageView();
    CreateTextureSampler();

    if (!CreateDescriptorSetLayout()) return false;
    if (!CreateUniformBuffers()) return false;
    if (!CreateDescriptorPoolAndSets()) return false;

    if (!CreateGraphicsPipeline()) return false;
    if (!CreateForgePipeline()) return false;

    // Depth buffer: creato DOPO la pipeline (ha bisogno del command pool per i layout)
    if (!CreateDepthResources()) {
        std::cerr << "[VULKAN ERROR] Impossibile creare il Depth Buffer!" << std::endl;
        return false;
    }

    if (!CreateFramebuffers()) return false;
    if (!CreateSyncObjects()) return false;

    // Inizializza ImGui dopo che Vulkan è pronto
    InitImGui(hwnd);
    
    std::cout << "[VULKAN] Motore Grafico pronto. Pronti a renderizzare!" << std::endl;

    return true;
}

bool RenderManager::CheckValidationLayerSupport() {
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

std::vector<const char*> RenderManager::GetRequiredExtensions(XrManager* xrManager) {
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

bool RenderManager::CreateVulkanInstance(XrManager* xrManager) {
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

// ---------------------------------------------------------
// STEP 1: CREAZIONE DELLA SURFACE (Il ponte con Windows)
// ---------------------------------------------------------
bool RenderManager::CreateSurface(void* hwnd, void* hinstance) {
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

QueueFamilyIndices RenderManager::FindQueueFamilies(VkPhysicalDevice device) {
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

bool RenderManager::PickPhysicalDevice(XrManager* xrManager) {
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

int RenderManager::RateDeviceSuitability(VkPhysicalDevice device) {
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

// ---------------------------------------------------------
// STEP 2: CREAZIONE DEL LOGICAL DEVICE
// ---------------------------------------------------------
bool RenderManager::CreateLogicalDevice() {
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

// ---------------------------------------------------------
// STEP 3: CREAZIONE DELLA SWAPCHAIN (La Pellicola)
// ---------------------------------------------------------
bool RenderManager::CreateSwapchain(void* hwnd) {
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

bool RenderManager::CreateImageViews() {
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

void RenderManager::RenderStereo(XrManager* xrManager) {
    // TODO: Implementare il render pass stereo per OpenXR
}

// ---------------------------------------------------------
// DEPTH BUFFER HELPERS
// ---------------------------------------------------------

VkFormat RenderManager::FindSupportedFormat(const std::vector<VkFormat>& candidates,
                                             VkImageTiling tiling,
                                             VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);
        if (tiling == VK_IMAGE_TILING_LINEAR  && (props.linearTilingFeatures  & features) == features) return format;
        if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) return format;
    }
    std::cerr << "[VULKAN ERROR] Nessun formato depth supportato!" << std::endl;
    return VK_FORMAT_UNDEFINED;
}

VkFormat RenderManager::FindDepthFormat() {
    return FindSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

bool RenderManager::CreateDepthResources() {
    VkFormat depthFormat = FindDepthFormat();
    if (depthFormat == VK_FORMAT_UNDEFINED) return false;

    // Crea l'immagine depth (stessa dimensione della swapchain)
    CreateImage(m_swapchainExtent.width, m_swapchainExtent.height, 1,
                depthFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY,
                m_depthImage, m_depthImageAllocation);

    // Crea l'image view per il depth (usa solo l'aspetto DEPTH, non STENCIL)
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = m_depthImage;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = depthFormat;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    // Aggiungi l'aspetto STENCIL se supportato dal formato
    if (depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT || depthFormat == VK_FORMAT_D24_UNORM_S8_UINT) {
        viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_depthImageView) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] Impossibile creare la Depth Image View!" << std::endl;
        return false;
    }
    std::cout << "[VULKAN] Depth Buffer creato (" << depthFormat << ")." << std::endl;
    return true;
}

// ---------------------------------------------------------
// STEP 1: RENDER PASS (Cosa fare con i pixel)
// ---------------------------------------------------------
bool RenderManager::CreateRenderPass() {
    // --- Attachment colore ---
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = m_swapchainImageFormat;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // --- Attachment depth/stencil (Stencil Buffer attivo per Portali) ---
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format         = FindDepthFormat();
    depthAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;     // Pulisce Z-buffer
    depthAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;     // Pulisce Stencil a 0 ogni frame
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef; // collega il depth

    // Dependency: include sia color che depth nelle barriere di sincronizzazione
    VkSubpassDependency dependency{};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                             | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                             | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                             | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments    = attachments.data();
    renderPassInfo.subpassCount    = 1;
    renderPassInfo.pSubpasses      = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies   = &dependency;

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] vkCreateRenderPass fallito!" << std::endl;
        return false;
    }
    std::cout << "[VULKAN] RenderPass creato (con Depth Buffer)." << std::endl;
    return true;
}

// ---------------------------------------------------------
// STEP 2: FRAMEBUFFERS (Collegano Swapchain e RenderPass)
// ---------------------------------------------------------
bool RenderManager::CreateFramebuffers() {
    std::cout << "[DEBUG] CreateFramebuffers chiamato! m_renderPass = " << m_renderPass << std::endl;
    m_framebuffers.resize(m_swapchainImageViews.size());
    for (size_t i = 0; i < m_swapchainImageViews.size(); i++) {
        // Ogni framebuffer ha: color attachment + depth attachment
        std::array<VkImageView, 2> attachments = {
            m_swapchainImageViews[i],
            m_depthImageView          // FIX: depth buffer!
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass      = m_renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        
        if (m_renderPass == VK_NULL_HANDLE) {
            std::cerr << "[RenderManager] ERROR: m_renderPass is VK_NULL_HANDLE during CreateFramebuffers!\n";
            return false;
        }
        framebufferInfo.pAttachments    = attachments.data();
        framebufferInfo.width           = m_swapchainExtent.width;
        framebufferInfo.height          = m_swapchainExtent.height;
        framebufferInfo.layers          = 1;

        if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_framebuffers[i]) != VK_SUCCESS) return false;
    }
    return true;
}

// ---------------------------------------------------------
// FASE 5: DESCRIPTOR SETS & UNIFORM BUFFERS
// ---------------------------------------------------------
bool RenderManager::CreateDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT; // Accessibile da Vertex e Fragment Shader

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerLayoutBinding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerLayoutBinding };
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) return false;
    return true;
}

uint32_t RenderManager::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    std::cerr << "[VULKAN ERROR] Impossibile trovare un tipo di memoria adatto!" << std::endl;
    return 0;
}

bool RenderManager::CreateUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    m_uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersAllocation.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        
        if (vmaCreateBuffer(m_vmaAllocator, &bufferInfo, &allocInfo, &m_uniformBuffers[i], &m_uniformBuffersAllocation[i], nullptr) != VK_SUCCESS) {
            return false;
        }

        VmaAllocationInfo vmaAllocInfo;
        vmaGetAllocationInfo(m_vmaAllocator, m_uniformBuffersAllocation[i], &vmaAllocInfo);
        m_uniformBuffersMapped[i] = vmaAllocInfo.pMappedData;
    }
    return true;
}

bool RenderManager::CreateDescriptorPoolAndSets() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool);

    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    m_descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    vkAllocateDescriptorSets(m_device, &allocInfo, m_descriptorSets.data());

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = m_textureImageView;
        imageInfo.sampler = m_textureSampler;

        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = m_descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = m_descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
    return true;
}

void RenderManager::UpdateUniformBuffer(uint32_t currentImage, glm::mat4 viewMatrix, glm::mat4 projMatrix, float seasonProgress) {
    UniformBufferObject ubo{};
    
    // Il triangolo resta fermo al centro (0,0,0)
    ubo.model = glm::mat4(1.0f); 

    // La telecamera ORA è controllata da te!
    ubo.view = viewMatrix;
    
    // La Proiezione: grandangolo dinamico regolabile
    ubo.proj = projMatrix;

    // Assegnamo il progresso stagionale passato dall'esterno
    ubo.seasonProgress = seasonProgress;

    // Copiamo i dati nella RAM della GPU
    memcpy(m_uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}

// ---------------------------------------------------------
// METODI HELPER FASE 4 (Shaders e Pipeline)
// ---------------------------------------------------------
std::vector<char> RenderManager::ReadFile(const std::string& filename) {
    namespace fs = std::filesystem;

    // Prova path relativi rispetto all'eseguibile (.exe)
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();

    // Estrai il nome del file (senza path)
    fs::path justFile = fs::path(filename).filename();

    std::vector<fs::path> candidates = {
        fs::path(filename),
        exeDir / filename,
        exeDir / justFile,
        exeDir / "shaders" / justFile,
        exeDir / "assets" / "shaders" / justFile,
        fs::current_path() / "bin" / justFile
    };

    for (auto& p : candidates) {
        std::ifstream file(p, std::ios::ate | std::ios::binary);
        if (!file.is_open()) continue;

        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();
        return buffer;
    }

    std::ostringstream oss;
    oss << "Impossibile aprire il file shader: " << filename
        << " (cwd=" << fs::current_path() << ", exeDir=" << exeDir << ")";
    std::cerr << "[VULKAN ERROR] " << oss.str() << std::endl;
    return std::vector<char>();
}

VkShaderModule RenderManager::CreateShaderModule(const std::vector<char>& code) {
    if (code.empty()) {
        std::cerr << "[VULKAN ERROR] Impossibile creare modulo shader: codice vuoto!" << std::endl;
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] Impossibile creare il modulo shader!" << std::endl;
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}

bool RenderManager::CreateGraphicsPipeline() {
    auto vertShaderCode = ReadFile("vert.spv");
    auto fragShaderCode = ReadFile("frag.spv");

    VkShaderModule vertShaderModule = CreateShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = CreateShaderModule(fragShaderCode);

    if (vertShaderModule == VK_NULL_HANDLE || fragShaderModule == VK_NULL_HANDLE) {
        std::cerr << "[VULKAN ERROR] Moduli shader principali mancanti!" << std::endl;
        if (vertShaderModule) vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
        if (fragShaderModule) vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    // Vertex Input — ora legge dalla struttura Vertex
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding   = 0;
    bindingDesc.stride    = sizeof(Vertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 8> attrDescs{};
    // location 0: posizione (vec3)
    attrDescs[0].binding  = 0;
    attrDescs[0].location = 0;
    attrDescs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrDescs[0].offset   = offsetof(Vertex, pos);
    // location 1: colore (vec4)
    attrDescs[1].binding  = 0;
    attrDescs[1].location = 1;
    attrDescs[1].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrDescs[1].offset   = offsetof(Vertex, color);
    // location 2: roughMetal (vec2)
    attrDescs[2].binding  = 0;
    attrDescs[2].location = 2;
    attrDescs[2].format   = VK_FORMAT_R32G32_SFLOAT;
    attrDescs[2].offset   = offsetof(Vertex, roughMetal);
    // location 3: indice texture (float)
    attrDescs[3].binding  = 0;
    attrDescs[3].location = 3;
    attrDescs[3].format   = VK_FORMAT_R32_SFLOAT;
    attrDescs[3].offset   = offsetof(Vertex, texIndex);
    // location 4: normale (vec3)
    attrDescs[4].binding  = 0;
    attrDescs[4].location = 4;
    attrDescs[4].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrDescs[4].offset   = offsetof(Vertex, normal);
    // location 5: Ambient Occlusion (float)
    attrDescs[5].binding  = 0;
    attrDescs[5].location = 5;
    attrDescs[5].format   = VK_FORMAT_R32_SFLOAT;
    attrDescs[5].offset   = offsetof(Vertex, ao);
    // location 6: Light (float)
    attrDescs[6].binding  = 0;
    attrDescs[6].location = 6;
    attrDescs[6].format   = VK_FORMAT_R32_SFLOAT;
    attrDescs[6].offset   = offsetof(Vertex, light);
    // location 7: Emissive (float)
    attrDescs[7].binding  = 0;
    attrDescs[7].location = 7;
    attrDescs[7].format   = VK_FORMAT_R32_SFLOAT;
    attrDescs[7].offset   = offsetof(Vertex, emissive);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount   = 1;
    vertexInputInfo.pVertexBindingDescriptions       = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = (uint32_t)attrDescs.size();
    vertexInputInfo.pVertexAttributeDescriptions    = attrDescs.data();

    // Assembly (Diciamo che stiamo disegnando un triangolo)
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport e Scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_swapchainExtent.width;
    viewport.height = (float)m_swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = m_swapchainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer (Disegna l'interno del triangolo)
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // Disattivato il culling per vedere entrambi i lati!
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling (Disattivato)
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth Stencil (FIX PRINCIPALE: test profondità abilitato!)
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable       = VK_TRUE;             // test: il pixel più vicino vince
    depthStencil.depthWriteEnable      = VK_TRUE;             // scrivi profondità nel buffer
    depthStencil.depthCompareOp        = VK_COMPARE_OP_LESS;  // vince chi ha z minore (più vicino)
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable     = VK_FALSE;

    // Color Blending (Abilitata Trasparenza)
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Pipeline Layout (Dati uniformi)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4) + sizeof(glm::vec4); // model + colorOffset

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) return false;

    // Costruzione finale della Pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState   = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pDepthStencilState  = &depthStencil; // FIX: collega il depth test
    pipelineInfo.pColorBlendState    = &colorBlending;
    pipelineInfo.layout              = m_pipelineLayout;
    pipelineInfo.renderPass          = m_renderPass;
    pipelineInfo.subpass             = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline) != VK_SUCCESS) return false;

    // --- PIPELINE DEL PORTALE (Scrive 1 nello stencil, colore disabilitato) ---
    VkPipelineDepthStencilStateCreateInfo portalStencil{};
    portalStencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    portalStencil.depthTestEnable       = VK_TRUE;
    portalStencil.depthWriteEnable      = VK_FALSE; // Non scrivere nel depth buffer! Lascia il "buco" infinito
    portalStencil.depthCompareOp        = VK_COMPARE_OP_LESS;
    portalStencil.stencilTestEnable     = VK_TRUE;  // ATTIVA STENCIL!
    portalStencil.front.compareOp       = VK_COMPARE_OP_ALWAYS;
    portalStencil.front.passOp          = VK_STENCIL_OP_REPLACE; // Metti 1 dove c'è il portale
    portalStencil.front.failOp          = VK_STENCIL_OP_KEEP;
    portalStencil.front.depthFailOp     = VK_STENCIL_OP_KEEP;
    portalStencil.front.compareMask     = 0xFF;
    portalStencil.front.writeMask       = 0xFF;
    portalStencil.front.reference       = 1;
    portalStencil.back = portalStencil.front; // Stesso comportamento su entrambe le facce

    VkPipelineColorBlendAttachmentState noColorBlend{};
    noColorBlend.colorWriteMask = 0; // Disabilita la scrittura sui colori
    noColorBlend.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo noColorBlending{};
    noColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    noColorBlending.logicOpEnable = VK_FALSE;
    noColorBlending.attachmentCount = 1;
    noColorBlending.pAttachments = &noColorBlend;

    pipelineInfo.pDepthStencilState = &portalStencil;
    pipelineInfo.pColorBlendState   = &noColorBlending;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_portalPipeline) != VK_SUCCESS) return false;

    // --- PIPELINE DELL'ALTRO MONDO (Disegna SOLO dove stencil == 1) ---
    VkPipelineDepthStencilStateCreateInfo otherWorldStencil{};
    otherWorldStencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    otherWorldStencil.depthTestEnable       = VK_TRUE;
    otherWorldStencil.depthWriteEnable      = VK_TRUE; 
    otherWorldStencil.depthCompareOp        = VK_COMPARE_OP_LESS;
    otherWorldStencil.stencilTestEnable     = VK_TRUE;
    otherWorldStencil.front.compareOp       = VK_COMPARE_OP_EQUAL; // Disegna solo se stencil == reference (1)
    otherWorldStencil.front.passOp          = VK_STENCIL_OP_KEEP;
    otherWorldStencil.front.failOp          = VK_STENCIL_OP_KEEP;
    otherWorldStencil.front.depthFailOp     = VK_STENCIL_OP_KEEP;
    otherWorldStencil.front.compareMask     = 0xFF;
    otherWorldStencil.front.writeMask       = 0x00; // Non modificare più lo stencil
    otherWorldStencil.front.reference       = 1;
    otherWorldStencil.back = otherWorldStencil.front;

    pipelineInfo.pDepthStencilState = &otherWorldStencil;
    pipelineInfo.pColorBlendState   = &colorBlending; // Rimetti i colori normali

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_otherWorldPipeline) != VK_SUCCESS) return false;

    // Pulizia dei moduli shader locali (sono già compilati nella pipeline!)
    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);

    // =========================================================================
    // SKY PIPELINE (Nessun Vertex Buffer, usa gl_VertexIndex nel vertex shader)
    // =========================================================================
    auto skyVertCode = ReadFile("bin/sky_vert.spv");
    auto skyFragCode = ReadFile("bin/sky_frag.spv");
    
    VkShaderModule skyVertModule = CreateShaderModule(skyVertCode);
    VkShaderModule skyFragModule = CreateShaderModule(skyFragCode);

    if (skyVertModule == VK_NULL_HANDLE || skyFragModule == VK_NULL_HANDLE) {
        std::cerr << "[VULKAN ERROR] Moduli shader per il cielo mancanti!" << std::endl;
        if (skyVertModule) vkDestroyShaderModule(m_device, skyVertModule, nullptr);
        if (skyFragModule) vkDestroyShaderModule(m_device, skyFragModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo skyShaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, skyVertModule, "main", nullptr },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, skyFragModule, "main", nullptr }
    };

    VkPipelineVertexInputStateCreateInfo skyVertexInputInfo{};
    skyVertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    skyVertexInputInfo.vertexBindingDescriptionCount = 0;
    skyVertexInputInfo.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo skyInputAssembly{};
    skyInputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    skyInputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    skyInputAssembly.primitiveRestartEnable = VK_FALSE;

    // Riutilizziamo viewportState, rasterizer (ma culling=NONE per sicurezza), multisampling, colorBlending
    VkPipelineRasterizationStateCreateInfo skyRasterizer = rasterizer;
    skyRasterizer.cullMode = VK_CULL_MODE_NONE; // Disabilita culling per il fullscreen quad
    skyRasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo skyDepthStencil{};
    skyDepthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    skyDepthStencil.depthTestEnable = VK_FALSE;  // Niente depth test
    skyDepthStencil.depthWriteEnable = VK_FALSE; // Niente depth write
    skyDepthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    skyDepthStencil.stencilTestEnable = VK_FALSE;

    // Push Constants per il cielo
    VkPushConstantRange skyPushConstant{};
    skyPushConstant.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    skyPushConstant.offset = 0;
    skyPushConstant.size = sizeof(glm::mat4) * 2 + sizeof(float) * 4; // 128 + 16 = 144 bytes

    VkPipelineLayoutCreateInfo skyPipelineLayoutInfo{};
    skyPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    skyPipelineLayoutInfo.pushConstantRangeCount = 1;
    skyPipelineLayoutInfo.pPushConstantRanges = &skyPushConstant;

    if (vkCreatePipelineLayout(m_device, &skyPipelineLayoutInfo, nullptr, &m_skyPipelineLayout) != VK_SUCCESS) {
        return false;
    }

    VkGraphicsPipelineCreateInfo skyPipelineInfo{};
    skyPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    skyPipelineInfo.stageCount = 2;
    skyPipelineInfo.pStages = skyShaderStages;
    skyPipelineInfo.pVertexInputState = &skyVertexInputInfo;
    skyPipelineInfo.pInputAssemblyState = &skyInputAssembly;
    skyPipelineInfo.pViewportState = &viewportState;
    skyPipelineInfo.pRasterizationState = &skyRasterizer;
    skyPipelineInfo.pMultisampleState = &multisampling;
    skyPipelineInfo.pDepthStencilState = &skyDepthStencil;
    skyPipelineInfo.pColorBlendState = &colorBlending;
    skyPipelineInfo.pDynamicState = nullptr;
    skyPipelineInfo.layout = m_skyPipelineLayout;
    skyPipelineInfo.renderPass = m_renderPass;
    skyPipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &skyPipelineInfo, nullptr, &m_skyPipeline) != VK_SUCCESS) {
        return false;
    }

    vkDestroyShaderModule(m_device, skyFragModule, nullptr);
    vkDestroyShaderModule(m_device, skyVertModule, nullptr);

    return true;
}

// ---------------------------------------------------------
// STEP 3: COMMAND POOL & BUFFER (La memoria per le istruzioni)
// ---------------------------------------------------------
bool RenderManager::CreateCommandPoolAndBuffer() {
    QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) return false;

    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)m_commandBuffers.size();

    if (vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) return false;
    return true;
}

// ---------------------------------------------------------
// STEP 4: SINCRONIZZAZIONE E RENDER LOOP (Il semaforo)
// ---------------------------------------------------------
bool RenderManager::CreateSyncObjects() {
    uint32_t swapchainImageCount = (uint32_t)m_swapchainImages.size();

    // imageAvailable: 1 per FRAME IN VOLO (protetto dal fence, consumato da vkQueueSubmit)
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    // renderFinished: 1 per IMMAGINE SWAPCHAIN (indicizzato da imageIndex)
    // La swapchain restituisce imageIndex solo quando la sua presentazione e' finita
    // => renderFinishedSemaphores[imageIndex] e' garantito libero ogni volta
    m_renderFinishedSemaphores.resize(swapchainImageCount);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            return false;
        }
    }
    for (size_t i = 0; i < swapchainImageCount; i++) {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

struct CameraFrustum {
    glm::vec4 planes[6];
    
    void extract(const glm::mat4& vp) {
        // Left
        planes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);
        // Right
        planes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);
        // Bottom
        planes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]);
        // Top
        planes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]);
        // Near
        planes[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]);
        // Far
        planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);
        
        for (int i=0; i<6; ++i) {
            float length = glm::length(glm::vec3(planes[i]));
            planes[i] /= length;
        }
    }
    
    bool containsAABB(const glm::vec3& min, const glm::vec3& max) const {
        for (int i=0; i<6; ++i) {
            glm::vec3 p(
                planes[i].x > 0 ? max.x : min.x,
                planes[i].y > 0 ? max.y : min.y,
                planes[i].z > 0 ? max.z : min.z
            );
            if (glm::dot(glm::vec3(planes[i]), p) + planes[i].w < 0.0f) {
                return false;
            }
        }
        return true;
    }
};

// --> QUESTA E' LA FUNZIONE CHE DISEGNA EFFETTIVAMENTE! <--
void RenderManager::RenderFairworld(VkCommandBuffer cmd, glm::mat4 viewMatrix, glm::vec3 skyColor, SharedContext* context, AssetManager* assets, MobManager* mobManager, Player* player) {
    // --- SKY PASS ---
    if (m_skyPipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyPipeline);
        
        struct SkyPushConstants {
            glm::mat4 invView;
            glm::mat4 invProj;
            float timeOfDay;
            float moonPhase;
            glm::vec2 dummy;
        } skyPC;
        
        // Per il cielo vogliamo solo la rotazione, quindi azzeriamo la traslazione (W=0 e P=0,0,0,1)
        glm::mat4 skyView = viewMatrix;
        skyView[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        
        glm::mat4 projMatrix = context ? context->activeCameraView.projectionMatrix : glm::perspective(glm::radians(45.0f), m_swapchainExtent.width / (float)m_swapchainExtent.height, 0.1f, 1000.0f);
        skyPC.invView = glm::inverse(skyView);
        skyPC.invProj = glm::inverse(projMatrix);
        skyPC.timeOfDay = context && context->engine ? context->engine->GetTimeManager().GetTimeOfDay() : 0.5f;
        skyPC.moonPhase = context && context->engine ? context->engine->GetTimeManager().GetMoonPhase() : 0.5f;
        
        vkCmdPushConstants(cmd, m_skyPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SkyPushConstants), &skyPC);
        
        // 3 vertici autogenerati in glsl (gl_VertexIndex)
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    // Calcolo progresso stagionale
    float rawYearProgress = 0.0f;
    if (context && context->engine) {
        int currentDay = context->engine->GetTimeManager().GetCurrentDay();
        // Mappiamo i giorni in un ciclo annuale di 365 giorni
        rawYearProgress = fmod((float)currentDay, 365.0f) / 365.0f;
    }
    // Applica distorsione per rallentare estate/inverno (modello biologico)
    float seasonalUboValue = (sin((rawYearProgress * 2.0f * glm::pi<float>()) - (glm::pi<float>() / 2.0f)) + 1.0f) * 0.5f;

    float aspectUniform = (float)m_swapchainExtent.width / (float)m_swapchainExtent.height;
    glm::mat4 uboProjMatrix = context ? context->activeCameraView.projectionMatrix : glm::perspective(glm::radians(m_fov), aspectUniform, 0.1f, 1000.0f);
    if (!context) uboProjMatrix[1][1] *= -1;

    UpdateUniformBuffer(m_currentFrame, viewMatrix, uboProjMatrix, seasonalUboValue);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSets[m_currentFrame], 0, nullptr);

    // Disegna tutti i chunk legacy (se presenti)
    for (const auto& pair : m_chunkBuffers) {
        const auto& chunkBuf = pair.second;
        if (chunkBuf.vertexBuffer != VK_NULL_HANDLE && chunkBuf.indexBuffer != VK_NULL_HANDLE && chunkBuf.indexCount > 0) {
            VkBuffer vertexBuffers[] = { chunkBuf.vertexBuffer };
            VkDeviceSize offsets[]   = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, chunkBuf.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            glm::mat4 identityModel = glm::mat4(1.0f);
            glm::vec4 noColorOffset = glm::vec4(0.0f);
            vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &identityModel);
            vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4), sizeof(glm::vec4), &noColorOffset);

            vkCmdDrawIndexed(cmd, chunkBuf.indexCount, 1, 0, 0, 0);
        }
    }

    // --- DISEGNO CHUNK FORGEWORLD (Nuovo mondo procedurale in PlayState) ---
    if (context && context->forgeWorld && m_globalVramBuffer != VK_NULL_HANDLE) {
        // Le mesh del ForgeWorld usano PBR e push constants dedicate
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_forgePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_forgePipelineLayout, 0, 1, &m_descriptorSets[m_currentFrame], 0, nullptr);
        
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)m_swapchainExtent.width;
        viewport.height = (float)m_swapchainExtent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = m_swapchainExtent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        ForgePushConstantData pcData{};
        VkDeviceSize offsets[] = { 0 };

        auto& registry = context->forgeWorld->GetRegistry();
        auto view = registry.view<fw::MeshComponent, fw::TransformComponent>();

        // ATTENZIONE: In RenderFairworld viewMatrix è SOLO la View matrix!
        float aspect = (float)m_swapchainExtent.width / (float)m_swapchainExtent.height;
        glm::mat4 projMatrix = context ? context->activeCameraView.projectionMatrix : glm::perspective(glm::radians(m_fov), aspect, 0.1f, 1000.0f);
        glm::mat4 viewProjMatrix = projMatrix * viewMatrix;

        CameraFrustum frustum;
        frustum.extract(viewProjMatrix);

        for (auto entity : view) {
            const auto& mesh = view.get<fw::MeshComponent>(entity);
            const auto& trans = view.get<fw::TransformComponent>(entity);

            if (!mesh.vramAlloc.valid || mesh.vertices.empty()) continue;

            // Renderizziamo Chunk e Prefab (escludiamo griglia e sfere di preview dell'editor)
            if (mesh.type == fw::MeshType::Chunk || mesh.type == fw::MeshType::Prefab) {
                fw::Mat4 fwModel = trans.worldMatrix();
                glm::mat4 model = glm::transpose(*reinterpret_cast<glm::mat4*>(&fwModel));

                fw::AABB bounds = mesh.bounds();
                glm::vec3 center((bounds.min.x + bounds.max.x)*0.5f, (bounds.min.y + bounds.max.y)*0.5f, (bounds.min.z + bounds.max.z)*0.5f);
                glm::vec3 extents((bounds.max.x - bounds.min.x)*0.5f, (bounds.max.y - bounds.min.y)*0.5f, (bounds.max.z - bounds.min.z)*0.5f);
                
                glm::vec3 worldCenter = glm::vec3(model * glm::vec4(center, 1.0f));
                glm::vec3 worldExtents(
                    std::abs(model[0][0]) * extents.x + std::abs(model[1][0]) * extents.y + std::abs(model[2][0]) * extents.z,
                    std::abs(model[0][1]) * extents.x + std::abs(model[1][1]) * extents.y + std::abs(model[2][1]) * extents.z,
                    std::abs(model[0][2]) * extents.x + std::abs(model[1][2]) * extents.y + std::abs(model[2][2]) * extents.z
                );
                
                if (!frustum.containsAABB(worldCenter - worldExtents, worldCenter + worldExtents)) continue;

                pcData.mvp = viewProjMatrix * model;
                pcData.useColorOverride = 0;
                pcData.seasonProgress = seasonalUboValue;

                vkCmdPushConstants(cmd, m_forgePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ForgePushConstantData), &pcData);

                offsets[0] = mesh.vramAlloc.offset;
                VkBuffer vertexBuffers[] = { m_globalVramBuffer };
                vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
                
                vkCmdDraw(cmd, (uint32_t)mesh.vertices.size(), 1, 0, 0);
            }
        }
        
        // RIPRISTINA LA PIPELINE ORIGINALE PER GLI ALTRI ELEMENTI DI FAIRWORLD
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSets[m_currentFrame], 0, nullptr);
    }
        if (m_ghostVertexBuffer != VK_NULL_HANDLE && m_ghostIndexBuffer != VK_NULL_HANDLE && m_ghostIndexCount > 0) {
            VkBuffer ghostBuffers[] = { m_ghostVertexBuffer };
            VkDeviceSize offsets[]   = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, ghostBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, m_ghostIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

            glm::mat4 ghostModel = glm::mat4(1.0f);
            // Usa a>0.5 per forzare il vertex shader a usare il colore dell'offset.
            // E usa alpha = 0.5 per la trasparenza (richiede blending abilitato).
            glm::vec4 ghostColorOffset = glm::vec4(0.0f, 0.8f, 1.0f, 0.6f); 
            vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &ghostModel);
            vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4), sizeof(glm::vec4), &ghostColorOffset);

            vkCmdDrawIndexed(cmd, m_ghostIndexCount, 1, 0, 0, 0);
        }

        // --- DISEGNO MOB TRAMITE PUSH CONSTANTS E INSTANCING MANUALE ---
        if (mobManager && assets) {
            for (const auto& mob : mobManager->instances) {
                if (!mob.isAlive) continue;

                auto* tmpl = assets->GetMobByID(mob.templateID);
                if (!tmpl) continue;

                std::string path = tmpl->resources.modelPath;
                if (path.empty()) path = "assets/models/mob.vox"; // fallback

                auto it = m_mobMeshes.find(path);
                if (it == m_mobMeshes.end() || it->second.indexCount == 0) continue;

                VoxelMesh& mesh = it->second;

                VkBuffer mobVertexBuffers[] = { mesh.vertexBuffer };
                VkDeviceSize offsets[]   = { 0 };
                vkCmdBindVertexBuffers(cmd, 0, 1, mobVertexBuffers, offsets);
                vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

                glm::mat4 mobModel = glm::translate(glm::mat4(1.0f), mob.position);
                
                // Applica il colore in base al danno ricevuto (Rosso se in cooldown attacco per feedback)
                glm::vec4 colorOffset = glm::vec4(0.0f);
                if (mob.attackCooldownTimer > 0.0f) {
                    colorOffset = glm::vec4(1.0f, 0.2f, 0.2f, 1.0f); // Override colore a rosso
                }
                vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &mobModel);
                vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4), sizeof(glm::vec4), &colorOffset);

                vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
            }
        }

    // --- DISEGNO ARMA DEL PLAYER (Prima Persona / VR) ---
    if (player && !player->equippedWeaponPath.empty()) {
        auto it = m_mobMeshes.find(player->equippedWeaponPath);
        if (it == m_mobMeshes.end()) {
            LoadMobMesh(player->equippedWeaponPath);
            it = m_mobMeshes.find(player->equippedWeaponPath);
        }

        if (it != m_mobMeshes.end() && it->second.indexCount > 0) {
            // Disabilita il Depth Test per l'arma (per simulare il render in overlay come negli FPS, 
            // ma dato che non stiamo ricreando la pipeline al volo, la posizioneremo semplicemente 
            // molto vicina alla telecamera, oppure la disegneremo normalmente). 
            // In VR è corretto che subisca il depth test rispetto al mondo.
            
            VoxelMesh& mesh = it->second;

            VkBuffer weaponVertexBuffers[] = { mesh.vertexBuffer };
            VkDeviceSize offsets[]   = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, weaponVertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            // La matrice è GIA' IN WORLD-SPACE (calcolata nel Player.rightHandTransform)!
            glm::mat4 weaponModel = player->rightHandTransform;
            
            // Scaliamo un po' l'arma per farla sembrare un oggetto in mano (0.4x)
            weaponModel = glm::scale(weaponModel, glm::vec3(0.4f));

            glm::vec4 colorOffset = glm::vec4(0.0f); // Nessun feedback danno sull'arma

            vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &weaponModel);
            vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4), sizeof(glm::vec4), &colorOffset);

            vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
        }
    }

}

void RenderManager::RenderDesktop(glm::mat4 viewMatrix, glm::vec3 skyColor, SharedContext* context, AssetManager* assets, MobManager* mobManager, Player* player) {
    if (m_device == VK_NULL_HANDLE) return;
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    // imageAvailable[currentFrame]: protetto dal fence sopra => e' sicuro risegnalarlo
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame],
        VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR) {
        RecreateSwapchain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        std::cerr << "[VULKAN ERROR] Impossibile acquisire l'immagine della swapchain! Error: " << result << std::endl;
        return;
    }

    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);
    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);

    // REGISTRAZIONE DEI COMANDI
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(m_commandBuffers[m_currentFrame], &beginInfo);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = m_swapchainExtent;

    // Svuota sia il colore che il depth ogni frame
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color        = {{ skyColor.x, skyColor.y, skyColor.z, 1.0f }}; // colore dinamico cielo
    clearValues[1].depthStencil = { 1.0f, 0 };                     // depth = 1.0 (massimo)
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues    = clearValues.data();

    vkCmdBeginRenderPass(m_commandBuffers[m_currentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    bool isForge = (context && context->isForgeMode);
    if (isForge) {
        float aspect = (float)m_swapchainExtent.width / (float)m_swapchainExtent.height;
        glm::mat4 projMatrix = context ? context->activeCameraView.projectionMatrix : glm::perspective(glm::radians(m_fov), aspect, 0.1f, 1000.0f);
        glm::mat4 viewProjMatrix = projMatrix * viewMatrix;
        
        RenderForge(m_commandBuffers[m_currentFrame], viewProjMatrix, context);
    } else {
        RenderFairworld(m_commandBuffers[m_currentFrame], viewMatrix, skyColor, context, assets, mobManager, player);
    }

    ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data) {
        ImGui_ImplVulkan_RenderDrawData(draw_data, m_commandBuffers[m_currentFrame]);
    }

    vkCmdEndRenderPass(m_commandBuffers[m_currentFrame]);
    vkEndCommandBuffer(m_commandBuffers[m_currentFrame]);

    // Submit: attende imageAvailable[currentFrame], segnala renderFinished[imageIndex]
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];

    VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[imageIndex] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]) != VK_SUCCESS) {
            std::cerr << "[VULKAN ERROR] Impossibile sottomettere il Draw Command Buffer!" << std::endl;
        }
    }

    // Presentazione dell'immagine sullo schermo
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = { m_swapchain };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult resultPresent;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        resultPresent = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    }
    if (resultPresent == VK_ERROR_OUT_OF_DATE_KHR || resultPresent == VK_SUBOPTIMAL_KHR || resultPresent == VK_ERROR_SURFACE_LOST_KHR) {
        RecreateSwapchain();
    } else if (resultPresent != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] Impossibile presentare l'immagine della swapchain! Error: " << resultPresent << std::endl;
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    
    // Esegui il flush dei comandi asincroni (Voxel Meshes, etc)
    FlushTransferBatch();
}

// ---------------------------------------------------------
// VERTEX / INDEX BUFFER HELPERS
// ---------------------------------------------------------
bool RenderManager::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VmaMemoryUsage vmaUsage,
                                 VkBuffer& buffer, VmaAllocation& bufferAllocation, VmaAllocationCreateFlags flags) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size  = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = vmaUsage;
    allocInfo.flags = flags;

    if (vmaCreateBuffer(m_vmaAllocator, &bufferInfo, &allocInfo, &buffer, &bufferAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }
    return true;
}

void RenderManager::CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) {
    // Alloca un command buffer temporaneo per la copia
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuf;
    vkAllocateCommandBuffers(m_device, &allocInfo, &cmdBuf);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuf, &beginInfo);

    VkBufferCopy copyRegion{ 0, 0, size };
    vkCmdCopyBuffer(cmdBuf, src, dst, 1, &copyRegion);

    vkEndCommandBuffer(cmdBuf);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmdBuf;
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmdBuf);
}

void RenderManager::DestroyChunkBuffer(ChunkCoord coord) {
    auto it = m_chunkBuffers.find(coord);
    if (it != m_chunkBuffers.end()) {
        // [FIX] Flush the transfer batch FIRST, because the OLD buffer might have just been created 
        // and recorded in the current transfer batch (if Update() ran multiple times this frame).
        FlushTransferBatch();

        vkDeviceWaitIdle(m_device);
        if (it->second.vertexBuffer != VK_NULL_HANDLE) { vmaDestroyBuffer(m_vmaAllocator, it->second.vertexBuffer, it->second.vertexBufferAllocation); }
        if (it->second.indexBuffer != VK_NULL_HANDLE) { vmaDestroyBuffer(m_vmaAllocator, it->second.indexBuffer, it->second.indexBufferAllocation); }
        m_chunkBuffers.erase(it);
    }
}

void RenderManager::InvalidateForgeCache() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    vkDeviceWaitIdle(m_device);
    
    // Clear legacy chunk buffers
    for (auto& pair : m_chunkBuffers) {
        if (pair.second.vertexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_vmaAllocator, pair.second.vertexBuffer, pair.second.vertexBufferAllocation);
        }
        if (pair.second.indexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_vmaAllocator, pair.second.indexBuffer, pair.second.indexBufferAllocation);
        }
    }
    m_chunkBuffers.clear();
}

void RenderManager::UploadChunkMesh(ChunkCoord coord, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    DestroyChunkBuffer(coord);
    if (vertices.empty() || indices.empty()) return;

    VulkanChunkBuffer chunkBuf;
    chunkBuf.indexCount = (uint32_t)indices.size();

    VkDeviceSize vertexSize = sizeof(vertices[0]) * vertices.size();
    VkDeviceSize indexSize = sizeof(indices[0]) * indices.size();
    VkDeviceSize totalSize = vertexSize + indexSize;

    // 1. Allineiamo il cursore per i vertici
    m_currentOffset = AlignMemory(m_currentOffset);
    uint64_t vertexOffset = m_currentOffset;
    
    // 2. Allineiamo il cursore per gli indici subito dopo i vertici
    m_currentOffset += vertexSize;
    m_currentOffset = AlignMemory(m_currentOffset);
    uint64_t indexOffset = m_currentOffset;
    m_currentOffset += indexSize;

    // [!] Prevenzione Overflow: Se il buffer è pieno, dobbiamo forzare un flush immediato
    if (m_currentOffset > STAGING_BUFFER_SIZE) {
        FlushTransferBatch();
        m_currentOffset = 0;
        vertexOffset = 0;
        indexOffset = AlignMemory(vertexSize);
        m_currentOffset = indexOffset + indexSize;
    }

    // 3. Copia fulminea in RAM (nel puntatore mappato da VMA)
    uint8_t* dstMapped = static_cast<uint8_t*>(m_mappedStagingData);
    memcpy(dstMapped + vertexOffset, vertices.data(), (size_t)vertexSize);
    memcpy(dstMapped + indexOffset, indices.data(), (size_t)indexSize);

    // 4. Crea i buffer di destinazione in VRAM (usando il tuo VmaPool dedicato ai chunk se vuoi, ma qui usiamo CreateBuffer globale con pool opzionale)
    // Per ora allochiamo con VMA normale in VRAM
    VkBufferCreateInfo vbInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    vbInfo.size = vertexSize;
    vbInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo vbAllocInfo = {};
    vbAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    vbAllocInfo.pool = m_chunkVmaPool; // Use the chunk pool!
    VkResult vbResult = vmaCreateBuffer(m_vmaAllocator, &vbInfo, &vbAllocInfo, &chunkBuf.vertexBuffer, &chunkBuf.vertexBufferAllocation, nullptr);
    if (vbResult != VK_SUCCESS || chunkBuf.vertexBuffer == VK_NULL_HANDLE) {
        std::cerr << "[UploadChunkMesh] vmaCreateBuffer for vertex buffer FAILED: " << vbResult << std::endl;
        return;
    }

    VkBufferCreateInfo ibInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    ibInfo.size = indexSize;
    ibInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    ibInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo ibAllocInfo = {};
    ibAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    ibAllocInfo.pool = m_chunkVmaPool;
    VkResult ibResult = vmaCreateBuffer(m_vmaAllocator, &ibInfo, &ibAllocInfo, &chunkBuf.indexBuffer, &chunkBuf.indexBufferAllocation, nullptr);
    if (ibResult != VK_SUCCESS || chunkBuf.indexBuffer == VK_NULL_HANDLE) {
        std::cerr << "[UploadChunkMesh] vmaCreateBuffer for index buffer FAILED: " << ibResult << std::endl;
        // Cleanup vertex buffer created before
        if (chunkBuf.vertexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_vmaAllocator, chunkBuf.vertexBuffer, chunkBuf.vertexBufferAllocation);
        }
        return;
    }

    // 5. Registra i comandi di copia nel Transfer Command Buffer
    // Verifica che gli handle siano validi prima di usarli
    if (m_transferCommandBuffer == VK_NULL_HANDLE || m_stagingRingBuffer == VK_NULL_HANDLE) {
        std::cerr << "[UploadChunkMesh] TransferCommandBuffer or StagingRingBuffer is INVALID!" << std::endl;
        // Cleanup both buffers
        vmaDestroyBuffer(m_vmaAllocator, chunkBuf.vertexBuffer, chunkBuf.vertexBufferAllocation);
        vmaDestroyBuffer(m_vmaAllocator, chunkBuf.indexBuffer, chunkBuf.indexBufferAllocation);
        return;
    }

    VkBufferCopy vertexCopyRegion = {};
    vertexCopyRegion.srcOffset = vertexOffset;
    vertexCopyRegion.dstOffset = 0;
    vertexCopyRegion.size = vertexSize;
    vkCmdCopyBuffer(m_transferCommandBuffer, m_stagingRingBuffer, chunkBuf.vertexBuffer, 1, &vertexCopyRegion);

    VkBufferCopy indexCopyRegion = {};
    indexCopyRegion.srcOffset = indexOffset;
    indexCopyRegion.dstOffset = 0;
    indexCopyRegion.size = indexSize;
    vkCmdCopyBuffer(m_transferCommandBuffer, m_stagingRingBuffer, chunkBuf.indexBuffer, 1, &indexCopyRegion);

    m_chunkBuffers[coord] = chunkBuf;
}

void RenderManager::FlushTransferBatch() {
    if (m_currentOffset == 0) return; // Niente da flussare

    // Chiudi il command buffer e sottomettilo alla coda di trasferimento
    vkEndCommandBuffer(m_transferCommandBuffer);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_transferCommandBuffer;

    vkQueueSubmit(m_transferQueue, 1, &submitInfo, VK_NULL_HANDLE);

    // Iterazione 1: WaitIdle (Sincronizzazione dura a fine batch)
    vkQueueWaitIdle(m_transferQueue);

    // Resetta l'offset (Tail raggiunge Head) e il command buffer per il prossimo batch
    m_currentOffset = 0;
    vkResetCommandBuffer(m_transferCommandBuffer, 0);
    
    // Fai ripartire la registrazione del Command Buffer
    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(m_transferCommandBuffer, &beginInfo);
}


void RenderManager::UploadGhostMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) {
        m_ghostIndexCount = 0;
        return;
    }
    VkDeviceSize vSize = sizeof(vertices[0]) * vertices.size();
    VkDeviceSize iSize = sizeof(indices[0]) * indices.size();

    VkBuffer stagingBuf; VmaAllocation stagingMem;
    if (!CreateBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VMA_MEMORY_USAGE_CPU_TO_GPU,
                      stagingBuf, stagingMem)) return;

    void* data;
    vmaMapMemory(m_vmaAllocator, stagingMem, &data);
    memcpy(data, vertices.data(), (size_t)vSize);
    vmaUnmapMemory(m_vmaAllocator, stagingMem);

    if (m_ghostVertexBuffer != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
        vmaDestroyBuffer(m_vmaAllocator, m_ghostVertexBuffer, m_ghostVertexBufferAllocation);
        vmaDestroyBuffer(m_vmaAllocator, m_ghostIndexBuffer, m_ghostIndexBufferAllocation);
    }

    if (!CreateBuffer(vSize,
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VMA_MEMORY_USAGE_GPU_ONLY,
                      m_ghostVertexBuffer, m_ghostVertexBufferAllocation)) return;

    CopyBuffer(stagingBuf, m_ghostVertexBuffer, vSize);
    vmaDestroyBuffer(m_vmaAllocator, stagingBuf, stagingMem);

    if (!CreateBuffer(iSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VMA_MEMORY_USAGE_CPU_TO_GPU,
                      stagingBuf, stagingMem)) return;

    vmaMapMemory(m_vmaAllocator, stagingMem, &data);
    memcpy(data, indices.data(), (size_t)iSize);
    vmaUnmapMemory(m_vmaAllocator, stagingMem);

    if (!CreateBuffer(iSize,
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VMA_MEMORY_USAGE_GPU_ONLY,
                      m_ghostIndexBuffer, m_ghostIndexBufferAllocation)) return;

    CopyBuffer(stagingBuf, m_ghostIndexBuffer, iSize);
    vmaDestroyBuffer(m_vmaAllocator, stagingBuf, stagingMem);

    m_ghostIndexCount = (uint32_t)indices.size();
}

// --- GENERATORE DI MESH DINAMICHE PER I MODELLI VOXEL ---
void RenderManager::LoadAllMobMeshes(AssetManager& assets) {
    const auto& mobs = assets.GetMobs();
    for (const auto& mobTemplate : mobs) {
        std::string path = mobTemplate.resources.modelPath;
        if (path.empty()) path = "assets/models/mob.vox";
        LoadMobMesh(path);
    }
}

void RenderManager::LoadMobMesh(const std::string& filepath) {
    if (m_mobMeshes.find(filepath) != m_mobMeshes.end()) return; // Già caricata

    using json = nlohmann::json;
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cout << "[VULKAN] Nessun modello trovato in " << filepath << std::endl;
        m_mobMeshes[filepath] = VoxelMesh(); // Mark as failed to prevent spam
        return;
    }

    json j;
    try {
        file >> j;
    } catch (...) {
        std::cout << "[VULKAN] Errore nel parse del modello " << filepath << std::endl;
        return;
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    if (j.contains("voxels")) {
        for (auto& item : j["voxels"]) {
            float vx = item["x"];
            float vy = item["z"]; // scambia y e z per usare z come profondità nell'editor (Z-up vs Y-up)
            float vz = item["y"];
            
            // Centriamo il modello rispetto allo 0 (assumendo griglia 16x16x16)
            vx = (vx - 8.0f) * 0.1f;
            vy = vy * 0.1f; // I piedi a y=0
            vz = (vz - 8.0f) * 0.1f;
            
            float s = 0.05f; // Mezza dimensione del cubetto

            float r = (float)item["r"] / 255.0f;
            float g = (float)item["g"] / 255.0f;
            float b = (float)item["b"] / 255.0f;
            glm::vec4 col4(r, g, b, 1.0f);

            uint32_t startIdx = (uint32_t)vertices.size();

            // 8 vertici del cubetto
            vertices.push_back({{vx - s, vy - s, vz + s}, col4, {0.0f, 0.0f}, -1.0f, {0,0,0}, 1.0f, 1.0f, 0.0f}); // 0: front bottom left
            vertices.push_back({{vx + s, vy - s, vz + s}, col4, {1.0f, 0.0f}, -1.0f, {0,0,0}, 1.0f, 1.0f, 0.0f}); // 1: front bottom right
            vertices.push_back({{vx + s, vy + s, vz + s}, col4, {1.0f, 1.0f}, -1.0f, {0,0,0}, 1.0f, 1.0f, 0.0f}); // 2: front top right
            vertices.push_back({{vx - s, vy + s, vz + s}, col4, {0.0f, 1.0f}, -1.0f, {0,0,0}, 1.0f, 1.0f, 0.0f}); // 3: front top left
            vertices.push_back({{vx - s, vy - s, vz - s}, col4, {0.0f, 0.0f}, -1.0f, {0,0,0}, 1.0f, 1.0f, 0.0f}); // 4: back bottom left
            vertices.push_back({{vx + s, vy - s, vz - s}, col4, {1.0f, 0.0f}, -1.0f, {0,0,0}, 1.0f, 1.0f, 0.0f}); // 5: back bottom right
            vertices.push_back({{vx + s, vy + s, vz - s}, col4, {1.0f, 1.0f}, -1.0f, {0,0,0}, 1.0f, 1.0f, 0.0f}); // 6: back top right
            vertices.push_back({{vx - s, vy + s, vz - s}, col4, {0.0f, 1.0f}, -1.0f, {0,0,0}, 1.0f, 1.0f, 0.0f}); // 7: back top left

            // Indici per le 6 facce (12 triangoli)
            uint32_t cubeIndices[] = {
                // Front
                0, 1, 2, 2, 3, 0,
                // Right
                1, 5, 6, 6, 2, 1,
                // Back
                5, 4, 7, 7, 6, 5,
                // Left
                4, 0, 3, 3, 7, 4,
                // Top
                3, 2, 6, 6, 7, 3,
                // Bottom
                4, 5, 1, 1, 0, 4
            };

            for (int i = 0; i < 36; i++) {
                indices.push_back(startIdx + cubeIndices[i]);
            }
        }
    }

    if (vertices.empty()) return;

    VoxelMesh newMesh;

    // Crea i buffer Vulkan
    CreateBuffer(sizeof(vertices[0]) * vertices.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, newMesh.vertexBuffer, newMesh.vertexBufferAllocation);
    CreateBuffer(sizeof(indices[0]) * indices.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, newMesh.indexBuffer, newMesh.indexBufferAllocation);

    VkBuffer stagingBuffer;
    VmaAllocation stagingBufferMemory;

    // Upload Vertices
    CreateBuffer(sizeof(vertices[0]) * vertices.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, stagingBuffer, stagingBufferMemory);
    void* data;
    vmaMapMemory(m_vmaAllocator, stagingBufferMemory, &data);
    memcpy(data, vertices.data(), (size_t)(sizeof(vertices[0]) * vertices.size()));
    vmaUnmapMemory(m_vmaAllocator, stagingBufferMemory);
    CopyBuffer(stagingBuffer, newMesh.vertexBuffer, sizeof(vertices[0]) * vertices.size());
    vmaDestroyBuffer(m_vmaAllocator, stagingBuffer, stagingBufferMemory);

    // Upload Indices
    CreateBuffer(sizeof(indices[0]) * indices.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, stagingBuffer, stagingBufferMemory);
    vmaMapMemory(m_vmaAllocator, stagingBufferMemory, &data);
    memcpy(data, indices.data(), (size_t)(sizeof(indices[0]) * indices.size()));
    vmaUnmapMemory(m_vmaAllocator, stagingBufferMemory);
    CopyBuffer(stagingBuffer, newMesh.indexBuffer, sizeof(indices[0]) * indices.size());
    vmaDestroyBuffer(m_vmaAllocator, stagingBuffer, stagingBufferMemory);

    newMesh.indexCount = (uint32_t)indices.size();
    
    m_mobMeshes[filepath] = newMesh;
    std::cout << "[VULKAN] Generata mesh per mob da '" << filepath << "' con " << newMesh.indexCount / 3 << " triangoli." << std::endl;
}

void RenderManager::Shutdown() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        if (m_imguiDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_device, m_imguiDescriptorPool, nullptr);
        }

        if (m_textureSampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_device, m_textureSampler, nullptr);
            m_textureSampler = VK_NULL_HANDLE;
        }
        if (m_textureImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, m_textureImageView, nullptr);
            m_textureImageView = VK_NULL_HANDLE;
        }
        if (m_textureImage != VK_NULL_HANDLE) {
            vmaDestroyImage(m_vmaAllocator, m_textureImage, m_textureImageAllocation);
            m_textureImage = VK_NULL_HANDLE;
            m_textureImageAllocation = VK_NULL_HANDLE;
        }

        // Depth buffer cleanup
        if (m_depthImageView   != VK_NULL_HANDLE) { vkDestroyImageView(m_device, m_depthImageView, nullptr);   m_depthImageView = VK_NULL_HANDLE; }
        if (m_depthImage       != VK_NULL_HANDLE) { vmaDestroyImage(m_vmaAllocator, m_depthImage, m_depthImageAllocation); m_depthImage = VK_NULL_HANDLE; m_depthImageAllocation = VK_NULL_HANDLE; }

    if (m_graphicsPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
    if (m_portalPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_portalPipeline, nullptr);
    if (m_otherWorldPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_otherWorldPipeline, nullptr);
    if (m_skyPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_skyPipeline, nullptr);
    if (m_pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
            m_pipelineLayout = VK_NULL_HANDLE;
        }
    if (m_skyPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_skyPipelineLayout, nullptr);
            m_skyPipelineLayout = VK_NULL_HANDLE;
        }

        if (m_forgePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_forgePipeline, nullptr);
            m_forgePipeline = VK_NULL_HANDLE;
        }
        if (m_forgePipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_forgePipelineLayout, nullptr);
            m_forgePipelineLayout = VK_NULL_HANDLE;
        }

        for (size_t i = 0; i < m_imageAvailableSemaphores.size(); i++) {
            vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
        }
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
            vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
            
            vmaDestroyBuffer(m_vmaAllocator, m_uniformBuffers[i], m_uniformBuffersAllocation[i]);
        }

        if (m_descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
            m_descriptorPool = VK_NULL_HANDLE;
        }

        if (m_descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
            m_descriptorSetLayout = VK_NULL_HANDLE;
        }
        if (m_commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_device, m_commandPool, nullptr);
            m_commandPool = VK_NULL_HANDLE;
        }
        for (auto framebuffer : m_framebuffers) {
            vkDestroyFramebuffer(m_device, framebuffer, nullptr);
        }
        if (m_renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_device, m_renderPass, nullptr);
            m_renderPass = VK_NULL_HANDLE;
        }
        for (auto imageView : m_swapchainImageViews) {
            vkDestroyImageView(m_device, imageView, nullptr);
        }
        for (auto& pair : m_chunkBuffers) {
            if (pair.second.vertexBuffer != VK_NULL_HANDLE) vmaDestroyBuffer(m_vmaAllocator, pair.second.vertexBuffer, pair.second.vertexBufferAllocation);
            if (pair.second.indexBuffer != VK_NULL_HANDLE) vmaDestroyBuffer(m_vmaAllocator, pair.second.indexBuffer, pair.second.indexBufferAllocation);
        }
        m_chunkBuffers.clear();
        if (m_ghostVertexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_vmaAllocator, m_ghostVertexBuffer, m_ghostVertexBufferAllocation);
        }
        if (m_ghostIndexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_vmaAllocator, m_ghostIndexBuffer, m_ghostIndexBufferAllocation);
        }
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        
        if (m_stagingRingBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_vmaAllocator, m_stagingRingBuffer, m_stagingAllocation);
            m_stagingRingBuffer = VK_NULL_HANDLE;
        }
        
        if (m_globalVramBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_vmaAllocator, m_globalVramBuffer, m_globalVramAllocation);
            m_globalVramBuffer = VK_NULL_HANDLE;
        }
        if (m_chunkVmaPool != VK_NULL_HANDLE) {
            vmaDestroyPool(m_vmaAllocator, m_chunkVmaPool);
            m_chunkVmaPool = VK_NULL_HANDLE;
        }
        if (m_transferCommandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_device, m_transferCommandPool, nullptr);
            m_transferCommandPool = VK_NULL_HANDLE;
        }
        if (m_vmaAllocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(m_vmaAllocator);
            m_vmaAllocator = VK_NULL_HANDLE;
            std::cout << "[VMA] VmaAllocator distrutto." << std::endl;
        }

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
        std::cout << "[VULKAN] Istanza distrutta." << std::endl;
    }
}

void RenderManager::InitImGui(void* hwnd) {
    // 1. Crea Descriptor Pool per ImGui
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;
    vkCreateDescriptorPool(m_device, &pool_info, nullptr, &m_imguiDescriptorPool);

    // 2. Init ImGui contest
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    
    // Carica un font di sistema moderno (Segoe UI) per un look super professionale
    ImFontConfig fontConfig;
    fontConfig.OversampleH = 2;
    fontConfig.OversampleV = 2;
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f, &fontConfig);
    
    // --- MODERN PREMIUM DARK THEME ---
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.WindowRounding = 8.0f;
    style.FramePadding = ImVec2(8.0f, 6.0f);
    style.FrameRounding = 6.0f;
    style.ItemSpacing = ImVec2(8.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
    style.ScrollbarSize = 12.0f;
    style.ScrollbarRounding = 12.0f;
    style.GrabMinSize = 12.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.PopupRounding = 6.0f;
    
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.12f, 0.12f, 0.14f, 0.95f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.12f, 0.12f, 0.14f, 0.98f);
    colors[ImGuiCol_Border]                 = ImVec4(0.25f, 0.25f, 0.27f, 0.50f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.30f, 0.30f, 0.33f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.10f, 0.10f, 0.12f, 0.51f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.10f, 0.10f, 0.12f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.31f, 0.31f, 0.33f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.41f, 0.41f, 0.43f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.51f, 0.51f, 0.53f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.50f, 0.80f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.20f, 0.35f, 0.50f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.25f, 0.45f, 0.65f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.30f, 0.55f, 0.75f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.20f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.25f, 0.30f, 0.35f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.30f, 0.35f, 0.40f, 1.00f);
    colors[ImGuiCol_Separator]              = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.40f, 0.70f, 1.00f, 0.78f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.40f, 0.70f, 1.00f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.40f, 0.70f, 1.00f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.40f, 0.70f, 1.00f, 0.95f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.18f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.25f, 0.45f, 0.65f, 1.00f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.20f, 0.35f, 0.50f, 1.00f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.15f, 0.20f, 0.25f, 1.00f);

    // 3. Init backend
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = m_instance;
    init_info.PhysicalDevice = m_physicalDevice;
    init_info.Device = m_device;
    QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    init_info.QueueFamily = indices.graphicsFamily.value();
    init_info.Queue = m_graphicsQueue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = m_imguiDescriptorPool;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.MinImageCount = 2;
    init_info.ImageCount = (uint32_t)m_swapchainImages.size();
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = nullptr;
    init_info.PipelineInfoMain.RenderPass = m_renderPass;
    ImGui_ImplVulkan_Init(&init_info);
    // 4. Font: caricati automaticamente da ImGui >= 2025/09/26
}

// ---------------------------------------------------------
// TEXTURE ARRAY (16x16 Pixel Editor)
// ---------------------------------------------------------
void RenderManager::CreateTextureImage() {
    uint32_t texWidth = 16, texHeight = 16;
    uint32_t layerCount = 16; // Supporta fino a 16 BlockType (0-15)
    VkDeviceSize imageSize = texWidth * texHeight * 4 * layerCount;

    // Crea un buffer temporaneo (staging buffer) inizializzato con colori procedurali per identificare i blocchi
    std::vector<uint8_t> pixels(imageSize, 255);
    for (uint32_t layer = 0; layer < layerCount; layer++) {
        uint8_t r = (layer * 45) % 255;
        uint8_t g = (layer * 85) % 255;
        uint8_t b = (layer * 125) % 255;
        
        // Colori specifici (hardcoded per test)
        if (layer == 1) { r = 60; g = 180; b = 40; } // Grass
        else if (layer == 2) { r = 100; g = 60; b = 30; } // Dirt
        else if (layer == 3) { r = 120; g = 120; b = 120; } // Stone
        else if (layer == 7) { r = 255; g = 100; b = 0; } // Lava
        
        for (uint32_t i = 0; i < texWidth * texHeight; i++) {
            uint32_t index = (layer * texWidth * texHeight + i) * 4;
            // Motivo a scacchiera per simulare una texture
            bool checker = ((i % texWidth) / 4 + (i / texWidth) / 4) % 2 == 0;
            pixels[index + 0] = checker ? r : std::max(0, r - 30);
            pixels[index + 1] = checker ? g : std::max(0, g - 30);
            pixels[index + 2] = checker ? b : std::max(0, b - 30);
            pixels[index + 3] = 255; // Alpha
        }
    }

    VkBuffer stagingBuffer;
    VmaAllocation stagingBufferMemory;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, stagingBuffer, stagingBufferMemory);

    void* data;
    vmaMapMemory(m_vmaAllocator, stagingBufferMemory, &data);
    memcpy(data, pixels.data(), static_cast<size_t>(imageSize));
    vmaUnmapMemory(m_vmaAllocator, stagingBufferMemory);

    CreateImage(texWidth, texHeight, layerCount, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_GPU_ONLY, m_textureImage, m_textureImageAllocation);

    TransitionImageLayout(m_textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, layerCount);
    CopyBufferToImage(stagingBuffer, m_textureImage, texWidth, texHeight, layerCount);
    TransitionImageLayout(m_textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, layerCount);

    vmaDestroyBuffer(m_vmaAllocator, stagingBuffer, stagingBufferMemory);
}

void RenderManager::CreateTextureImageView() {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_textureImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; // IMPORTANTE: Array di Texture!
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 16; // Deve corrispondere al numero di layer

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_textureImageView) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] Impossibile creare texture image view!" << std::endl;
        return;
    }
}

void RenderManager::CreateTextureSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST; // Pixel Art (no sfocatura!)
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_textureSampler) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] Impossibile creare texture sampler!" << std::endl;
        return;
    }
}

void RenderManager::CreateImage(uint32_t width, uint32_t height, uint32_t layerCount, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VmaMemoryUsage vmaUsage, VkImage& image, VmaAllocation& imageAllocation) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = layerCount; // Supporto Array!
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = vmaUsage;

    if (vmaCreateImage(m_vmaAllocator, &imageInfo, &allocInfo, &image, &imageAllocation, nullptr) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] Impossibile allocare memory image con VMA!" << std::endl;
    }
}

VkCommandBuffer RenderManager::BeginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void RenderManager::EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

void RenderManager::TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layerCount) {
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layerCount;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        throw std::invalid_argument("Transizione layout non supportata!");
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    EndSingleTimeCommands(commandBuffer);
}

void RenderManager::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount) {
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    std::vector<VkBufferImageCopy> regions;
    VkDeviceSize layerSize = width * height * 4; // 4 bytes per pixel (RGBA)

    for (uint32_t i = 0; i < layerCount; i++) {
        VkBufferImageCopy region{};
        region.bufferOffset = i * layerSize;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;

        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = i;
        region.imageSubresource.layerCount = 1;

        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};
        regions.push_back(region);
    }

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(regions.size()), regions.data());
    EndSingleTimeCommands(commandBuffer);
}

void RenderManager::UpdateTextureLayer(uint32_t layerIndex, const void* pixelData, uint32_t width, uint32_t height) {
    VkDeviceSize imageSize = width * height * 4;

    VkBuffer stagingBuffer;
    VmaAllocation stagingBufferMemory;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, stagingBuffer, stagingBufferMemory);

    void* data;
    vmaMapMemory(m_vmaAllocator, stagingBufferMemory, &data);
    memcpy(data, pixelData, static_cast<size_t>(imageSize));
    vmaUnmapMemory(m_vmaAllocator, stagingBufferMemory);

    // Dobbiamo transizionare il layout prima di poter copiare di nuovo
    TransitionImageLayout(m_textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 10);

    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = layerIndex; // Aggiorniamo SOLO questo layer!
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, m_textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    EndSingleTimeCommands(commandBuffer);

    // Rimettiamo in lettura per lo shader
    TransitionImageLayout(m_textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 10);

    vmaDestroyBuffer(m_vmaAllocator, stagingBuffer, stagingBufferMemory);
    std::cout << "[VULKAN] Texture Array Layer " << layerIndex << " aggiornato in tempo reale!" << std::endl;
}

void RenderManager::LoadBlockTextures(const std::string& baseDir, const std::vector<BlockDef>& blocks) {
    for (const auto& block : blocks) {
        if (block.id < 1 || block.id >= 10) continue; // Supportiamo solo layer validi 1-9
        
        // Cerca una texture per il blocco (preferiamo tex_top per la resa a terra visiva)
        std::string filename = block.tex_top;
        if (filename.empty()) filename = block.tex_side;
        if (filename.empty()) filename = block.tex_bottom;
        
        if (filename.empty()) continue;
        
        std::string fullPath = baseDir + filename;
        int width, height, channels;
        unsigned char* data = stbi_load(fullPath.c_str(), &width, &height, &channels, 4);
        if (data) {
            std::cout << "[VULKAN] Caricamento texture '" << fullPath << "' per il Layer " << block.id << " (" << block.name << ")" << std::endl;
            UpdateTextureLayer((uint32_t)block.id, data, (uint32_t)width, (uint32_t)height);
            stbi_image_free(data);
        } else {
            std::cout << "[VULKAN] Info: Impossibile caricare la texture '" << fullPath << "' per " << block.name << ". Rimarrà bianca." << std::endl;
        }
    }
}

bool RenderManager::LoadTextureFromFile(const std::string& filePath, uint32_t layerIndex) {
    int width, height, channels;
    unsigned char* data = stbi_load(filePath.c_str(), &width, &height, &channels, 4);
    if (data) {
        UpdateTextureLayer(layerIndex, data, (uint32_t)width, (uint32_t)height);
        stbi_image_free(data);
        std::cout << "[VULKAN] Texture caricata da file '" << filePath << "' sul Layer " << layerIndex << std::endl;
        return true;
    }
    return false;
}

void RenderManager::CleanupSwapchain() {
    for (auto framebuffer : m_framebuffers) {
        vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    }
    m_framebuffers.clear();

    if (m_depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }
    if (m_depthImage != VK_NULL_HANDLE) {
        vmaDestroyImage(m_vmaAllocator, m_depthImage, m_depthImageAllocation); m_depthImage = VK_NULL_HANDLE; m_depthImageAllocation = VK_NULL_HANDLE; }

    for (auto imageView : m_swapchainImageViews) {
        vkDestroyImageView(m_device, imageView, nullptr);
    }
    m_swapchainImageViews.clear();

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

void RenderManager::RecreateSwapchain() {
    if (m_device == VK_NULL_HANDLE || m_hwnd == nullptr || m_renderPass == VK_NULL_HANDLE) return;

    RECT rect;
    GetClientRect((HWND)m_hwnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    while (width == 0 || height == 0) {
        GetClientRect((HWND)m_hwnd, &rect);
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
        Sleep(10);
    }

    vkDeviceWaitIdle(m_device);

    CleanupSwapchain();

    CreateSwapchain(m_hwnd);
    CreateImageViews();
    CreateDepthResources();
    CreateFramebuffers();

    if (m_graphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
        m_graphicsPipeline = VK_NULL_HANDLE;
    }
    if (m_portalPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_portalPipeline, nullptr);
        m_portalPipeline = VK_NULL_HANDLE;
    }
    if (m_otherWorldPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_otherWorldPipeline, nullptr);
        m_otherWorldPipeline = VK_NULL_HANDLE;
    }
    if (m_skyPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_skyPipeline, nullptr);
        m_skyPipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_skyPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_skyPipelineLayout, nullptr);
        m_skyPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_forgePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_forgePipeline, nullptr);
        m_forgePipeline = VK_NULL_HANDLE;
    }
    if (m_forgePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_forgePipelineLayout, nullptr);
        m_forgePipelineLayout = VK_NULL_HANDLE;
    }
    CreateGraphicsPipeline();
    CreateForgePipeline();

    std::cout << "[VULKAN] Swapchain e Pipeline ricreate con successo per il ridimensionamento (" << width << "x" << height << ")" << std::endl;
}

// ---------------------------------------------------------
// FASE 5: DEFRAMMENTAZIONE A CALDO (FAST)
// ---------------------------------------------------------
void RenderManager::DefragmentVRAM() {
    if (!m_vmaAllocator || !m_chunkVmaPool) return;

    VmaDefragmentationInfo defragInfo = {};
    defragInfo.pool = m_chunkVmaPool;
    defragInfo.flags = VMA_DEFRAGMENTATION_FLAG_ALGORITHM_FAST_BIT;
    
    VmaDefragmentationContext defragCtx = VK_NULL_HANDLE;
    VkResult res = vmaBeginDefragmentation(m_vmaAllocator, &defragInfo, &defragCtx);
    
    if (res == VK_SUCCESS) {
        VmaDefragmentationPassMoveInfo pass = {};
        res = vmaBeginDefragmentationPass(m_vmaAllocator, defragCtx, &pass);
        if (res == VK_SUCCESS) {
            // Approccio "Fast" invisibile:
            // VMA unira' logicamente lo spazio libero frammentato nei suoi metadati.
            // Ignoriamo gli spostamenti fisici proposti per non dover distruggere/ricreare i VkBuffer
            // e non bloccare la GPU durante lo streaming dei chunk.
            for (uint32_t i = 0; i < pass.moveCount; i++) {
                pass.pMoves[i].operation = VMA_DEFRAGMENTATION_MOVE_OPERATION_IGNORE;
            }
            vmaEndDefragmentationPass(m_vmaAllocator, defragCtx, &pass);
        }
        vmaEndDefragmentation(m_vmaAllocator, defragCtx, nullptr);
        std::cout << "[VMA] DefragmentVRAM() Fast-Pass completato." << std::endl;
    }
}

bool RenderManager::CreateForgePipeline() {
    auto vertShaderCode = ReadFile("forge_vert.spv");
    auto fragShaderCode = ReadFile("forge_frag.spv");

    VkShaderModule vertShaderModule = CreateShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = CreateShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    // Vertex Input — legge dalla struttura Vertex definita in RenderManager.h
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding   = 0;
    bindingDesc.stride    = sizeof(Vertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 8> attrDescs{};
    attrDescs[0].binding  = 0; attrDescs[0].location = 0; attrDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrDescs[0].offset = offsetof(Vertex, pos);
    attrDescs[1].binding  = 0; attrDescs[1].location = 1; attrDescs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrDescs[1].offset = offsetof(Vertex, color);
    attrDescs[2].binding  = 0; attrDescs[2].location = 2; attrDescs[2].format = VK_FORMAT_R32G32_SFLOAT;    attrDescs[2].offset = offsetof(Vertex, roughMetal);
    attrDescs[3].binding  = 0; attrDescs[3].location = 3; attrDescs[3].format = VK_FORMAT_R32_SFLOAT;       attrDescs[3].offset = offsetof(Vertex, texIndex);
    attrDescs[4].binding  = 0; attrDescs[4].location = 4; attrDescs[4].format = VK_FORMAT_R32G32B32_SFLOAT; attrDescs[4].offset = offsetof(Vertex, normal);
    attrDescs[5].binding  = 0; attrDescs[5].location = 5; attrDescs[5].format = VK_FORMAT_R32_SFLOAT;       attrDescs[5].offset = offsetof(Vertex, ao);
    attrDescs[6].binding  = 0; attrDescs[6].location = 6; attrDescs[6].format = VK_FORMAT_R32_SFLOAT;       attrDescs[6].offset = offsetof(Vertex, light);
    attrDescs[7].binding  = 0; attrDescs[7].location = 7; attrDescs[7].format = VK_FORMAT_R32_SFLOAT;       attrDescs[7].offset = offsetof(Vertex, emissive);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInputInfo.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport State, Rasterizer, Multisample
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // Nessun culling per Forge
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Alpha Blending abilitato per la griglia/selezioni fantasma
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    // --- PUSH CONSTANTS FORGE ---
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ForgePushConstantData);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    pipelineLayoutInfo.setLayoutCount = 1; 
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;

    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_forgePipelineLayout) != VK_SUCCESS) {
        std::cerr << "[VULKAN] Errore creazione Forge Pipeline Layout!\n";
        return false;
    }

    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_forgePipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_forgePipeline) != VK_SUCCESS) {
        std::cerr << "[VULKAN] Errore creazione Forge Graphics Pipeline!\n";
        return false;
    }

    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);

    return true;
}

void RenderManager::RenderForge(VkCommandBuffer cmd, const glm::mat4& viewProjMatrix, SharedContext* context) {
    if (!context || !context->forgeWorld) return;
    auto* forgeWorld = context->forgeWorld;

    // ==========================================
    // 1. SETUP GLOBALE (Cambio di stato singolo)
    // ==========================================
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_forgePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_forgePipelineLayout, 0, 1, &m_descriptorSets[m_currentFrame], 0, nullptr);

    ForgePushConstantData pcData{};
    VkDeviceSize offsets[] = {0};

    float rawYearProgress = 0.0f;
    if (context && context->engine) {
        int currentDay = context->engine->GetTimeManager().GetCurrentDay();
        rawYearProgress = fmod((float)currentDay, 365.0f) / 365.0f;
    }
    float seasonalUboValue = (sin((rawYearProgress * 2.0f * glm::pi<float>()) - (glm::pi<float>() / 2.0f)) + 1.0f) * 0.5f;
    pcData.seasonProgress = seasonalUboValue;

    // ==========================================
    // 2. FASE STATICA: La Griglia di Lavoro
    // ==========================================
    
    // Configura Viewport e Scissor dinamicamente
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_swapchainExtent.width;
    viewport.height = (float)m_swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapchainExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    if (m_globalVramBuffer != VK_NULL_HANDLE) {
        auto& registry = forgeWorld->GetRegistry();
        auto view = registry.view<fw::MeshComponent, fw::TransformComponent>();

        CameraFrustum frustum;
        frustum.extract(viewProjMatrix);

        for (auto entity : view) {
            const auto& mesh = view.get<fw::MeshComponent>(entity);
            const auto& trans = view.get<fw::TransformComponent>(entity);

            if (!mesh.vramAlloc.valid || mesh.vertices.empty()) continue;

            if (mesh.type == fw::MeshType::Editor || mesh.type == fw::MeshType::Chunk) {
                fw::Mat4 fwModel = trans.worldMatrix();
                glm::mat4 model = glm::transpose(*reinterpret_cast<glm::mat4*>(&fwModel));

                fw::AABB bounds = mesh.bounds();
                glm::vec3 center((bounds.min.x + bounds.max.x)*0.5f, (bounds.min.y + bounds.max.y)*0.5f, (bounds.min.z + bounds.max.z)*0.5f);
                glm::vec3 extents((bounds.max.x - bounds.min.x)*0.5f, (bounds.max.y - bounds.min.y)*0.5f, (bounds.max.z - bounds.min.z)*0.5f);
                
                glm::vec3 worldCenter = glm::vec3(model * glm::vec4(center, 1.0f));
                glm::vec3 worldExtents(
                    std::abs(model[0][0]) * extents.x + std::abs(model[1][0]) * extents.y + std::abs(model[2][0]) * extents.z,
                    std::abs(model[0][1]) * extents.x + std::abs(model[1][1]) * extents.y + std::abs(model[2][1]) * extents.z,
                    std::abs(model[0][2]) * extents.x + std::abs(model[1][2]) * extents.y + std::abs(model[2][2]) * extents.z
                );
                
                if (!frustum.containsAABB(worldCenter - worldExtents, worldCenter + worldExtents)) continue;

                pcData.mvp = viewProjMatrix * model;
                pcData.useColorOverride = 0;
                pcData.colorOverride = glm::vec4(0.0f); // FIX: Reset override to prevent state leaking!
                pcData.seasonProgress = seasonalUboValue;
                
                if (mesh.colorOverride[3] > 0.0f) {
                    pcData.useColorOverride = 1;
                    pcData.colorOverride = glm::vec4(mesh.colorOverride[0], mesh.colorOverride[1], mesh.colorOverride[2], mesh.colorOverride[3]);
                }

                vkCmdPushConstants(cmd, m_forgePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ForgePushConstantData), &pcData);

                offsets[0] = mesh.vramAlloc.offset;
                VkBuffer vertexBuffers[] = { m_globalVramBuffer };
                vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
                
                vkCmdDraw(cmd, (uint32_t)mesh.vertices.size(), 1, 0, 0);
            }
        }

        // ==========================================
        // 3. FASE DINAMICA / TRASPARENTE: Elementi di Selezione
        // ==========================================
        pcData.useColorOverride = 1;
        for (auto entity : view) {
            const auto& mesh = view.get<fw::MeshComponent>(entity);
            const auto& trans = view.get<fw::TransformComponent>(entity);

            if (!mesh.vramAlloc.valid || mesh.vertices.empty()) continue;

            if (mesh.name != "GridBox" && mesh.type != fw::MeshType::Chunk) {
                fw::Mat4 fwModel = trans.worldMatrix();
                glm::mat4 model = glm::transpose(*reinterpret_cast<glm::mat4*>(&fwModel));


                pcData.mvp = viewProjMatrix * model;
                pcData.colorOverride = glm::vec4(mesh.colorOverride[0], mesh.colorOverride[1], mesh.colorOverride[2], mesh.colorOverride[3] > 0.0f ? mesh.colorOverride[3] : 1.0f);

                vkCmdPushConstants(cmd, m_forgePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ForgePushConstantData), &pcData);

                offsets[0] = mesh.vramAlloc.offset;
                VkBuffer vertexBuffers[] = { m_globalVramBuffer };
                vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
                
                vkCmdDraw(cmd, (uint32_t)mesh.vertices.size(), 1, 0, 0);
            }
        }
    }
}
