#include "pch.h"
#include "RenderManager.h"
#include "FAIRWORLD.h"
#include "AssetManager.h"
#include "stb_image.h"
#include "XrManager.h"
#include <map>
#include <set>
#include <fstream>
#include "json.hpp"
#include "MobManager.h"
#include "SharedContext.h"
#include "TimeManager.h"
#include "StateManager.h"
#include "ForgeWorld.h"
#include "ForgeComponents.h"
#include <algorithm>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_vulkan.h>
#include <fstream>
#include <sstream>





RenderManager::RenderManager() : m_isVRMode(false) {
    m_blockMakerRenderer = std::make_unique<fw::BlockMakerRenderer>();
    m_mapRenderer = std::make_unique<fw::MapRenderer>();
    m_forgeRenderer = std::make_unique<fw::ForgeRenderer>();
    m_playRenderer = std::make_unique<fw::PlayRenderer>();
    m_physicsLabRenderer = std::make_unique<fw::PhysicsLabRenderer>();
    m_chunkEditorRenderer = std::make_unique<fw::ChunkEditorRenderer>();
    m_planetMapperRenderer = std::make_unique<fw::PlanetMapperRenderer>();
    m_solarSystemRenderer = std::make_unique<fw::SolarSystemRenderer>();
}

VkDeviceMemory RenderManager::GetStagingDeviceMemory() const {
    return m_memory ? m_memory->GetStagingDeviceMemory() : VK_NULL_HANDLE;
}

RenderManager::~RenderManager() {
    Shutdown();
}

bool RenderManager::Init(bool isVRMode, XrManager* xrManager, void* hwnd, void* hinstance) {
    m_isVRMode = isVRMode;
    m_hwnd = hwnd;

    m_core = std::make_unique<fw::VulkanCore>();
    if (!m_core->Initialize(isVRMode, xrManager, hwnd, hinstance)) return false;
    m_memory = std::make_unique<fw::VulkanMemory>(m_core.get());
    if (!m_memory->Initialize()) return false;

    // --- 1. CREATE TRANSFER COMMAND POOL E COMMAND BUFFER ---
    fw::QueueFamilyIndices indices = m_core->FindQueueFamilies(m_core->GetPhysicalDevice());
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = indices.transferFamily.value();
    
    if (vkCreateCommandPool(m_core->GetDevice(), &poolInfo, nullptr, &m_transferCommandPool) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] failed to create transfer command pool!" << std::endl;
        return false;
    }
    
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = m_transferCommandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_core->GetDevice(), &cmdAllocInfo, &m_transferCommandBuffer) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] failed to allocate transfer command buffer!" << std::endl;
        return false;
    }
    
    // Inizia subito a registrare
    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(m_transferCommandBuffer, &beginInfo);

    // FASE 3.3: Creazione della Swapchain (Gia' gestita da VulkanCore)
    // FASE 3.4, 4 e 5: Creazione del Render Loop, Pipeline e UBO
    if (!CreateRenderPass()) return false;
    
    if (!CreateCommandPoolAndBuffer()) return false;

    // L'inizializzazione del Layout dei Descriptor viene fatta qui.
    // L'allocazione dei set viene fatta in CreateDescriptorPoolAndSets (senza aggiornarli con le texture).
    // Le texture (Albedo, ecc.) verranno caricate esternamente in CreatePBRTextures, dove aggiorneremo i set.

    if (!CreateDescriptorSetLayout()) return false;
    if (!m_memory->CreateUniformBuffers(sizeof(UniformBufferObject))) return false;
    if (!m_memory->CreateDescriptorPoolAndSets(m_descriptorSetLayout, m_forgeDescriptorSetLayout, sizeof(UniformBufferObject))) return false;

    if (!CreateGraphicsPipeline()) return false;
    if (!CreateForgePipeline()) return false;
    
    // Inizializza la pipeline del terreno (Compute Shader)
    m_terrainPipeline = std::make_unique<TerrainPipelineSystem>(GetDevice(), GetPhysicalDevice());
    auto genCode = ReadFile("terrain_generation.spv");
    VkShaderModule genModule = CreateShaderModule(genCode);
    m_terrainPipeline->init(50000, 1000, genModule); 
    vkDestroyShaderModule(GetDevice(), genModule, nullptr);

    // Depth buffer: creato DOPO la pipeline (ha bisogno del command pool per i layout)
    if (!CreateDepthResources()) {
        std::cerr << "[VULKAN ERROR] Impossibile creare il Depth Buffer!" << std::endl;
        return false;
    }

    if (!CreateFramebuffers()) return false;
    if (!CreateSyncObjects()) return false;

    // Inizializza ImGui dopo che Vulkan è pronto
    InitImGui(hwnd);
    
    m_isFullyInitialized = true; // Da questo momento, RecreateSwapchain è sicuro
    std::cout << "[VULKAN] Motore Grafico pronto. Pronti a renderizzare!" << std::endl;

    return true;
}




// ---------------------------------------------------------
// STEP 1: CREAZIONE DELLA SURFACE (Il ponte con Windows)
// ---------------------------------------------------------




// ---------------------------------------------------------
// STEP 2: CREAZIONE DEL LOGICAL DEVICE
// ---------------------------------------------------------

