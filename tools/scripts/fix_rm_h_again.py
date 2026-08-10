import re

file_path = "D:/FAIRWORLD/FAIRWORLD/src/render/RenderManager.h"
with open(file_path, "r") as f:
    content = f.read()

# Fix redefined m_core
content = re.sub(r'std::unique_ptr<fw::VulkanCore> m_core;\n', '', content)
# Insert m_core exactly once after the first private:
content = content.replace('private:', 'private:\n    std::unique_ptr<fw::VulkanCore> m_core;\n', 1)

# Fix GetWindowWidth() and GetWindowHeight()
content = content.replace('uint32_t GetWindowWidth() const { return m_swapchainExtent.width; }', 'uint32_t GetWindowWidth() const { return m_core ? m_core->GetSwapchainExtent().width : 0; }')
content = content.replace('uint32_t GetWindowHeight() const { return m_swapchainExtent.height; }', 'uint32_t GetWindowHeight() const { return m_core ? m_core->GetSwapchainExtent().height : 0; }')

with open(file_path, "w") as f:
    f.write(content)
