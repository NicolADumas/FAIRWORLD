#include "pch.h"
#include "MapRenderer.h"
#include "SharedContext.h"
#include "ForgeWorld.h"
#include "ForgeComponents.h"
#include <iostream>
#include <cmath>

namespace fw {

struct MapCameraFrustum {
    glm::vec4 planes[6];

    void extract(const glm::mat4& vpInput) {
        glm::mat4 vp = vpInput;
        if (vp[1][1] < 0.0f) {
            vp[0][1] = -vp[0][1];
            vp[1][1] = -vp[1][1];
            vp[2][1] = -vp[2][1];
            vp[3][1] = -vp[3][1];
        }
        planes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);
        planes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);
        planes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]);
        planes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]);
        planes[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]);
        planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);

        for (int i = 0; i < 6; ++i) {
            float len = glm::length(glm::vec3(planes[i]));
            if (len > 0.00001f) {
                planes[i] /= len;
            }
        }
    }

    bool containsAABB(const glm::vec3& min, const glm::vec3& max) const {
        for (int i = 0; i < 6; ++i) {
            glm::vec3 p(
                planes[i].x > 0 ? max.x : min.x,
                planes[i].y > 0 ? max.y : min.y,
                planes[i].z > 0 ? max.z : min.z
            );
            if (glm::dot(glm::vec3(planes[i]), p) + planes[i].w < 0.0f) {
                return false;
            }
        }
        return true;
    }
};

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

    MapCameraFrustum frustum;
    frustum.extract(viewProjMatrix);

    MapForgePushConstantData pcData{};
    VkDeviceSize offsets[] = { 0 };

    auto& registry = context->forgeWorld->GetRegistry();
    auto view = registry.view<fw::MeshComponent, fw::TransformComponent>();

    for (auto entity : view) {
        const auto& mesh = view.get<fw::MeshComponent>(entity);
        const auto& trans = view.get<fw::TransformComponent>(entity);

        if (!mesh.vramAlloc.valid || mesh.vertices.empty()) continue;
        if (mesh.type != fw::MeshType::Chunk && mesh.type != fw::MeshType::Prefab) continue;

        fw::Mat4 fwModel = trans.worldMatrix();
        glm::mat4 model = glm::transpose(*reinterpret_cast<glm::mat4*>(&fwModel));

        fw::AABB bounds = mesh.bounds();
        glm::vec3 center((bounds.min.x + bounds.max.x) * 0.5f, (bounds.min.y + bounds.max.y) * 0.5f, (bounds.min.z + bounds.max.z) * 0.5f);
        glm::vec3 extents((bounds.max.x - bounds.min.x) * 0.5f, (bounds.max.y - bounds.min.y) * 0.5f, (bounds.max.z - bounds.min.z) * 0.5f);

        glm::vec3 worldCenter = glm::vec3(model * glm::vec4(center, 1.0f));
        glm::vec3 worldExtents(
            std::abs(model[0][0]) * extents.x + std::abs(model[1][0]) * extents.y + std::abs(model[2][0]) * extents.z,
            std::abs(model[0][1]) * extents.x + std::abs(model[1][1]) * extents.y + std::abs(model[2][1]) * extents.z,
            std::abs(model[0][2]) * extents.x + std::abs(model[1][2]) * extents.y + std::abs(model[2][2]) * extents.z
        );

        if (!frustum.containsAABB(worldCenter - worldExtents, worldCenter + worldExtents)) continue;

        pcData.mvp = viewProjMatrix * model;
        pcData.useColorOverride = 0;
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
