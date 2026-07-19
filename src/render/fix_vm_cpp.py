import re

vm_cpp = "D:/FAIRWORLD/FAIRWORLD/src/render/vulkan/VulkanMemory.cpp"
vm_h = "D:/FAIRWORLD/FAIRWORLD/src/render/vulkan/VulkanMemory.h"
rm_cpp = "D:/FAIRWORLD/FAIRWORLD/src/render/RenderManager.cpp"

# Fix VulkanMemory.h
with open(vm_h, "r") as f:
    h_content = f.read()

if "#define MAX_FRAMES_IN_FLIGHT 2" not in h_content:
    h_content = h_content.replace("#include <vector>", "#include <vector>\n\n#define MAX_FRAMES_IN_FLIGHT 2")

h_content = h_content.replace("bool CreateUniformBuffers();", "bool CreateUniformBuffers(size_t uniformBufferSize);")
h_content = h_content.replace("bool CreateDescriptorPoolAndSets();", "bool CreateDescriptorPoolAndSets(VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSetLayout forgeDescriptorSetLayout);")

with open(vm_h, "w") as f:
    f.write(h_content)

# Fix VulkanMemory.cpp
with open(vm_cpp, "r") as f:
    cpp_content = f.read()

cpp_content = cpp_content.replace("bool VulkanMemory::CreateUniformBuffers() {", "bool VulkanMemory::CreateUniformBuffers(size_t uniformBufferSize) {")
cpp_content = cpp_content.replace("sizeof(UniformBufferObject)", "uniformBufferSize")
cpp_content = cpp_content.replace("sizeof(RenderManager::UniformBufferObject)", "uniformBufferSize")

cpp_content = cpp_content.replace("bool VulkanMemory::CreateDescriptorPoolAndSets() {", "bool VulkanMemory::CreateDescriptorPoolAndSets(VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSetLayout forgeDescriptorSetLayout) {")

cpp_content = cpp_content.replace("m_descriptorSetLayout", "descriptorSetLayout")
cpp_content = cpp_content.replace("m_forgeDescriptorSetLayout", "forgeDescriptorSetLayout")

with open(vm_cpp, "w") as f:
    f.write(cpp_content)

# Fix RenderManager.cpp
with open(rm_cpp, "r") as f:
    rm_content = f.read()

rm_content = rm_content.replace("m_memory->CreateUniformBuffers()", "m_memory->CreateUniformBuffers(sizeof(UniformBufferObject))")
rm_content = rm_content.replace("m_memory->CreateDescriptorPoolAndSets()", "m_memory->CreateDescriptorPoolAndSets(m_descriptorSetLayout, m_forgeDescriptorSetLayout)")

with open(rm_cpp, "w") as f:
    f.write(rm_content)

