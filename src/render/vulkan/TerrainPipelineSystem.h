#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <stdexcept>
#include "ChunkCullingTypes.h"

#include "TerrainGPUTypes.h"

// ============================================================
// Incapsula: creazione SSBO per Istanze (MapRegionGPU), Vertex/Index Buffers
// e dispatch del terrain_generation compute shader.
// ============================================================

struct TerrainGenPushConstants {
    uint32_t numChunks;
    uint32_t numRegions;
    float planetRadius;
    float _pad;
};

class TerrainPipelineSystem {
public:
    TerrainPipelineSystem(VkDevice device, VkPhysicalDevice physicalDevice)
        : m_device(device), m_physicalDevice(physicalDevice) {}

    ~TerrainPipelineSystem() { destroy(); }

    // Da chiamare una volta, quando si conosce il numero massimo di chunk
    void init(uint32_t maxChunks, uint32_t maxRegions, VkShaderModule computeShaderModule) {
        m_maxChunks = maxChunks;
        m_maxRegions = maxRegions;

        createBuffer(sizeof(ChunkData) * maxChunks,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     m_chunkBuffer, m_chunkMemory);

        createBuffer(sizeof(fw::MapRegionGPU) * maxRegions,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     m_regionBuffer, m_regionMemory);

        // Alloca spazio sufficiente per i vertici (es. 289 vertici per chunk se 16x16 LOD0)
        uint32_t maxVertices = maxChunks * 289;
        createBuffer(sizeof(float) * 8 * maxVertices, // pos(3), normal(3), uv/mat(2)
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     m_vertexBuffer, m_vertexMemory);

        // Index Buffer condiviso per un chunk 16x16 (16*16 quad -> 1536 indici)
        std::vector<uint32_t> indices;
        indices.reserve(16 * 16 * 6);
        for (uint32_t z = 0; z < 16; ++z) {
            for (uint32_t x = 0; x < 16; ++x) {
                uint32_t topLeft = z * 17 + x;
                uint32_t topRight = topLeft + 1;
                uint32_t bottomLeft = (z + 1) * 17 + x;
                uint32_t bottomRight = bottomLeft + 1;

                indices.push_back(topLeft);
                indices.push_back(bottomLeft);
                indices.push_back(topRight);

                indices.push_back(topRight);
                indices.push_back(bottomLeft);
                indices.push_back(bottomRight);
            }
        }
        m_indexCount = (uint32_t)indices.size();

        // NOTA: Per un uso reale, bisognerebbe allocare con uno staging buffer. 
        // Qui allochiamo direttamente come device local e usiamo la memoria se accessibile dall'host
        // o preferibilmente aggiungiamo un metodo di uploadIndices()
        createBuffer(sizeof(uint32_t) * indices.size(),
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     m_indexBuffer, m_indexMemory);

        createBuffer(sizeof(VkDrawIndexedIndirectCommand) * maxChunks,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     m_indirectBuffer, m_indirectMemory);

        createDescriptorSetLayout();
        createDescriptorPoolAndSet();
        createPipelineLayout();
        createComputePipeline(computeShaderModule);
    }

    // Da chiamare quando cambia la lista dei chunk (es. streaming del mondo)
    // NB: idealmente si aggiorna solo la porzione modificata, non l'intero buffer.
    void uploadData(VkCommandBuffer cmd, const std::vector<ChunkData>& chunks, const std::vector<fw::MapRegionGPU>& regions,
                          VkBuffer stagingBuffer, VkDeviceMemory /*stagingMemory*/) {
        
        uint32_t chunkBytes = sizeof(ChunkData) * chunks.size();
        uint32_t regionBytes = sizeof(fw::MapRegionGPU) * regions.size();
        
        if (chunkBytes > 0) {
            VkBufferCopy copyChunk{};
            copyChunk.srcOffset = 0;
            copyChunk.dstOffset = 0;
            copyChunk.size = chunkBytes;
            vkCmdCopyBuffer(cmd, stagingBuffer, m_chunkBuffer, 1, &copyChunk);
        }
        
        if (regionBytes > 0) {
            VkBufferCopy copyRegion{};
            copyRegion.srcOffset = chunkBytes; // assumiamo che lo staging buffer contenga chunks seguiti da regions
            copyRegion.dstOffset = 0;
            copyRegion.size = regionBytes;
            vkCmdCopyBuffer(cmd, stagingBuffer, m_regionBuffer, 1, &copyRegion);
        }

        VkBufferMemoryBarrier barriers[2]{};
        barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].buffer = m_chunkBuffer;
        barriers[0].offset = 0;
        barriers[0].size = VK_WHOLE_SIZE;
        
