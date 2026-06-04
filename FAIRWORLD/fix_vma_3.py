import os
import re

file_path = r"d:\FILE OF FAIRWORLD\FAIRWORLD\FAIRWORLD\RenderManager.cpp"
with open(file_path, "r", encoding="utf-8") as f:
    content = f.read()

# Replace vkMapMemory
def replace_map_memory(match):
    alloc = match.group(1).strip()
    pdata = match.group(2).strip()
    return f"vmaMapMemory(m_vmaAllocator, {alloc}, {pdata});"

content = re.sub(
    r"vkMapMemory\(\s*[^,]+,\s*([^,]+),\s*[^,]+,\s*[^,]+,\s*[^,]+,\s*([^)]+)\s*\);",
    replace_map_memory,
    content
)

# Replace vkUnmapMemory
def replace_unmap_memory(match):
    alloc = match.group(1).strip()
    return f"vmaUnmapMemory(m_vmaAllocator, {alloc});"

content = re.sub(
    r"vkUnmapMemory\(\s*[^,]+,\s*([^)]+)\s*\);",
    replace_unmap_memory,
    content
)

with open(file_path, "w", encoding="utf-8") as f:
    f.write(content)
print("vkMapMemory and vkUnmapMemory replaced with VMA equivalents.")
