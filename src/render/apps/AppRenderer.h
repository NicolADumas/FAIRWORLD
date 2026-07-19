#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

struct SharedContext;

namespace fw {

class AppRenderer {
public:
    virtual ~AppRenderer() = default;

    // Chiamato una volta dal RenderManager quando la sub-applicazione viene registrata
    virtual bool Initialize(VkDevice device, VkRenderPass renderPass, VkExtent2D extent) = 0;

    // Chiamato ogni frame per eseguire i comandi di disegno per questa specifica applicazione
    virtual void Draw(VkCommandBuffer cmd, SharedContext* context, glm::mat4 viewMatrix, glm::mat4 projMatrix) = 0;
    
    // Rilascia le risorse specifiche dell'applicazione
    virtual void Cleanup(VkDevice device) = 0;
};

} // namespace fw
