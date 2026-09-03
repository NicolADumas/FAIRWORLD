#pragma once
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include "ForgeComponents.h"

namespace fw {

// =============================================================================
// FRUSTUM CULLING
// =============================================================================
struct CameraFrustum {
    glm::vec4 planes[6];

    void Extract(const glm::mat4& vpInput) {
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

    bool ContainsAABB(const glm::vec3& min, const glm::vec3& max) const {
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

// =============================================================================
// VISIBILITY POLICY INTERFACES
// =============================================================================

class VisibilityPolicy {
public:
    virtual ~VisibilityPolicy() = default;

    // Ritorna true se l'entità deve essere renderizzata in questo contesto
    virtual bool IsVisible(entt::registry& registry, entt::entity entity, const MeshComponent& mesh, const TransformComponent& transform) const = 0;
};

// Policy base per rendering di mondi estesi (Play, Map, SolarSystem)
// Effettua Frustum Culling e rispetta il VisibilityComponent
class WorldVisibilityPolicy : public VisibilityPolicy {
private:
    CameraFrustum m_frustum;
    bool m_useFrustumCulling;

public:
    WorldVisibilityPolicy(const glm::mat4& viewProjMatrix, bool enableCulling = true) 
        : m_useFrustumCulling(enableCulling) {
        if (m_useFrustumCulling) {
            m_frustum.Extract(viewProjMatrix);
        }
    }

    bool IsVisible(entt::registry& registry, entt::entity entity, const MeshComponent& mesh, const TransformComponent& transform) const override {
        // 1. Level: Entity Override
        if (auto* vis = registry.try_get<VisibilityComponent>(entity)) {
            if (!vis->enabled) return false;
        }

        // 2. Resource Validation (Rimosso check su mesh.vertices.empty() per permettere il clear VRAM post-upload)
        if (mesh.vramAlloc == fw::INVALID_VRAM_HANDLE) return false;

        // 3. Culling (Frustum) disabilitato temporaneamente per debug
        if (false && m_useFrustumCulling) {
            fw::Mat4 fwModel = transform.computeGlobalMatrix(registry);
            glm::mat4 model = glm::transpose(*reinterpret_cast<const glm::mat4*>(&fwModel));

            fw::AABB bounds = mesh.bounds();
            glm::vec3 center((bounds.min.x + bounds.max.x) * 0.5f, (bounds.min.y + bounds.max.y) * 0.5f, (bounds.min.z + bounds.max.z) * 0.5f);
            glm::vec3 extents((bounds.max.x - bounds.min.x) * 0.5f, (bounds.max.y - bounds.min.y) * 0.5f, (bounds.max.z - bounds.min.z) * 0.5f);

            glm::vec3 worldCenter = glm::vec3(model * glm::vec4(center, 1.0f));
            glm::vec3 worldExtents(
                std::abs(model[0][0]) * extents.x + std::abs(model[1][0]) * extents.y + std::abs(model[2][0]) * extents.z,
                std::abs(model[0][1]) * extents.x + std::abs(model[1][1]) * extents.y + std::abs(model[2][1]) * extents.z,
                std::abs(model[0][2]) * extents.x + std::abs(model[1][2]) * extents.y + std::abs(model[2][2]) * extents.z
            );

            if (!m_frustum.ContainsAABB(worldCenter - worldExtents, worldCenter + worldExtents)) {
                return false;
            }
        }

        return true;
    }
};

// Policy per Editor (Forge, ChunkEditor)
// A differenza del World, potrebbe bypassare il culling se ci sono debug view speciali,
// ma per ora si comporta come un frustum standard, rispettando sempre la Visibility.
class EditorVisibilityPolicy : public VisibilityPolicy {
private:
    CameraFrustum m_frustum;
public:
    EditorVisibilityPolicy(const glm::mat4& viewProjMatrix) {
        m_frustum.Extract(viewProjMatrix);
    }

    bool IsVisible(entt::registry& registry, entt::entity entity, const MeshComponent& mesh, const TransformComponent& transform) const override {
        // In editor permettiamo l'override.
        if (auto* vis = registry.try_get<VisibilityComponent>(entity)) {
            if (!vis->enabled) return false;
        }
        
        if (mesh.vramAlloc == fw::INVALID_VRAM_HANDLE) return false;

        // Frustum culling temporaneamente disabilitato
        if (false) {
            fw::Mat4 fwModel = transform.computeGlobalMatrix(registry);
            glm::mat4 model = glm::transpose(*reinterpret_cast<const glm::mat4*>(&fwModel));
            fw::AABB bounds = mesh.bounds();
            glm::vec3 center((bounds.min.x + bounds.max.x) * 0.5f, (bounds.min.y + bounds.max.y) * 0.5f, (bounds.min.z + bounds.max.z) * 0.5f);
            glm::vec3 extents((bounds.max.x - bounds.min.x) * 0.5f, (bounds.max.y - bounds.min.y) * 0.5f, (bounds.max.z - bounds.min.z) * 0.5f);

            glm::vec3 worldCenter = glm::vec3(model * glm::vec4(center, 1.0f));
            glm::vec3 worldExtents(
                std::abs(model[0][0]) * extents.x + std::abs(model[1][0]) * extents.y + std::abs(model[2][0]) * extents.z,
                std::abs(model[0][1]) * extents.x + std::abs(model[1][1]) * extents.y + std::abs(model[2][1]) * extents.z,
                std::abs(model[0][2]) * extents.x + std::abs(model[1][2]) * extents.y + std::abs(model[2][2]) * extents.z
            );

            if (!m_frustum.ContainsAABB(worldCenter - worldExtents, worldCenter + worldExtents)) {
                return false;
            }
        }

        return true;
    }
};

} // namespace fw
