#pragma once
#include "AppRenderer.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include "../vulkan/TerrainPipelineSystem.h"

struct SharedContext;

namespace fw {

// Ape Operaia (Worker Renderer) dedicata in esclusiva al Planet Mapper e Globo LOD
class PlanetMapperRenderer : public AppRenderer {
public:
    PlanetMapperRenderer() = default;
    ~PlanetMapperRenderer() override = default;

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

    void SetTerrainPipeline(TerrainPipelineSystem* tps) { m_terrainPipeline = tps; }
    void SetTerrainNumChunks(uint32_t n) { m_terrainNumChunks = n; }
    
private:
    VkPipeline m_pipeline{ VK_NULL_HANDLE };
    VkPipelineLayout m_pipelineLayout{ VK_NULL_HANDLE };
    VkBuffer m_globalVramBuffer{ VK_NULL_HANDLE };
    VkExtent2D m_extent{ 0, 0 };
    const std::vector<VkDescriptorSet>* m_descriptorSets{ nullptr };
    uint32_t m_currentFrame{ 0 };
    TerrainPipelineSystem* m_terrainPipeline{ nullptr };
    bool m_terrainPipelineInitialized{ false };
    uint32_t m_terrainNumChunks{ 0 };
};

} // namespace fw
