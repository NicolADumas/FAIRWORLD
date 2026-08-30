#include "pch.h"
#include "BlockMakerRenderer.h"
#include "SharedContext.h"
#include "ForgeWorld.h"
#include "ForgeComponents.h"

#include "RenderManager.h"
#include "VisibilityPolicy.h"

namespace fw {

bool BlockMakerRenderer::Initialize(VkDevice device, VkRenderPass renderPass, VkExtent2D extent) {
    m_extent = extent;
    return true;
}

void BlockMakerRenderer::Cleanup(VkDevice device) {
    // Pipeline cleanup is handled by RenderManager for now
}

void BlockMakerRenderer::Draw(VkCommandBuffer cmd, SharedContext* context, glm::mat4 viewMatrix, glm::mat4 projMatrix) {
    if (!context || !context->forgeWorld) return;
    if (m_pipeline == VK_NULL_HANDLE) return;

    // --- 1. Imposta Viewport e Scissor (OBBLIGATORIO per pipeline con stati dinamici) ---
    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = (float)m_extent.width;
    viewport.height   = (float)m_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // --- 2. Bind Pipeline ---
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    // --- 3. Bind Descriptor Sets (texture PBR, UBO) ---
    if (m_descriptorSets && !m_descriptorSets->empty() && m_currentFrame < m_descriptorSets->size()) {
        VkDescriptorSet ds = (*m_descriptorSets)[m_currentFrame];
        if (ds != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &ds, 0, nullptr);
        }
    }

    glm::mat4 viewProjMatrix = projMatrix * viewMatrix;
    EditorVisibilityPolicy visibilityPolicy(viewProjMatrix);

    // --- 4. Disegna le entità ECS ---
    auto& registry = context->forgeWorld->GetRegistry();
    auto view = registry.view<fw::MeshComponent, fw::TransformComponent>();

    for (auto entity : view) {
        const auto& mesh = view.get<fw::MeshComponent>(entity);
        const auto& trans = view.get<fw::TransformComponent>(entity);

        if (!visibilityPolicy.IsVisible(registry, entity, mesh, trans)) continue;

        // Renderizza solo blocchi di tipo Chunk o Editor
        if (mesh.type == fw::MeshType::Chunk || mesh.type == fw::MeshType::Editor) {
            fw::Mat4 fwModel = trans.worldMatrix();
            glm::mat4 model = glm::transpose(*reinterpret_cast<glm::mat4*>(&fwModel));

            ForgePushConstantData pcData{};
            pcData.mvp              = viewProjMatrix * model;
            pcData.useColorOverride = 0;
            pcData.seasonProgress   = 0.0f; // Nessuna stagione in BlockMaker
            pcData.lightDir         = glm::vec4(0.5f, -1.0f, 0.5f, 0.0f);
            pcData.cameraPos        = glm::vec4(0.0f);

            vkCmdPushConstants(cmd, m_pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(ForgePushConstantData), &pcData);

            VkDeviceSize offsets[] = { mesh.vramAlloc.offset };
            VkBuffer vertexBuffers[] = { m_globalVramBuffer };
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            
            vkCmdDraw(cmd, (uint32_t)mesh.vertices.size(), 1, 0, 0);
        }
    }
}

} // namespace fw
