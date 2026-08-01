#pragma once
#include "AppRenderer.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

struct SharedContext;

namespace fw {

// Ape Operaia (Worker Renderer) dedicata in esclusiva all'App Chunk Editor
class ChunkEditorRenderer : public AppRenderer {
public:
    ChunkEditorRenderer() = default;
    ~ChunkEditorRenderer() override = default;

    bool Initialize(VkDevice device, VkRenderPass renderPass, VkExtent2D extent) override;
    void Draw(VkCommandBuffer cmd, SharedContext* context, glm::mat4 viewMatrix, glm::mat4 projMatrix) override;
    void Cleanup(VkDevice device) override;

    void SetPipeline(VkPipeline pipeline, VkPipelineLayout layout) {
        m_pipeline = pipeline;
        m_pipelineLayout = layout;
    }

    void SetGlobalBuffer(VkBuffer buffer) {
        m_globalVramBuffer = buffer;
    }

    void SetSwapchainExtent(VkExtent2D extent) {
        m_extent = extent;
    }

    void SetDescriptorSets(const std::vector<VkDescriptorSet>* sets) {
        m_descriptorSets = sets;
    }

    void SetCurrentFrame(uint32_t frame) {
        m_currentFrame = frame;
    }

private:
    VkPipeline m_pipeline{ VK_NULL_HANDLE };
    VkPipelineLayout m_pipelineLayout{ VK_NULL_HANDLE };
    VkBuffer m_globalVramBuffer{ VK_NULL_HANDLE };
    VkExtent2D m_extent{ 0, 0 };
    const std::vector<VkDescriptorSet>* m_descriptorSets{ nullptr };
    uint32_t m_currentFrame{ 0 };
};

} // namespace fw
