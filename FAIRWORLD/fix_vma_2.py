import os
import re

file_path = r"d:\FILE OF FAIRWORLD\FAIRWORLD\FAIRWORLD\RenderManager.cpp"
with open(file_path, "r", encoding="utf-8") as f:
    content = f.read()

# 1. Replace ALL VkDeviceMemory with VmaAllocation in RenderManager.cpp
content = content.replace("VkDeviceMemory", "VmaAllocation")

# 2. Fix allocInfo redefinition in CreateUniformBuffers (around line 620)
# vmaGetAllocationInfo(m_vmaAllocator, m_uniformBuffersAllocation[i], &allocInfo);
# We can rename it to vmaAllocInfo
content = content.replace(
    "VmaAllocationInfo allocInfo;",
    "VmaAllocationInfo vmaAllocInfo;"
)
content = content.replace(
    "vmaGetAllocationInfo(m_vmaAllocator, m_uniformBuffersAllocation[i], &allocInfo);",
    "vmaGetAllocationInfo(m_vmaAllocator, m_uniformBuffersAllocation[i], &vmaAllocInfo);"
)
content = content.replace(
    "m_uniformBuffersMapped[i] = allocInfo.pMappedData;",
    "m_uniformBuffersMapped[i] = vmaAllocInfo.pMappedData;"
)

# 3. Some CreateBuffer calls use VkMemoryPropertyFlags (like VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
# Let's replace the usages with VMA_MEMORY_USAGE equivalents
content = content.replace(
    "VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT",
    "VMA_MEMORY_USAGE_CPU_TO_GPU"
)
content = content.replace(
    "VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT",
    "VMA_MEMORY_USAGE_GPU_ONLY"
)

# 4. In case there is an old CreateBuffer definition still lying around, let's remove it if it exists
# We replaced lines 1174 to 1195. Let's make sure there isn't another one.
# It seems the compiler says: `RenderManager.cpp(1174,21): vedere la dichiarazione di 'RenderManager::CreateBuffer'`
# Which means the compiler SAW the correct definition at 1174, but it complained that callers are passing VkDeviceMemory!
# Which we just fixed in step 1.

with open(file_path, "w", encoding="utf-8") as f:
    f.write(content)
print("Fixes applied.")
