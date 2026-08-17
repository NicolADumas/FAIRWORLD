#include "pch.h"
#include "PlanetMapperRenderer.h"
#include "SharedContext.h"
#include "ForgeWorld.h"
#include "ForgeComponents.h"
#include <iostream>
#include <cmath>

namespace fw {

struct PlanetMapperFrustum {
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

struct PlanetMapperPushConstants {
    glm::mat4 mvp;
    glm::vec4 colorOverride;
    int useColorOverride;
    float seasonProgress;
    uint32_t grid_width;
    uint32_t debug_lens_active;
    glm::vec4 lightDir;
    glm::vec4 cameraPos;
};

bool PlanetMapperRenderer::Initialize(VkDevice device, VkRenderPass renderPass, VkExtent2D extent) {
    m_extent = extent;
    return true;
}

void PlanetMapperRenderer::Draw(VkCommandBuffer cmd, SharedContext* context, glm::mat4 viewMatrix, glm::mat4 projMatrix) {
    if (!context || !context->forgeWorld) return;
    if (m_pipeline == VK_NULL_HANDLE || m_pipelineLayout == VK_NULL_HANDLE) return;
    if (!m_terrainPipelineInitialized && m_terrainPipeline) {
        // Manda il dispatch di generazione terreno una volta (o ogni volta che serve aggiornare)
        // In futuro, il RenderManager o il TerrainPipelineSystem stesso orchestrerà il compute!
        // Per ora ci limitiamo a dire al pipeline system di fare un dispatch rapido per sicurezza?
        // In realtà dovremmo solo chiamare il dispatch nel momento in cui vengono modificati i biomi.
        m_terrainPipelineInitialized = true;
    }

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
    
    PlanetMapperPushConstants pcData{};
    pcData.mvp = viewProjMatrix; // La mesh è già in World Space
    pcData.useColorOverride = 0;
    pcData.seasonProgress = 0.0f;
    pcData.lightDir = glm::vec4(context->previewLightDir, 1.0f);
    pcData.cameraPos = glm::vec4(context->activeCameraView.cameraPosition, 1.0f);

    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PlanetMapperPushConstants), &pcData);

    // --- DISEGNO INDIRETTO VIA GPU ---
    if (m_terrainPipeline && m_terrainPipeline->getVertexBuffer() && m_terrainPipeline->getIndirectBuffer()) {
        VkDeviceSize offsets[] = { 0 };
        VkBuffer vertexBuffers[] = { m_terrainPipeline->getVertexBuffer() };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, m_terrainPipeline->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        // drawCount è numChunks, usiamo 1 per ora o leggiamo da un buffer
        // L'assunzione temporanea è 100 chunk generati
        uint32_t numChunks = 100; // Da passare dinamicamente
        vkCmdDrawIndexedIndirect(cmd, m_terrainPipeline->getIndirectBuffer(), 0, numChunks, sizeof(VkDrawIndexedIndirectCommand));
    }
}

void PlanetMapperRenderer::Cleanup(VkDevice device) {
    // Risorse VRAM gestite dalla Regina (RenderManager)
}

} // namespace fw