// ---------------------------------------------------------
// STEP 3: CREAZIONE DELLA SWAPCHAIN (La Pellicola)
// ---------------------------------------------------------


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
        vkGetPhysicalDeviceFormatProperties(m_core->GetPhysicalDevice(), format, &props);
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
    CreateImage(m_core->GetSwapchainExtent().width, m_core->GetSwapchainExtent().height, 1,
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

    if (vkCreateImageView(m_core->GetDevice(), &viewInfo, nullptr, &m_depthImageView) != VK_SUCCESS) {
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
    colorAttachment.format         = m_core->GetSwapchainImageFormat();
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

    if (vkCreateRenderPass(m_core->GetDevice(), &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
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
    if (m_renderPass == VK_NULL_HANDLE) {
        OutputDebugStringA("[RenderManager] ERROR: m_renderPass is VK_NULL_HANDLE during CreateFramebuffers!\n");
        return false;
    }

    char debugMsg[256];
    sprintf_s(debugMsg, "[DEBUG] CreateFramebuffers chiamato! m_renderPass = %p\n", (void*)m_renderPass);
    OutputDebugStringA(debugMsg);

    m_framebuffers.resize(m_core->GetSwapchainImageViews().size());
    for (size_t i = 0; i < m_core->GetSwapchainImageViews().size(); i++) {
        // Ogni framebuffer ha: color attachment + depth attachment
        std::array<VkImageView, 2> attachments = {
            m_core->GetSwapchainImageViews()[i],
            m_depthImageView          // FIX: depth buffer!
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass      = m_renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments    = attachments.data();
        framebufferInfo.width           = m_core->GetSwapchainExtent().width;
        framebufferInfo.height          = m_core->GetSwapchainExtent().height;
        framebufferInfo.layers          = 1;

        if (vkCreateFramebuffer(m_core->GetDevice(), &framebufferInfo, nullptr, &m_framebuffers[i]) != VK_SUCCESS) return false;
    }
    return true;
}

// ---------------------------------------------------------
// FASE 5: DESCRIPTOR SETS & UNIFORM BUFFERS
// ---------------------------------------------------------
bool RenderManager::CreateDescriptorSetLayout() {
    // 1. LEGACY DESCRIPTOR SET LAYOUT (Per shader.vert e shader.frag)
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

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

    if (vkCreateDescriptorSetLayout(m_core->GetDevice(), &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) return false;

    // 2. FORGE DESCRIPTOR SET LAYOUT (Per forge_vert.spv e forge_frag.spv)
    VkDescriptorSetLayoutBinding albedoLayoutBinding{};
    albedoLayoutBinding.binding = 0;
    albedoLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    albedoLayoutBinding.descriptorCount = 1;
    albedoLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding normalLayoutBinding{};
    normalLayoutBinding.binding = 1;
    normalLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalLayoutBinding.descriptorCount = 1;
    normalLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding ormLayoutBinding{};
    ormLayoutBinding.binding = 2;
    ormLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ormLayoutBinding.descriptorCount = 1;
    ormLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 3> forgeBindings = { albedoLayoutBinding, normalLayoutBinding, ormLayoutBinding };
    VkDescriptorSetLayoutCreateInfo forgeLayoutInfo{};
    forgeLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    forgeLayoutInfo.bindingCount = static_cast<uint32_t>(forgeBindings.size());
    forgeLayoutInfo.pBindings = forgeBindings.data();

    if (vkCreateDescriptorSetLayout(m_core->GetDevice(), &forgeLayoutInfo, nullptr, &m_forgeDescriptorSetLayout) != VK_SUCCESS) return false;

    return true;
}




void RenderManager::UpdateUniformBuffer(uint32_t currentImage, glm::mat4 viewMatrix, glm::mat4 projMatrix, float seasonProgress, SharedContext* context) {
    UniformBufferObject ubo{};
    
    // Il triangolo resta fermo al centro (0,0,0)
    ubo.model = glm::mat4(1.0f); 

    // La telecamera ORA è controllata da te!
    ubo.view = viewMatrix;
    
    // La Proiezione: grandangolo dinamico regolabile
    ubo.proj = projMatrix;

    // Assegnamo il progresso stagionale passato dall'esterno
    ubo.seasonProgress = seasonProgress;

    // Aggiungiamo il Color Mode per debug e i parametri BlockMaker
    if (context) {
        ubo.debugColorMode = context->debugColorMode;
        ubo.isBlockMakerMode = context->isBlockMakerMode ? 1 : 0;
        ubo.globalLightDir = glm::vec4(context->previewLightDir, 0.0f);
    } else {
        ubo.debugColorMode = 0;
        ubo.isBlockMakerMode = 0;
        ubo.globalLightDir = glm::vec4(0.5f, -1.0f, 0.5f, 0.0f);
    }

    // Copiamo i dati nella RAM della GPU
    memcpy(m_memory->GetUniformBuffersMapped()[currentImage], &ubo, sizeof(ubo));
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
    if (vkCreateShaderModule(m_core->GetDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
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
        if (vertShaderModule) vkDestroyShaderModule(m_core->GetDevice(), vertShaderModule, nullptr);
        if (fragShaderModule) vkDestroyShaderModule(m_core->GetDevice(), fragShaderModule, nullptr);
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
    // location 3: indice texture / materialID (uint)
    attrDescs[3].binding  = 0;
    attrDescs[3].location = 3;
    attrDescs[3].format   = VK_FORMAT_R32_UINT;
    attrDescs[3].offset   = offsetof(Vertex, materialID);
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
    viewport.width = (float)m_core->GetSwapchainExtent().width;
    viewport.height = (float)m_core->GetSwapchainExtent().height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = m_core->GetSwapchainExtent();

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

    if (vkCreatePipelineLayout(m_core->GetDevice(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) return false;

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

    if (vkCreateGraphicsPipelines(m_core->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline) != VK_SUCCESS) return false;

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

    if (vkCreateGraphicsPipelines(m_core->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_portalPipeline) != VK_SUCCESS) return false;

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

    if (vkCreateGraphicsPipelines(m_core->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_otherWorldPipeline) != VK_SUCCESS) return false;

    // Pulizia dei moduli shader locali (sono già compilati nella pipeline!)
    vkDestroyShaderModule(m_core->GetDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(m_core->GetDevice(), vertShaderModule, nullptr);

    // =========================================================================
    // SKY PIPELINE (Nessun Vertex Buffer, usa gl_VertexIndex nel vertex shader)
    // =========================================================================
    auto skyVertCode = ReadFile("bin/sky_vert.spv");
    auto skyFragCode = ReadFile("bin/sky_frag.spv");
    
    VkShaderModule skyVertModule = CreateShaderModule(skyVertCode);
    VkShaderModule skyFragModule = CreateShaderModule(skyFragCode);

    if (skyVertModule == VK_NULL_HANDLE || skyFragModule == VK_NULL_HANDLE) {
        std::cerr << "[VULKAN ERROR] Moduli shader per il cielo mancanti!" << std::endl;
        if (skyVertModule) vkDestroyShaderModule(m_core->GetDevice(), skyVertModule, nullptr);
        if (skyFragModule) vkDestroyShaderModule(m_core->GetDevice(), skyFragModule, nullptr);
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

    if (vkCreatePipelineLayout(m_core->GetDevice(), &skyPipelineLayoutInfo, nullptr, &m_skyPipelineLayout) != VK_SUCCESS) {
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

    if (vkCreateGraphicsPipelines(m_core->GetDevice(), VK_NULL_HANDLE, 1, &skyPipelineInfo, nullptr, &m_skyPipeline) != VK_SUCCESS) {
        return false;
    }

    vkDestroyShaderModule(m_core->GetDevice(), skyFragModule, nullptr);
    vkDestroyShaderModule(m_core->GetDevice(), skyVertModule, nullptr);

    return true;
}

// ---------------------------------------------------------
// STEP 3: COMMAND POOL & BUFFER (La memoria per le istruzioni)
// ---------------------------------------------------------
bool RenderManager::CreateCommandPoolAndBuffer() {
    fw::QueueFamilyIndices indices = m_core->FindQueueFamilies(m_core->GetPhysicalDevice());

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

    if (vkCreateCommandPool(m_core->GetDevice(), &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) return false;

    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)m_commandBuffers.size();

    if (vkAllocateCommandBuffers(m_core->GetDevice(), &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) return false;
    return true;
}

// ---------------------------------------------------------
// STEP 4: SINCRONIZZAZIONE E RENDER LOOP (Il semaforo)
// ---------------------------------------------------------
bool RenderManager::CreateSyncObjects() {
    uint32_t swapchainImageCount = (uint32_t)m_core->GetSwapchainImages().size();

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
        if (vkCreateSemaphore(m_core->GetDevice(), &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_core->GetDevice(), &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            return false;
        }
    }
    for (size_t i = 0; i < swapchainImageCount; i++) {
        if (vkCreateSemaphore(m_core->GetDevice(), &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS) {
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
            if (length > 0.00001f) {
                planes[i] /= length;
            }
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
void RenderManager::RenderFairworld(VkCommandBuffer cmd, glm::mat4 viewMatrix, glm::vec3 skyColor, SharedContext* context, AssetManager* assets, MobManager* mobManager, Player* player, fw::ForgeWorld* overrideWorld) {
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
        
        glm::mat4 projMatrix = context ? context->activeCameraView.projectionMatrix : glm::perspective(glm::radians(45.0f), m_core->GetSwapchainExtent().width / (float)m_core->GetSwapchainExtent().height, 0.1f, 1000.0f);
        skyPC.invView = glm::inverse(skyView);
        skyPC.invProj = glm::inverse(projMatrix);
        skyPC.timeOfDay = context && context->engine ? (context->engine->GetTimeManager().GetTimeOfDay() / 24.0f) : 0.5f;
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

    float aspectUniform = (float)m_core->GetSwapchainExtent().width / (float)m_core->GetSwapchainExtent().height;
    glm::mat4 uboProjMatrix = context ? context->activeCameraView.projectionMatrix : glm::perspective(glm::radians(m_fov), aspectUniform, 0.1f, 1000.0f);
    if (!context) uboProjMatrix[1][1] *= -1;

    UpdateUniformBuffer(m_currentFrame, viewMatrix, uboProjMatrix, seasonalUboValue, context);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_memory->GetDescriptorSets()[m_currentFrame], 0, nullptr);

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

    // --- FAIRWORLD INSTANCED MESHER ---
    if (m_forgePipeline != VK_NULL_HANDLE && context && (context->forgeWorld || overrideWorld)) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_forgePipeline);
        
        if (!m_memory->GetForgeDescriptorSets().empty() && m_memory->GetForgeDescriptorSets()[m_currentFrame] != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_forgePipelineLayout, 0, 1, &m_memory->GetForgeDescriptorSets()[m_currentFrame], 0, nullptr);
        }

        // Viewport e Scissor dinamici
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)m_core->GetSwapchainExtent().width;
        viewport.height = (float)m_core->GetSwapchainExtent().height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = m_core->GetSwapchainExtent();
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        ForgePushConstantData pcData{};
        VkDeviceSize offsets[] = { 0 };

        fw::GameWorld* activeWorld = overrideWorld ? overrideWorld : context->forgeWorld;
        auto& registry = activeWorld->GetRegistry();
        auto view = registry.view<fw::MeshComponent, fw::TransformComponent>();

        // ATTENZIONE: In RenderFairworld viewMatrix è SOLO la View matrix!
        float aspect = (float)m_core->GetSwapchainExtent().width / (float)m_core->GetSwapchainExtent().height;
        glm::mat4 projMatrix = context ? context->activeCameraView.projectionMatrix : glm::perspective(glm::radians(m_fov), aspect, 0.1f, 2000.0f);
        glm::mat4 viewProjMatrix = projMatrix * viewMatrix;
        
        // Per l'estrazione delle culling planes usiamo una prospettiva standard (senza Y-flip)
        glm::mat4 cullProj = glm::perspective(glm::radians(m_fov), aspect, 0.1f, 2000.0f);
        CameraFrustum frustum;
        frustum.extract(cullProj * viewMatrix);

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
                VkBuffer vertexBuffers[] = { m_memory->GetGlobalVramBuffer() };
                vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
                
                vkCmdDraw(cmd, (uint32_t)mesh.vertices.size(), 1, 0, 0);
            }
        }
        
        // RIPRISTINA LA PIPELINE ORIGINALE PER GLI ALTRI ELEMENTI DI FAIRWORLD
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_memory->GetDescriptorSets()[m_currentFrame], 0, nullptr);
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
    if (m_core->GetDevice() == VK_NULL_HANDLE) return;
    vkWaitForFences(m_core->GetDevice(), 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    // imageAvailable[currentFrame]: protetto dal fence sopra => e' sicuro risegnalarlo
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(m_core->GetDevice(), m_core->GetSwapchain(), UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame],
        VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR) {
        RecreateSwapchain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        std::cerr << "[VULKAN ERROR] Impossibile acquisire l'immagine della swapchain! Error: " << result << std::endl;
        return;
    }

    vkResetFences(m_core->GetDevice(), 1, &m_inFlightFences[m_currentFrame]);
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
    renderPassInfo.renderArea.extent = m_core->GetSwapchainExtent();

    // Svuota sia il colore che il depth ogni frame
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color        = {{ skyColor.x, skyColor.y, skyColor.z, 1.0f }}; // colore dinamico cielo
    clearValues[1].depthStencil = { 1.0f, 0 };                     // depth = 1.0 (massimo)
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues    = clearValues.data();

    vkCmdBeginRenderPass(m_commandBuffers[m_currentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    bool isForge = (context && context->isForgeMode);
    if (isForge) {
        float aspect = (float)m_core->GetSwapchainExtent().width / (float)m_core->GetSwapchainExtent().height;
        glm::mat4 projMatrix = context ? context->activeCameraView.projectionMatrix : glm::perspective(glm::radians(m_fov), aspect, 0.1f, 1000.0f);
        glm::mat4 viewProjMatrix = projMatrix * viewMatrix;
        glm::mat4 invView = glm::inverse(viewMatrix);
        glm::vec3 cameraPos = glm::vec3(invView[3]);
        RenderForge(m_commandBuffers[m_currentFrame], viewProjMatrix, cameraPos, context);
    } else {
        if (context && context->stateManager && context->stateManager->GetCurrentState()) {
            GameMode mode = context->engine->GetGameMode();
            float aspect = (float)m_core->GetSwapchainExtent().width / (float)m_core->GetSwapchainExtent().height;
            glm::mat4 projMatrix = context->activeCameraView.projectionMatrix;
            if (projMatrix == glm::mat4(0.0f)) {
                projMatrix = glm::perspective(glm::radians(m_fov), aspect, 0.1f, 1000.0f);
            }

            if (mode == GameMode::BlockMaker && m_blockMakerRenderer) {
                m_blockMakerRenderer->SetSwapchainExtent(m_core->GetSwapchainExtent());
                m_blockMakerRenderer->SetCurrentFrame(m_currentFrame);
                m_blockMakerRenderer->Draw(m_commandBuffers[m_currentFrame], context, viewMatrix, projMatrix);
            }
            else if (mode == GameMode::Map && m_mapRenderer) {
                m_mapRenderer->SetSwapchainExtent(m_core->GetSwapchainExtent());
                m_mapRenderer->SetCurrentFrame(m_currentFrame);
                m_mapRenderer->Draw(m_commandBuffers[m_currentFrame], context, viewMatrix, projMatrix);
            }
            else if (mode == GameMode::ChunkEditor && m_chunkEditorRenderer) {
                m_chunkEditorRenderer->SetSwapchainExtent(m_core->GetSwapchainExtent());
                m_chunkEditorRenderer->SetCurrentFrame(m_currentFrame);
                m_chunkEditorRenderer->Draw(m_commandBuffers[m_currentFrame], context, viewMatrix, projMatrix);
            }
            else if (mode == GameMode::PlanetMapper && m_planetMapperRenderer) {
                m_planetMapperRenderer->SetSwapchainExtent(m_core->GetSwapchainExtent());
                m_planetMapperRenderer->SetCurrentFrame(m_currentFrame);
                m_planetMapperRenderer->Draw(m_commandBuffers[m_currentFrame], context, viewMatrix, projMatrix);
                
                // Fallback to also draw standard MapRenderer meshes (Spherical LODs)
                if (m_mapRenderer) {
                    m_mapRenderer->SetSwapchainExtent(m_core->GetSwapchainExtent());
                    m_mapRenderer->SetCurrentFrame(m_currentFrame);
                    m_mapRenderer->Draw(m_commandBuffers[m_currentFrame], context, viewMatrix, projMatrix);
                }
            }
            else if (mode == GameMode::SolarSystem && m_solarSystemRenderer) {
                m_solarSystemRenderer->SetSwapchainExtent(m_core->GetSwapchainExtent());
                m_solarSystemRenderer->SetCurrentFrame(m_currentFrame);
                m_solarSystemRenderer->Draw(m_commandBuffers[m_currentFrame], context, viewMatrix, projMatrix);
            }
            else if (mode == GameMode::Dev && m_forgeRenderer) {
                m_forgeRenderer->SetSwapchainExtent(m_core->GetSwapchainExtent());
                m_forgeRenderer->SetCurrentFrame(m_currentFrame);
                m_forgeRenderer->Draw(m_commandBuffers[m_currentFrame], context, viewMatrix, projMatrix);
            }
            else if (mode == GameMode::PhysicsLab && m_physicsLabRenderer) {
                m_physicsLabRenderer->SetSwapchainExtent(m_core->GetSwapchainExtent());
                m_physicsLabRenderer->SetCurrentFrame(m_currentFrame);
                m_physicsLabRenderer->Draw(m_commandBuffers[m_currentFrame], context, viewMatrix, projMatrix);
            }
            else if (mode == GameMode::Play && m_playRenderer) {
                m_playRenderer->SetSwapchainExtent(m_core->GetSwapchainExtent());
                m_playRenderer->SetCurrentFrame(m_currentFrame);
                m_playRenderer->Draw(m_commandBuffers[m_currentFrame], context, viewMatrix, projMatrix);
                RenderFairworld(m_commandBuffers[m_currentFrame], viewMatrix, skyColor, context, assets, mobManager, player);
            }
            else {
                RenderFairworld(m_commandBuffers[m_currentFrame], viewMatrix, skyColor, context, assets, mobManager, player);
            }
        }
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
        std::lock_guard<std::mutex> lock((*m_core->GetQueueMutex()));
        if (vkQueueSubmit(m_core->GetGraphicsQueue(), 1, &submitInfo, m_inFlightFences[m_currentFrame]) != VK_SUCCESS) {
            std::cerr << "[VULKAN ERROR] Impossibile sottomettere il Draw Command Buffer!" << std::endl;
        }
    }

    // Presentazione dell'immagine sullo schermo
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = { m_core->GetSwapchain() };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult resultPresent;
    {
        std::lock_guard<std::mutex> lock((*m_core->GetQueueMutex()));
        resultPresent = vkQueuePresentKHR(m_core->GetPresentQueue(), &presentInfo);
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
void RenderManager::CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) {
    // Alloca un command buffer temporaneo per la copia
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuf;
    vkAllocateCommandBuffers(m_core->GetDevice(), &allocInfo, &cmdBuf);

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
    vkQueueSubmit(m_core->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_core->GetGraphicsQueue());

    vkFreeCommandBuffers(m_core->GetDevice(), m_commandPool, 1, &cmdBuf);
}

void RenderManager::DestroyChunkBuffer(ChunkCoord coord) {
    auto it = m_chunkBuffers.find(coord);
    if (it != m_chunkBuffers.end()) {
        // [FIX] Flush the transfer batch FIRST, because the OLD buffer might have just been created 
        // and recorded in the current transfer batch (if Update() ran multiple times this frame).
        FlushTransferBatch();

        vkDeviceWaitIdle(m_core->GetDevice());
        if (it->second.vertexBuffer != VK_NULL_HANDLE) { vmaDestroyBuffer(m_memory->GetAllocator(), it->second.vertexBuffer, it->second.vertexBufferAllocation); }
        if (it->second.indexBuffer != VK_NULL_HANDLE) { vmaDestroyBuffer(m_memory->GetAllocator(), it->second.indexBuffer, it->second.indexBufferAllocation); }
        m_chunkBuffers.erase(it);
    }
}

void RenderManager::InvalidateForgeCache() {
    std::lock_guard<std::mutex> lock((*m_core->GetQueueMutex()));
    vkDeviceWaitIdle(m_core->GetDevice());
    
    // Clear legacy chunk buffers
    for (auto& pair : m_chunkBuffers) {
        if (pair.second.vertexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_memory->GetAllocator(), pair.second.vertexBuffer, pair.second.vertexBufferAllocation);
        }
        if (pair.second.indexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_memory->GetAllocator(), pair.second.indexBuffer, pair.second.indexBufferAllocation);
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
    uint8_t* dstMapped = static_cast<uint8_t*>(m_memory->GetMappedStagingData());
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
    vbAllocInfo.pool = m_memory->GetChunkVmaPool(); // Use the chunk pool!
    VkResult vbResult = vmaCreateBuffer(m_memory->GetAllocator(), &vbInfo, &vbAllocInfo, &chunkBuf.vertexBuffer, &chunkBuf.vertexBufferAllocation, nullptr);
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
    ibAllocInfo.pool = m_memory->GetChunkVmaPool();
    VkResult ibResult = vmaCreateBuffer(m_memory->GetAllocator(), &ibInfo, &ibAllocInfo, &chunkBuf.indexBuffer, &chunkBuf.indexBufferAllocation, nullptr);
    if (ibResult != VK_SUCCESS || chunkBuf.indexBuffer == VK_NULL_HANDLE) {
        std::cerr << "[UploadChunkMesh] vmaCreateBuffer for index buffer FAILED: " << ibResult << std::endl;
        // Cleanup vertex buffer created before
        if (chunkBuf.vertexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_memory->GetAllocator(), chunkBuf.vertexBuffer, chunkBuf.vertexBufferAllocation);
        }
        return;
    }

    // 5. Registra i comandi di copia nel Transfer Command Buffer
    // Verifica che gli handle siano validi prima di usarli
    if (m_transferCommandBuffer == VK_NULL_HANDLE || m_memory->GetStagingRingBuffer() == VK_NULL_HANDLE) {
        std::cerr << "[UploadChunkMesh] TransferCommandBuffer or StagingRingBuffer is INVALID!" << std::endl;
        // Cleanup both buffers
        vmaDestroyBuffer(m_memory->GetAllocator(), chunkBuf.vertexBuffer, chunkBuf.vertexBufferAllocation);
        vmaDestroyBuffer(m_memory->GetAllocator(), chunkBuf.indexBuffer, chunkBuf.indexBufferAllocation);
        return;
    }

    VkBufferCopy vertexCopyRegion = {};
    vertexCopyRegion.srcOffset = vertexOffset;
    vertexCopyRegion.dstOffset = 0;
    vertexCopyRegion.size = vertexSize;
    vkCmdCopyBuffer(m_transferCommandBuffer, m_memory->GetStagingRingBuffer(), chunkBuf.vertexBuffer, 1, &vertexCopyRegion);

    VkBufferCopy indexCopyRegion = {};
    indexCopyRegion.srcOffset = indexOffset;
    indexCopyRegion.dstOffset = 0;
    indexCopyRegion.size = indexSize;
    vkCmdCopyBuffer(m_transferCommandBuffer, m_memory->GetStagingRingBuffer(), chunkBuf.indexBuffer, 1, &indexCopyRegion);

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

    vkQueueSubmit(m_core->GetTransferQueue(), 1, &submitInfo, VK_NULL_HANDLE);

    // Iterazione 1: WaitIdle (Sincronizzazione dura a fine batch)
    vkQueueWaitIdle(m_core->GetTransferQueue());

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
    if (!m_memory->CreateBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VMA_MEMORY_USAGE_CPU_TO_GPU,
                      stagingBuf, stagingMem)) return;

    void* data;
    vmaMapMemory(m_memory->GetAllocator(), stagingMem, &data);
    memcpy(data, vertices.data(), (size_t)vSize);
    vmaUnmapMemory(m_memory->GetAllocator(), stagingMem);

    if (m_ghostVertexBuffer != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_core->GetDevice());
        vmaDestroyBuffer(m_memory->GetAllocator(), m_ghostVertexBuffer, m_ghostVertexBufferAllocation);
        vmaDestroyBuffer(m_memory->GetAllocator(), m_ghostIndexBuffer, m_ghostIndexBufferAllocation);
    }

    if (!m_memory->CreateBuffer(vSize,
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VMA_MEMORY_USAGE_GPU_ONLY,
                      m_ghostVertexBuffer, m_ghostVertexBufferAllocation)) return;

    CopyBuffer(stagingBuf, m_ghostVertexBuffer, vSize);
    vmaDestroyBuffer(m_memory->GetAllocator(), stagingBuf, stagingMem);

    if (!m_memory->CreateBuffer(iSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VMA_MEMORY_USAGE_CPU_TO_GPU,
                      stagingBuf, stagingMem)) return;

    vmaMapMemory(m_memory->GetAllocator(), stagingMem, &data);
    memcpy(data, indices.data(), (size_t)iSize);
    vmaUnmapMemory(m_memory->GetAllocator(), stagingMem);

    if (!m_memory->CreateBuffer(iSize,
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VMA_MEMORY_USAGE_GPU_ONLY,
                      m_ghostIndexBuffer, m_ghostIndexBufferAllocation)) return;

    CopyBuffer(stagingBuf, m_ghostIndexBuffer, iSize);
    vmaDestroyBuffer(m_memory->GetAllocator(), stagingBuf, stagingMem);

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
            vertices.push_back({{vx - s, vy - s, vz + s}, col4, {0.0f, 0.0f}, 0, {0,0,0}, 1.0f, 1.0f, 0.0f}); // 0: front bottom left
            vertices.push_back({{vx + s, vy - s, vz + s}, col4, {1.0f, 0.0f}, 0, {0,0,0}, 1.0f, 1.0f, 0.0f}); // 1: front bottom right
            vertices.push_back({{vx + s, vy + s, vz + s}, col4, {1.0f, 1.0f}, 0, {0,0,0}, 1.0f, 1.0f, 0.0f}); // 2: front top right
            vertices.push_back({{vx - s, vy + s, vz + s}, col4, {0.0f, 1.0f}, 0, {0,0,0}, 1.0f, 1.0f, 0.0f}); // 3: front top left
            vertices.push_back({{vx - s, vy - s, vz - s}, col4, {0.0f, 0.0f}, 0, {0,0,0}, 1.0f, 1.0f, 0.0f}); // 4: back bottom left
            vertices.push_back({{vx + s, vy - s, vz - s}, col4, {1.0f, 0.0f}, 0, {0,0,0}, 1.0f, 1.0f, 0.0f}); // 5: back bottom right
            vertices.push_back({{vx + s, vy + s, vz - s}, col4, {1.0f, 1.0f}, 0, {0,0,0}, 1.0f, 1.0f, 0.0f}); // 6: back top right
            vertices.push_back({{vx - s, vy + s, vz - s}, col4, {0.0f, 1.0f}, 0, {0,0,0}, 1.0f, 1.0f, 0.0f}); // 7: back top left

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
    m_memory->CreateBuffer(sizeof(vertices[0]) * vertices.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, newMesh.vertexBuffer, newMesh.vertexBufferAllocation);
    m_memory->CreateBuffer(sizeof(indices[0]) * indices.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, newMesh.indexBuffer, newMesh.indexBufferAllocation);

    VkBuffer stagingBuffer;
    VmaAllocation stagingBufferMemory;

    // Upload Vertices
    m_memory->CreateBuffer(sizeof(vertices[0]) * vertices.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, stagingBuffer, stagingBufferMemory);
    void* data;
    vmaMapMemory(m_memory->GetAllocator(), stagingBufferMemory, &data);
    memcpy(data, vertices.data(), (size_t)(sizeof(vertices[0]) * vertices.size()));
    vmaUnmapMemory(m_memory->GetAllocator(), stagingBufferMemory);
    CopyBuffer(stagingBuffer, newMesh.vertexBuffer, sizeof(vertices[0]) * vertices.size());
    vmaDestroyBuffer(m_memory->GetAllocator(), stagingBuffer, stagingBufferMemory);

    // Upload Indices
    m_memory->CreateBuffer(sizeof(indices[0]) * indices.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, stagingBuffer, stagingBufferMemory);
    vmaMapMemory(m_memory->GetAllocator(), stagingBufferMemory, &data);
    memcpy(data, indices.data(), (size_t)(sizeof(indices[0]) * indices.size()));
    vmaUnmapMemory(m_memory->GetAllocator(), stagingBufferMemory);
    CopyBuffer(stagingBuffer, newMesh.indexBuffer, sizeof(indices[0]) * indices.size());
    vmaDestroyBuffer(m_memory->GetAllocator(), stagingBuffer, stagingBufferMemory);

    newMesh.indexCount = (uint32_t)indices.size();
    
    m_mobMeshes[filepath] = newMesh;
    std::cout << "[VULKAN] Generata mesh per mob da '" << filepath << "' con " << newMesh.indexCount / 3 << " triangoli." << std::endl;
}

void RenderManager::Shutdown() {
    m_isFullyInitialized = false; // Blocca RecreateSwapchain durante lo shutdown
    if (m_core->GetDevice() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_core->GetDevice());

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        if (m_memory->GetImguiDescriptorPool() != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_core->GetDevice(), m_memory->GetImguiDescriptorPool(), nullptr);
        }

        if (m_textureSampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_core->GetDevice(), m_textureSampler, nullptr);
            m_textureSampler = VK_NULL_HANDLE;
        }
        if (m_albedoImageView != VK_NULL_HANDLE) { vkDestroyImageView(m_core->GetDevice(), m_albedoImageView, nullptr); m_albedoImageView = VK_NULL_HANDLE; }
        if (m_normalImageView != VK_NULL_HANDLE) { vkDestroyImageView(m_core->GetDevice(), m_normalImageView, nullptr); m_normalImageView = VK_NULL_HANDLE; }
        if (m_ormImageView    != VK_NULL_HANDLE) { vkDestroyImageView(m_core->GetDevice(), m_ormImageView,    nullptr); m_ormImageView    = VK_NULL_HANDLE; }
        if (m_albedoImage != VK_NULL_HANDLE) { vmaDestroyImage(m_memory->GetAllocator(), m_albedoImage, m_albedoImageAllocation); m_albedoImage = VK_NULL_HANDLE; }
        if (m_normalImage != VK_NULL_HANDLE) { vmaDestroyImage(m_memory->GetAllocator(), m_normalImage, m_normalImageAllocation); m_normalImage = VK_NULL_HANDLE; }
        if (m_ormImage    != VK_NULL_HANDLE) { vmaDestroyImage(m_memory->GetAllocator(), m_ormImage,    m_ormImageAllocation);    m_ormImage    = VK_NULL_HANDLE; }
        if (m_memory->GetForgeDescriptorPool() != VK_NULL_HANDLE) { vkDestroyDescriptorPool(m_core->GetDevice(), m_memory->GetForgeDescriptorPool(), nullptr); m_memory->GetForgeDescriptorPool() = VK_NULL_HANDLE; }
        if (m_forgeDescriptorSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(m_core->GetDevice(), m_forgeDescriptorSetLayout, nullptr); m_forgeDescriptorSetLayout = VK_NULL_HANDLE; }

        // Depth buffer cleanup
        if (m_depthImageView   != VK_NULL_HANDLE) { vkDestroyImageView(m_core->GetDevice(), m_depthImageView, nullptr);   m_depthImageView = VK_NULL_HANDLE; }
        if (m_depthImage       != VK_NULL_HANDLE) { vmaDestroyImage(m_memory->GetAllocator(), m_depthImage, m_depthImageAllocation); m_depthImage = VK_NULL_HANDLE; m_depthImageAllocation = VK_NULL_HANDLE; }

    if (m_graphicsPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_core->GetDevice(), m_graphicsPipeline, nullptr);
    if (m_portalPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_core->GetDevice(), m_portalPipeline, nullptr);
    if (m_otherWorldPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_core->GetDevice(), m_otherWorldPipeline, nullptr);
    if (m_skyPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_core->GetDevice(), m_skyPipeline, nullptr);
    if (m_pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_core->GetDevice(), m_pipelineLayout, nullptr);
            m_pipelineLayout = VK_NULL_HANDLE;
        }
    if (m_skyPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_core->GetDevice(), m_skyPipelineLayout, nullptr);
            m_skyPipelineLayout = VK_NULL_HANDLE;
        }

        if (m_forgePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_core->GetDevice(), m_forgePipeline, nullptr);
            m_forgePipeline = VK_NULL_HANDLE;
        }
        if (m_forgePipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_core->GetDevice(), m_forgePipelineLayout, nullptr);
            m_forgePipelineLayout = VK_NULL_HANDLE;
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(m_core->GetDevice(), m_imageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(m_core->GetDevice(), m_renderFinishedSemaphores[i], nullptr);
            vkDestroyFence(m_core->GetDevice(), m_inFlightFences[i], nullptr);
        }

        if (m_memory->GetDescriptorPool() != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_core->GetDevice(), m_memory->GetDescriptorPool(), nullptr);
            m_memory->GetDescriptorPool() = VK_NULL_HANDLE;
        }

        if (m_descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_core->GetDevice(), m_descriptorSetLayout, nullptr);
            m_descriptorSetLayout = VK_NULL_HANDLE;
        }
        if (m_commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_core->GetDevice(), m_commandPool, nullptr);
            m_commandPool = VK_NULL_HANDLE;
        }
        for (auto framebuffer : m_framebuffers) {
            vkDestroyFramebuffer(m_core->GetDevice(), framebuffer, nullptr);
        }
        if (m_renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_core->GetDevice(), m_renderPass, nullptr);
            m_renderPass = VK_NULL_HANDLE;
        }
        for (auto imageView : m_core->GetSwapchainImageViews()) {
            vkDestroyImageView(m_core->GetDevice(), imageView, nullptr);
        }
        for (auto& pair : m_chunkBuffers) {
            if (pair.second.vertexBuffer != VK_NULL_HANDLE) vmaDestroyBuffer(m_memory->GetAllocator(), pair.second.vertexBuffer, pair.second.vertexBufferAllocation);
            if (pair.second.indexBuffer != VK_NULL_HANDLE) vmaDestroyBuffer(m_memory->GetAllocator(), pair.second.indexBuffer, pair.second.indexBufferAllocation);
        }
        m_chunkBuffers.clear();
        if (m_ghostVertexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_memory->GetAllocator(), m_ghostVertexBuffer, m_ghostVertexBufferAllocation);
        }
        if (m_ghostIndexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_memory->GetAllocator(), m_ghostIndexBuffer, m_ghostIndexBufferAllocation);
        }
        vkDestroySwapchainKHR(m_core->GetDevice(), m_core->GetSwapchain(), nullptr);
        
        if (m_transferCommandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_core->GetDevice(), m_transferCommandPool, nullptr);
            m_transferCommandPool = VK_NULL_HANDLE;
        }

        m_memory.reset();

        m_core.reset();
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
    vkCreateDescriptorPool(m_core->GetDevice(), &pool_info, nullptr, &m_memory->GetImguiDescriptorPool());

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
    init_info.Instance = m_core->GetInstance();
    init_info.PhysicalDevice = m_core->GetPhysicalDevice();
    init_info.Device = m_core->GetDevice();
    fw::QueueFamilyIndices indices = m_core->FindQueueFamilies(m_core->GetPhysicalDevice());
    init_info.QueueFamily = indices.graphicsFamily.value();
    init_info.Queue = m_core->GetGraphicsQueue();
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = m_memory->GetImguiDescriptorPool();
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.MinImageCount = 2;
    init_info.ImageCount = (uint32_t)m_core->GetSwapchainImages().size();
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
void RenderManager::CreatePBRTextures(const fw::PackedTextureData& data) {
    if (data.layerCount == 0 || data.albedoData.empty()) return;

    VkDeviceSize imageSize = data.width * data.height * 4 * data.layerCount;

    auto createTextureArray = [&](const std::vector<uint8_t>& pixels, VkImage& image, VmaAllocation& alloc, VkImageView& view) {
        VkBuffer stagingBuffer;
        VmaAllocation stagingBufferMemory;
        
        // Usiamo CPU_ONLY per evitare di esaurire la memoria BAR (CPU_TO_GPU) allocando 256MB
        if (!m_memory->CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer, stagingBufferMemory)) {
            std::cerr << "[VULKAN ERROR] CreateBuffer (Staging) fallito per " << imageSize / 1024 / 1024 << " MB!" << std::endl;
            return;
        }

        void* mappedData = nullptr;
        if (vmaMapMemory(m_memory->GetAllocator(), stagingBufferMemory, &mappedData) != VK_SUCCESS || mappedData == nullptr) {
            std::cerr << "[VULKAN ERROR] Impossibile mappare lo staging buffer!" << std::endl;
            return;
        }
        memcpy(mappedData, pixels.data(), static_cast<size_t>(imageSize));
        vmaUnmapMemory(m_memory->GetAllocator(), stagingBufferMemory);

        CreateImage(data.width, data.height, data.layerCount, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, 
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_GPU_ONLY, image, alloc);

        TransitionImageLayout(image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, data.layerCount);
        CopyBufferToImage(stagingBuffer, image, data.width, data.height, data.layerCount);
        TransitionImageLayout(image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, data.layerCount);

        vmaDestroyBuffer(m_memory->GetAllocator(), stagingBuffer, stagingBufferMemory);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = data.layerCount;

        if (vkCreateImageView(m_core->GetDevice(), &viewInfo, nullptr, &view) != VK_SUCCESS) {
            std::cerr << "[VULKAN ERROR] Impossibile creare texture image view!" << std::endl;
        }
    };

    createTextureArray(data.albedoData, m_albedoImage, m_albedoImageAllocation, m_albedoImageView);
    createTextureArray(data.normalData, m_normalImage, m_normalImageAllocation, m_normalImageView);
    createTextureArray(data.ormData, m_ormImage, m_ormImageAllocation, m_ormImageView);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
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

    if (vkCreateSampler(m_core->GetDevice(), &samplerInfo, nullptr, &m_textureSampler) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] Impossibile creare texture sampler!" << std::endl;
    }
    
    // --- CREA/AGGIORNA FORGE DESCRIPTOR SETS ---
    if (m_memory->GetForgeDescriptorPool() != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_core->GetDevice(), m_memory->GetForgeDescriptorPool(), nullptr);
        m_memory->GetForgeDescriptorPool() = VK_NULL_HANDLE;
    }
    
    std::array<VkDescriptorPoolSize, 1> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) * 3;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    if (vkCreateDescriptorPool(m_core->GetDevice(), &poolInfo, nullptr, &m_memory->GetForgeDescriptorPool()) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] CreateForgeDescriptorPool fallito!" << std::endl;
        return;
    }

    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_forgeDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_memory->GetForgeDescriptorPool();
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    m_memory->GetForgeDescriptorSets().resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(m_core->GetDevice(), &allocInfo, m_memory->GetForgeDescriptorSets().data()) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] vkAllocateDescriptorSets per forge fallito!" << std::endl;
        return;
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorImageInfo albedoInfo{};
        albedoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        albedoInfo.imageView = m_albedoImageView;
        albedoInfo.sampler = m_textureSampler;

        VkDescriptorImageInfo normalInfo{};
        normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        normalInfo.imageView = m_normalImageView;
        normalInfo.sampler = m_textureSampler;

        VkDescriptorImageInfo ormInfo{};
        ormInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ormInfo.imageView = m_ormImageView;
        ormInfo.sampler = m_textureSampler;

        std::array<VkWriteDescriptorSet, 3> descriptorWrites{};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = m_memory->GetForgeDescriptorSets()[i];
        descriptorWrites[0].dstBinding = 0; // albedo
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &albedoInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = m_memory->GetForgeDescriptorSets()[i];
        descriptorWrites[1].dstBinding = 1; // normal
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &normalInfo;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = m_memory->GetForgeDescriptorSets()[i];
        descriptorWrites[2].dstBinding = 2; // orm
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &ormInfo;
        vkUpdateDescriptorSets(m_core->GetDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);

        // --- Aggiorna anche i vecchi descriptor set (m_memory->GetDescriptorSets()) ---
        // Altrimenti il binding fallirà e manderà in crash le vecchie pipeline (es. per mob e player)
        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = m_memory->GetUniformBuffers()[i];
        uboInfo.offset = 0;
        uboInfo.range = sizeof(UniformBufferObject);

        std::array<VkWriteDescriptorSet, 2> legacyWrites{};

        legacyWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        legacyWrites[0].dstSet = m_memory->GetDescriptorSets()[i];
        legacyWrites[0].dstBinding = 0;
        legacyWrites[0].dstArrayElement = 0;
        legacyWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        legacyWrites[0].descriptorCount = 1;
        legacyWrites[0].pBufferInfo = &uboInfo;

        legacyWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        legacyWrites[1].dstSet = m_memory->GetDescriptorSets()[i];
        legacyWrites[1].dstBinding = 1;
        legacyWrites[1].dstArrayElement = 0;
        legacyWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        legacyWrites[1].descriptorCount = 1;
        legacyWrites[1].pImageInfo = &albedoInfo; // Usiamo l'albedo come texture legacy

        vkUpdateDescriptorSets(m_core->GetDevice(), static_cast<uint32_t>(legacyWrites.size()), legacyWrites.data(), 0, nullptr);
    }
}

