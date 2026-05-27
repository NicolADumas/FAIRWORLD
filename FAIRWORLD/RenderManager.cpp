#include "pch.h"
#include "RenderManager.h"
#include "AssetManager.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "XrManager.h"
#include <map>
#include <set>
#include <fstream>
#include "json.hpp"
#include "MobManager.h"
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_vulkan.h>

RenderManager::RenderManager() : m_isVRMode(false) {}

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
    appInfo.apiVersion = VK_API_VERSION_1_3; // Usiamo Vulkan 1.3, lo standard moderno

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

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
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
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }
        if (indices.isComplete()) break;
        i++;
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
    std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };

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

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;

    // Abilitiamo l'estensione Swapchain necessaria per mostrare immagini a schermo
    const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
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
    
    std::cout << "[VULKAN] Logical Device e Code Grafiche configurate." << std::endl;
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
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                m_depthImage, m_depthImageMemory);

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

    // --- Attachment depth (FIX: risolve facce trasparenti) ---
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format         = FindDepthFormat();
    depthAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;     // pulisce ogni frame
    depthAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE; // non serve dopo
    depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
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
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; // Accessibile solo dal Vertex Shader

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
    throw std::runtime_error("Impossibile trovare un tipo di memoria adatto!");
}

bool RenderManager::CreateUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    m_uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_uniformBuffers[i]);

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_device, m_uniformBuffers[i], &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        vkAllocateMemory(m_device, &allocInfo, nullptr, &m_uniformBuffersMemory[i]);
        vkBindBufferMemory(m_device, m_uniformBuffers[i], m_uniformBuffersMemory[i], 0);

        // Mappiamo la memoria in modo permanente per scriverci dentro ogni frame
        vkMapMemory(m_device, m_uniformBuffersMemory[i], 0, bufferSize, 0, &m_uniformBuffersMapped[i]);
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

void RenderManager::UpdateUniformBuffer(uint32_t currentImage, glm::mat4 viewMatrix) {
    UniformBufferObject ubo{};
    
    // Il triangolo resta fermo al centro (0,0,0)
    ubo.model = glm::mat4(1.0f); 

    // La telecamera ORA è controllata da te!
    ubo.view = viewMatrix;
    
    // La Proiezione: grandangolo dinamico regolabile
    float aspect = 1.0f;
    if (m_swapchainExtent.height > 0 && m_swapchainExtent.width > 0) {
        aspect = (float)m_swapchainExtent.width / (float)m_swapchainExtent.height;
    }
    ubo.proj = glm::perspective(glm::radians(m_fov), aspect, 0.1f, 100.0f);
    ubo.proj[1][1] *= -1; // GLM nasce per OpenGL, invertiamo l'asse Y per Vulkan

    // Copiamo i dati nella RAM della GPU
    memcpy(m_uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}

// ---------------------------------------------------------
// METODI HELPER FASE 4 (Shaders e Pipeline)
// ---------------------------------------------------------
std::vector<char> RenderManager::ReadFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Impossibile aprire il file shader: " + filename);
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

VkShaderModule RenderManager::CreateShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Impossibile creare il modulo shader!");
    }
    return shaderModule;
}

