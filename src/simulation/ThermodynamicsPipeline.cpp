#include "ThermodynamicsPipeline.h"
#include <fstream>
#include <stdexcept>
#include <iostream>

ThermodynamicsPipeline::~ThermodynamicsPipeline() {
    Cleanup();
}

void ThermodynamicsPipeline::Initialize(VkDevice device, VmaAllocator allocator) {
    m_device = device;
    m_allocator = allocator;

    CreateDescriptorSetLayout();
    CreatePipeline();
}

void ThermodynamicsPipeline::CreateDescriptorSetLayout() {
    // Binding 0: Read Grid
    VkDescriptorSetLayoutBinding readBinding{};
    readBinding.binding = 0;
    readBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    readBinding.descriptorCount = 1;
    readBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: Write Grid
    VkDescriptorSetLayoutBinding writeBinding{};
    writeBinding.binding = 1;
    writeBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writeBinding.descriptorCount = 1;
    writeBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 2: Event State
    VkDescriptorSetLayoutBinding eventBinding{};
    eventBinding.binding = 2;
    eventBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    eventBinding.descriptorCount = 1;
    eventBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::vector<VkDescriptorSetLayoutBinding> bindings = { readBinding, writeBinding, eventBinding };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute descriptor set layout!");
    }
}

VkShaderModule ThermodynamicsPipeline::CreateShaderModule(const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode = code.data();

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }
    return shaderModule;
}

void ThermodynamicsPipeline::CreatePipeline() {
    // 1. Leggi lo shader SPV
    // MOCK: In produzione si dovrebbe usare il VirtualFileSystem dell'engine
    std::ifstream file("assets/shaders/thermodynamics.comp.spv", std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[Warning] Could not find thermodynamics.comp.spv. Compute Pipeline NOT created.\n";
        return;
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();

    VkShaderModule compShaderModule = CreateShaderModule(buffer);

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = compShaderModule;
    shaderStageInfo.pName = "main";

    // 2. Push Constants
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute pipeline layout!");
    }

    // 3. Compute Pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.stage = shaderStageInfo;

    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_computePipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute pipeline!");
    }

    vkDestroyShaderModule(m_device, compShaderModule, nullptr);
}

void ThermodynamicsPipeline::AllocateBuffers(uint32_t total_cells, uint32_t max_events) {
    m_totalCells = total_cells;
    m_maxEvents = max_events;
    m_readBufferIndex = 0;

    VkDeviceSize gridSize = total_cells * 8; // sizeof(CellData) = 8

    // 1. Grid Buffers (Ping-Pong)
    for (int i = 0; i < 2; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = gridSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        if (vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &m_gridBuffers[i], &m_gridAllocations[i], nullptr) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate grid buffer");
        }
    }

    // 2. Event Buffer
    // Struttura: event_count (4 byte) + pad (12 byte) + ReadbackEvent[max_events] (16 byte ciascuno)
    VkDeviceSize eventSize = 16 + max_events * sizeof(fw::ReadbackEvent);

    VkBufferCreateInfo eventBufferInfo{};
    eventBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    eventBufferInfo.size = eventSize;
    eventBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    eventBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo eventAllocInfo{};
    eventAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    eventAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocResultInfo;
    if (vmaCreateBuffer(m_allocator, &eventBufferInfo, &eventAllocInfo, &m_eventBuffer, &m_eventAllocation, &allocResultInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate event buffer");
    }
    
    // Mappatura persistente accessibile tramite VMA (O(1) read)
    m_eventMappedData = allocResultInfo.pMappedData;

    // Reset iniziale dell'event counter
    uint32_t* counterPtr = static_cast<uint32_t*>(m_eventMappedData);
    *counterPtr = 0;

    // 3. Creazione Descriptor Pool e Set
    std::vector<VkDescriptorPoolSize> poolSizes = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute descriptor pool!");
    }

    VkDescriptorSetAllocateInfo allocSetInfo{};
    allocSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocSetInfo.descriptorPool = m_descriptorPool;
    allocSetInfo.descriptorSetCount = 1;
    allocSetInfo.pSetLayouts = &m_descriptorSetLayout;

    if (vkAllocateDescriptorSets(m_device, &allocSetInfo, &m_descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate compute descriptor set!");
    }

    UpdateDescriptorSets();
}