void RenderManager::UpdateTextureLayerSolidColor(VkImage image, uint32_t layerIndex, uint32_t width, uint32_t height, const glm::vec4& color) {
    if (image == VK_NULL_HANDLE) return;

    VkDeviceSize imageSize = width * height * 4;
    std::vector<uint8_t> pixels(imageSize);
    uint8_t r = static_cast<uint8_t>(glm::clamp(color.r * 255.0f, 0.0f, 255.0f));
    uint8_t g = static_cast<uint8_t>(glm::clamp(color.g * 255.0f, 0.0f, 255.0f));
    uint8_t b = static_cast<uint8_t>(glm::clamp(color.b * 255.0f, 0.0f, 255.0f));
    uint8_t a = static_cast<uint8_t>(glm::clamp(color.a * 255.0f, 0.0f, 255.0f));
    
    for (size_t i = 0; i < imageSize; i += 4) {
        pixels[i] = r;
        pixels[i+1] = g;
        pixels[i+2] = b;
        pixels[i+3] = a;
    }

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    m_memory->CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer, stagingAllocation);

    void* data;
    vmaMapMemory(m_memory->GetAllocator(), stagingAllocation, &data);
    memcpy(data, pixels.data(), static_cast<size_t>(imageSize));
    vmaUnmapMemory(m_memory->GetAllocator(), stagingAllocation);

    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    // Transizione a TRANSFER_DST
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = layerIndex;
    barrier.subresourceRange.layerCount = 1;

    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copia
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = layerIndex;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transizione indietro a SHADER_READ_ONLY
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    EndSingleTimeCommands(commandBuffer);

    vmaDestroyBuffer(m_memory->GetAllocator(), stagingBuffer, stagingAllocation);
}

