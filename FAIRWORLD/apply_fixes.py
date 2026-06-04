import re

file_path = r"d:\FILE OF FAIRWORLD\FAIRWORLD\FAIRWORLD\RenderManager.cpp"
with open(file_path, "r", encoding="utf-8") as f:
    content = f.read()

# Fix CreateUniformBuffers
uniform_pattern = r"bool RenderManager::CreateUniformBuffers\(\)\s*\{.*?return true;\n\}"

new_uniform = """bool RenderManager::CreateUniformBuffers() {
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
}"""
content = re.sub(uniform_pattern, new_uniform, content, flags=re.DOTALL)


# Fix Init()
target_init_string = """    if (vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator) != VK_SUCCESS) {
        std::cerr << "[VULKAN ERROR] Impossibile inizializzare VmaAllocator!" << std::endl;
        return false;
    }"""

replacement_init_string = """    if (vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator) != VK_SUCCESS) {
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
        throw std::runtime_error("failed to create transfer command pool!");
    }
    
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = m_transferCommandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &m_transferCommandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate transfer command buffer!");
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
        throw std::runtime_error("failed to create VMA Pool for chunks!");
    }

    // --- 3. CREATE RING BUFFER (STAGING PERSISTENTE) ---
    VkBufferCreateInfo stagingBufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    stagingBufInfo.size = STAGING_BUFFER_SIZE;
    stagingBufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    
    VmaAllocationCreateInfo stagingAllocInfo = {};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    
    if (vmaCreateBuffer(m_vmaAllocator, &stagingBufInfo, &stagingAllocInfo, &m_stagingRingBuffer, &m_stagingAllocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("failed to create Staging Ring Buffer!");
    }
    
    VmaAllocationInfo vmaRingInfo;
    vmaGetAllocationInfo(m_vmaAllocator, m_stagingAllocation, &vmaRingInfo);
    m_mappedStagingData = vmaRingInfo.pMappedData;"""

if target_init_string in content and "m_stagingRingBuffer =" not in content:
    content = content.replace(target_init_string, replacement_init_string)

# Fix Shutdown
target_shutdown_string = """    if (m_vmaAllocator) {
        vmaDestroyAllocator(m_vmaAllocator);
        m_vmaAllocator = VK_NULL_HANDLE;
    }"""

replacement_shutdown_string = """    if (m_stagingRingBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_vmaAllocator, m_stagingRingBuffer, m_stagingAllocation);
        m_stagingRingBuffer = VK_NULL_HANDLE;
    }
    if (m_chunkVmaPool != VK_NULL_HANDLE) {
        vmaDestroyPool(m_vmaAllocator, m_chunkVmaPool);
        m_chunkVmaPool = VK_NULL_HANDLE;
    }
    if (m_transferCommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_transferCommandPool, nullptr);
        m_transferCommandPool = VK_NULL_HANDLE;
    }
    if (m_vmaAllocator) {
        vmaDestroyAllocator(m_vmaAllocator);
        m_vmaAllocator = VK_NULL_HANDLE;
    }"""

if target_shutdown_string in content and "m_stagingRingBuffer != VK_NULL_HANDLE" not in content:
    content = content.replace(target_shutdown_string, replacement_shutdown_string)

with open(file_path, "w", encoding="utf-8") as f:
    f.write(content)

print("RenderManager fixes applied successfully.")
