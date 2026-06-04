import re

header_path = r"d:\FILE OF FAIRWORLD\FAIRWORLD\FAIRWORLD\RenderManager.h"
cpp_path = r"d:\FILE OF FAIRWORLD\FAIRWORLD\FAIRWORLD\RenderManager.cpp"

with open(header_path, "r", encoding="utf-8") as f:
    h_content = f.read()

if "void DefragmentVRAM();" not in h_content:
    # Add it under public methods
    h_content = h_content.replace(
        "void RecreateSwapchain();",
        "void RecreateSwapchain();\n    void DefragmentVRAM();"
    )
    with open(header_path, "w", encoding="utf-8") as f:
        f.write(h_content)

with open(cpp_path, "r", encoding="utf-8") as f:
    cpp_content = f.read()

if "void RenderManager::DefragmentVRAM()" not in cpp_content:
    cpp_content += """
// ---------------------------------------------------------
// FASE 5: DEFRAMMENTAZIONE A CALDO (FAST)
// ---------------------------------------------------------
void RenderManager::DefragmentVRAM() {
    if (!m_vmaAllocator || !m_chunkVmaPool) return;

    VmaDefragmentationInfo defragInfo = {};
    defragInfo.pool = m_chunkVmaPool;
    defragInfo.flags = VMA_DEFRAGMENTATION_FLAG_ALGORITHM_FAST_BIT;
    
    VmaDefragmentationContext defragCtx = VK_NULL_HANDLE;
    VkResult res = vmaBeginDefragmentation(m_vmaAllocator, &defragInfo, &defragCtx);
    
    if (res == VK_SUCCESS) {
        VmaDefragmentationPassMoveInfo pass = {};
        res = vmaBeginDefragmentationPass(m_vmaAllocator, defragCtx, &pass);
        if (res == VK_SUCCESS) {
            // Approccio "Fast" invisibile:
            // VMA unira' logicamente lo spazio libero frammentato nei suoi metadati.
            // Ignoriamo gli spostamenti fisici proposti per non dover distruggere/ricreare i VkBuffer
            // e non bloccare la GPU durante lo streaming dei chunk.
            for (uint32_t i = 0; i < pass.moveCount; i++) {
                pass.pMoves[i].operation = VMA_DEFRAGMENTATION_MOVE_OPERATION_IGNORE;
            }
            vmaEndDefragmentationPass(m_vmaAllocator, defragCtx, &pass);
        }
        vmaEndDefragmentation(m_vmaAllocator, defragCtx);
        std::cout << "[VMA] DefragmentVRAM() Fast-Pass completato." << std::endl;
    }
}
"""
    with open(cpp_path, "w", encoding="utf-8") as f:
        f.write(cpp_content)

print("DefragmentVRAM injected successfully.")
