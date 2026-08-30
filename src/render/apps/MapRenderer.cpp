#include "pch.h"
#include "MapRenderer.h"
#include "SharedContext.h"
#include "ForgeWorld.h"
#include "ForgeComponents.h"
#include <iostream>
#include <cmath>

#include "VisibilityPolicy.h"

namespace fw {

struct MapForgePushConstantData {
    glm::mat4 mvp;
    glm::vec4 colorOverride;
    int useColorOverride;
    float seasonProgress;
    uint32_t grid_width;
    uint32_t debug_lens_active;
    glm::vec4 lightDir;
    glm::vec4 cameraPos;
};

bool MapRenderer::Initialize(VkDevice device, VkRenderPass renderPass, VkExtent2D extent) {
    m_extent = extent;
    return true;
}

void MapRenderer::Draw(VkCommandBuffer cmd, SharedContext* context, glm::mat4 viewMatrix, glm::mat4 projMatrix) {
    if (!context || !context->forgeWorld) return;
    if (m_pipeline == VK_NULL_HANDLE || m_pipelineLayout == VK_NULL_HANDLE) return;
    if (m_globalVramBuffer == VK_NULL_HANDLE) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    if (m_descriptorSets && !m_descriptorSets->empty() && (*m_descriptorSets)[m_currentFrame] != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &(*m_descriptorSets)[m_currentFrame], 0, nullptr);
    }

    VkViewport viewport{};
    if (context->isMapBuilderMode) {
        viewport.x = m_extent.width * 0.45f;
        viewport.y = 0.0f;
        viewport.width = m_extent.width * 0.55f;
    } else {
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)m_extent.width;
    }
    viewport.height = (float)m_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = m_extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    glm::mat4 viewProjMatrix = projMatrix * viewMatrix;

    WorldVisibilityPolicy visibilityPolicy(viewProjMatrix);

    MapForgePushConstantData pcData{};
    VkDeviceSize offsets[] = { 0 };

    auto& registry = context->forgeWorld->GetRegistry();
    auto view = registry.view<fw::MeshComponent, fw::TransformComponent>();

    for (auto entity : view) {
        const auto& mesh = view.get<fw::MeshComponent>(entity);
        const auto& trans = view.get<fw::TransformComponent>(entity);

        if (!visibilityPolicy.IsVisible(registry, entity, mesh, trans)) continue;

        if (mesh.type != fw::MeshType::Chunk && mesh.type != fw::MeshType::Prefab && mesh.type != fw::MeshType::Standard) continue;

        fw::Mat4 fwModel = trans.worldMatrix();
        glm::mat4 model = glm::transpose(*reinterpret_cast<glm::mat4*>(&fwModel));

        pcData.mvp = viewProjMatrix * model;
        if (mesh.colorOverride[3] > 0.5f) {
            pcData.useColorOverride = 1;
            pcData.colorOverride = glm::vec4(mesh.colorOverride[0], mesh.colorOverride[1], mesh.colorOverride[2], mesh.colorOverride[3]);
        } else {
            pcData.useColorOverride = 0;
            pcData.colorOverride = glm::vec4(1.0f);
        }
        pcData.seasonProgress = 0.0f;
        pcData.lightDir = glm::vec4(context->previewLightDir, 1.0f);
        pcData.cameraPos = glm::vec4(context->activeCameraView.cameraPosition, 1.0f);

        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MapForgePushConstantData), &pcData);

        offsets[0] = mesh.vramAlloc.offset;
        VkBuffer vertexBuffers[] = { m_globalVramBuffer };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

        vkCmdDraw(cmd, (uint32_t)mesh.vertices.size(), 1, 0, 0);
    }
}

void MapRenderer::Cleanup(VkDevice device) {
    // Pipeline e risorse VRAM sono gestite da RenderManager
}

} // namespace fw

