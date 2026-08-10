import os

src_file = "D:/FAIRWORLD/FAIRWORLD/src/render/RenderManager.cpp"
dst_file = "D:/FAIRWORLD/FAIRWORLD/src/render/vulkan/VulkanMemory.cpp"

with open(src_file, "r") as f:
    lines = f.readlines()

# Extract VulkanMemory methods
memory_methods = [
    "FindMemoryType(uint32_t",
    "CreateBuffer(VkDeviceSize",
    "CreateUniformBuffers()",
    "CreateDescriptorPoolAndSets()"
]

vulkan_mem_content = """#include "pch.h"
#include "VulkanMemory.h"
#include "VulkanCore.h"
#include "RenderManager.h" // Per struct UniformBufferObject, ecc.
#include <iostream>

namespace fw {

VulkanMemory::VulkanMemory(VulkanCore* core) : m_core(core) {}

VulkanMemory::~VulkanMemory() { Cleanup(); }

bool VulkanMemory::Initialize() {
    // Qui andrà l'inizializzazione di VMA e dei buffer globali, ma per ora lo lasciamo vuoto
    // o estraiamo solo quello che serve in seguito.
    return true;
}

void VulkanMemory::Cleanup() {
    // Cleanup will be implemented here
}

"""

new_rm_lines = []
skip = False
extracted_code = ""
brace_count = 0
in_target_method = False

for line in lines:
    if not in_target_method:
        is_target = False
        for m in memory_methods:
            if f"RenderManager::{m}" in line or (f"uint32_t RenderManager::" in line and "FindMemoryType" in m):
                is_target = True
                break
        
        if is_target:
            in_target_method = True
            brace_count = line.count("{") - line.count("}")
            
            # Replace RenderManager with VulkanMemory
            mod_line = line.replace("RenderManager::", "VulkanMemory::")
            extracted_code += mod_line
            continue
        
        new_rm_lines.append(line)
    else:
        # We are inside the extracted method
        mod_line = line
        
        # Replace m_device with m_core->GetDevice() etc
        replacements = {
            r'm_device': 'm_core->GetDevice()',
            r'm_physicalDevice': 'm_core->GetPhysicalDevice()',
            r'm_swapchainExtent': 'm_core->GetSwapchainExtent()'
        }
        for old, new_ in replacements.items():
            import re
            mod_line = re.sub(r'\\b' + old + r'\\b', new_, mod_line)
            
        extracted_code += mod_line
        brace_count += line.count("{") - line.count("}")
        if brace_count <= 0:
            in_target_method = False

with open(dst_file, "w") as f:
    f.write(vulkan_mem_content + extracted_code + "\\n} // namespace fw\\n")

with open(src_file, "w") as f:
    f.writelines(new_rm_lines)