bool RenderManager::CreateGraphicsPipeline() {
    auto vertShaderCode = ReadFile("vert.spv");
    auto fragShaderCode = ReadFile("frag.spv");

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

    // Vertex Input — ora legge dalla struttura Vertex
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding   = 0;
    bindingDesc.stride    = sizeof(Vertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 5> attrDescs{};
    // location 0: posizione (vec3)
    attrDescs[0].binding  = 0;
    attrDescs[0].location = 0;
    attrDescs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrDescs[0].offset   = offsetof(Vertex, pos);
    // location 1: colore (vec3)
    attrDescs[1].binding  = 0;
    attrDescs[1].location = 1;
    attrDescs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrDescs[1].offset   = offsetof(Vertex, color);
    // location 2: coordinate UV (vec2)
    attrDescs[2].binding  = 0;
    attrDescs[2].location = 2;
    attrDescs[2].format   = VK_FORMAT_R32G32_SFLOAT;
    attrDescs[2].offset   = offsetof(Vertex, texCoord);
    // location 3: indice texture nell'array (float)
    attrDescs[3].binding  = 0;
    attrDescs[3].location = 3;
    attrDescs[3].format   = VK_FORMAT_R32_SFLOAT;
    attrDescs[3].offset   = offsetof(Vertex, texIndex);
    // location 4: normale (vec3)
    attrDescs[4].binding  = 0;
    attrDescs[4].location = 4;
    attrDescs[4].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrDescs[4].offset   = offsetof(Vertex, normal);

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

    // Pulizia dei moduli shader locali (sono già compilati nella pipeline!)
    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);

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

// --> QUESTA E' LA FUNZIONE CHE DISEGNA EFFETTIVAMENTE! <--
void RenderManager::RenderDesktop(glm::mat4 viewMatrix, glm::vec3 skyColor, AssetManager* assets, MobManager* mobManager, Player* player) {
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    // imageAvailable[currentFrame]: protetto dal fence sopra => e' sicuro risegnalarlo
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame],
        VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Impossibile acquisire l'immagine della swapchain!");
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

    UpdateUniformBuffer(m_currentFrame, viewMatrix);

    vkCmdBindPipeline(m_commandBuffers[m_currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
    vkCmdBindDescriptorSets(m_commandBuffers[m_currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSets[m_currentFrame], 0, nullptr);

    // Disegna tutti i chunk visibili
    for (const auto& pair : m_chunkBuffers) {
        const auto& chunkBuf = pair.second;
        if (chunkBuf.vertexBuffer != VK_NULL_HANDLE && chunkBuf.indexBuffer != VK_NULL_HANDLE && chunkBuf.indexCount > 0) {
            VkBuffer vertexBuffers[] = { chunkBuf.vertexBuffer };
            VkDeviceSize offsets[]   = { 0 };
            vkCmdBindVertexBuffers(m_commandBuffers[m_currentFrame], 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(m_commandBuffers[m_currentFrame], chunkBuf.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            glm::mat4 identityModel = glm::mat4(1.0f);
            glm::vec4 noColorOffset = glm::vec4(0.0f);
            vkCmdPushConstants(m_commandBuffers[m_currentFrame], m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &identityModel);
            vkCmdPushConstants(m_commandBuffers[m_currentFrame], m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4), sizeof(glm::vec4), &noColorOffset);

            vkCmdDrawIndexed(m_commandBuffers[m_currentFrame], chunkBuf.indexCount, 1, 0, 0, 0);
        }
    }

    // --- DISEGNO MESH GHOST ---
    if (m_ghostVertexBuffer != VK_NULL_HANDLE && m_ghostIndexBuffer != VK_NULL_HANDLE && m_ghostIndexCount > 0) {
        VkBuffer ghostBuffers[] = { m_ghostVertexBuffer };
        VkDeviceSize offsets[]   = { 0 };
        vkCmdBindVertexBuffers(m_commandBuffers[m_currentFrame], 0, 1, ghostBuffers, offsets);
        vkCmdBindIndexBuffer(m_commandBuffers[m_currentFrame], m_ghostIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

        glm::mat4 ghostModel = glm::mat4(1.0f);
        // Usa a>0.5 per forzare il vertex shader a usare il colore dell'offset.
        // E usa alpha = 0.5 per la trasparenza (richiede blending abilitato).
        glm::vec4 ghostColorOffset = glm::vec4(0.0f, 0.8f, 1.0f, 0.6f); 
        vkCmdPushConstants(m_commandBuffers[m_currentFrame], m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &ghostModel);
        vkCmdPushConstants(m_commandBuffers[m_currentFrame], m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4), sizeof(glm::vec4), &ghostColorOffset);

        vkCmdDrawIndexed(m_commandBuffers[m_currentFrame], m_ghostIndexCount, 1, 0, 0, 0);
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
            vkCmdBindVertexBuffers(m_commandBuffers[m_currentFrame], 0, 1, mobVertexBuffers, offsets);
            vkCmdBindIndexBuffer(m_commandBuffers[m_currentFrame], mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            glm::mat4 mobModel = glm::translate(glm::mat4(1.0f), mob.position);
            
            // Applica il colore in base al danno ricevuto (Rosso se in cooldown attacco per feedback)
            glm::vec4 colorOffset = glm::vec4(0.0f);
            if (mob.attackCooldownTimer > 0.0f) {
                colorOffset = glm::vec4(1.0f, 0.2f, 0.2f, 1.0f); // Override colore a rosso
            }

            vkCmdPushConstants(m_commandBuffers[m_currentFrame], m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &mobModel);
            vkCmdPushConstants(m_commandBuffers[m_currentFrame], m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4), sizeof(glm::vec4), &colorOffset);

            vkCmdDrawIndexed(m_commandBuffers[m_currentFrame], mesh.indexCount, 1, 0, 0, 0);
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
            vkCmdBindVertexBuffers(m_commandBuffers[m_currentFrame], 0, 1, weaponVertexBuffers, offsets);
            vkCmdBindIndexBuffer(m_commandBuffers[m_currentFrame], mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            // La matrice è GIA' IN WORLD-SPACE (calcolata nel Player.rightHandTransform)!
            glm::mat4 weaponModel = player->rightHandTransform;
            
            // Scaliamo un po' l'arma per farla sembrare un oggetto in mano (0.4x)
            weaponModel = glm::scale(weaponModel, glm::vec3(0.4f));

            glm::vec4 colorOffset = glm::vec4(0.0f); // Nessun feedback danno sull'arma

            vkCmdPushConstants(m_commandBuffers[m_currentFrame], m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &weaponModel);
            vkCmdPushConstants(m_commandBuffers[m_currentFrame], m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4), sizeof(glm::vec4), &colorOffset);

            vkCmdDrawIndexed(m_commandBuffers[m_currentFrame], mesh.indexCount, 1, 0, 0, 0);
        }
    }

    ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data) {
        ImGui_ImplVulkan_RenderDrawData(draw_data, m_commandBuffers[m_currentFrame]);
    }

    vkCmdEndRenderPass(m_commandBuffers[m_currentFrame]);
    vkEndCommandBuffer(m_commandBuffers[m_currentFrame]);

    // Submit: attende imageAvailable[currentFrame], segnala renderFinished[imageIndex]
    VkSemaphore waitSemaphores[]   = { m_imageAvailableSemaphores[m_currentFrame] };
    VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[imageIndex] }; // <-- KEY FIX
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    VkSubmitInfo submitInfo{};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = waitSemaphores;
    submitInfo.pWaitDstStageMask    = waitStages;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &m_commandBuffers[m_currentFrame];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = signalSemaphores;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]);

    // Present: attende renderFinished[imageIndex]
    // La prossima volta che la swapchain restituisce imageIndex, la sua presentazione e' finita
    // => renderFinishedSemaphores[imageIndex] sara' gia' consumato e puo' essere risegnalato
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = signalSemaphores;
    VkSwapchainKHR swapchains[]    = { m_swapchain };
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = swapchains;
    presentInfo.pImageIndices      = &imageIndex;

    VkResult resultPresent = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    if (resultPresent == VK_ERROR_OUT_OF_DATE_KHR || resultPresent == VK_SUBOPTIMAL_KHR) {
        RecreateSwapchain();
    } else if (resultPresent != VK_SUCCESS) {
        throw std::runtime_error("Impossibile presentare l'immagine della swapchain!");
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

// ---------------------------------------------------------
// VERTEX / INDEX BUFFER HELPERS
// ---------------------------------------------------------
bool RenderManager::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags properties,
                                  VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size  = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) return false;

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(m_device, buffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, properties);
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) return false;

    vkBindBufferMemory(m_device, buffer, bufferMemory, 0);
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
        vkDeviceWaitIdle(m_device);
        if (it->second.vertexBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(m_device, it->second.vertexBuffer, nullptr); }
        if (it->second.vertexBufferMemory != VK_NULL_HANDLE) { vkFreeMemory(m_device, it->second.vertexBufferMemory, nullptr); }
        if (it->second.indexBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(m_device, it->second.indexBuffer, nullptr); }
        if (it->second.indexBufferMemory != VK_NULL_HANDLE) { vkFreeMemory(m_device, it->second.indexBufferMemory, nullptr); }
        m_chunkBuffers.erase(it);
    }
}