void RenderManager::UpdateMaterialFallback(uint32_t layerIndex, const glm::vec3& baseColor, float roughness, float metallic) {
    if (m_albedoImage == VK_NULL_HANDLE || m_ormImage == VK_NULL_HANDLE) return;

    // Aggiorna l'Albedo se non ci sono texture vere, altrimenti lascia la mappa originale intatta?
    // Nel Block Maker vogliamo forzare il colore fallback per feedback visivo, se l'utente sposta lo slider!
    UpdateTextureLayerSolidColor(m_albedoImage, layerIndex, 512, 512, glm::vec4(baseColor, 1.0f));

    // Aggiorna ORM map (Ambient Occlusion = 1.0, Roughness, Metallic)
    UpdateTextureLayerSolidColor(m_ormImage, layerIndex, 512, 512, glm::vec4(1.0f, roughness, metallic, 1.0f));
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

    if (vmaCreateImage(m_memory->GetAllocator(), &imageInfo, &allocInfo, &image, &imageAllocation, nullptr) != VK_SUCCESS) {
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
    vkAllocateCommandBuffers(m_core->GetDevice(), &allocInfo, &commandBuffer);

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

    vkQueueSubmit(m_core->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_core->GetGraphicsQueue());

    vkFreeCommandBuffers(m_core->GetDevice(), m_commandPool, 1, &commandBuffer);
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

void RenderManager::UpdateTextureLayer(uint32_t layerIndex, const void* pixelData, uint32_t width, uint32_t height, PBRTextureType type) {
    if (m_albedoImage == VK_NULL_HANDLE || m_normalImage == VK_NULL_HANDLE || m_ormImage == VK_NULL_HANDLE) return; // Texture PBR non ancora caricate
    VkDeviceSize imageSize = width * height * 4;


    VkBuffer stagingBuffer;
    VmaAllocation stagingBufferMemory;
    m_memory->CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, stagingBuffer, stagingBufferMemory);

    void* data;
    vmaMapMemory(m_memory->GetAllocator(), stagingBufferMemory, &data);
    memcpy(data, pixelData, static_cast<size_t>(imageSize));
    vmaUnmapMemory(m_memory->GetAllocator(), stagingBufferMemory);

    VkImage targetImage = VK_NULL_HANDLE;
    if (type == PBRTextureType::ALBEDO) targetImage = m_albedoImage;
    else if (type == PBRTextureType::NORMAL) targetImage = m_normalImage;
    else if (type == PBRTextureType::ORM) targetImage = m_ormImage;

    if (targetImage == VK_NULL_HANDLE) return;

    // Aggiorna il layer nell'array
    TransitionImageLayout(targetImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 256);

    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = width;
    region.bufferImageHeight = height;
    
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = layerIndex;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, targetImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    EndSingleTimeCommands(commandBuffer);

    // Transizione indietro a SHADER_READ_ONLY
    TransitionImageLayout(targetImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 256);

    vmaDestroyBuffer(m_memory->GetAllocator(), stagingBuffer, stagingBufferMemory);
    std::cout << "[VULKAN] Texture Layer " << layerIndex << " aggiornato in tempo reale!" << std::endl;
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
            UpdateTextureLayer((uint32_t)block.id, data, (uint32_t)width, (uint32_t)height, PBRTextureType::ALBEDO);
            stbi_image_free(data);
        } else {
            std::cout << "[VULKAN] Info: Impossibile caricare la texture '" << fullPath << "' per " << block.name << ". Rimarrà bianca." << std::endl;
        }
    }
}

