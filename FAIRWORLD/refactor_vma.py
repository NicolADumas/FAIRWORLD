import os
import re

file_path = r"d:\FILE OF FAIRWORLD\FAIRWORLD\FAIRWORLD\RenderManager.cpp"
with open(file_path, "r", encoding="utf-8") as f:
    content = f.read()

# 1. Update CreateBuffer definition
content = content.replace(
"""bool RenderManager::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags properties,
                                 VkBuffer& buffer, VkDeviceMemory& bufferMemory) {""",
"""bool RenderManager::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VmaMemoryUsage vmaUsage,
                                 VkBuffer& buffer, VmaAllocation& bufferAllocation, VmaAllocationCreateFlags flags) {"""
)

# 2. Update CreateBuffer body
create_buffer_old = """    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) return false;

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) return false;
    vkBindBufferMemory(m_device, buffer, bufferMemory, 0);
    return true;"""

create_buffer_new = """    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = vmaUsage;
    allocInfo.flags = flags;

    if (vmaCreateBuffer(m_vmaAllocator, &bufferInfo, &allocInfo, &buffer, &bufferAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }
    return true;"""

content = content.replace(create_buffer_old, create_buffer_new)

# 3. Update CreateImage definition
content = content.replace(
"""void RenderManager::CreateImage(uint32_t width, uint32_t height, uint32_t layerCount, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {""",
"""void RenderManager::CreateImage(uint32_t width, uint32_t height, uint32_t layerCount, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VmaMemoryUsage vmaUsage, VkImage& image, VmaAllocation& imageAllocation) {"""
)

# 4. Update CreateImage body
create_image_old = """    if (vkCreateImage(m_device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate image memory!");
    }

    vkBindImageMemory(m_device, image, imageMemory, 0);"""

create_image_new = """    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = vmaUsage;
    
    if (vmaCreateImage(m_vmaAllocator, &imageInfo, &allocInfo, &image, &imageAllocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image with VMA!");
    }"""
content = content.replace(create_image_old, create_image_new)


# 5. Replacements for Memory -> Allocation
content = content.replace("m_uniformBuffersMemory", "m_uniformBuffersAllocation")
content = content.replace("m_textureImageMemory", "m_textureImageAllocation")
content = content.replace("m_depthImageMemory", "m_depthImageAllocation")
content = content.replace("m_ghostVertexBufferMemory", "m_ghostVertexBufferAllocation")
content = content.replace("m_ghostIndexBufferMemory", "m_ghostIndexBufferAllocation")
content = content.replace("vertexBufferMemory", "vertexBufferAllocation")
content = content.replace("indexBufferMemory", "indexBufferAllocation")

# 6. Usage flags updates in calling code
content = content.replace(
    "VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT",
    "VMA_MEMORY_USAGE_CPU_TO_GPU"
)
content = content.replace(
    "VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT",
    "VMA_MEMORY_USAGE_GPU_ONLY"
)

# 7. vkMapMemory / vkUnmapMemory / vkFreeMemory -> vma routines
content = content.replace(
    """        vkMapMemory(m_device, m_uniformBuffersAllocation[i], 0, bufferSize, 0, &m_uniformBuffersMapped[i]);""",
    """        VmaAllocationInfo allocInfo;
        vmaGetAllocationInfo(m_vmaAllocator, m_uniformBuffersAllocation[i], &allocInfo);
        m_uniformBuffersMapped[i] = allocInfo.pMappedData;"""
)

# Replace uniform buffer creation mapped bit
content = content.replace(
    """        CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VMA_MEMORY_USAGE_CPU_TO_GPU,
                     m_uniformBuffers[i], m_uniformBuffersAllocation[i]);""",
    """        CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VMA_MEMORY_USAGE_CPU_TO_GPU,
                     m_uniformBuffers[i], m_uniformBuffersAllocation[i], VMA_ALLOCATION_CREATE_MAPPED_BIT);"""
)


# Replaces for vkDestroyBuffer / vkFreeMemory -> vmaDestroyBuffer
def replace_destroy_buffer(match):
    buf = match.group(1)
    alloc = match.group(2)
    return f"vmaDestroyBuffer(m_vmaAllocator, {buf}, {alloc});"

content = re.sub(
    r"vkDestroyBuffer\(m_device,\s*([^,]+),\s*nullptr\);\s*vkFreeMemory\(m_device,\s*([^,]+),\s*nullptr\);",
    replace_destroy_buffer,
    content
)

# Replaces for vkDestroyImage / vkFreeMemory -> vmaDestroyImage
def replace_destroy_image(match):
    img = match.group(1)
    alloc = match.group(2)
    return f"vmaDestroyImage(m_vmaAllocator, {img}, {alloc});"

content = re.sub(
    r"vkDestroyImage\(m_device,\s*([^,]+),\s*nullptr\);\s*vkFreeMemory\(m_device,\s*([^,]+),\s*nullptr\);",
    replace_destroy_image,
    content
)

# Replaces for staging buffers Map/Unmap
content = re.sub(
    r"vkMapMemory\(m_device,\s*stagingBufferAllocation,\s*0,\s*imageSize,\s*0,\s*&data\);",
    "vmaMapMemory(m_vmaAllocator, stagingBufferAllocation, &data);",
    content
)
content = re.sub(
    r"vkMapMemory\(m_device,\s*stagingBufferAllocation,\s*0,\s*bufferSize,\s*0,\s*&data\);",
    "vmaMapMemory(m_vmaAllocator, stagingBufferAllocation, &data);",
    content
)
content = re.sub(
    r"vkUnmapMemory\(m_device,\s*stagingBufferAllocation\);",
    "vmaUnmapMemory(m_vmaAllocator, stagingBufferAllocation);",
    content
)


# Save
with open(file_path, "w", encoding="utf-8") as f:
    f.write(content)
print("RenderManager.cpp refactored for VMA!")
