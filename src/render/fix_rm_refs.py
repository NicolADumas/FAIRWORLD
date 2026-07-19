import re

file_path = "D:/FAIRWORLD/FAIRWORLD/src/render/RenderManager.cpp"
with open(file_path, "r") as f:
    content = f.read()

replacements = {
    r'\bm_instance\b': 'm_core->GetInstance()',
    r'\bm_physicalDevice\b': 'm_core->GetPhysicalDevice()',
    r'\bm_device\b': 'm_core->GetDevice()',
    r'\bm_surface\b': 'm_core->GetSurface()',
    r'\bm_swapchain\b': 'm_core->GetSwapchain()',
    r'\bm_swapchainExtent\b': 'm_core->GetSwapchainExtent()',
    r'\bm_swapchainImageFormat\b': 'm_core->GetSwapchainImageFormat()',
    r'\bm_swapchainImages\b': 'm_core->GetSwapchainImages()',
    r'\bm_swapchainImageViews\b': 'm_core->GetSwapchainImageViews()',
    r'\bm_graphicsQueue\b': 'm_core->GetGraphicsQueue()',
    r'\bm_presentQueue\b': 'm_core->GetPresentQueue()',
    r'\bm_transferQueue\b': 'm_core->GetTransferQueue()',
    r'\bm_queueMutex\b': '(*m_core->GetQueueMutex())'
}

for pattern, repl in replacements.items():
    content = re.sub(pattern, repl, content)

# Special cases: GetTransferQueue() const in RenderManager.h returns what?
# RenderManager::GetTransferQueue() const { return m_core->GetTransferQueue(); }
# Let's fix RenderManager::Init
init_pattern = r'bool RenderManager::Init\(bool isVRMode, XrManager\* xrManager, void\* hwnd, void\* hinstance\) \{.*?(?=bool RenderManager::CheckValidationLayerSupport|\Z)'
init_replacement = """bool RenderManager::Init(bool isVRMode, XrManager* xrManager, void* hwnd, void* hinstance) {
    m_isVRMode = isVRMode;
    m_hwnd = hwnd;
    m_core = std::make_unique<fw::VulkanCore>();
    if (!m_core->Initialize(isVRMode, xrManager, hwnd, hinstance)) return false;

"""

with open(file_path, "w") as f:
    f.write(content)