        barriers[1] = barriers[0];
        barriers[1].buffer = m_regionBuffer;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 2, barriers, 0, nullptr);
    }

    void dispatch(VkCommandBuffer cmd, const TerrainGenPushConstants& pc) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                 m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                            0, sizeof(TerrainGenPushConstants), &pc);

        // Ogni workgroup calcola un intero chunk 17x17.
        // Quindi lanciamo esattamente pc.numChunks workgroup!
        uint32_t groupCount = pc.numChunks;
        vkCmdDispatch(cmd, groupCount, 1, 1);

        // Barrier: l'output vertex buffer deve essere visibile prima di essere letto
        // come Vertex Buffer
        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = m_vertexBuffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
            0, 0, nullptr, 1, &barrier, 0, nullptr);
    }

    VkBuffer getVertexBuffer() const { return m_vertexBuffer; }
    VkBuffer getIndexBuffer() const { return m_indexBuffer; }
    VkBuffer getIndirectBuffer() const { return m_indirectBuffer; }
    VkBuffer getChunkBuffer() const { return m_chunkBuffer; }
    VkBuffer getRegionBuffer() const { return m_regionBuffer; }
    uint32_t getIndexCount() const { return m_indexCount; }

    void destroy() {
        if (m_pipeline)            vkDestroyPipeline(m_device, m_pipeline, nullptr);
        if (m_pipelineLayout)      vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        if (m_descriptorSetLayout) vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        if (m_descriptorPool)      vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        if (m_chunkBuffer)         vkDestroyBuffer(m_device, m_chunkBuffer, nullptr);
        if (m_chunkMemory)         vkFreeMemory(m_device, m_chunkMemory, nullptr);
        if (m_regionBuffer)        vkDestroyBuffer(m_device, m_regionBuffer, nullptr);
        if (m_regionMemory)        vkFreeMemory(m_device, m_regionMemory, nullptr);
        if (m_vertexBuffer)        vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
        if (m_vertexMemory)        vkFreeMemory(m_device, m_vertexMemory, nullptr);
        if (m_indexBuffer)         vkDestroyBuffer(m_device, m_indexBuffer, nullptr);
        if (m_indexMemory)         vkFreeMemory(m_device, m_indexMemory, nullptr);
        if (m_indirectBuffer)      vkDestroyBuffer(m_device, m_indirectBuffer, nullptr);
        if (m_indirectMemory)      vkFreeMemory(m_device, m_indirectMemory, nullptr);

        m_pipeline = VK_NULL_HANDLE;
        m_pipelineLayout = VK_NULL_HANDLE;
        m_descriptorSetLayout = VK_NULL_HANDLE;
        m_descriptorPool = VK_NULL_HANDLE;
        m_chunkBuffer = VK_NULL_HANDLE;
        m_chunkMemory = VK_NULL_HANDLE;
        m_regionBuffer = VK_NULL_HANDLE;
        m_regionMemory = VK_NULL_HANDLE;
        m_vertexBuffer = VK_NULL_HANDLE;
        m_vertexMemory = VK_NULL_HANDLE;
        m_indexBuffer = VK_NULL_HANDLE;
        m_indexMemory = VK_NULL_HANDLE;
        m_indirectBuffer = VK_NULL_HANDLE;
        m_indirectMemory = VK_NULL_HANDLE;
    }

private:
    VkDevice         m_device;
    VkPhysicalDevice m_physicalDevice;
    uint32_t         m_maxChunks = 0;
    uint32_t         m_maxRegions = 0;

    VkBuffer       m_chunkBuffer  = VK_NULL_HANDLE;
    VkDeviceMemory m_chunkMemory  = VK_NULL_HANDLE;
    VkBuffer       m_regionBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_regionMemory = VK_NULL_HANDLE;
    VkBuffer       m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    VkBuffer       m_indexBuffer  = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory  = VK_NULL_HANDLE;
    uint32_t       m_indexCount   = 0;

    VkBuffer       m_indirectBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indirectMemory = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet       = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
    VkPipeline            m_pipeline            = VK_NULL_HANDLE;

    // --- Helpers di creazione risorse ---

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        throw std::runtime_error("Nessun memory type compatibile trovato per il buffer di culling");
    }

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags properties,
                       VkBuffer& buffer, VkDeviceMemory& memory) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer);

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        vkAllocateMemory(m_device, &allocInfo, nullptr, &memory);
        vkBindBufferMemory(m_device, buffer, memory, 0);
    }

    void createDescriptorSetLayout() {
        VkDescriptorSetLayoutBinding bindings[4]{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        
        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 4;
        layoutInfo.pBindings = bindings;

        vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout);
    }

    void createDescriptorPoolAndSet() {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount = 4;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 1;

        vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_descriptorSetLayout;

        vkAllocateDescriptorSets(m_device, &allocInfo, &m_descriptorSet);

        VkDescriptorBufferInfo chunkInfo{ m_chunkBuffer, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo regionInfo{ m_regionBuffer, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo vertexInfo{ m_vertexBuffer, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo indirectInfo{ m_indirectBuffer, 0, VK_WHOLE_SIZE };

        VkWriteDescriptorSet writes[4]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].pBufferInfo = &chunkInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo = &regionInfo;
        
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = m_descriptorSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].pBufferInfo = &vertexInfo;
        
        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = m_descriptorSet;
        writes[3].dstBinding = 3;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].pBufferInfo = &indirectInfo;

        vkUpdateDescriptorSets(m_device, 4, writes, 0, nullptr);
    }

    void createPipelineLayout() {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(TerrainGenPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout);
    }

    void createComputePipeline(VkShaderModule shaderModule) {
        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = m_pipelineLayout;

        vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);
    }
};
