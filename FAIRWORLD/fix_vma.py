import os
import re

file_path = r"d:\FILE OF FAIRWORLD\FAIRWORLD\FAIRWORLD\RenderManager.cpp"
with open(file_path, "r", encoding="utf-8") as f:
    content = f.read()

# 1. Fix CreateBuffer manual calls if any (line 1191)
content = re.sub(
    r"vkAllocateMemory\(m_device,\s*&allocInfo,\s*nullptr,\s*&bufferMemory\)\s*!=\s*VK_SUCCESS",
    "vmaCreateBuffer(m_vmaAllocator, &bufferInfo, &allocInfo, &buffer, &bufferAllocation, nullptr) != VK_SUCCESS",
    content
)

# Replace all vkFreeMemory(m_device, m_..., nullptr) -> vmaFreeMemory(m_vmaAllocator, m_...)
# Actually VMA uses vmaFreeMemory or vmaDestroyBuffer(allocator, buffer, allocation)
def replace_free_memory(match):
    var_name = match.group(1)
    return f"vmaFreeMemory(m_vmaAllocator, {var_name});"
content = re.sub(r"vkFreeMemory\(m_device,\s*([^,]+),\s*nullptr\);", replace_free_memory, content)

# 2. Fix uniform buffer manual alloc
# If they used vkAllocateMemory directly for uniforms
content = re.sub(
    r"vkAllocateMemory\(m_device,\s*&allocInfo,\s*nullptr,\s*&m_uniformBuffersAllocation\[i\]\);",
    "// vkAllocateMemory removed",
    content
)
content = re.sub(
    r"vkBindBufferMemory\(m_device,\s*m_uniformBuffers\[i\],\s*m_uniformBuffersAllocation\[i\],\s*0\);",
    "// vkBindBufferMemory removed",
    content
)
content = re.sub(
    r"vkBindBufferMemory\(m_device,\s*buffer,\s*bufferMemory,\s*0\);",
    "// vkBindBufferMemory removed",
    content
)

# 3. Fix vkAllocateMemory for images (line 1744)
content = re.sub(
    r"vkAllocateMemory\(m_device,\s*&allocInfo,\s*nullptr,\s*&imageMemory\)\s*!=\s*VK_SUCCESS",
    "vmaCreateImage(m_vmaAllocator, &imageInfo, &allocInfo, &image, &imageAllocation, nullptr) != VK_SUCCESS",
    content
)
content = re.sub(
    r"vkBindImageMemory\(m_device,\s*image,\s*imageMemory,\s*0\);",
    "// vkBindImageMemory removed",
    content
)

# Fix destroys that were not caught
content = re.sub(
    r"vkDestroyBuffer\(m_device,\s*it->second\.vertexBuffer,\s*nullptr\);\s*}\s*if\s*\(it->second\.vertexBufferAllocation\s*!=\s*VK_NULL_HANDLE\)\s*{\s*vmaFreeMemory\(m_vmaAllocator,\s*it->second\.vertexBufferAllocation\);\s*}",
    "vmaDestroyBuffer(m_vmaAllocator, it->second.vertexBuffer, it->second.vertexBufferAllocation); }",
    content
)
content = re.sub(
    r"vkDestroyBuffer\(m_device,\s*it->second\.indexBuffer,\s*nullptr\);\s*}\s*if\s*\(it->second\.indexBufferAllocation\s*!=\s*VK_NULL_HANDLE\)\s*{\s*vmaFreeMemory\(m_vmaAllocator,\s*it->second\.indexBufferAllocation\);\s*}",
    "vmaDestroyBuffer(m_vmaAllocator, it->second.indexBuffer, it->second.indexBufferAllocation); }",
    content
)
content = re.sub(
    r"vkDestroyImage\(m_device,\s*m_depthImage,\s*nullptr\);\s*m_depthImage\s*=\s*VK_NULL_HANDLE;\s*}\s*if\s*\(m_depthImageAllocation\s*!=\s*VK_NULL_HANDLE\)\s*{\s*vmaFreeMemory\(m_vmaAllocator,\s*m_depthImageAllocation\);\s*m_depthImageAllocation\s*=\s*VK_NULL_HANDLE;\s*}",
    "vmaDestroyImage(m_vmaAllocator, m_depthImage, m_depthImageAllocation); m_depthImage = VK_NULL_HANDLE; m_depthImageAllocation = VK_NULL_HANDLE; }",
    content
)
content = re.sub(
    r"vkDestroyBuffer\(m_device,\s*pair\.second\.vertexBuffer,\s*nullptr\);\s*if\s*\(pair\.second\.vertexBufferAllocation\s*!=\s*VK_NULL_HANDLE\)\s*vmaFreeMemory\(m_vmaAllocator,\s*pair\.second\.vertexBufferAllocation\);",
    "vmaDestroyBuffer(m_vmaAllocator, pair.second.vertexBuffer, pair.second.vertexBufferAllocation);",
    content
)
content = re.sub(
    r"vkDestroyBuffer\(m_device,\s*pair\.second\.indexBuffer,\s*nullptr\);\s*if\s*\(pair\.second\.indexBufferAllocation\s*!=\s*VK_NULL_HANDLE\)\s*vmaFreeMemory\(m_vmaAllocator,\s*pair\.second\.indexBufferAllocation\);",
    "vmaDestroyBuffer(m_vmaAllocator, pair.second.indexBuffer, pair.second.indexBufferAllocation);",
    content
)
content = re.sub(
    r"vkDestroyImage\(m_device,\s*m_depthImage,\s*nullptr\);\s*vmaFreeMemory\(m_vmaAllocator,\s*m_depthImageAllocation\);",
    "vmaDestroyImage(m_vmaAllocator, m_depthImage, m_depthImageAllocation);",
    content
)

with open(file_path, "w", encoding="utf-8") as f:
    f.write(content)
print("Fixes applied.")
