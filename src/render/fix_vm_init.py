import re

rm_cpp = "D:/FAIRWORLD/FAIRWORLD/src/render/RenderManager.cpp"
vm_cpp = "D:/FAIRWORLD/FAIRWORLD/src/render/vulkan/VulkanMemory.cpp"

with open(rm_cpp, 'r') as f:
    rm_content = f.read()

# Estrarre il blocco da spostare in VulkanMemory::Initialize()
# Il blocco inizia con: // Inizializza VMA (Vulkan Memory Allocator)
# Finisce con: std::cout << "[VMA] VmaAllocator e Global VRAM Buffer (512MB) inizializzati con successo." << std::endl;

start_str = "// Inizializza VMA (Vulkan Memory Allocator)"
end_str = "std::cout << \"[VMA] VmaAllocator e Global VRAM Buffer (512MB) inizializzati con successo.\" << std::endl;"

start_idx = rm_content.find(start_str)
end_idx = rm_content.find(end_str) + len(end_str)

if start_idx != -1 and end_idx != -1:
    extracted_block = rm_content[start_idx:end_idx]
    
    # Rimuovi il blocco da RenderManager.cpp
    rm_content = rm_content[:start_idx] + rm_content[end_idx:]
    
    with open(rm_cpp, 'w') as f:
        f.write(rm_content)
        
    # Adatta il blocco per VulkanMemory.cpp
    # m_core->GetPhysicalDevice() -> m_core->GetPhysicalDevice() (rimane uguale)
    # &m_memory->GetAllocator() -> &m_vmaAllocator
    # m_memory->GetAllocator() -> m_vmaAllocator
    # m_core->FindQueueFamilies -> m_core->FindQueueFamilies
    # &m_memory->GetStagingRingBuffer() -> &m_stagingRingBuffer
    # m_memory->GetMappedStagingData() = -> m_mappedStagingData = 
    # &m_memory->GetChunkVmaPool() -> &m_chunkVmaPool
    # &m_memory->GetGlobalVramBuffer() -> &m_globalVramBuffer
    
    extracted_block = extracted_block.replace("&m_memory->GetAllocator()", "&m_vmaAllocator")
    extracted_block = extracted_block.replace("m_memory->GetAllocator()", "m_vmaAllocator")
    extracted_block = extracted_block.replace("&m_memory->GetStagingRingBuffer()", "&m_stagingRingBuffer")
    extracted_block = extracted_block.replace("m_memory->GetMappedStagingData() =", "m_mappedStagingData =")
    extracted_block = extracted_block.replace("&m_memory->GetChunkVmaPool()", "&m_chunkVmaPool")
    extracted_block = extracted_block.replace("&m_memory->GetGlobalVramBuffer()", "&m_globalVramBuffer")
    
    # Rimuovi la creazione del command pool per i transfer, quella RIMANE in RenderManager
    # Perche' VulkanMemory non ha m_transferCommandPool
    # Aspetta, extracted_block contiene anche la creazione del transferCommandPool!
    # Devo toglierlo dall'extracted_block e rimetterlo in RenderManager!

    pass

