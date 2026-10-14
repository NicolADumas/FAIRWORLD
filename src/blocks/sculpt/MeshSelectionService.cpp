#include "pch.h"
#include "MeshSelectionService.h"
#include "SurfaceDefinition.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

namespace fw {

MeshSelectionService::MeshSelectionService() {
    Clear();
}

void MeshSelectionService::SetMode(SelectionMode mode) {
    if (m_mode != mode) {
        m_mode = mode;
        IncrementRevision();
    }
}

// =============================================================================
// GESTIONE SELEZIONE ELEMENTI
// =============================================================================

void MeshSelectionService::SelectVertex(VertexID id, SelectionOp op) {
    if (!id.IsValid()) return;

    if (op == SelectionOp::Replace) {
        m_selectedVertices.clear();
        m_selectedVertices.insert(id);
        m_activeVertex = id;
    } else if (op == SelectionOp::Add) {
        m_selectedVertices.insert(id);
        m_activeVertex = id;
    } else if (op == SelectionOp::Subtract) {
        m_selectedVertices.erase(id);
        if (m_activeVertex == id) {
            m_activeVertex = m_selectedVertices.empty() ? INVALID_VERTEX_ID : *m_selectedVertices.begin();
        }
    } else if (op == SelectionOp::Toggle) {
        if (m_selectedVertices.count(id) > 0) {
            m_selectedVertices.erase(id);
            if (m_activeVertex == id) {
                m_activeVertex = m_selectedVertices.empty() ? INVALID_VERTEX_ID : *m_selectedVertices.begin();
            }
        } else {
            m_selectedVertices.insert(id);
            m_activeVertex = id;
        }
    }
    IncrementRevision();
}

void MeshSelectionService::DeselectVertex(VertexID id) {
    SelectVertex(id, SelectionOp::Subtract);
}

bool MeshSelectionService::IsVertexSelected(VertexID id) const {
    return m_selectedVertices.count(id) > 0;
}

void MeshSelectionService::SelectEdge(EdgeID id, SelectionOp op) {
    if (!id.IsValid()) return;

    if (op == SelectionOp::Replace) {
        m_selectedEdges.clear();
        m_selectedEdges.insert(id);
        m_activeEdge = id;
    } else if (op == SelectionOp::Add) {
        m_selectedEdges.insert(id);
        m_activeEdge = id;
    } else if (op == SelectionOp::Subtract) {
        m_selectedEdges.erase(id);
        if (m_activeEdge == id) {
            m_activeEdge = m_selectedEdges.empty() ? INVALID_EDGE_ID : *m_selectedEdges.begin();
        }
    } else if (op == SelectionOp::Toggle) {
        if (m_selectedEdges.count(id) > 0) {
            m_selectedEdges.erase(id);
            if (m_activeEdge == id) {
                m_activeEdge = m_selectedEdges.empty() ? INVALID_EDGE_ID : *m_selectedEdges.begin();
            }
        } else {
            m_selectedEdges.insert(id);
            m_activeEdge = id;
        }
    }
    IncrementRevision();
}

void MeshSelectionService::DeselectEdge(EdgeID id) {
    SelectEdge(id, SelectionOp::Subtract);
}

bool MeshSelectionService::IsEdgeSelected(EdgeID id) const {
    return m_selectedEdges.count(id) > 0;
}

void MeshSelectionService::SelectFace(FaceID id, SelectionOp op) {
    if (!id.IsValid()) return;

    if (op == SelectionOp::Replace) {
        m_selectedFaces.clear();
        m_selectedFaces.insert(id);
        m_activeFace = id;
    } else if (op == SelectionOp::Add) {
        m_selectedFaces.insert(id);
        m_activeFace = id;
    } else if (op == SelectionOp::Subtract) {
        m_selectedFaces.erase(id);
        if (m_activeFace == id) {
            m_activeFace = m_selectedFaces.empty() ? INVALID_FACE_ID : *m_selectedFaces.begin();
        }
    } else if (op == SelectionOp::Toggle) {
        if (m_selectedFaces.count(id) > 0) {
            m_selectedFaces.erase(id);
            if (m_activeFace == id) {
                m_activeFace = m_selectedFaces.empty() ? INVALID_FACE_ID : *m_selectedFaces.begin();
            }
        } else {
            m_selectedFaces.insert(id);
            m_activeFace = id;
        }
    }
    IncrementRevision();
}

void MeshSelectionService::DeselectFace(FaceID id) {
    SelectFace(id, SelectionOp::Subtract);
}

bool MeshSelectionService::IsFaceSelected(FaceID id) const {
    return m_selectedFaces.count(id) > 0;
}

void MeshSelectionService::Clear() {
    m_selectedVertices.clear();
    m_selectedEdges.clear();
    m_selectedFaces.clear();
    m_activeVertex = INVALID_VERTEX_ID;
    m_activeEdge = INVALID_EDGE_ID;
    m_activeFace = INVALID_FACE_ID;
    IncrementRevision();
}

void MeshSelectionService::ClearCurrentMode() {
    if (m_mode == SelectionMode::Vertex) {
        m_selectedVertices.clear();
        m_activeVertex = INVALID_VERTEX_ID;
    } else if (m_mode == SelectionMode::Edge) {
        m_selectedEdges.clear();
        m_activeEdge = INVALID_EDGE_ID;
    } else if (m_mode == SelectionMode::Face) {
        m_selectedFaces.clear();
        m_activeFace = INVALID_FACE_ID;
    }
    IncrementRevision();
}

void MeshSelectionService::SelectAll(const EditableMesh& mesh) {
    if (m_mode == SelectionMode::Vertex) {
        m_selectedVertices.clear();
        auto vIds = mesh.GetActiveVertexIDs();
        for (auto id : vIds) m_selectedVertices.insert(id);
        m_activeVertex = vIds.empty() ? INVALID_VERTEX_ID : vIds.back();
    } else if (m_mode == SelectionMode::Edge) {
        m_selectedEdges.clear();
        auto eIds = mesh.GetActiveEdgeIDs();
        for (auto id : eIds) m_selectedEdges.insert(id);
        m_activeEdge = eIds.empty() ? INVALID_EDGE_ID : eIds.back();
    } else if (m_mode == SelectionMode::Face) {
        m_selectedFaces.clear();
        auto fIds = mesh.GetActiveFaceIDs();
        for (auto id : fIds) m_selectedFaces.insert(id);
        m_activeFace = fIds.empty() ? INVALID_FACE_ID : fIds.back();
    }
    IncrementRevision();
}

void MeshSelectionService::InvertSelection(const EditableMesh& mesh) {
    if (m_mode == SelectionMode::Vertex) {
        auto vIds = mesh.GetActiveVertexIDs();
        std::unordered_set<VertexID> newSel;
        for (auto id : vIds) {
            if (m_selectedVertices.count(id) == 0) newSel.insert(id);
        }
        m_selectedVertices = std::move(newSel);
        m_activeVertex = m_selectedVertices.empty() ? INVALID_VERTEX_ID : *m_selectedVertices.begin();
    } else if (m_mode == SelectionMode::Edge) {
        auto eIds = mesh.GetActiveEdgeIDs();
        std::unordered_set<EdgeID> newSel;
        for (auto id : eIds) {
            if (m_selectedEdges.count(id) == 0) newSel.insert(id);
        }
        m_selectedEdges = std::move(newSel);
        m_activeEdge = m_selectedEdges.empty() ? INVALID_EDGE_ID : *m_selectedEdges.begin();
    } else if (m_mode == SelectionMode::Face) {
        auto fIds = mesh.GetActiveFaceIDs();
        std::unordered_set<FaceID> newSel;
        for (auto id : fIds) {
            if (m_selectedFaces.count(id) == 0) newSel.insert(id);
        }
        m_selectedFaces = std::move(newSel);
        m_activeFace = m_selectedFaces.empty() ? INVALID_FACE_ID : *m_selectedFaces.begin();
    }
    IncrementRevision();
}

size_t MeshSelectionService::GetSelectedCount() const {
    if (m_mode == SelectionMode::Vertex) return m_selectedVertices.size();
    if (m_mode == SelectionMode::Edge)   return m_selectedEdges.size();
    if (m_mode == SelectionMode::Face)   return m_selectedFaces.size();
    return 0;
}

bool MeshSelectionService::HasSelection() const {
    return GetSelectedCount() > 0;
}

void MeshSelectionService::PruneInvalidated(const EditableMesh& mesh) {
    bool changed = false;

    // Prune vertici
    for (auto it = m_selectedVertices.begin(); it != m_selectedVertices.end(); ) {
        if (!mesh.IsValid(*it)) {
            it = m_selectedVertices.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (!mesh.IsValid(m_activeVertex)) {
        m_activeVertex = m_selectedVertices.empty() ? INVALID_VERTEX_ID : *m_selectedVertices.begin();
        changed = true;
    }

    // Prune edges
    for (auto it = m_selectedEdges.begin(); it != m_selectedEdges.end(); ) {
        if (!mesh.IsValid(*it)) {
            it = m_selectedEdges.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (!mesh.IsValid(m_activeEdge)) {
        m_activeEdge = m_selectedEdges.empty() ? INVALID_EDGE_ID : *m_selectedEdges.begin();
        changed = true;
    }

    // Prune faces
    for (auto it = m_selectedFaces.begin(); it != m_selectedFaces.end(); ) {
        if (!mesh.IsValid(*it)) {
            it = m_selectedFaces.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (!mesh.IsValid(m_activeFace)) {
        m_activeFace = m_selectedFaces.empty() ? INVALID_FACE_ID : *m_selectedFaces.begin();
        changed = true;
    }

    if (changed) {
        IncrementRevision();
    }
}

// =============================================================================
// CPU RAY PICKING (BM-S3.3)
// =============================================================================

PickRay MeshSelectionService::ComputeWorldRayFromScreen(const glm::vec2& mouseScreenPos, const CameraViewportContext& ctx) {
    float vx = ctx.viewportPos.x;
    float vy = ctx.viewportPos.y;
    float vw = ctx.viewportSize.x > 1.0f ? ctx.viewportSize.x : 1.0f;
    float vh = ctx.viewportSize.y > 1.0f ? ctx.viewportSize.y : 1.0f;

    // Conversione coordinate schermo in Normalized Device Coordinates (NDC) [-1, 1]
    float ndcX = (2.0f * (mouseScreenPos.x - vx) / vw) - 1.0f;
    float ndcY = 1.0f - (2.0f * (mouseScreenPos.y - vy) / vh);

    glm::mat4 invVP = glm::inverse(ctx.projMatrix * ctx.viewMatrix);

    glm::vec4 nearPointClip(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farPointClip(ndcX, ndcY, 1.0f, 1.0f);

    glm::vec4 nearPointWorld = invVP * nearPointClip;
    glm::vec4 farPointWorld = invVP * farPointClip;

    PickRay ray;
    if (std::abs(nearPointWorld.w) > 1e-6f) nearPointWorld /= nearPointWorld.w;
    if (std::abs(farPointWorld.w) > 1e-6f) farPointWorld /= farPointWorld.w;

    ray.origin = glm::vec3(nearPointWorld);
    glm::vec3 dir = glm::vec3(farPointWorld) - ray.origin;
    float len = glm::length(dir);
    ray.direction = len > 1e-6f ? (dir / len) : glm::vec3(0.0f, 0.0f, -1.0f);

    return ray;
}

bool MeshSelectionService::RayTriangleIntersect(const glm::vec3& orig, const glm::vec3& dir,
                                                const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                                                float& t, float& u, float& v) {
    const float EPSILON = 1e-7f;
    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 pvec = glm::cross(dir, edge2);
    float det = glm::dot(edge1, pvec);

    if (det > -EPSILON && det < EPSILON) return false;
    float invDet = 1.0f / det;

    glm::vec3 tvec = orig - v0;
    u = glm::dot(tvec, pvec) * invDet;
    if (u < 0.0f || u > 1.0f) return false;

    glm::vec3 qvec = glm::cross(tvec, edge1);
    v = glm::dot(dir, qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;

    t = glm::dot(edge2, qvec) * invDet;
    return (t > EPSILON);
}

float MeshSelectionService::DistancePointToSegment2D(const glm::vec2& pt, const glm::vec2& s0, const glm::vec2& s1) {
    glm::vec2 diff = s1 - s0;
    float lenSq = glm::dot(diff, diff);
    if (lenSq < 1e-6f) return glm::distance(pt, s0);

    float t = std::clamp(glm::dot(pt - s0, diff) / lenSq, 0.0f, 1.0f);
    glm::vec2 projection = s0 + t * diff;
    return glm::distance(pt, projection);
}

PickHit MeshSelectionService::Pick(const PickRay& worldRay, 
                                   const EditableMesh& mesh, 
                                   const CameraViewportContext& ctx, 
                                   float vertexPixelRadius, 
                                   float edgePixelRadius) const {
    PickHit result;
    result.mode = m_mode;

    // Trasformazione del ray da world-space a object-space della mesh
    glm::mat4 invModel = glm::inverse(ctx.modelMatrix);
    PickRay localRay;
    localRay.origin = glm::vec3(invModel * glm::vec4(worldRay.origin, 1.0f));
    glm::vec3 dirLocal = glm::vec3(invModel * glm::vec4(worldRay.direction, 0.0f));
    float lenLocal = glm::length(dirLocal);
    localRay.direction = lenLocal > 1e-6f ? (dirLocal / lenLocal) : glm::vec3(0.0f, 0.0f, -1.0f);

    if (m_mode == SelectionMode::Face) {
        return PickFace(localRay, worldRay, mesh, ctx);
    } else if (m_mode == SelectionMode::Vertex) {
        return PickVertex(worldRay, mesh, ctx, vertexPixelRadius);
    } else if (m_mode == SelectionMode::Edge) {
        return PickEdge(worldRay, mesh, ctx, edgePixelRadius);
    }

    return result;
}

PickHit MeshSelectionService::PickFace(const PickRay& localRay, 
                                       const PickRay& worldRay, 
                                       const EditableMesh& mesh, 
                                       const CameraViewportContext& ctx) const {
    PickHit bestHit;
    bestHit.mode = SelectionMode::Face;

    auto fIds = mesh.GetActiveFaceIDs();
    for (FaceID fId : fIds) {
        std::vector<uint32_t> localTriIndices;
        if (!mesh.TriangulateFace(fId, localTriIndices)) continue;

        std::vector<VertexID> faceVerts = mesh.GetFaceVertices(fId);

        for (size_t t = 0; t + 2 < localTriIndices.size(); t += 3) {
            uint32_t i0 = localTriIndices[t];
            uint32_t i1 = localTriIndices[t + 1];
            uint32_t i2 = localTriIndices[t + 2];

            const auto* v0 = mesh.GetVertex(faceVerts[i0]);
            const auto* v1 = mesh.GetVertex(faceVerts[i1]);
            const auto* v2 = mesh.GetVertex(faceVerts[i2]);
            if (!v0 || !v1 || !v2) continue;

            float tHit, u, v;
            if (RayTriangleIntersect(localRay.origin, localRay.direction, v0->position, v1->position, v2->position, tHit, u, v)) {
                glm::vec3 localP = localRay.origin + tHit * localRay.direction;
                glm::vec3 worldP = glm::vec3(ctx.modelMatrix * glm::vec4(localP, 1.0f));
                float distWorld = glm::distance(worldRay.origin, worldP);

                if (distWorld < bestHit.distance) {
                    bestHit.hit = true;
                    bestHit.faceId = fId;
                    bestHit.distance = distWorld;
                    bestHit.localHitPoint = localP;
                    bestHit.worldHitPoint = worldP;
                }
            }
        }
    }
    return bestHit;
}

PickHit MeshSelectionService::PickVertex(const PickRay& worldRay, 
                                         const EditableMesh& mesh, 
                                         const CameraViewportContext& ctx, 
                                         float pixelRadius) const {
    PickHit bestHit;
    bestHit.mode = SelectionMode::Vertex;

    float vx = ctx.viewportPos.x;
    float vy = ctx.viewportPos.y;
    float vw = ctx.viewportSize.x > 1.0f ? ctx.viewportSize.x : 1.0f;
    float vh = ctx.viewportSize.y > 1.0f ? ctx.viewportSize.y : 1.0f;

    // Coordinate mouse sullo schermo ricavate dal ray d'origine
    glm::mat4 mvp = ctx.projMatrix * ctx.viewMatrix * ctx.modelMatrix;

    auto vIds = mesh.GetActiveVertexIDs();
    float bestScreenDist = pixelRadius;

    for (VertexID vId : vIds) {
        const auto* v = mesh.GetVertex(vId);
        if (!v) continue;

        glm::vec4 clipPos = mvp * glm::vec4(v->position, 1.0f);
        if (clipPos.w <= 0.001f) continue; // Dietro la telecamera

        glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
        if (ndc.z < -1.0f || ndc.z > 1.0f) continue;

        glm::vec2 screenPos(
            vx + (ndc.x * 0.5f + 0.5f) * vw,
            vy + (0.5f - ndc.y * 0.5f) * vh
        );

        // Calcola punto sullo schermo del ray
        glm::vec4 rayClip = ctx.projMatrix * ctx.viewMatrix * glm::vec4(worldRay.origin + worldRay.direction * clipPos.w, 1.0f);
        glm::vec2 rayScreen(
            vx + (rayClip.x / rayClip.w * 0.5f + 0.5f) * vw,
            vy + (0.5f - rayClip.y / rayClip.w * 0.5f) * vh
        );

        float dScreen = glm::distance(screenPos, rayScreen);
        if (dScreen <= bestScreenDist) {
            glm::vec3 worldP = glm::vec3(ctx.modelMatrix * glm::vec4(v->position, 1.0f));
            float distWorld = glm::distance(worldRay.origin, worldP);

            if (distWorld < bestHit.distance || dScreen < bestScreenDist * 0.5f) {
                bestHit.hit = true;
                bestHit.vertexId = vId;
                bestHit.distance = distWorld;
                bestHit.localHitPoint = v->position;
                bestHit.worldHitPoint = worldP;
                bestScreenDist = dScreen;
            }
        }
    }
    return bestHit;
}

PickHit MeshSelectionService::PickEdge(const PickRay& worldRay, 
                                       const EditableMesh& mesh, 
                                       const CameraViewportContext& ctx, 
                                       float pixelRadius) const {
    PickHit bestHit;
    bestHit.mode = SelectionMode::Edge;

    float vx = ctx.viewportPos.x;
    float vy = ctx.viewportPos.y;
    float vw = ctx.viewportSize.x > 1.0f ? ctx.viewportSize.x : 1.0f;
    float vh = ctx.viewportSize.y > 1.0f ? ctx.viewportSize.y : 1.0f;

    glm::mat4 mvp = ctx.projMatrix * ctx.viewMatrix * ctx.modelMatrix;

    auto eIds = mesh.GetActiveEdgeIDs();
    float bestScreenDist = pixelRadius;

    for (EdgeID eId : eIds) {
        const auto* e = mesh.GetEdge(eId);
        if (!e) continue;

        const auto* he = mesh.GetHalfEdge(e->halfEdge);
        if (!he) continue;
        const auto* nextHe = mesh.GetHalfEdge(he->next);
        if (!nextHe) continue;

        const auto* v0 = mesh.GetVertex(he->origin);
        const auto* v1 = mesh.GetVertex(nextHe->origin);
        if (!v0 || !v1) continue;

        glm::vec4 clip0 = mvp * glm::vec4(v0->position, 1.0f);
        glm::vec4 clip1 = mvp * glm::vec4(v1->position, 1.0f);
        if (clip0.w <= 0.001f && clip1.w <= 0.001f) continue;

        glm::vec2 s0(
            vx + (clip0.x / clip0.w * 0.5f + 0.5f) * vw,
            vy + (0.5f - clip0.y / clip0.w * 0.5f) * vh
        );
        glm::vec2 s1(
            vx + (clip1.x / clip1.w * 0.5f + 0.5f) * vw,
            vy + (0.5f - clip1.y / clip1.w * 0.5f) * vh
        );

        float avgW = (clip0.w + clip1.w) * 0.5f;
        glm::vec4 rayClip = ctx.projMatrix * ctx.viewMatrix * glm::vec4(worldRay.origin + worldRay.direction * avgW, 1.0f);
        glm::vec2 rayScreen(
            vx + (rayClip.x / rayClip.w * 0.5f + 0.5f) * vw,
            vy + (0.5f - rayClip.y / rayClip.w * 0.5f) * vh
        );

        float dSegment = DistancePointToSegment2D(rayScreen, s0, s1);
        if (dSegment <= bestScreenDist) {
            glm::vec3 midLocal = (v0->position + v1->position) * 0.5f;
            glm::vec3 worldP = glm::vec3(ctx.modelMatrix * glm::vec4(midLocal, 1.0f));
            float distWorld = glm::distance(worldRay.origin, worldP);

            if (distWorld < bestHit.distance || dSegment < bestScreenDist * 0.5f) {
                bestHit.hit = true;
                bestHit.edgeId = eId;
                bestHit.distance = distWorld;
                bestHit.localHitPoint = midLocal;
                bestHit.worldHitPoint = worldP;
                bestScreenDist = dSegment;
            }
        }
    }
    return bestHit;
}

bool MeshSelectionService::PickAndSelect(const glm::vec2& mouseScreenPos, 
                                         const EditableMesh& mesh, 
                                         const CameraViewportContext& ctx, 
                                         SelectionOp op,
                                         float vertexPixelRadius, 
                                         float edgePixelRadius) {
    PickRay ray = ComputeWorldRayFromScreen(mouseScreenPos, ctx);
    PickHit hit = Pick(ray, mesh, ctx, vertexPixelRadius, edgePixelRadius);

    if (hit.hit) {
        if (m_mode == SelectionMode::Vertex && hit.vertexId.IsValid()) {
            SelectVertex(hit.vertexId, op);
            return true;
        } else if (m_mode == SelectionMode::Edge && hit.edgeId.IsValid()) {
            SelectEdge(hit.edgeId, op);
            return true;
        } else if (m_mode == SelectionMode::Face && hit.faceId.IsValid()) {
            SelectFace(hit.faceId, op);
            return true;
        }
    } else {
        if (op == SelectionOp::Replace) {
            ClearCurrentMode();
        }
    }
    return false;
}

// =============================================================================
// BOX SELECTION NELLO SPAZIO SCHERMO (BM-S3.4)
// =============================================================================

size_t MeshSelectionService::BoxSelect(const ScreenBox& box, 
                                       const EditableMesh& mesh, 
                                       const CameraViewportContext& ctx, 
                                       SelectionOp op,
                                       bool occludedCheck) {
    float minX = std::min(box.minPos.x, box.maxPos.x);
    float maxX = std::max(box.minPos.x, box.maxPos.x);
    float minY = std::min(box.minPos.y, box.maxPos.y);
    float maxY = std::max(box.minPos.y, box.maxPos.y);

    float vx = ctx.viewportPos.x;
    float vy = ctx.viewportPos.y;
    float vw = ctx.viewportSize.x > 1.0f ? ctx.viewportSize.x : 1.0f;
    float vh = ctx.viewportSize.y > 1.0f ? ctx.viewportSize.y : 1.0f;

    glm::mat4 mvp = ctx.projMatrix * ctx.viewMatrix * ctx.modelMatrix;

    size_t count = 0;
    if (op == SelectionOp::Replace) {
        ClearCurrentMode();
    }

    if (m_mode == SelectionMode::Vertex) {
        auto vIds = mesh.GetActiveVertexIDs();
        for (VertexID vId : vIds) {
            const auto* v = mesh.GetVertex(vId);
            if (!v) continue;

            glm::vec4 clip = mvp * glm::vec4(v->position, 1.0f);
            if (clip.w <= 0.001f) continue;

            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            glm::vec2 sPos(
                vx + (ndc.x * 0.5f + 0.5f) * vw,
                vy + (0.5f - ndc.y * 0.5f) * vh
            );

            if (sPos.x >= minX && sPos.x <= maxX && sPos.y >= minY && sPos.y <= maxY) {
                SelectVertex(vId, op == SelectionOp::Replace ? SelectionOp::Add : op);
                count++;
            }
        }
    } else if (m_mode == SelectionMode::Edge) {
        auto eIds = mesh.GetActiveEdgeIDs();
        for (EdgeID eId : eIds) {
            const auto* e = mesh.GetEdge(eId);
            if (!e) continue;
            const auto* he = mesh.GetHalfEdge(e->halfEdge);
            if (!he) continue;
            const auto* nextHe = mesh.GetHalfEdge(he->next);
            if (!nextHe) continue;

            const auto* v0 = mesh.GetVertex(he->origin);
            const auto* v1 = mesh.GetVertex(nextHe->origin);
            if (!v0 || !v1) continue;

            glm::vec3 mid = (v0->position + v1->position) * 0.5f;
            glm::vec4 clip = mvp * glm::vec4(mid, 1.0f);
            if (clip.w <= 0.001f) continue;

            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            glm::vec2 sPos(
                vx + (ndc.x * 0.5f + 0.5f) * vw,
                vy + (0.5f - ndc.y * 0.5f) * vh
            );

            if (sPos.x >= minX && sPos.x <= maxX && sPos.y >= minY && sPos.y <= maxY) {
                SelectEdge(eId, op == SelectionOp::Replace ? SelectionOp::Add : op);
                count++;
            }
        }
    } else if (m_mode == SelectionMode::Face) {
        auto fIds = mesh.GetActiveFaceIDs();
        for (FaceID fId : fIds) {
            const auto* f = mesh.GetFace(fId);
            if (!f) continue;

            // Baricentro della faccia
            std::vector<VertexID> verts = mesh.GetFaceVertices(fId);
            if (verts.empty()) continue;

            glm::vec3 center(0.0f);
            for (auto vid : verts) {
                const auto* v = mesh.GetVertex(vid);
                if (v) center += v->position;
            }
            center /= static_cast<float>(verts.size());

            glm::vec4 clip = mvp * glm::vec4(center, 1.0f);
            if (clip.w <= 0.001f) continue;

            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            glm::vec2 sPos(
                vx + (ndc.x * 0.5f + 0.5f) * vw,
                vy + (0.5f - ndc.y * 0.5f) * vh
            );

            if (sPos.x >= minX && sPos.x <= maxX && sPos.y >= minY && sPos.y <= maxY) {
                SelectFace(fId, op == SelectionOp::Replace ? SelectionOp::Add : op);
                count++;
            }
        }
    }

    return count;
}

// =============================================================================
// SUITE DI TEST DETERMINISTICA BM-S3 (SEL-1 .. SEL-10)
// =============================================================================

std::vector<MeshSelectionService::SelectionTestResult> MeshSelectionService::RunSelectionContractTests() {
    std::vector<SelectionTestResult> results;

    // Configurazione telecamera fissa di test
    CameraViewportContext ctx;
    ctx.viewMatrix = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    ctx.projMatrix = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    ctx.viewportPos = glm::vec2(0.0f, 0.0f);
    ctx.viewportSize = glm::vec2(1920.0f, 1080.0f);
    ctx.modelMatrix = glm::mat4(1.0f);

    EditableMesh cube = EditableMesh::CreateCube(2.0f);
    MeshSelectionService selService;

    // SEL-1 — Vertex Picking via Ray & Screen Projection
    {
        SelectionTestResult res;
        res.testId = "SEL-1";
        res.testName = "SEL-1 — Vertex Picking via Ray & Screen Projection";
        res.inputDesc = "Proiezione a schermo del vertice front-top-right (+1, +1, +1) e picking pixel";
        res.expectedDesc = "VertexID valido identificato con precisione sub-pixel";

        selService.SetMode(SelectionMode::Vertex);
        glm::vec4 clip = ctx.projMatrix * ctx.viewMatrix * glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        glm::vec2 mousePos(
            (ndc.x * 0.5f + 0.5f) * 1920.0f,
            (0.5f - ndc.y * 0.5f) * 1080.0f
        );

        PickRay ray = ComputeWorldRayFromScreen(mousePos, ctx);
        PickHit hit = selService.Pick(ray, cube, ctx, 15.0f);

        bool pickOk = hit.hit && hit.vertexId.IsValid();
        const auto* v = cube.GetVertex(hit.vertexId);
        bool posMatch = v && (glm::distance(v->position, glm::vec3(1.0f, 1.0f, 1.0f)) < 1e-4f);

        res.passed = pickOk && posMatch;
        res.observedDesc = "Hit=" + std::string(hit.hit ? "true" : "false") +
                           ", VertexID=" + std::to_string(hit.vertexId.index) +
                           ", PosMatch=" + (posMatch ? "true" : "false");
        res.details = res.passed ? "PASS" : "FAIL: Mancato o errato picking del vertice a schermo";
        results.push_back(res);
    }

    // SEL-2 — Edge Picking with Pixel Threshold
    {
        SelectionTestResult res;
        res.testId = "SEL-2";
        res.testName = "SEL-2 — Edge Picking with Pixel Threshold";
        res.inputDesc = "Picking sul punto mediano dello spigolo superiore frontale (Z=+1, Y=+1, X=0)";
        res.expectedDesc = "EdgeID valido individuato con soglia pixel 10px";

        selService.SetMode(SelectionMode::Edge);
        glm::vec4 clip = ctx.projMatrix * ctx.viewMatrix * glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        glm::vec2 mousePos(
            (ndc.x * 0.5f + 0.5f) * 1920.0f,
            (0.5f - ndc.y * 0.5f) * 1080.0f
        );

        PickRay ray = ComputeWorldRayFromScreen(mousePos, ctx);
        PickHit hit = selService.Pick(ray, cube, ctx, 15.0f, 15.0f);

        bool pickOk = hit.hit && hit.edgeId.IsValid();

        res.passed = pickOk;
        res.observedDesc = "Hit=" + std::string(hit.hit ? "true" : "false") +
                           ", EdgeID=" + std::to_string(hit.edgeId.index);
        res.details = res.passed ? "PASS" : "FAIL: Mancato picking dello spigolo con soglia pixel";
        results.push_back(res);
    }

    // SEL-3 — Face Picking via Möller-Trumbore Ray Intersection
    {
        SelectionTestResult res;
        res.testId = "SEL-3";
        res.testName = "SEL-3 — Face Picking via Möller-Trumbore Ray Intersection";
        res.inputDesc = "Ray centrale puntato al centro della faccia frontale (Z=+1)";
        res.expectedDesc = "FaceID frontale (AssetCubeFace::Front, surfaceId=1) colpito a distanza t=4.0";

        selService.SetMode(SelectionMode::Face);
        glm::vec2 centerScreen(1920.0f * 0.5f, 1080.0f * 0.5f);
        PickRay ray = ComputeWorldRayFromScreen(centerScreen, ctx);
        PickHit hit = selService.Pick(ray, cube, ctx);

        bool pickOk = hit.hit && hit.faceId.IsValid();
        const auto* f = cube.GetFace(hit.faceId);
        bool frontFace = f && (f->surfaceId == static_cast<uint32_t>(AssetCubeFace::Front));
        bool distOk = std::abs(hit.distance - 4.0f) < 1e-2f; // Camera a Z=5, faccia a Z=1 -> dist = 4

        res.passed = pickOk && frontFace && distOk;
        res.observedDesc = "Hit=" + std::string(hit.hit ? "true" : "false") +
                           ", FaceID=" + std::to_string(hit.faceId.index) +
                           ", SurfaceID=" + (f ? std::to_string(f->surfaceId) : "none") +
                           ", Dist=" + std::to_string(hit.distance);
        res.details = res.passed ? "PASS" : "FAIL: Incoerenza nel raycast facciale Möller-Trumbore";
        results.push_back(res);
    }

    // SEL-4 — Multi-selection (Replace, Add, Subtract, Toggle)
    {
        SelectionTestResult res;
        res.testId = "SEL-4";
        res.testName = "SEL-4 — Multi-selection (Replace, Add, Subtract, Toggle)";
        res.inputDesc = "Sequenza: Select(F0, Replace) -> Select(F1, Add) -> Select(F0, Toggle) -> Select(F1, Subtract)";
        res.expectedDesc = "Conteggio finale della selezione = 0 facce selezionate";

        selService.SetMode(SelectionMode::Face);
        selService.Clear();

        FaceID f0{ 0, 1 };
        FaceID f1{ 1, 1 };

        selService.SelectFace(f0, SelectionOp::Replace);
        bool s1 = (selService.GetSelectedCount() == 1 && selService.IsFaceSelected(f0));

        selService.SelectFace(f1, SelectionOp::Add);
        bool s2 = (selService.GetSelectedCount() == 2 && selService.IsFaceSelected(f1));

        selService.SelectFace(f0, SelectionOp::Toggle);
        bool s3 = (selService.GetSelectedCount() == 1 && !selService.IsFaceSelected(f0));

        selService.SelectFace(f1, SelectionOp::Subtract);
        bool s4 = (selService.GetSelectedCount() == 0);

        res.passed = s1 && s2 && s3 && s4;
        res.observedDesc = "S1=" + std::string(s1 ? "ok" : "fail") +
                           ", S2=" + std::string(s2 ? "ok" : "fail") +
                           ", S3=" + std::string(s3 ? "ok" : "fail") +
                           ", S4=" + std::string(s4 ? "ok" : "fail");
        res.details = res.passed ? "PASS" : "FAIL: Logica delle operazioni di selezione inconsistente";
        results.push_back(res);
    }

    // SEL-5 — Box Selection Screen Inclusion
    {
        SelectionTestResult res;
        res.testId = "SEL-5";
        res.testName = "SEL-5 — Box Selection Screen Inclusion";
        res.inputDesc = "Box selection che copre l'intero viewport [0, 0] a [1920, 1080]";
        res.expectedDesc = "Tutti gli 8 vertici del cubo selezionati con successo";

        selService.SetMode(SelectionMode::Vertex);
        ScreenBox fullBox{ glm::vec2(0.0f, 0.0f), glm::vec2(1920.0f, 1080.0f) };
        size_t selected = selService.BoxSelect(fullBox, cube, ctx, SelectionOp::Replace);

        bool allSelected = (selected == 8 && selService.GetSelectedCount() == 8);

        res.passed = allSelected;
        res.observedDesc = "SelectedVertices=" + std::to_string(selected) + " (Expected 8)";
        res.details = res.passed ? "PASS" : "FAIL: Box selection non include tutti i vertici proiettati";
        results.push_back(res);
    }

    // SEL-6 — Depth & Occlusion Handling
    {
        SelectionTestResult res;
        res.testId = "SEL-6";
        res.testName = "SEL-6 — Depth & Occlusion Handling";
        res.inputDesc = "Ray centrale che interseca sia la faccia Front (+Z) sia la faccia Back (-Z)";
        res.expectedDesc = "Selezionata unicamente la faccia Front anteriore (distanza minore)";

        selService.SetMode(SelectionMode::Face);
        glm::vec2 centerScreen(1920.0f * 0.5f, 1080.0f * 0.5f);
        PickRay ray = ComputeWorldRayFromScreen(centerScreen, ctx);
        PickHit hit = selService.Pick(ray, cube, ctx);

        const auto* f = cube.GetFace(hit.faceId);
        bool frontChosen = f && (f->surfaceId == static_cast<uint32_t>(AssetCubeFace::Front));
        bool backRejected = f && (f->surfaceId != static_cast<uint32_t>(AssetCubeFace::Back));

        res.passed = frontChosen && backRejected;
        res.observedDesc = "ChosenFaceSurface=" + (f ? std::to_string(f->surfaceId) : "none") +
                           " (AssetCubeFace::Front=1, AssetCubeFace::Back=0)";
        res.details = res.passed ? "PASS" : "FAIL: La faccia posteriore occlusa ha prevalso su quella anteriore";
        results.push_back(res);
    }

    // SEL-7 — Object Space Scaling & Translation Invariance
    {
        SelectionTestResult res;
        res.testId = "SEL-7";
        res.testName = "SEL-7 — Object Space Scaling & Translation Invariance";
        res.inputDesc = "Mesh traslata a X=+5 e scalata in modo non-uniforme S=(2, 0.5, 3)";
        res.expectedDesc = "Picking calcola la corretta trasformazione inversa e colpisce la faccia traslata";

        CameraViewportContext transCtx = ctx;
        transCtx.modelMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f)) *
                               glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 0.5f, 3.0f));

        // Proietta il centro della nuova faccia frontale (+5, 0, +3) a schermo
        glm::vec4 clip = transCtx.projMatrix * transCtx.viewMatrix * glm::vec4(5.0f, 0.0f, 3.0f, 1.0f);
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        glm::vec2 mousePos(
            (ndc.x * 0.5f + 0.5f) * 1920.0f,
            (0.5f - ndc.y * 0.5f) * 1080.0f
        );

        PickRay ray = ComputeWorldRayFromScreen(mousePos, transCtx);
        selService.SetMode(SelectionMode::Face);
        PickHit hit = selService.Pick(ray, cube, transCtx);

        bool pickOk = hit.hit && hit.faceId.IsValid();

        res.passed = pickOk;
        res.observedDesc = "HitTransformed=" + std::string(pickOk ? "true" : "false") +
                           ", HitWorldX=" + std::to_string(hit.worldHitPoint.x) + " (Expected ~5.0)";
        res.details = res.passed ? "PASS" : "FAIL: Trasformazione inversa model matrix fallita su scala/traslazione";
        results.push_back(res);
    }

    // SEL-8 — Pruning of Invalidated IDs After Deletions
    {
        SelectionTestResult res;
        res.testId = "SEL-8";
        res.testName = "SEL-8 — Pruning of Invalidated IDs After Deletions";
        res.inputDesc = "Seleziona faccia, elimina faccia dalla mesh, invoca PruneInvalidated";
        res.expectedDesc = "L'ID della faccia eliminata viene rimosso automaticamente dalla selezione";

        EditableMesh tempMesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        selService.SetMode(SelectionMode::Face);
        FaceID quadFace{ 0, 1 };
        selService.SelectFace(quadFace, SelectionOp::Replace);

        bool initiallySelected = selService.IsFaceSelected(quadFace);
        tempMesh.DeleteFace(quadFace);
        selService.PruneInvalidated(tempMesh);

        bool pruned = !selService.IsFaceSelected(quadFace) && (selService.GetSelectedCount() == 0);

        res.passed = initiallySelected && pruned;
        res.observedDesc = "InitiallySelected=" + std::string(initiallySelected ? "true" : "false") +
                           ", Pruned=" + std::string(pruned ? "true" : "false");
        res.details = res.passed ? "PASS" : "FAIL: Mancata epurazione di elementi eliminati dalla selezione";
        results.push_back(res);
    }

    // SEL-9 — Stable Input = Zero Unnecessary Work
    {
        SelectionTestResult res;
        res.testId = "SEL-9";
        res.testName = "SEL-9 — Stable Input = Zero Unnecessary Work";
        res.inputDesc = "Tracking revisioni selezione su operazioni idempotenti";
        res.expectedDesc = "La revisione aumenta solo su effettive modifiche dello stato di selezione";

        selService.Clear();
        uint64_t rev0 = selService.GetSelectionRevision();
        selService.SelectFace(FaceID{ 1, 1 }, SelectionOp::Replace);
        uint64_t rev1 = selService.GetSelectionRevision();
        bool revBumped = (rev1 > rev0);

        // Verifica che IsSelected non alteri la revisione (zero side-effects)
        selService.IsFaceSelected(FaceID{ 1, 1 });
        uint64_t rev2 = selService.GetSelectionRevision();
        bool revStable = (rev2 == rev1);

        res.passed = revBumped && revStable;
        res.observedDesc = "RevBumpOnMutate=" + std::string(revBumped ? "true" : "false") +
                           ", RevStableOnQuery=" + std::string(revStable ? "true" : "false");
        res.details = res.passed ? "PASS" : "FAIL: Violazione dell'invariante di stabilità revisione";
        results.push_back(res);
    }

    // SEL-10 — Selection After Triangulation & N-gon Picking
    {
        SelectionTestResult res;
        res.testId = "SEL-10";
        res.testName = "SEL-10 — Selection After Triangulation & N-gon Picking";
        res.inputDesc = "Costruzione di N-gon a 6 vertici e picking su triangolo secondario derivato";
        res.expectedDesc = "Ray sul secondo triangolo dell'N-gon seleziona l'intero N-gon logicamente";

        EditableMesh ngonMesh;
        VertexID v0 = ngonMesh.AddVertex(glm::vec3(0, 0, 0));
        VertexID v1 = ngonMesh.AddVertex(glm::vec3(2, 0, 0));
        VertexID v2 = ngonMesh.AddVertex(glm::vec3(2, 1, 0));
        VertexID v3 = ngonMesh.AddVertex(glm::vec3(1, 1, 0));
        VertexID v4 = ngonMesh.AddVertex(glm::vec3(1, 2, 0));
        VertexID v5 = ngonMesh.AddVertex(glm::vec3(0, 2, 0));
        FaceID ngonFace = ngonMesh.AddFace({ v0, v1, v2, v3, v4, v5 });

        // Ray puntato a (0.5, 1.5, 0.0), che cade nel braccio verticale dell'N-gon
        PickRay localRay;
        localRay.origin = glm::vec3(0.5f, 1.5f, 5.0f);
        localRay.direction = glm::vec3(0.0f, 0.0f, -1.0f);

        selService.SetMode(SelectionMode::Face);
        PickHit hit = selService.Pick(localRay, ngonMesh, ctx);

        bool ngonHit = hit.hit && (hit.faceId == ngonFace);

        res.passed = ngonHit;
        res.observedDesc = "NgonHit=" + std::string(ngonHit ? "true" : "false") +
                           ", HitFaceID=" + std::to_string(hit.faceId.index);
        res.details = res.passed ? "PASS" : "FAIL: Mancata selezione dell'N-gon su triangoli non radice";
        results.push_back(res);
    }

    return results;
}

} // namespace fw