bool RenderManager::LoadPBRTextureFromFile(const std::string& filePath, uint32_t layerIndex, PBRTextureType type) {
    int width, height, channels;
    unsigned char* data = stbi_load(filePath.c_str(), &width, &height, &channels, 4);
    if (data) {
        UpdateTextureLayer(layerIndex, data, (uint32_t)width, (uint32_t)height, type);
        stbi_image_free(data);
        std::cout << "[VULKAN] PBR Texture caricata da file '" << filePath << "' sul Layer " << layerIndex << std::endl;
        return true;
    }
    return false;
}

void RenderManager::CleanupSwapchain() {
    for (auto framebuffer : m_framebuffers) {
        vkDestroyFramebuffer(m_core->GetDevice(), framebuffer, nullptr);
    }
    m_framebuffers.clear();

    if (m_depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_core->GetDevice(), m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }
    if (m_depthImage != VK_NULL_HANDLE) {
        vmaDestroyImage(m_memory->GetAllocator(), m_depthImage, m_depthImageAllocation); m_depthImage = VK_NULL_HANDLE; m_depthImageAllocation = VK_NULL_HANDLE; }
}

void RenderManager::RecreateSwapchain() {
    if (m_core->GetDevice() == VK_NULL_HANDLE || m_hwnd == nullptr || m_renderPass == VK_NULL_HANDLE) return;

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

    vkDeviceWaitIdle(m_core->GetDevice());

    CleanupSwapchain();

    m_core->RecreateSwapchain(m_hwnd);
    
    CreateDepthResources();
    CreateFramebuffers();

    if (m_graphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_core->GetDevice(), m_graphicsPipeline, nullptr);
        m_graphicsPipeline = VK_NULL_HANDLE;
    }
    if (m_portalPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_core->GetDevice(), m_portalPipeline, nullptr);
        m_portalPipeline = VK_NULL_HANDLE;
    }
    if (m_otherWorldPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_core->GetDevice(), m_otherWorldPipeline, nullptr);
        m_otherWorldPipeline = VK_NULL_HANDLE;
    }
    if (m_skyPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_core->GetDevice(), m_skyPipeline, nullptr);
        m_skyPipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_core->GetDevice(), m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_skyPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_core->GetDevice(), m_skyPipelineLayout, nullptr);
        m_skyPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_forgePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_core->GetDevice(), m_forgePipeline, nullptr);
        m_forgePipeline = VK_NULL_HANDLE;
    }
    if (m_forgePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_core->GetDevice(), m_forgePipelineLayout, nullptr);
        m_forgePipelineLayout = VK_NULL_HANDLE;
    }
    CreateGraphicsPipeline();
    CreateForgePipeline(); // Aggiorna anche il BlockMakerRenderer con extent aggiornato

    std::cout << "[VULKAN] Swapchain e Pipeline ricreate con successo per il ridimensionamento (" << width << "x" << height << ")" << std::endl;
}