void ThermodynamicsPipeline::UpdateDescriptorSets() {
    VkDescriptorBufferInfo readBufferInfo{};
    readBufferInfo.buffer = m_gridBuffers[m_readBufferIndex];
    readBufferInfo.offset = 0;
    readBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo writeBufferInfo{};
    writeBufferInfo.buffer = m_gridBuffers[1 - m_readBufferIndex];
    writeBufferInfo.offset = 0;
    writeBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo eventBufferInfo{};
    eventBufferInfo.buffer = m_eventBuffer;
    eventBufferInfo.offset = 0;
    eventBufferInfo.range = VK_WHOLE_SIZE;

    std::vector<VkWriteDescriptorSet> descriptorWrites(3);

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = m_descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &readBufferInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = m_descriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pBufferInfo = &writeBufferInfo;

    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = m_descriptorSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pBufferInfo = &eventBufferInfo;

    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void ThermodynamicsPipeline::RecordComputeCommands(VkCommandBuffer cmdBuffer, const PushConstants& pc) {
    if (m_computePipeline == VK_NULL_HANDLE) return;

    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
    vkCmdPushConstants(cmdBuffer, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);

    uint32_t groupCountX = (pc.grid_width + 15) / 16;
    uint32_t groupCountY = (pc.grid_height + 15) / 16;
    vkCmdDispatch(cmdBuffer, groupCountX, groupCountY, 1);

    // Memory Barrier per sincronizzazione GPU -> CPU (Readback Eventi) e GPU -> GPU (Ping-Pong Write)
    // Dobbiamo assicurarci che la scrittura sui buffer SSBO sia terminata prima 
    // che la CPU acceda alla memoria mappata e prima del dispatch successivo
    VkMemoryBarrier memoryBarrier{};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_READ_BIT;

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
        0,
        1, &memoryBarrier,
        0, nullptr,
        0, nullptr
    );
}

void ThermodynamicsPipeline::SwapBuffers() {
    m_readBufferIndex = 1 - m_readBufferIndex;
    UpdateDescriptorSets();
}

std::vector<fw::ReadbackEvent> ThermodynamicsPipeline::FetchEvents() {
    if (!m_eventMappedData) return {};

    uint32_t* pCounter = static_cast<uint32_t*>(m_eventMappedData);
    uint32_t count = *pCounter;

    // Safety clamp (nel caso lo shader sfori)
    if (count > m_maxEvents) {
        count = m_maxEvents;
    }

    std::vector<fw::ReadbackEvent> result;
    if (count > 0) {
        result.reserve(count);
        // I dati iniziano dopo 16 byte (count + 12 byte padding)
        fw::ReadbackEvent* pEvents = reinterpret_cast<fw::ReadbackEvent*>(
            static_cast<char*>(m_eventMappedData) + 16
        );

        for (uint32_t i = 0; i < count; ++i) {
            result.push_back(pEvents[i]);
        }
    }

    // Reset del counter per il prossimo dispatch
    *pCounter = 0;

    return result;
}

void ThermodynamicsPipeline::Cleanup() {
    if (!m_device) return;

    if (m_eventBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_eventBuffer, m_eventAllocation);
        m_eventBuffer = VK_NULL_HANDLE;
    }

    for (int i = 0; i < 2; ++i) {
        if (m_gridBuffers[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_allocator, m_gridBuffers[i], m_gridAllocations[i]);
            m_gridBuffers[i] = VK_NULL_HANDLE;
        }
    }

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }

    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    if (m_computePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_computePipeline, nullptr);
        m_computePipeline = VK_NULL_HANDLE;
    }
}
