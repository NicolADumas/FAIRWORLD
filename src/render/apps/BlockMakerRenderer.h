#pragma once
#include "AppRenderer.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

struct SharedContext;

namespace fw {

class BlockMakerRenderer : public AppRenderer {
public:
    BlockMakerRenderer() = default;
    ~BlockMakerRenderer() override = default;

    bool Initialize(VkDevice device, VkRenderPass renderPass, VkExtent2D extent) override;
    void Draw(VkCommandBuffer cmd, SharedContext* context, glm::mat4 viewMatrix, glm::mat4 projMatrix) override;
    void Cleanup(VkDevice device) override;

    // Setter temporanei finché VulkanPipeline non viene isolato
    void SetPipeline(VkPipeline pipeline, VkPipelineLayout layout) {
        m_pipeline = pipeline;
        m_pipelineLayout = layout;
    }
    
    void SetGlobalBuffer(VkBuffer buffer) {
        m_globalVramBuffer = buffer;
    }

    // Setter per viewport/scissor
    void SetSwapchainExtent(VkExtent2D extent) {
        m_extent = extent;
    }

    // Setter per i descriptor sets di default (forge descriptor sets globali per ambiente/blocchi non-painter)
    void SetDescriptorSets(const std::vector<VkDescriptorSet>* sets) {
        m_descriptorSets = sets;
    }
    void SetDefaultDescriptorSets(const std::vector<VkDescriptorSet>* sets) {
        m_descriptorSets = sets;
    }

    // Setter per i descriptor sets del Painter dedicati (preview shape attiva)
    void SetPainterDescriptorSets(const std::vector<VkDescriptorSet>* sets) {
        m_painterDescriptorSets = sets;
    }

    // Setter per il frame corrente
    void SetCurrentFrame(uint32_t frame) {
        m_currentFrame = frame;
    }

private:
    VkPipeline m_pipeline{ VK_NULL_HANDLE };
    VkPipelineLayout m_pipelineLayout{ VK_NULL_HANDLE };
    VkBuffer m_globalVramBuffer{ VK_NULL_HANDLE };
    VkExtent2D m_extent{ 0, 0 };
    const std::vector<VkDescriptorSet>* m_descriptorSets{ nullptr };
    const std::vector<VkDescriptorSet>* m_painterDescriptorSets{ nullptr };
    uint32_t m_currentFrame{ 0 };
};

} // namespace fw