void RenderManager::UploadChunkMesh(ChunkCoord coord, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    DestroyChunkBuffer(coord);

    if (vertices.empty() || indices.empty()) return;

    VulkanChunkBuffer chunkBuf;
    chunkBuf.indexCount = (uint32_t)indices.size();

    VkDeviceSize vSize = sizeof(vertices[0]) * vertices.size();
    VkDeviceSize iSize = sizeof(indices[0]) * indices.size();

    // Staging Vertex
    VkBuffer stagingVBuf; VkDeviceMemory stagingVMem;
    if (!CreateBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingVBuf, stagingVMem)) return;
    void* vData; vkMapMemory(m_device, stagingVMem, 0, vSize, 0, &vData);
    memcpy(vData, vertices.data(), (size_t)vSize);
    vkUnmapMemory(m_device, stagingVMem);

    if (!CreateBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, chunkBuf.vertexBuffer, chunkBuf.vertexBufferMemory)) return;
    CopyBuffer(stagingVBuf, chunkBuf.vertexBuffer, vSize);
    vkDestroyBuffer(m_device, stagingVBuf, nullptr);
    vkFreeMemory(m_device, stagingVMem, nullptr);

    // Staging Index
    VkBuffer stagingIBuf; VkDeviceMemory stagingIMem;
    if (!CreateBuffer(iSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingIBuf, stagingIMem)) return;
    void* iData; vkMapMemory(m_device, stagingIMem, 0, iSize, 0, &iData);
    memcpy(iData, indices.data(), (size_t)iSize);
    vkUnmapMemory(m_device, stagingIMem);

    if (!CreateBuffer(iSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, chunkBuf.indexBuffer, chunkBuf.indexBufferMemory)) return;
    CopyBuffer(stagingIBuf, chunkBuf.indexBuffer, iSize);
    vkDestroyBuffer(m_device, stagingIBuf, nullptr);
    vkFreeMemory(m_device, stagingIMem, nullptr);

    m_chunkBuffers[coord] = chunkBuf;
}

