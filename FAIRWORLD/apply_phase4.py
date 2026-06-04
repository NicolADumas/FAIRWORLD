import os
import re

header_path = r"d:\FILE OF FAIRWORLD\FAIRWORLD\FAIRWORLD\RenderManager.h"
cpp_path = r"d:\FILE OF FAIRWORLD\FAIRWORLD\FAIRWORLD\RenderManager.cpp"

with open(header_path, "r", encoding="utf-8") as f:
    h_content = f.read()

# Add Ring Buffer state variables to RenderManager.h
state_vars = """    // --- Variabili per il VMA Staging Ring Buffer ---
    VkBuffer m_stagingRingBuffer{ VK_NULL_HANDLE };
    VmaAllocation m_stagingAllocation{ VK_NULL_HANDLE };
    void* m_mappedStagingData = nullptr; // Puntatore fisso alla RAM
    const uint64_t STAGING_BUFFER_SIZE = 32 * 1024 * 1024; // 32 MB
    uint64_t m_currentOffset = 0; // Il cursore 'Head'
    VkCommandBuffer m_transferCommandBuffer{ VK_NULL_HANDLE };
    VkCommandPool m_transferCommandPool{ VK_NULL_HANDLE };
    
    // --- VmaPool dedicato per Chunk ---
    VmaPool m_chunkVmaPool{ VK_NULL_HANDLE };

    inline uint64_t AlignMemory(uint64_t offset, uint64_t alignment = 256) {
        return (offset + alignment - 1) & ~(alignment - 1);
    }
    void FlushTransferBatch();"""

h_content = h_content.replace(
    "    VkCommandPool m_commandPool{ VK_NULL_HANDLE };",
    "    VkCommandPool m_commandPool{ VK_NULL_HANDLE };\n" + state_vars
)

with open(header_path, "w", encoding="utf-8") as f:
    f.write(h_content)


with open(cpp_path, "r", encoding="utf-8") as f:
    cpp_content = f.read()

# Update UploadChunkMesh
old_upload = """void RenderManager::UploadChunkMesh(ChunkCoord coord, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    DestroyChunkBuffer(coord);

    if (vertices.empty() || indices.empty()) return;

    VulkanChunkBuffer chunkBuf;
    chunkBuf.indexCount = (uint32_t)indices.size();

    VkDeviceSize vSize = sizeof(vertices[0]) * vertices.size();
    VkDeviceSize iSize = sizeof(indices[0]) * indices.size();

    // Staging Vertex
    VkBuffer stagingVBuf; VmaAllocation stagingVMem;
    if (!CreateBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, stagingVBuf, stagingVMem)) return;
    void* vData; vmaMapMemory(m_vmaAllocator, stagingVMem, &vData);
    memcpy(vData, vertices.data(), (size_t)vSize);
    vmaUnmapMemory(m_vmaAllocator, stagingVMem);

    if (!CreateBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, chunkBuf.vertexBuffer, chunkBuf.vertexBufferAllocation)) return;
    CopyBuffer(stagingVBuf, chunkBuf.vertexBuffer, vSize);
    vmaDestroyBuffer(m_vmaAllocator, stagingVBuf, stagingVMem);

    // Staging Index
    VkBuffer stagingIBuf; VmaAllocation stagingIMem;
    if (!CreateBuffer(iSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, stagingIBuf, stagingIMem)) return;
    void* iData; vmaMapMemory(m_vmaAllocator, stagingIMem, &iData);
    memcpy(iData, indices.data(), (size_t)iSize);
    vmaUnmapMemory(m_vmaAllocator, stagingIMem);

    if (!CreateBuffer(iSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, chunkBuf.indexBuffer, chunkBuf.indexBufferAllocation)) return;
    CopyBuffer(stagingIBuf, chunkBuf.indexBuffer, iSize);
    vmaDestroyBuffer(m_vmaAllocator, stagingIBuf, stagingIMem);

    m_chunkBuffers[coord] = chunkBuf;
}"""

# Use regex to match the method if there are slight differences
upload_pattern = r"void RenderManager::UploadChunkMesh\(ChunkCoord coord, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices\) \{.*?\n\s*m_chunkBuffers\[coord\] = chunkBuf;\n\}"

new_upload = """void RenderManager::UploadChunkMesh(ChunkCoord coord, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
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
    vmaCreateBuffer(m_vmaAllocator, &vbInfo, &vbAllocInfo, &chunkBuf.vertexBuffer, &chunkBuf.vertexBufferAllocation, nullptr);

    VkBufferCreateInfo ibInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    ibInfo.size = indexSize;
    ibInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    ibInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo ibAllocInfo = {};
    ibAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    ibAllocInfo.pool = m_chunkVmaPool;
    vmaCreateBuffer(m_vmaAllocator, &ibInfo, &ibAllocInfo, &chunkBuf.indexBuffer, &chunkBuf.indexBufferAllocation, nullptr);

    // 5. Registra i comandi di copia nel Transfer Command Buffer
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
"""

cpp_content = re.sub(upload_pattern, new_upload, cpp_content, flags=re.DOTALL)


# Now we must create the VmaPool, StagingRingBuffer, TransferCommandPool, TransferCommandBuffer in RenderManager::Init
# We will do this right after VmaAllocator is created.
# Let's find: vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator);
init_vma_pattern = r"vmaCreateAllocator\(&allocatorInfo, &m_vmaAllocator\);\s*if\s*\(!m_vmaAllocator\)\s*\{\s*throw std::runtime_error\(\"Failed to create VMA allocator!\"\);\s*\}"

init_vma_addition = """    vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator);
    if (!m_vmaAllocator) {
        throw std::runtime_error("Failed to create VMA allocator!");
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
    // vmaPoolInfo.flags = VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT;
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
    m_mappedStagingData = vmaRingInfo.pMappedData;
"""

cpp_content = re.sub(init_vma_pattern, init_vma_addition, cpp_content)

# We also need to destroy them in Shutdown!
# Let's find vmaDestroyAllocator
shutdown_pattern = r"if\s*\(m_vmaAllocator\)\s*\{\s*vmaDestroyAllocator\(m_vmaAllocator\);\s*m_vmaAllocator = VK_NULL_HANDLE;\s*\}"

shutdown_addition = """    if (m_stagingRingBuffer != VK_NULL_HANDLE) {
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

cpp_content = re.sub(shutdown_pattern, shutdown_addition, cpp_content)


with open(cpp_path, "w", encoding="utf-8") as f:
    f.write(cpp_content)

print("RenderManager Phase 4 iteration 1 applied.")