// ---------------------------------------------------------
// FASE 5: DEFRAMMENTAZIONE A CALDO (FAST)
// ---------------------------------------------------------
void RenderManager::DefragmentVRAM() {
    if (!m_memory->GetAllocator() || !m_memory->GetChunkVmaPool()) return;

    VmaDefragmentationInfo defragInfo = {};
    defragInfo.pool = m_memory->GetChunkVmaPool();
    defragInfo.flags = VMA_DEFRAGMENTATION_FLAG_ALGORITHM_FAST_BIT;
    
    VmaDefragmentationContext defragCtx = VK_NULL_HANDLE;
    VkResult res = vmaBeginDefragmentation(m_memory->GetAllocator(), &defragInfo, &defragCtx);
    
    if (res == VK_SUCCESS) {
        VmaDefragmentationPassMoveInfo pass = {};
        res = vmaBeginDefragmentationPass(m_memory->GetAllocator(), defragCtx, &pass);
        if (res == VK_SUCCESS) {
            // Approccio "Fast" invisibile:
            // VMA unira' logicamente lo spazio libero frammentato nei suoi metadati.
            // Ignoriamo gli spostamenti fisici proposti per non dover distruggere/ricreare i VkBuffer
            // e non bloccare la GPU durante lo streaming dei chunk.
            for (uint32_t i = 0; i < pass.moveCount; i++) {
                pass.pMoves[i].operation = VMA_DEFRAGMENTATION_MOVE_OPERATION_IGNORE;
            }
            vmaEndDefragmentationPass(m_memory->GetAllocator(), defragCtx, &pass);
        }
        vmaEndDefragmentation(m_memory->GetAllocator(), defragCtx, nullptr);
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
    attrDescs[3].binding  = 0; attrDescs[3].location = 3; attrDescs[3].format = VK_FORMAT_R32_UINT;         attrDescs[3].offset = offsetof(Vertex, materialID);
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
    pipelineLayoutInfo.pSetLayouts = &m_forgeDescriptorSetLayout;

    if (vkCreatePipelineLayout(m_core->GetDevice(), &pipelineLayoutInfo, nullptr, &m_forgePipelineLayout) != VK_SUCCESS) {
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

    if (vkCreateGraphicsPipelines(m_core->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_forgePipeline) != VK_SUCCESS) {
        std::cerr << "[VULKAN] Errore creazione Forge Graphics Pipeline!\n";
        return false;
    }

    vkDestroyShaderModule(m_core->GetDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(m_core->GetDevice(), vertShaderModule, nullptr);

    // Inizializza le Api Artigiane (Sub-Renderers per ogni App)
    auto initSubRenderer = [&](auto& renderer) {
        if (renderer) {
            renderer->SetPipeline(m_forgePipeline, m_forgePipelineLayout);
            renderer->SetGlobalBuffer(m_memory->GetGlobalVramBuffer());
            renderer->SetSwapchainExtent(m_core->GetSwapchainExtent());
            renderer->SetDescriptorSets(&m_memory->GetForgeDescriptorSets());
            
            // Se il renderer ha il metodo SetTerrainPipeline (PlanetMapperRenderer) usa SFINAE o un cast
            // Ma auto& renderer è un unique_ptr, quindi facciamo un cast grezzo se serve o lo settiamo esplicitamente
        }
    };

    initSubRenderer(m_blockMakerRenderer);
    initSubRenderer(m_mapRenderer);
    initSubRenderer(m_forgeRenderer);
    initSubRenderer(m_playRenderer);
    initSubRenderer(m_physicsLabRenderer);
    initSubRenderer(m_chunkEditorRenderer);
    initSubRenderer(m_planetMapperRenderer);
    if (m_planetMapperRenderer) {
        m_planetMapperRenderer->SetTerrainPipeline(m_terrainPipeline.get());
    }
    initSubRenderer(m_solarSystemRenderer);

    return true;
}

void RenderManager::RenderForge(VkCommandBuffer cmd, const glm::mat4& viewProjMatrix, glm::vec3 cameraPos, SharedContext* context) {
    if (!context || !context->forgeWorld) return;
    auto* forgeWorld = context->forgeWorld;

    // ==========================================
    // 1. SETUP GLOBALE (Cambio di stato singolo)
    // ==========================================
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_forgePipeline);
    
    if (!m_memory->GetForgeDescriptorSets().empty() && m_memory->GetForgeDescriptorSets()[m_currentFrame] != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_forgePipelineLayout, 0, 1, &m_memory->GetForgeDescriptorSets()[m_currentFrame], 0, nullptr);
    }

    ForgePushConstantData pcData{};
    VkDeviceSize offsets[] = {0};

    float rawYearProgress = 0.0f;
    if (context && context->engine) {
        int currentDay = context->engine->GetTimeManager().GetCurrentDay();
        rawYearProgress = fmod((float)currentDay, 365.0f) / 365.0f;
    }
    float seasonalUboValue = (sin((rawYearProgress * 2.0f * glm::pi<float>()) - (glm::pi<float>() / 2.0f)) + 1.0f) * 0.5f;
    pcData.seasonProgress = seasonalUboValue;
    pcData.cameraPos = glm::vec4(cameraPos, 1.0f);
    if (context && context->isBlockMakerMode) {
        pcData.lightDir = glm::vec4(context->previewLightDir, 0.0f);
    } else {
        pcData.lightDir = glm::vec4(0.5f, -1.0f, 0.5f, 0.0f);
    }

    // ==========================================
    // 2. FASE STATICA: La Griglia di Lavoro
    // ==========================================
    
    // Configura Viewport e Scissor dinamicamente
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_core->GetSwapchainExtent().width;
    viewport.height = (float)m_core->GetSwapchainExtent().height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_core->GetSwapchainExtent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    if (m_memory->GetGlobalVramBuffer() != VK_NULL_HANDLE) {
        auto& registry = forgeWorld->GetRegistry();
        auto view = registry.view<fw::MeshComponent, fw::TransformComponent>();

        CameraFrustum frustum;
        frustum.extract(viewProjMatrix);

        for (auto entity : view) {
            const auto& mesh = view.get<fw::MeshComponent>(entity);
            const auto& trans = view.get<fw::TransformComponent>(entity);

            if (!mesh.vramAlloc.valid || mesh.vertices.empty()) continue;

            if (context->isBlockMakerMode) {
                // In BlockMakerMode, disegna solo l'entità PreviewBlock e l'ambiente BlockMakerEnv. Controlliamo i metadati
                if (registry.any_of<fw::MetadataComponent>(entity)) {
                    auto& metaName = registry.get<fw::MetadataComponent>(entity).name;
                    if (metaName != "PreviewBlock" && metaName != "BlockMakerEnv") {
                        continue;
                    }
                } else {
                    continue;
                }
            }

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
                VkBuffer vertexBuffers[] = { m_memory->GetGlobalVramBuffer() };
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
                VkBuffer vertexBuffers[] = { m_memory->GetGlobalVramBuffer() };
                vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
                
                vkCmdDraw(cmd, (uint32_t)mesh.vertices.size(), 1, 0, 0);
            }
        }
    }
}