void RenderManager::UploadGhostMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) {
        m_ghostIndexCount = 0;
        return;
    }
    VkDeviceSize vSize = sizeof(vertices[0]) * vertices.size();
    VkDeviceSize iSize = sizeof(indices[0]) * indices.size();

    VkBuffer stagingBuf; VkDeviceMemory stagingMem;
    if (!CreateBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      stagingBuf, stagingMem)) return;

    void* data;
    vkMapMemory(m_device, stagingMem, 0, vSize, 0, &data);
    memcpy(data, vertices.data(), (size_t)vSize);
    vkUnmapMemory(m_device, stagingMem);

    if (m_ghostVertexBuffer != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
        vkDestroyBuffer(m_device, m_ghostVertexBuffer, nullptr);
        vkFreeMemory(m_device, m_ghostVertexBufferMemory, nullptr);
        vkDestroyBuffer(m_device, m_ghostIndexBuffer, nullptr);
        vkFreeMemory(m_device, m_ghostIndexBufferMemory, nullptr);
    }

    if (!CreateBuffer(vSize,
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      m_ghostVertexBuffer, m_ghostVertexBufferMemory)) return;

    CopyBuffer(stagingBuf, m_ghostVertexBuffer, vSize);
    vkDestroyBuffer(m_device, stagingBuf, nullptr);
    vkFreeMemory(m_device, stagingMem, nullptr);

    if (!CreateBuffer(iSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      stagingBuf, stagingMem)) return;

    vkMapMemory(m_device, stagingMem, 0, iSize, 0, &data);
    memcpy(data, indices.data(), (size_t)iSize);
    vkUnmapMemory(m_device, stagingMem);

    if (!CreateBuffer(iSize,
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      m_ghostIndexBuffer, m_ghostIndexBufferMemory)) return;

    CopyBuffer(stagingBuf, m_ghostIndexBuffer, iSize);
    vkDestroyBuffer(m_device, stagingBuf, nullptr);
    vkFreeMemory(m_device, stagingMem, nullptr);

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
            glm::vec3 col(r, g, b);

            uint32_t startIdx = (uint32_t)vertices.size();

            // 8 vertici del cubetto
            vertices.push_back({{vx - s, vy - s, vz + s}, col, {0.0f, 0.0f}, -1.0f}); // 0: front bottom left
            vertices.push_back({{vx + s, vy - s, vz + s}, col, {1.0f, 0.0f}, -1.0f}); // 1: front bottom right
            vertices.push_back({{vx + s, vy + s, vz + s}, col, {1.0f, 1.0f}, -1.0f}); // 2: front top right
            vertices.push_back({{vx - s, vy + s, vz + s}, col, {0.0f, 1.0f}, -1.0f}); // 3: front top left
            vertices.push_back({{vx - s, vy - s, vz - s}, col, {0.0f, 0.0f}, -1.0f}); // 4: back bottom left
            vertices.push_back({{vx + s, vy - s, vz - s}, col, {1.0f, 0.0f}, -1.0f}); // 5: back bottom right
            vertices.push_back({{vx + s, vy + s, vz - s}, col, {1.0f, 1.0f}, -1.0f}); // 6: back top right
            vertices.push_back({{vx - s, vy + s, vz - s}, col, {0.0f, 1.0f}, -1.0f}); // 7: back top left

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
    CreateBuffer(sizeof(vertices[0]) * vertices.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, newMesh.vertexBuffer, newMesh.vertexBufferMemory);
    CreateBuffer(sizeof(indices[0]) * indices.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, newMesh.indexBuffer, newMesh.indexBufferMemory);

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    // Upload Vertices
    CreateBuffer(sizeof(vertices[0]) * vertices.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
    void* data;
    vkMapMemory(m_device, stagingBufferMemory, 0, sizeof(vertices[0]) * vertices.size(), 0, &data);
    memcpy(data, vertices.data(), (size_t)(sizeof(vertices[0]) * vertices.size()));
    vkUnmapMemory(m_device, stagingBufferMemory);
    CopyBuffer(stagingBuffer, newMesh.vertexBuffer, sizeof(vertices[0]) * vertices.size());
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);

    // Upload Indices
    CreateBuffer(sizeof(indices[0]) * indices.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
    vkMapMemory(m_device, stagingBufferMemory, 0, sizeof(indices[0]) * indices.size(), 0, &data);
    memcpy(data, indices.data(), (size_t)(sizeof(indices[0]) * indices.size()));
    vkUnmapMemory(m_device, stagingBufferMemory);
    CopyBuffer(stagingBuffer, newMesh.indexBuffer, sizeof(indices[0]) * indices.size());
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);

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

        // Depth buffer cleanup
        if (m_depthImageView   != VK_NULL_HANDLE) { vkDestroyImageView(m_device, m_depthImageView, nullptr);   m_depthImageView = VK_NULL_HANDLE; }
        if (m_depthImage       != VK_NULL_HANDLE) { vkDestroyImage(m_device, m_depthImage, nullptr);           m_depthImage = VK_NULL_HANDLE; }
        if (m_depthImageMemory != VK_NULL_HANDLE) { vkFreeMemory(m_device, m_depthImageMemory, nullptr);       m_depthImageMemory = VK_NULL_HANDLE; }

        if (m_graphicsPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
            m_graphicsPipeline = VK_NULL_HANDLE;
        }
        if (m_pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
            m_pipelineLayout = VK_NULL_HANDLE;
        }

        for (size_t i = 0; i < m_imageAvailableSemaphores.size(); i++) {
            vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
        }
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
            vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
            
            vkDestroyBuffer(m_device, m_uniformBuffers[i], nullptr);
            vkFreeMemory(m_device, m_uniformBuffersMemory[i], nullptr);
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
            if (pair.second.vertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, pair.second.vertexBuffer, nullptr);
            if (pair.second.vertexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, pair.second.vertexBufferMemory, nullptr);
            if (pair.second.indexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, pair.second.indexBuffer, nullptr);
            if (pair.second.indexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, pair.second.indexBufferMemory, nullptr);
        }
        m_chunkBuffers.clear();
        if (m_ghostVertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, m_ghostVertexBuffer, nullptr);
            vkFreeMemory(m_device, m_ghostVertexBufferMemory, nullptr);
        }
        if (m_ghostIndexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, m_ghostIndexBuffer, nullptr);
            vkFreeMemory(m_device, m_ghostIndexBufferMemory, nullptr);
        }
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
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
    uint32_t layerCount = 10; // Fino a 10 texture contemporanee
    VkDeviceSize imageSize = texWidth * texHeight * 4 * layerCount;

    // Crea un buffer temporaneo (staging buffer) inizializzato con pixel vuoti
    std::vector<uint8_t> pixels(imageSize, 255); // Tutto bianco di default

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(m_device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels.data(), static_cast<size_t>(imageSize));
    vkUnmapMemory(m_device, stagingBufferMemory);

    CreateImage(texWidth, texHeight, layerCount, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_textureImage, m_textureImageMemory);

    TransitionImageLayout(m_textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, layerCount);
    CopyBufferToImage(stagingBuffer, m_textureImage, texWidth, texHeight, layerCount);
    TransitionImageLayout(m_textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, layerCount);

    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);
}

