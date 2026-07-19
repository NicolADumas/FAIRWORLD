import re
import os

h_file = "D:/FAIRWORLD/FAIRWORLD/src/render/RenderManager.h"
cpp_file = "D:/FAIRWORLD/FAIRWORLD/src/render/RenderManager.cpp"

# --- Fix RenderManager.h ---
with open(h_file, "r") as f:
    h_content = f.read()

h_content = h_content.replace('#include "vulkan/VulkanCore.h"', '#include "vulkan/VulkanCore.h"\n#include "vulkan/VulkanMemory.h"')

if 'std::unique_ptr<fw::VulkanMemory> m_memory;' not in h_content:
    h_content = h_content.replace('std::unique_ptr<fw::VulkanCore> m_core;', 'std::unique_ptr<fw::VulkanCore> m_core;\n    std::unique_ptr<fw::VulkanMemory> m_memory;')

fields_to_remove = [
    r'VmaAllocator m_vmaAllocator\{ VK_NULL_HANDLE \};',
    r'VkBuffer m_stagingRingBuffer\{ VK_NULL_HANDLE \};',
    r'VmaAllocation m_stagingAllocation\{ VK_NULL_HANDLE \};',
    r'void\* m_mappedStagingData = nullptr;',
    r'VmaPool m_chunkVmaPool\{ VK_NULL_HANDLE \};',
    r'VkBuffer m_globalVramBuffer\{ VK_NULL_HANDLE \};',
    r'VmaAllocation m_globalVramAllocation\{ VK_NULL_HANDLE \};',
    r'std::vector<VkBuffer> m_uniformBuffers;',
    r'std::vector<VmaAllocation> m_uniformBuffersAllocation;',
    r'std::vector<void\*> m_uniformBuffersMapped;',
    r'VkDescriptorPool m_descriptorPool\{ VK_NULL_HANDLE \};',
    r'std::vector<VkDescriptorSet> m_descriptorSets;',
    r'VkDescriptorPool m_imguiDescriptorPool\{ VK_NULL_HANDLE \};',
    r'VkDescriptorPool m_forgeDescriptorPool\{ VK_NULL_HANDLE \};',
    r'std::vector<VkDescriptorSet> m_forgeDescriptorSets;',
    r'VkDescriptorPool m_skyDescriptorPool\{ VK_NULL_HANDLE \};',
    r'std::vector<VkDescriptorSet> m_skyDescriptorSets;'
]

methods_to_remove = [
    r'uint32_t FindMemoryType\(uint32_t typeFilter, VkMemoryPropertyFlags properties\);',
    r'bool CreateBuffer\(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory\);',
    r'bool CreateUniformBuffers\(\);',
    r'bool CreateDescriptorPoolAndSets\(\);'
]

for field in fields_to_remove:
    h_content = re.sub(field, '', h_content, flags=re.DOTALL)

for method in methods_to_remove:
    h_content = re.sub(method, '', h_content)

# Fix Getters in RenderManager.h
getter_replacements = {
    r'VkBuffer GetStagingRingBuffer\(\) const \{ return m_stagingRingBuffer; \}': 'VkBuffer GetStagingRingBuffer() const { return m_memory ? m_memory->GetStagingRingBuffer() : VK_NULL_HANDLE; }',
    r'void\* GetMappedStagingData\(\) const \{ return m_mappedStagingData; \}': 'void* GetMappedStagingData() const { return m_memory ? m_memory->GetMappedStagingData() : nullptr; }',
    r'VkBuffer GetGlobalVramBuffer\(\) const \{ return m_globalVramBuffer; \}': 'VkBuffer GetGlobalVramBuffer() const { return m_memory ? m_memory->GetGlobalVramBuffer() : VK_NULL_HANDLE; }'
}
for old, new_ in getter_replacements.items():
    h_content = re.sub(old, new_, h_content)

with open(h_file, "w") as f:
    f.write(h_content)

# --- Fix RenderManager.cpp ---
with open(cpp_file, "r") as f:
    cpp_content = f.read()

cpp_replacements = {
    r'\bm_vmaAllocator\b': 'm_memory->GetAllocator()',
    r'\bm_stagingRingBuffer\b': 'm_memory->GetStagingRingBuffer()',
    r'\bm_mappedStagingData\b': 'm_memory->GetMappedStagingData()',
    r'\bm_chunkVmaPool\b': 'm_memory->GetChunkVmaPool()',
    r'\bm_globalVramBuffer\b': 'm_memory->GetGlobalVramBuffer()',
    r'\bm_uniformBuffers\b': 'm_memory->GetUniformBuffers()',
    r'\bm_uniformBuffersMapped\b': 'm_memory->GetUniformBuffersMapped()',
    r'\bm_descriptorPool\b': 'm_memory->GetDescriptorPool()',
    r'\bm_descriptorSets\b': 'm_memory->GetDescriptorSets()',
    r'\bm_imguiDescriptorPool\b': 'm_memory->GetImguiDescriptorPool()',
    r'\bm_forgeDescriptorPool\b': 'm_memory->GetForgeDescriptorPool()',
    r'\bm_forgeDescriptorSets\b': 'm_memory->GetForgeDescriptorSets()'
}

for pattern, repl in cpp_replacements.items():
    cpp_content = re.sub(pattern, repl, cpp_content)

# Fix Init to instantiate VulkanMemory
init_pattern = r'(m_core = std::make_unique<fw::VulkanCore>\(\);\n\s*if \(!m_core->Initialize\(isVRMode, xrManager, hwnd, hinstance\)\) return false;)'
init_replacement = r'\1\n    m_memory = std::make_unique<fw::VulkanMemory>(m_core.get());\n    if (!m_memory->Initialize()) return false;'
cpp_content = re.sub(init_pattern, init_replacement, cpp_content)

with open(cpp_file, "w") as f:
    f.write(cpp_content)