VulkanTextureArray RenderManager::CreateTextureArray(
    VkDevice device, 
    VmaAllocator allocator, 
    VkCommandBuffer cmdBuffer, 
    const std::vector<uint8_t>& pixelData, 
    uint32_t width, 
    uint32_t height, 
    uint32_t layerCount, 
    VkFormat format
) {
    VulkanTextureArray result;
    result.format = format;
    result.layerCount = layerCount;

    VkDeviceSize imageSize = width * height * 4 * layerCount;
    if (imageSize == 0 || pixelData.empty()) {
        std::cerr << "[VulkanTextureManager] Error: Pixel data is empty or layerCount is 0.\n";
        return result;
    }

    // 1. Create Staging Buffer
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;

    VkBufferCreateInfo stagingBufferInfo = {};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = imageSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo = {};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo stagingAllocResultInfo;
    if (vmaCreateBuffer(allocator, &stagingBufferInfo, &stagingAllocInfo, &stagingBuffer, &stagingAllocation, &stagingAllocResultInfo) != VK_SUCCESS) {
        std::cerr << "[VulkanTextureManager] Error: Failed to create staging buffer.\n";
        return result;
    }

    memcpy(stagingAllocResultInfo.pMappedData, pixelData.data(), (size_t)imageSize);

    // 2. Create VkImage (2D Array)
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = layerCount; // CRITICAL: Set arrayLayers
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo imageAllocInfo = {};
    imageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &imageAllocInfo, &result.image, &result.allocation, nullptr) != VK_SUCCESS) {
        std::cerr << "[VulkanTextureManager] Error: Failed to create Vulkan image.\n";
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return result;
    }

    // 3. Transition Image Layout (UNDEFINED -> TRANSFER_DST_OPTIMAL)
    VkImageMemoryBarrier barrier1 = {};
    barrier1.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier1.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier1.image = result.image;
    barrier1.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier1.subresourceRange.baseMipLevel = 0;
    barrier1.subresourceRange.levelCount = 1;
    barrier1.subresourceRange.baseArrayLayer = 0;
    barrier1.subresourceRange.layerCount = layerCount; // CRITICAL: Transition all layers
    barrier1.srcAccessMask = 0;
    barrier1.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier1);

    // 4. Copy Buffer To Image
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = layerCount; // CRITICAL: Copy to all layers
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(cmdBuffer, stagingBuffer, result.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // 5. Transition Image Layout (TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL)
    VkImageMemoryBarrier barrier2 = {};
    barrier2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier2.image = result.image;
    barrier2.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier2.subresourceRange.baseMipLevel = 0;
    barrier2.subresourceRange.levelCount = 1;
    barrier2.subresourceRange.baseArrayLayer = 0;
    barrier2.subresourceRange.layerCount = layerCount; // CRITICAL: Transition all layers
    barrier2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier2);

    // 6. Create VkImageView (VK_IMAGE_VIEW_TYPE_2D_ARRAY)
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = result.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; // CRITICAL: 2D Array
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = layerCount; // CRITICAL: View all layers

    if (vkCreateImageView(device, &viewInfo, nullptr, &result.view) != VK_SUCCESS) {
        std::cerr << "[VulkanTextureManager] Error: Failed to create Vulkan image view.\n";
    }

    result.stagingBuffer = stagingBuffer;
    result.stagingAllocation = stagingAllocation;

    return result;
}