void RenderManager::CreateTextureImageView() {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_textureImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; // IMPORTANTE: Array di Texture!
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 10; // Deve corrispondere al numero di layer

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_textureImageView) != VK_SUCCESS) {
        throw std::runtime_error("Impossibile creare texture image view!");
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
        throw std::runtime_error("Impossibile creare texture sampler!");
    }
}

void RenderManager::CreateImage(uint32_t width, uint32_t height, uint32_t layerCount, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
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

    if (vkCreateImage(m_device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("Impossibile creare image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Impossibile allocare memory image!");
    }

    vkBindImageMemory(m_device, image, imageMemory, 0);
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

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = layerCount;

    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    EndSingleTimeCommands(commandBuffer);
}

void RenderManager::UpdateTextureLayer(uint32_t layerIndex, const void* pixelData, uint32_t width, uint32_t height) {
    VkDeviceSize imageSize = width * height * 4;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(m_device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixelData, static_cast<size_t>(imageSize));
    vkUnmapMemory(m_device, stagingBufferMemory);

    // Dobbiamo transizionare il layout prima di poter copiare di nuovo
    TransitionImageLayout(m_textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 10);

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
    TransitionImageLayout(m_textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 10);

    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);
    std::cout << "[VULKAN] Texture Array Layer " << layerIndex << " aggiornato in tempo reale!" << std::endl;
}

void RenderManager::LoadBlockTextures(const std::string& baseDir, const std::vector<BlockDef>& blocks) {
    for (const auto& block : blocks) {
        if (block.id < 1 || block.id >= 10) continue; // Supportiamo solo layer validi 1-9
        
        // Cerca una texture per il blocco (preferiamo tex_side, poi tex_top, poi tex_bottom)
        std::string filename = block.tex_side;
        if (filename.empty()) filename = block.tex_top;
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
        vkDestroyImage(m_device, m_depthImage, nullptr);
        m_depthImage = VK_NULL_HANDLE;
    }
    if (m_depthImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_depthImageMemory, nullptr);
        m_depthImageMemory = VK_NULL_HANDLE;
    }

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
    if (m_device == VK_NULL_HANDLE || m_hwnd == nullptr) return;

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
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    CreateGraphicsPipeline();

    std::cout << "[VULKAN] Swapchain e Pipeline ricreate con successo per il ridimensionamento (" << width << "x" << height << ")" << std::endl;
}
