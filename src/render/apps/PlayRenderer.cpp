#include "pch.h"
#include "FAIRWORLD.h"
#include "RenderManager.h"
#include "PlayRenderer.h"
#include "SharedContext.h"
#include "ForgeWorld.h"
#include "ForgeComponents.h"
#include <iostream>
#include <cmath>

#include "VisibilityPolicy.h"

namespace fw {

struct PlayForgePushConstantData {
    glm::mat4 mvp;
    glm::vec4 colorOverride;
    int useColorOverride;
    float curvatureRadius;
    glm::vec2 chunkWorldXZ;
    glm::vec4 lightDir;
    glm::vec4 cameraPos;
};

bool PlayRenderer::Initialize(VkDevice device, VkRenderPass renderPass, VkExtent2D extent) {
    m_extent = extent;
    return true;
}

void PlayRenderer::Draw(VkCommandBuffer cmd, SharedContext* context, glm::mat4 viewMatrix, glm::mat4 projMatrix) {
    if (!context) return;

    // Disegna la scena del mondo principale tramite ForgeWorld (se attivo) o fallback
    if (context->forgeWorld && m_pipeline != VK_NULL_HANDLE && m_pipelineLayout != VK_NULL_HANDLE && m_globalVramBuffer != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

        if (m_descriptorSets && !m_descriptorSets->empty() && (*m_descriptorSets)[m_currentFrame] != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &(*m_descriptorSets)[m_currentFrame], 0, nullptr);
        }

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)m_extent.width;
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

        PlayForgePushConstantData pcData{};
        VkDeviceSize offsets[] = { 0 };

        auto& registry = context->forgeWorld->GetRegistry();
        auto view = registry.view<fw::MeshComponent, fw::TransformComponent>();

        float curvatureRadius = 0.0f;
        if (context->forgeWorld && context->forgeWorld->GetRegistry().valid(context->forgeWorld->GetPlanetEntity())) {
            auto& geom = context->forgeWorld->GetRegistry().get<fw::PlanetGeometryComponent>(context->forgeWorld->GetPlanetEntity());
            if (!geom.isLogicalSphere) {
                curvatureRadius = geom.planetRadius;
            }
        }

        for (auto entity : view) {
            const auto& mesh = view.get<fw::MeshComponent>(entity);
            const auto& trans = view.get<fw::TransformComponent>(entity);

            if (!visibilityPolicy.IsVisible(registry, entity, mesh, trans)) continue;

            fw::Mat4 fwModel = trans.computeGlobalMatrix(registry);
            glm::mat4 model = glm::transpose(*reinterpret_cast<glm::mat4*>(&fwModel));

            pcData.mvp = viewProjMatrix * model;
            if (mesh.colorOverride[3] > 0.5f) {
                pcData.useColorOverride = 1;
                pcData.colorOverride = glm::vec4(mesh.colorOverride[0], mesh.colorOverride[1], mesh.colorOverride[2], mesh.colorOverride[3]);
            } else {
                pcData.useColorOverride = 0;
                pcData.colorOverride = glm::vec4(0.0f);
            }
            pcData.curvatureRadius = curvatureRadius;
            pcData.chunkWorldXZ = glm::vec2(trans.location.x, trans.location.z);
            pcData.lightDir = glm::vec4(context->previewLightDir, 1.0f);
            pcData.cameraPos = glm::vec4(context->activeCameraView.cameraPosition, 1.0f);

            vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PlayForgePushConstantData), &pcData);

            auto allocInfo = context->vramAllocator->GetAllocation(mesh.vramAlloc);
            if (allocInfo.valid && allocInfo.compartmentIdx < context->engine->GetRenderManager()->GetVramCompartments().size()) {
                VkBuffer buf = context->engine->GetRenderManager()->GetVramCompartments()[allocInfo.compartmentIdx];
                VkDeviceSize newOffsets[] = { 0 };
                vkCmdBindVertexBuffers(cmd, 0, 1, &buf, newOffsets);
                vkCmdDraw(cmd, (uint32_t)mesh.vertices.size(), 1, allocInfo.offset / sizeof(fw::Vertex), 0);
            }
        }
    }
}

void PlayRenderer::Cleanup(VkDevice device) {
}

} // namespace fw

