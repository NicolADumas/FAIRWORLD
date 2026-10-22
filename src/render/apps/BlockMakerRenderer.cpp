#include "pch.h"
#include "FAIRWORLD.h"
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
    static int s_drawLogCount = 0;
    bool shouldLog = (s_drawLogCount < 3);

    if (!context || !context->forgeWorld) {
        if (shouldLog) {
            std::cout << "[BM-R1][3D] BlockMakerRenderer::Draw() uscito prematuramente: context o context->forgeWorld nullo!\n";
            s_drawLogCount++;
        }
        return;
    }
    if (m_pipeline == VK_NULL_HANDLE) {
        if (shouldLog) {
            std::cout << "[BM-R1][3D] BlockMakerRenderer::Draw() uscito prematuramente: m_pipeline Vulkan e' VK_NULL_HANDLE!\n";
            s_drawLogCount++;
        }
        return;
    }

    if (shouldLog) {
        int gameModeVal = context->engine ? static_cast<int>(context->engine->GetGameMode()) : -1;
        std::cout << "[BM-R1][3D] BlockMakerRenderer::Draw() avviato. GameMode=" << gameModeVal
                  << " (5=BlockMaker), Extent=(" << m_extent.width << "x" << m_extent.height << ")\n";
    }

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

    // --- 3. Setup Descriptor Sets (Isolamento BM-P01) ---
    VkDescriptorSet defaultDs = VK_NULL_HANDLE;
    if (m_descriptorSets && !m_descriptorSets->empty() && m_currentFrame < m_descriptorSets->size()) {
        defaultDs = (*m_descriptorSets)[m_currentFrame];
    }

    VkDescriptorSet painterDs = VK_NULL_HANDLE;
    if (m_painterDescriptorSets && !m_painterDescriptorSets->empty() && m_currentFrame < m_painterDescriptorSets->size()) {
        painterDs = (*m_painterDescriptorSets)[m_currentFrame];
    }

    VkDescriptorSet currentBoundDs = VK_NULL_HANDLE;
    if (defaultDs != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &defaultDs, 0, nullptr);
        currentBoundDs = defaultDs;
    }

    glm::mat4 viewProjMatrix = projMatrix * viewMatrix;
    EditorVisibilityPolicy visibilityPolicy(viewProjMatrix);

    // --- 4. Disegna le entità ECS ---
    auto& registry = context->forgeWorld->GetRegistry();
    auto view = registry.view<fw::MeshComponent, fw::TransformComponent>();

    uint32_t totalEntitiesInView = 0;
    uint32_t drawnEntitiesCount = 0;

    for (auto entity : view) {
        totalEntitiesInView++;
        const auto& mesh = view.get<fw::MeshComponent>(entity);
        const auto& trans = view.get<fw::TransformComponent>(entity);

        bool isVisible = visibilityPolicy.IsVisible(registry, entity, mesh, trans);
        std::string metaName = registry.any_of<fw::MetadataComponent>(entity) ? registry.get<fw::MetadataComponent>(entity).name : "Unnamed";

        if (shouldLog) {
            std::cout << "[BM-R1][3D] Entita ECS [" << metaName << "] EntityID=" << (uint32_t)entity
                      << " | Vertici=" << mesh.vertices.size()
                      << " | Triangoli=" << (mesh.vertices.size() / 3)
                      << " | MeshType=" << (int)mesh.type
                      << " | VramHandle=" << mesh.vramAlloc
                      << " | IsVisible=" << (isVisible ? "SI" : "NO") << "\n";
        }

        if (!isVisible) continue;

        // Renderizza solo blocchi di tipo Chunk o Editor
        if (mesh.type == fw::MeshType::Chunk || mesh.type == fw::MeshType::Editor) {
            // BM-P01: Isolamento rigoroso. Solo l'entità PreviewBlock (Shape in editing) riceve painterDs.
            // BlockMakerEnv (pavimento/ambiente) e altre entità usano defaultDs (Forge PBR standard).
            VkDescriptorSet targetDs = defaultDs;
            if (metaName == "PreviewBlock") {
                targetDs = (painterDs != VK_NULL_HANDLE) ? painterDs : defaultDs;
            }

            if (targetDs != VK_NULL_HANDLE && targetDs != currentBoundDs) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &targetDs, 0, nullptr);
                currentBoundDs = targetDs;
            }
            fw::Mat4 fwModel = trans.computeGlobalMatrix(registry);
            glm::mat4 model = glm::transpose(*reinterpret_cast<glm::mat4*>(&fwModel));

            ForgePushConstantData pcData{};
            pcData.mvp              = viewProjMatrix * model;
            pcData.useColorOverride = 0;
            pcData.curvatureRadius  = 0.0f; // Nessuna curvatura in BlockMaker
            pcData.chunkWorldXZ     = glm::vec2(trans.location.x, trans.location.z);
            pcData.lightDir         = glm::vec4(0.5f, -1.0f, 0.5f, 0.0f);
            pcData.cameraPos        = glm::vec4(0.0f);

            vkCmdPushConstants(cmd, m_pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(ForgePushConstantData), &pcData);

            auto allocInfo = context->vramAllocator ? context->vramAllocator->GetAllocation(mesh.vramAlloc) : fw::VramAllocationInfo{};
            bool vramValid = allocInfo.valid && context->engine && context->engine->GetRenderManager() &&
                             (allocInfo.compartmentIdx < context->engine->GetRenderManager()->GetVramCompartments().size());

            if (vramValid) {
                VkBuffer buf = context->engine->GetRenderManager()->GetVramCompartments()[allocInfo.compartmentIdx];
                VkDeviceSize offsets[] = { allocInfo.offset };
                vkCmdBindVertexBuffers(cmd, 0, 1, &buf, offsets);
                vkCmdDraw(cmd, (uint32_t)mesh.vertices.size(), 1, 0, 0);
                drawnEntitiesCount++;
            } else if (shouldLog) {
                std::cout << "[BM-R1][3D] ERRORE VRAM per entita [" << metaName << "]: allocInfo.valid="
                          << (allocInfo.valid ? "true" : "false")
                          << " | CompartmentIdx=" << allocInfo.compartmentIdx << "\n";
            }
        }
    }

    if (shouldLog) {
        std::cout << "[BM-R1][3D] BlockMakerRenderer::Draw() completato: Entita nel view=" << totalEntitiesInView
                  << ", Entita disegnate con vkCmdDraw=" << drawnEntitiesCount << "\n";
        s_drawLogCount++;
    }
}

} // namespace fw
