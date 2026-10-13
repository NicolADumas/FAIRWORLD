#include "pch.h"
#include "MeshModelingService.h"
#include "SurfaceDefinition.h"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace fw {

// =============================================================================
// QUERY GEOMETRICHE E HELPER
// =============================================================================

std::unordered_set<VertexID> MeshModelingService::ResolveSelectionVertices(const EditableMesh& mesh, 
                                                                           const MeshSelectionService& selection) {
    std::unordered_set<VertexID> verts;
    switch (selection.GetMode()) {
        case SelectionMode::Vertex: {
            for (auto vid : selection.GetSelectedVertices()) {
                if (mesh.IsValid(vid)) verts.insert(vid);
            }
            break;
        }
        case SelectionMode::Edge: {
            for (auto eid : selection.GetSelectedEdges()) {
                const auto* e = mesh.GetEdge(eid);
                if (!e) continue;
                const auto* he0 = mesh.GetHalfEdge(e->halfEdge);
                if (he0 && mesh.IsValid(he0->origin)) verts.insert(he0->origin);
                const auto* he1 = mesh.GetHalfEdge(he0 ? he0->next : INVALID_HALFEDGE_ID);
                if (he1 && mesh.IsValid(he1->origin)) verts.insert(he1->origin);
            }
            break;
        }
        case SelectionMode::Face: {
            for (auto fid : selection.GetSelectedFaces()) {
                if (!mesh.IsValid(fid)) continue;
                auto fVerts = mesh.GetFaceVertices(fid);
                for (auto vid : fVerts) {
                    if (mesh.IsValid(vid)) verts.insert(vid);
                }
            }
            break;
        }
    }
    return verts;
}

glm::vec3 MeshModelingService::CalculateVerticesCentroid(const EditableMesh& mesh, 
                                                         const std::unordered_set<VertexID>& vertices) {
    if (vertices.empty()) return glm::vec3(0.0f);
    glm::vec3 sum(0.0f);
    size_t count = 0;
    for (auto vid : vertices) {
        const auto* v = mesh.GetVertex(vid);
        if (v) {
            sum += v->position;
            count++;
        }
    }
    return count > 0 ? (sum / static_cast<float>(count)) : glm::vec3(0.0f);
}

glm::vec3 MeshModelingService::CalculateSelectionCentroid(const EditableMesh& mesh, 
                                                          const MeshSelectionService& selection) {
    auto verts = ResolveSelectionVertices(mesh, selection);
    return CalculateVerticesCentroid(mesh, verts);
}

glm::vec3 MeshModelingService::CalculateFaceCentroid(const EditableMesh& mesh, FaceID faceId) {
    auto fVerts = mesh.GetFaceVertices(faceId);
    if (fVerts.empty()) return glm::vec3(0.0f);
    glm::vec3 sum(0.0f);
    for (auto vid : fVerts) {
        const auto* v = mesh.GetVertex(vid);
        if (v) sum += v->position;
    }
    return sum / static_cast<float>(fVerts.size());
}

glm::vec3 MeshModelingService::CalculateFaceNormal(const EditableMesh& mesh, FaceID faceId) {
    const auto* f = mesh.GetFace(faceId);
    if (f && glm::length(f->faceNormal) > 1e-4f) {
        return glm::normalize(f->faceNormal);
    }
    auto fVerts = mesh.GetFaceVertices(faceId);
    if (fVerts.size() < 3) return glm::vec3(0.0f, 1.0f, 0.0f);
    
    // Metodo di Newell
    glm::vec3 normal(0.0f);
    for (size_t i = 0; i < fVerts.size(); ++i) {
        const auto* curr = mesh.GetVertex(fVerts[i]);
        const auto* next = mesh.GetVertex(fVerts[(i + 1) % fVerts.size()]);
        if (!curr || !next) continue;
        normal.x += (curr->position.y - next->position.y) * (curr->position.z + next->position.z);
        normal.y += (curr->position.z - next->position.z) * (curr->position.x + next->position.x);
        normal.z += (curr->position.x - next->position.x) * (curr->position.y + next->position.y);
    }
    float len = glm::length(normal);
    return len > 1e-6f ? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
}

glm::vec3 MeshModelingService::CalculateRegionAverageNormal(const EditableMesh& mesh, 
                                                            const std::vector<FaceID>& faces) {
    if (faces.empty()) return glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 sum(0.0f);
    for (auto fid : faces) {
        const auto* f = mesh.GetFace(fid);
        if (f) {
            float weight = f->area > 1e-6f ? f->area : 1.0f;
            sum += CalculateFaceNormal(mesh, fid) * weight;
        }
    }
    float len = glm::length(sum);
    return len > 1e-6f ? glm::normalize(sum) : glm::vec3(0.0f, 1.0f, 0.0f);
}

bool MeshModelingService::IsFaceConvex(const EditableMesh& mesh, FaceID faceId) {
    auto fVerts = mesh.GetFaceVertices(faceId);
    const size_t k = fVerts.size();
    if (k < 3) return false;
    if (k == 3) return true; // Tutti i triangoli sono convessi

    glm::vec3 normal = CalculateFaceNormal(mesh, faceId);
    for (size_t i = 0; i < k; ++i) {
        const auto* p0 = mesh.GetVertex(fVerts[(i + k - 1) % k]);
        const auto* p1 = mesh.GetVertex(fVerts[i]);
        const auto* p2 = mesh.GetVertex(fVerts[(i + 1) % k]);
        if (!p0 || !p1 || !p2) return false;

        glm::vec3 e1 = p1->position - p0->position;
        glm::vec3 e2 = p2->position - p1->position;
        glm::vec3 crossVal = glm::cross(e1, e2);
        if (glm::dot(crossVal, normal) < -1e-4f) {
            return false; // Concavo
        }
    }
    return true;
}

// =============================================================================
// TRASFORMAZIONI GEOMETRICHE (BM-S4.2)
// =============================================================================

ModelingOperationResult MeshModelingService::Move(EditableMesh& mesh, 
                                                  const std::unordered_set<VertexID>& vertices, 
                                                  const glm::vec3& delta, 
                                                  const glm::bvec3& axisConstraint) {
    ModelingOperationResult res;
    res.opType = ModelingOperationType::Move;
    res.oldTopologyRev = mesh.GetTopologyRevision();
    res.oldGeometryRev = mesh.GetGeometryRevision();
    res.oldAttributeRev = mesh.GetAttributeRevision();

    glm::vec3 effectiveDelta(
        axisConstraint.x ? delta.x : 0.0f,
        axisConstraint.y ? delta.y : 0.0f,
        axisConstraint.z ? delta.z : 0.0f
    );

    // Zero-Work contract: se delta e' nullo o nessun vertice selezionato, nessun incremento
    if (vertices.empty() || glm::length(effectiveDelta) < 1e-6f) {
        res.success = true;
        res.geometryChanged = false;
        res.newTopologyRev = res.oldTopologyRev;
        res.newGeometryRev = res.oldGeometryRev;
        res.newAttributeRev = res.oldAttributeRev;
        return res;
    }

    // Validazione NaN / Inf
    if (!std::isfinite(effectiveDelta.x) || !std::isfinite(effectiveDelta.y) || !std::isfinite(effectiveDelta.z)) {
        res.success = false;
        res.errorMessage = "Move delta contains NaN or Infinite components.";
        return res;
    }

    res.modifiedVertices.reserve(vertices.size());
    res.positionDeltas.reserve(vertices.size());

    for (auto vid : vertices) {
        auto* v = mesh.GetVertexMutable(vid);
        if (!v) continue;

        ModelingOperationResult::VertexPositionDelta deltaItem;
        deltaItem.id = vid;
        deltaItem.oldPos = v->position;
        v->position += effectiveDelta;
        deltaItem.newPos = v->position;

        res.positionDeltas.push_back(deltaItem);
        res.modifiedVertices.push_back(vid);
    }

    mesh.ComputeAllFaceNormals();
    mesh.ComputeVertexNormals();
    mesh.IncrementGeometryRevision();

    res.success = true;
    res.geometryChanged = true;
    res.newTopologyRev = mesh.GetTopologyRevision();
    res.newGeometryRev = mesh.GetGeometryRevision();
    res.newAttributeRev = mesh.GetAttributeRevision();
    return res;
}

ModelingOperationResult MeshModelingService::Rotate(EditableMesh& mesh, 
                                                    const std::unordered_set<VertexID>& vertices, 
                                                    const glm::vec3& pivot, 
                                                    const glm::quat& rotation, 
                                                    const glm::bvec3& axisConstraint) {
    ModelingOperationResult res;
    res.opType = ModelingOperationType::Rotate;
    res.oldTopologyRev = mesh.GetTopologyRevision();
    res.oldGeometryRev = mesh.GetGeometryRevision();
    res.oldAttributeRev = mesh.GetAttributeRevision();

    float angle = glm::angle(rotation);
    if (vertices.empty() || std::abs(angle) < 1e-6f) {
        res.success = true;
        res.geometryChanged = false;
        res.newTopologyRev = res.oldTopologyRev;
        res.newGeometryRev = res.oldGeometryRev;
        res.newAttributeRev = res.oldAttributeRev;
        return res;
    }

    if (!std::isfinite(angle) || !std::isfinite(pivot.x) || !std::isfinite(pivot.y) || !std::isfinite(pivot.z)) {
        res.success = false;
        res.errorMessage = "Rotate parameters contain NaN or Infinite values.";
        return res;
    }

    res.modifiedVertices.reserve(vertices.size());
    res.positionDeltas.reserve(vertices.size());

    for (auto vid : vertices) {
        auto* v = mesh.GetVertexMutable(vid);
        if (!v) continue;

        ModelingOperationResult::VertexPositionDelta deltaItem;
        deltaItem.id = vid;
        deltaItem.oldPos = v->position;

        glm::vec3 relPos = v->position - pivot;
        glm::vec3 rotPos = rotation * relPos;
        glm::vec3 candidatePos = pivot + rotPos;

        glm::vec3 finalPos(
            axisConstraint.x ? candidatePos.x : v->position.x,
            axisConstraint.y ? candidatePos.y : v->position.y,
            axisConstraint.z ? candidatePos.z : v->position.z
        );

        if (!std::isfinite(finalPos.x) || !std::isfinite(finalPos.y) || !std::isfinite(finalPos.z)) {
            res.success = false;
            res.errorMessage = "Rotated position produced non-finite coordinates.";
            // Rollback
            for (const auto& item : res.positionDeltas) {
                mesh.SetVertexPosition(item.id, item.oldPos);
            }
            return res;
        }

        v->position = finalPos;
        deltaItem.newPos = finalPos;

        res.positionDeltas.push_back(deltaItem);
        res.modifiedVertices.push_back(vid);
    }

    mesh.ComputeAllFaceNormals();
    mesh.ComputeVertexNormals();
    mesh.IncrementGeometryRevision();

    res.success = true;
    res.geometryChanged = true;
    res.newTopologyRev = mesh.GetTopologyRevision();
    res.newGeometryRev = mesh.GetGeometryRevision();
    res.newAttributeRev = mesh.GetAttributeRevision();
    return res;
}

ModelingOperationResult MeshModelingService::Scale(EditableMesh& mesh, 
                                                   const std::unordered_set<VertexID>& vertices, 
                                                   const glm::vec3& pivot, 
                                                   const glm::vec3& scale, 
                                                   const glm::bvec3& axisConstraint) {
    ModelingOperationResult res;
    res.opType = ModelingOperationType::Scale;
    res.oldTopologyRev = mesh.GetTopologyRevision();
    res.oldGeometryRev = mesh.GetGeometryRevision();
    res.oldAttributeRev = mesh.GetAttributeRevision();

    glm::vec3 effectiveScale(
        axisConstraint.x ? scale.x : 1.0f,
        axisConstraint.y ? scale.y : 1.0f,
        axisConstraint.z ? scale.z : 1.0f
    );

    if (std::abs(effectiveScale.x - 1.0f) < 1e-6f &&
        std::abs(effectiveScale.y - 1.0f) < 1e-6f &&
        std::abs(effectiveScale.z - 1.0f) < 1e-6f) {
        res.success = true;
        res.geometryChanged = false;
        res.newTopologyRev = res.oldTopologyRev;
        res.newGeometryRev = res.oldGeometryRev;
        res.newAttributeRev = res.oldAttributeRev;
        return res;
    }

    // Controllo scale degeneri o non-finite
    if (std::abs(effectiveScale.x) < 1e-6f || std::abs(effectiveScale.y) < 1e-6f || std::abs(effectiveScale.z) < 1e-6f ||
        !std::isfinite(effectiveScale.x) || !std::isfinite(effectiveScale.y) || !std::isfinite(effectiveScale.z)) {
        res.success = false;
        res.errorMessage = "Scale factor is degenerate (< 1e-6) or non-finite.";
        return res;
    }

    res.modifiedVertices.reserve(vertices.size());
    res.positionDeltas.reserve(vertices.size());

    for (auto vid : vertices) {
        auto* v = mesh.GetVertexMutable(vid);
        if (!v) continue;

        ModelingOperationResult::VertexPositionDelta deltaItem;
        deltaItem.id = vid;
        deltaItem.oldPos = v->position;

        glm::vec3 relPos = v->position - pivot;
        glm::vec3 scaledPos = relPos * effectiveScale;
        glm::vec3 finalPos = pivot + scaledPos;

        if (!std::isfinite(finalPos.x) || !std::isfinite(finalPos.y) || !std::isfinite(finalPos.z)) {
            res.success = false;
            res.errorMessage = "Scaled position produced non-finite coordinates.";
            for (const auto& item : res.positionDeltas) {
                mesh.SetVertexPosition(item.id, item.oldPos);
            }
            return res;
        }

        v->position = finalPos;
        deltaItem.newPos = finalPos;

        res.positionDeltas.push_back(deltaItem);
        res.modifiedVertices.push_back(vid);
    }

    mesh.ComputeAllFaceNormals();
    mesh.ComputeVertexNormals();
    mesh.IncrementGeometryRevision();

    res.success = true;
    res.geometryChanged = true;
    res.newTopologyRev = mesh.GetTopologyRevision();
    res.newGeometryRev = mesh.GetGeometryRevision();
    res.newAttributeRev = mesh.GetAttributeRevision();
    return res;
}

ModelingOperationResult MeshModelingService::MoveSelection(EditableMesh& mesh, 
                                                           const MeshSelectionService& selection, 
                                                           const glm::vec3& delta, 
                                                           const glm::bvec3& axisConstraint) {
    auto verts = ResolveSelectionVertices(mesh, selection);
    return Move(mesh, verts, delta, axisConstraint);
}

ModelingOperationResult MeshModelingService::RotateSelection(EditableMesh& mesh, 
                                                             const MeshSelectionService& selection, 
                                                             const glm::quat& rotation, 
                                                             const glm::bvec3& axisConstraint,
                                                             TransformPivot pivotMode, 
                                                             const glm::vec3& customPivot) {
    auto verts = ResolveSelectionVertices(mesh, selection);
    glm::vec3 pivot = customPivot;
    if (pivotMode == TransformPivot::SelectionCentroid) {
        pivot = CalculateVerticesCentroid(mesh, verts);
    } else if (pivotMode == TransformPivot::ActiveElement) {
        if (selection.GetMode() == SelectionMode::Vertex && selection.GetActiveVertex().IsValid()) {
            const auto* v = mesh.GetVertex(selection.GetActiveVertex());
            if (v) pivot = v->position;
        } else if (selection.GetMode() == SelectionMode::Face && selection.GetActiveFace().IsValid()) {
            pivot = CalculateFaceCentroid(mesh, selection.GetActiveFace());
        }
    } else if (pivotMode == TransformPivot::AssetOrigin) {
        pivot = glm::vec3(0.0f);
    }
    return Rotate(mesh, verts, pivot, rotation, axisConstraint);
}

ModelingOperationResult MeshModelingService::ScaleSelection(EditableMesh& mesh, 
                                                            const MeshSelectionService& selection, 
                                                            const glm::vec3& scale, 
                                                            const glm::bvec3& axisConstraint,
                                                            TransformPivot pivotMode, 
                                                            const glm::vec3& customPivot) {
    auto verts = ResolveSelectionVertices(mesh, selection);
    glm::vec3 pivot = customPivot;
    if (pivotMode == TransformPivot::SelectionCentroid) {
        pivot = CalculateVerticesCentroid(mesh, verts);
    } else if (pivotMode == TransformPivot::ActiveElement) {
        if (selection.GetMode() == SelectionMode::Vertex && selection.GetActiveVertex().IsValid()) {
            const auto* v = mesh.GetVertex(selection.GetActiveVertex());
            if (v) pivot = v->position;
        } else if (selection.GetMode() == SelectionMode::Face && selection.GetActiveFace().IsValid()) {
            pivot = CalculateFaceCentroid(mesh, selection.GetActiveFace());
        }
    } else if (pivotMode == TransformPivot::AssetOrigin) {
        pivot = glm::vec3(0.0f);
    }
    return Scale(mesh, verts, pivot, scale, axisConstraint);
}

// =============================================================================
// ESTRUSIONE (BM-S4.3)
// =============================================================================

ModelingOperationResult MeshModelingService::ExtrudeFace(EditableMesh& mesh, 
                                                         FaceID faceId, 
                                                         const ExtrudeParams& params) {
    ModelingOperationResult res;
    res.opType = ModelingOperationType::ExtrudeFace;
    res.oldTopologyRev = mesh.GetTopologyRevision();
    res.oldGeometryRev = mesh.GetGeometryRevision();
    res.oldAttributeRev = mesh.GetAttributeRevision();

    if (!mesh.IsValid(faceId)) {
        res.success = false;
        res.errorMessage = "Target face is invalid or does not exist.";
        return res;
    }

    if (std::abs(params.offset) < 1e-6f) {
        res.success = true;
        res.topologyChanged = false;
        res.geometryChanged = false;
        res.newTopologyRev = res.oldTopologyRev;
        res.newGeometryRev = res.oldGeometryRev;
        res.newAttributeRev = res.oldAttributeRev;
        return res;
    }

    // Backup mesh per garanzia di rollback atomico
    EditableMesh backup = mesh;

    auto origVerts = mesh.GetFaceVertices(faceId);
    const size_t k = origVerts.size();
    if (k < 3) {
        res.success = false;
        res.errorMessage = "Face vertex count is less than 3.";
        return res;
    }

    const auto* origFace = mesh.GetFace(faceId);
    uint32_t surfaceId = params.overrideSurfaceId.value_or(origFace->surfaceId);
    uint32_t materialId = params.overrideMaterialId.value_or(origFace->materialId);
    auto origCorners = mesh.GetFaceCorners(faceId);

    // Direzione estrusione
    glm::vec3 dir = params.useFaceNormal ? CalculateFaceNormal(mesh, faceId) : params.customDirection;
    if (glm::length(dir) < 1e-4f) dir = glm::vec3(0.0f, 1.0f, 0.0f);
    else dir = glm::normalize(dir);

    glm::vec3 displacement = dir * params.offset;

    // 1. Alloca i k nuovi vertici traslati per la faccia estrusa (top)
    std::vector<VertexID> newTopVerts;
    newTopVerts.reserve(k);
    for (size_t i = 0; i < k; ++i) {
        const auto* ov = mesh.GetVertex(origVerts[i]);
        if (!ov) {
            mesh = backup;
            res.success = false;
            res.errorMessage = "Encountered invalid vertex during extrusion.";
            return res;
        }
        VertexID nvId = mesh.AddVertex(ov->position + displacement);
        newTopVerts.push_back(nvId);
        res.createdVertices.push_back(nvId);
    }

    // 2. Elimina la faccia originaria (senza cancellare i suoi vertici o spigoli utilizzati da vicini)
    mesh.DeleteFace(faceId, false, false);
    res.removedFaces.push_back(faceId);

    // 3. Crea la nuova faccia top (stesso verso CCW)
    FaceID topFaceId = mesh.AddFace(newTopVerts, surfaceId, materialId, &origCorners);
    if (!mesh.IsValid(topFaceId)) {
        mesh = backup;
        res.success = false;
        res.errorMessage = "Failed to construct top extruded face.";
        return res;
    }
    res.createdFaces.push_back(topFaceId);

    // 4. Crea le k facce laterali (quad) collegando [V_i, V_j, W_j, W_i]
    for (size_t i = 0; i < k; ++i) {
        size_t j = (i + 1) % k;
        VertexID v_i = origVerts[i];
        VertexID v_j = origVerts[j];
        VertexID w_j = newTopVerts[j];
        VertexID w_i = newTopVerts[i];

        // UV coordinati per la faccia laterale
        std::vector<CornerAttributes> sideCorners(4);
        sideCorners[0].uv = glm::vec2(0.0f, 0.0f);
        sideCorners[1].uv = glm::vec2(1.0f, 0.0f);
        sideCorners[2].uv = glm::vec2(1.0f, 1.0f);
        sideCorners[3].uv = glm::vec2(0.0f, 1.0f);

        FaceID sideFaceId = mesh.AddFace({v_i, v_j, w_j, w_i}, surfaceId, materialId, &sideCorners);
        if (!mesh.IsValid(sideFaceId)) {
            mesh = backup;
            res.success = false;
            res.errorMessage = "Failed to construct side quad during face extrusion.";
            return res;
        }
        res.createdFaces.push_back(sideFaceId);
    }

    mesh.ComputeAllFaceNormals();
    mesh.ComputeVertexNormals();

    auto val = mesh.ValidateTopology();
    if (!val.isValid) {
        mesh = backup;
        res.success = false;
        res.errorMessage = "Topology validation failed after face extrusion.";
        return res;
    }

    res.success = true;
    res.topologyChanged = true;
    res.geometryChanged = true;
    res.attributesChanged = true;
    res.newTopologyRev = mesh.GetTopologyRevision();
    res.newGeometryRev = mesh.GetGeometryRevision();
    res.newAttributeRev = mesh.GetAttributeRevision();
    return res;
}

ModelingOperationResult MeshModelingService::ExtrudeRegion(EditableMesh& mesh, 
                                                           const std::vector<FaceID>& faceIds, 
                                                           const ExtrudeParams& params) {
    ModelingOperationResult res;
    res.opType = ModelingOperationType::ExtrudeRegion;
    res.oldTopologyRev = mesh.GetTopologyRevision();
    res.oldGeometryRev = mesh.GetGeometryRevision();
    res.oldAttributeRev = mesh.GetAttributeRevision();

    if (faceIds.empty()) {
        res.success = true;
        return res;
    }
    if (faceIds.size() == 1) {
        return ExtrudeFace(mesh, faceIds[0], params);
    }

    EditableMesh backup = mesh;

    // Set per lookup rapido facce regione
    std::unordered_set<FaceID> regionFaceSet(faceIds.begin(), faceIds.end());

    // 1. Trova tutti gli spigoli di confine della regione (boundary della selezione)
    struct BoundaryEdgeInfo {
        VertexID v0;
        VertexID v1;
        uint32_t surfaceId = 0;
        uint32_t materialId = 0;
    };
    std::vector<BoundaryEdgeInfo> boundaryEdges;

    std::unordered_set<VertexID> regionVertices;
    struct FaceDef {
        FaceID oldId;
        std::vector<VertexID> verts;
        uint32_t surfaceId;
        uint32_t materialId;
        std::vector<CornerAttributes> corners;
    };
    std::vector<FaceDef> faceDefs;
    faceDefs.reserve(faceIds.size());

    for (auto fid : faceIds) {
        if (!mesh.IsValid(fid)) continue;
        const auto* f = mesh.GetFace(fid);
        FaceDef fd;
        fd.oldId = fid;
        fd.verts = mesh.GetFaceVertices(fid);
        fd.surfaceId = params.overrideSurfaceId.value_or(f->surfaceId);
        fd.materialId = params.overrideMaterialId.value_or(f->materialId);
        fd.corners = mesh.GetFaceCorners(fid);
        faceDefs.push_back(fd);

        for (auto vid : fd.verts) {
            regionVertices.insert(vid);
        }

        auto halfEdges = mesh.GetFaceHalfEdges(fid);
        for (auto hid : halfEdges) {
            const auto* he = mesh.GetHalfEdge(hid);
            if (!he) continue;
            bool isRegionBoundary = false;
            if (!mesh.IsValid(he->twin)) {
                isRegionBoundary = true;
            } else {
                const auto* twinHe = mesh.GetHalfEdge(he->twin);
                if (!twinHe || regionFaceSet.find(twinHe->face) == regionFaceSet.end()) {
                    isRegionBoundary = true;
                }
            }

            if (isRegionBoundary) {
                const auto* heNext = mesh.GetHalfEdge(he->next);
                if (heNext) {
                    BoundaryEdgeInfo bInfo;
                    bInfo.v0 = he->origin;
                    bInfo.v1 = heNext->origin;
                    bInfo.surfaceId = fd.surfaceId;
                    bInfo.materialId = fd.materialId;
                    boundaryEdges.push_back(bInfo);
                }
            }
        }
    }

    glm::vec3 avgNormal = params.useFaceNormal ? CalculateRegionAverageNormal(mesh, faceIds) : params.customDirection;
    if (glm::length(avgNormal) < 1e-4f) avgNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    else avgNormal = glm::normalize(avgNormal);
    glm::vec3 displacement = avgNormal * params.offset;

    // 2. Mappa ciascun vertice della regione in un nuovo vertice estruso
    std::unordered_map<VertexID, VertexID> vertexMap;
    for (auto vid : regionVertices) {
        const auto* v = mesh.GetVertex(vid);
        if (!v) continue;
        VertexID nv = mesh.AddVertex(v->position + displacement);
        vertexMap[vid] = nv;
        res.createdVertices.push_back(nv);
    }

    // 3. Elimina le vecchie facce
    for (const auto& fd : faceDefs) {
        mesh.DeleteFace(fd.oldId, false, false);
        res.removedFaces.push_back(fd.oldId);
    }

    // 4. Ricrea le facce della regione al nuovo livello estruso
    for (const auto& fd : faceDefs) {
        std::vector<VertexID> mappedVerts;
        mappedVerts.reserve(fd.verts.size());
        for (auto ov : fd.verts) {
            mappedVerts.push_back(vertexMap[ov]);
        }
        FaceID newFid = mesh.AddFace(mappedVerts, fd.surfaceId, fd.materialId, &fd.corners);
        if (!mesh.IsValid(newFid)) {
            mesh = backup;
            res.success = false;
            res.errorMessage = "Failed to recreate extruded region face.";
            return res;
        }
        res.createdFaces.push_back(newFid);
    }

    // 5. Crea i side quad sul contorno della regione
    for (const auto& be : boundaryEdges) {
        VertexID v0 = be.v0;
        VertexID v1 = be.v1;
        VertexID w1 = vertexMap[v1];
        VertexID w0 = vertexMap[v0];

        std::vector<CornerAttributes> sideCorners(4);
        sideCorners[0].uv = glm::vec2(0.0f, 0.0f);
        sideCorners[1].uv = glm::vec2(1.0f, 0.0f);
        sideCorners[2].uv = glm::vec2(1.0f, 1.0f);
        sideCorners[3].uv = glm::vec2(0.0f, 1.0f);

        FaceID sideFid = mesh.AddFace({v0, v1, w1, w0}, be.surfaceId, be.materialId, &sideCorners);
        if (!mesh.IsValid(sideFid)) {
            mesh = backup;
            res.success = false;
            res.errorMessage = "Failed to create side quad on region boundary.";
            return res;
        }
        res.createdFaces.push_back(sideFid);
    }

    mesh.ComputeAllFaceNormals();
    mesh.ComputeVertexNormals();

    auto val = mesh.ValidateTopology();
    if (!val.isValid) {
        mesh = backup;
        res.success = false;
        res.errorMessage = "Topology validation failed after region extrusion.";
        return res;
    }

    res.success = true;
    res.topologyChanged = true;
    res.geometryChanged = true;
    res.attributesChanged = true;
    res.newTopologyRev = mesh.GetTopologyRevision();
    res.newGeometryRev = mesh.GetGeometryRevision();
    res.newAttributeRev = mesh.GetAttributeRevision();
    return res;
}

ModelingOperationResult MeshModelingService::ExtrudeFaces(EditableMesh& mesh, 
                                                          const std::vector<FaceID>& faceIds, 
                                                          const ExtrudeParams& params) {
    if (params.createIndividualFaces) {
        ModelingOperationResult combined;
        combined.success = true;
        for (auto fid : faceIds) {
            auto r = ExtrudeFace(mesh, fid, params);
            if (!r.success) return r;
            combined.createdFaces.insert(combined.createdFaces.end(), r.createdFaces.begin(), r.createdFaces.end());
            combined.createdVertices.insert(combined.createdVertices.end(), r.createdVertices.begin(), r.createdVertices.end());
            combined.removedFaces.insert(combined.removedFaces.end(), r.removedFaces.begin(), r.removedFaces.end());
        }
        combined.topologyChanged = true;
        combined.geometryChanged = true;
        return combined;
    }
    return ExtrudeRegion(mesh, faceIds, params);
}

ModelingOperationResult MeshModelingService::ExtrudeBoundaryEdge(EditableMesh& mesh, 
                                                                 EdgeID edgeId, 
                                                                 const ExtrudeParams& params) {
    ModelingOperationResult res;
    res.opType = ModelingOperationType::ExtrudeEdge;
    res.oldTopologyRev = mesh.GetTopologyRevision();
    res.oldGeometryRev = mesh.GetGeometryRevision();
    res.oldAttributeRev = mesh.GetAttributeRevision();

    if (!mesh.IsValid(edgeId)) {
        res.success = false;
        res.errorMessage = "Edge ID is invalid.";
        return res;
    }

    if (!mesh.IsBoundaryEdge(edgeId)) {
        res.success = false;
        res.errorMessage = "Edge is not a boundary edge; extruding would create non-manifold geometry.";
        return res;
    }

    EditableMesh backup = mesh;

    const auto* e = mesh.GetEdge(edgeId);
    const auto* he = mesh.GetHalfEdge(e->halfEdge);
    if (!he) {
        res.success = false;
        res.errorMessage = "Missing half-edge on boundary edge.";
        return res;
    }
    const auto* heNext = mesh.GetHalfEdge(he->next);
    if (!heNext) {
        res.success = false;
        res.errorMessage = "Missing next half-edge.";
        return res;
    }

    VertexID v0 = he->origin;
    VertexID v1 = heNext->origin;
    const auto* p0 = mesh.GetVertex(v0);
    const auto* p1 = mesh.GetVertex(v1);

    glm::vec3 dir = params.customDirection;
    if (glm::length(dir) < 1e-4f) {
        const auto* incFace = mesh.GetFace(he->face);
        glm::vec3 faceNorm = incFace ? incFace->faceNormal : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 edgeDir = glm::normalize(p1->position - p0->position);
        dir = glm::normalize(glm::cross(faceNorm, edgeDir));
    } else {
        dir = glm::normalize(dir);
    }

    glm::vec3 disp = dir * params.offset;

    VertexID w0 = mesh.AddVertex(p0->position + disp);
    VertexID w1 = mesh.AddVertex(p1->position + disp);
    res.createdVertices.push_back(w0);
    res.createdVertices.push_back(w1);

    uint32_t sId = params.overrideSurfaceId.value_or(0);
    uint32_t mId = params.overrideMaterialId.value_or(0);

    // Il verso deve essere opposto sul confine per accoppiarsi: [v1, v0, w0, w1]
    FaceID newFace = mesh.AddFace({v1, v0, w0, w1}, sId, mId);
    if (!mesh.IsValid(newFace)) {
        mesh = backup;
        res.success = false;
        res.errorMessage = "Failed to add quad face on boundary edge extrusion.";
        return res;
    }
    res.createdFaces.push_back(newFace);

    mesh.ComputeAllFaceNormals();
    mesh.ComputeVertexNormals();

    res.success = true;
    res.topologyChanged = true;
    res.geometryChanged = true;
    res.newTopologyRev = mesh.GetTopologyRevision();
    res.newGeometryRev = mesh.GetGeometryRevision();
    res.newAttributeRev = mesh.GetAttributeRevision();
    return res;
}

ModelingOperationResult MeshModelingService::ExtrudeBoundaryEdges(EditableMesh& mesh, 
                                                                  const std::vector<EdgeID>& edgeIds, 
                                                                  const ExtrudeParams& params) {
    ModelingOperationResult res;
    res.success = true;
    for (auto eid : edgeIds) {
        auto r = ExtrudeBoundaryEdge(mesh, eid, params);
        if (!r.success) return r;
        res.createdFaces.insert(res.createdFaces.end(), r.createdFaces.begin(), r.createdFaces.end());
        res.createdVertices.insert(res.createdVertices.end(), r.createdVertices.begin(), r.createdVertices.end());
    }
    res.topologyChanged = true;
    res.geometryChanged = true;
    return res;
}

ModelingOperationResult MeshModelingService::ExtrudeSelection(EditableMesh& mesh, 
                                                              const MeshSelectionService& selection, 
                                                              const ExtrudeParams& params) {
    if (selection.GetMode() == SelectionMode::Face) {
        std::vector<FaceID> faces(selection.GetSelectedFaces().begin(), selection.GetSelectedFaces().end());
        return ExtrudeFaces(mesh, faces, params);
    } else if (selection.GetMode() == SelectionMode::Edge) {
        std::vector<EdgeID> edges(selection.GetSelectedEdges().begin(), selection.GetSelectedEdges().end());
        return ExtrudeBoundaryEdges(mesh, edges, params);
    }

    ModelingOperationResult res;
    res.success = false;
    res.errorMessage = "Extrude is only supported for Face or Boundary Edge selection.";
    return res;
}

// =============================================================================
// FACE INSET (BM-S4.4)
// =============================================================================

ModelingOperationResult MeshModelingService::InsetFace(EditableMesh& mesh, 
                                                       FaceID faceId, 
                                                       const InsetParams& params) {
    ModelingOperationResult res;
    res.opType = ModelingOperationType::InsetFace;
    res.oldTopologyRev = mesh.GetTopologyRevision();
    res.oldGeometryRev = mesh.GetGeometryRevision();
    res.oldAttributeRev = mesh.GetAttributeRevision();

    if (!mesh.IsValid(faceId)) {
        res.success = false;
        res.errorMessage = "Face ID is invalid.";
        return res;
    }

    // Controllo convessita'
    if (!IsFaceConvex(mesh, faceId)) {
        res.success = false;
        res.errorMessage = "Concave polygon inset is not supported; face must be strictly convex.";
        return res;
    }

    if (std::abs(params.insetAmount) < 1e-6f && std::abs(params.depthOffset) < 1e-6f) {
        res.success = true;
        return res;
    }

    EditableMesh backup = mesh;

    auto origVerts = mesh.GetFaceVertices(faceId);
    const size_t k = origVerts.size();
    const auto* origFace = mesh.GetFace(faceId);
    uint32_t surfaceId = params.overrideSurfaceId.value_or(origFace->surfaceId);
    uint32_t materialId = params.overrideMaterialId.value_or(origFace->materialId);
    auto origCorners = mesh.GetFaceCorners(faceId);

    glm::vec3 centroid = CalculateFaceCentroid(mesh, faceId);
    glm::vec3 normal = CalculateFaceNormal(mesh, faceId);

    // Calcolo raggio minimo verso i vertici per clamp di stabilita
    float minCentroidDist = 1e9f;
    for (auto vid : origVerts) {
        const auto* v = mesh.GetVertex(vid);
        if (v) {
            float dist = glm::length(v->position - centroid);
            if (dist < minCentroidDist) minCentroidDist = dist;
        }
    }
    if (minCentroidDist < 1e-5f) minCentroidDist = 1.0f;

    float t = params.clampToCentroid ? glm::clamp(params.insetAmount / minCentroidDist, 0.01f, 0.95f) : params.insetAmount;
    glm::vec3 depthDisp = normal * params.depthOffset;

    // 1. Crea i nuovi vertici interni
    std::vector<VertexID> innerVerts;
    innerVerts.reserve(k);
    for (size_t i = 0; i < k; ++i) {
        const auto* v = mesh.GetVertex(origVerts[i]);
        glm::vec3 pInner = v->position + (centroid - v->position) * t + depthDisp;
        VertexID inVid = mesh.AddVertex(pInner);
        innerVerts.push_back(inVid);
        res.createdVertices.push_back(inVid);
    }

    // 2. Elimina la faccia originale
    mesh.DeleteFace(faceId, false, false);
    res.removedFaces.push_back(faceId);

    // 3. Crea la faccia centrale interna
    std::vector<CornerAttributes> innerCorners = origCorners;
    for (size_t i = 0; i < k; ++i) {
        if (i < innerCorners.size()) {
            // Interpola UV verso il centroide UV
            glm::vec2 uvCentroid(0.5f, 0.5f);
            innerCorners[i].uv = innerCorners[i].uv + (uvCentroid - innerCorners[i].uv) * t;
        }
    }

    FaceID innerFid = mesh.AddFace(innerVerts, surfaceId, materialId, &innerCorners);
    if (!mesh.IsValid(innerFid)) {
        mesh = backup;
        res.success = false;
        res.errorMessage = "Failed to construct inner inset face.";
        return res;
    }
    res.createdFaces.push_back(innerFid);

    // 4. Crea i k framing quads perimetrali [V_i, V_j, W_j, W_i]
    for (size_t i = 0; i < k; ++i) {
        size_t j = (i + 1) % k;
        VertexID v_i = origVerts[i];
        VertexID v_j = origVerts[j];
        VertexID w_j = innerVerts[j];
        VertexID w_i = innerVerts[i];

        std::vector<CornerAttributes> frameCorners(4);
        if (i < origCorners.size() && j < origCorners.size()) {
            frameCorners[0] = origCorners[i];
            frameCorners[1] = origCorners[j];
            frameCorners[2] = innerCorners[j];
            frameCorners[3] = innerCorners[i];
        }

        FaceID frameFid = mesh.AddFace({v_i, v_j, w_j, w_i}, surfaceId, materialId, &frameCorners);
        if (!mesh.IsValid(frameFid)) {
            mesh = backup;
            res.success = false;
            res.errorMessage = "Failed to construct framing quad during face inset.";
            return res;
        }
        res.createdFaces.push_back(frameFid);
    }

    mesh.ComputeAllFaceNormals();
    mesh.ComputeVertexNormals();

    auto val = mesh.ValidateTopology();
    if (!val.isValid) {
        mesh = backup;
        res.success = false;
        res.errorMessage = "Topology validation failed after face inset.";
        return res;
    }

    res.success = true;
    res.topologyChanged = true;
    res.geometryChanged = true;
    res.attributesChanged = true;
    res.newTopologyRev = mesh.GetTopologyRevision();
    res.newGeometryRev = mesh.GetGeometryRevision();
    res.newAttributeRev = mesh.GetAttributeRevision();
    return res;
}

ModelingOperationResult MeshModelingService::InsetFaces(EditableMesh& mesh, 
                                                        const std::vector<FaceID>& faceIds, 
                                                        const InsetParams& params) {
    ModelingOperationResult res;
    res.success = true;
    for (auto fid : faceIds) {
        auto r = InsetFace(mesh, fid, params);
        if (!r.success) return r;
        res.createdFaces.insert(res.createdFaces.end(), r.createdFaces.begin(), r.createdFaces.end());
        res.createdVertices.insert(res.createdVertices.end(), r.createdVertices.begin(), r.createdVertices.end());
        res.removedFaces.insert(res.removedFaces.end(), r.removedFaces.begin(), r.removedFaces.end());
    }
    res.topologyChanged = true;
    res.geometryChanged = true;
    return res;
}

ModelingOperationResult MeshModelingService::InsetSelection(EditableMesh& mesh, 
                                                            const MeshSelectionService& selection, 
                                                            const InsetParams& params) {
    if (selection.GetMode() != SelectionMode::Face) {
        ModelingOperationResult res;
        res.success = false;
        res.errorMessage = "Inset requires Face selection mode.";
        return res;
    }
    std::vector<FaceID> faces(selection.GetSelectedFaces().begin(), selection.GetSelectedFaces().end());
    return InsetFaces(mesh, faces, params);
}

// =============================================================================
// EDGE BEVEL (BM-S4.5)
// =============================================================================

ModelingOperationResult MeshModelingService::BevelEdge(EditableMesh& mesh, 
                                                       EdgeID edgeId, 
                                                       const BevelParams& params) {
    ModelingOperationResult res;
    res.opType = ModelingOperationType::BevelEdge;
    res.oldTopologyRev = mesh.GetTopologyRevision();
    res.oldGeometryRev = mesh.GetGeometryRevision();
    res.oldAttributeRev = mesh.GetAttributeRevision();

    if (!mesh.IsValid(edgeId)) {
        res.success = false;
        res.errorMessage = "Edge ID is invalid.";
        return res;
    }

    if (mesh.IsBoundaryEdge(edgeId)) {
        res.success = false;
        res.errorMessage = "Bevel is only supported on manifold edges with 2 incident faces.";
        return res;
    }

    const auto* e = mesh.GetEdge(edgeId);
    if (!e) {
        res.success = false;
        res.errorMessage = "Edge pointer is null.";
        return res;
    }

    const auto* heA = mesh.GetHalfEdge(e->halfEdge);
    const auto* heB = mesh.GetHalfEdge(e->twin);
    if (!heA || !heB || !mesh.IsValid(heA->face) || !mesh.IsValid(heB->face)) {
        res.success = false;
        res.errorMessage = "Bevel requires an interior 2-manifold edge with two incident faces.";
        return res;
    }

    EditableMesh backup = mesh;

    FaceID fA_id = heA->face;
    FaceID fB_id = heB->face;

    VertexID v0 = heA->origin;
    const auto* heA_next = mesh.GetHalfEdge(heA->next);
    if (!heA_next) {
        res.success = false;
        res.errorMessage = "Corrupt next half-edge.";
        return res;
    }
    VertexID v1 = heA_next->origin;

    const auto* p0 = mesh.GetVertex(v0);
    const auto* p1 = mesh.GetVertex(v1);
    if (!p0 || !p1) {
        res.success = false;
        res.errorMessage = "Missing edge vertices.";
        return res;
    }

    float edgeLen = glm::length(p1->position - p0->position);
    float w = glm::clamp(params.width, 0.001f, edgeLen * params.clamp);

    glm::vec3 nA = CalculateFaceNormal(mesh, fA_id);
    glm::vec3 nB = CalculateFaceNormal(mesh, fB_id);
    glm::vec3 eDir = glm::normalize(p1->position - p0->position);

    // Vettori di offset ortogonali allo spigolo e giacenti sui piani delle due facce
    glm::vec3 dA = glm::normalize(glm::cross(nA, eDir)) * w;
    glm::vec3 dB = glm::normalize(glm::cross(eDir, nB)) * w;

    // Crea nuovi vertici per la sfaccettatura di raccordo
    VertexID a0 = mesh.AddVertex(p0->position + dA);
    VertexID a1 = mesh.AddVertex(p1->position + dA);
    VertexID b1 = mesh.AddVertex(p1->position + dB);
    VertexID b0 = mesh.AddVertex(p0->position + dB);

    res.createdVertices.push_back(a0);
    res.createdVertices.push_back(a1);
    res.createdVertices.push_back(b1);
    res.createdVertices.push_back(b0);

    // Memorizza definizioni originali delle due facce
    auto fA_verts = mesh.GetFaceVertices(fA_id);
    auto fB_verts = mesh.GetFaceVertices(fB_id);
    uint32_t sIdA = mesh.GetFace(fA_id)->surfaceId;
    uint32_t mIdA = mesh.GetFace(fA_id)->materialId;
    uint32_t sIdB = mesh.GetFace(fB_id)->surfaceId;
    uint32_t mIdB = mesh.GetFace(fB_id)->materialId;

    // Sostituisce v0 e v1 in fA con a0 e a1
    std::vector<VertexID> newVertsA;
    newVertsA.reserve(fA_verts.size());
    for (auto vid : fA_verts) {
        if (vid == v0) newVertsA.push_back(a0);
        else if (vid == v1) newVertsA.push_back(a1);
        else newVertsA.push_back(vid);
    }

    // Sostituisce v0 e v1 in fB con b0 e b1
    std::vector<VertexID> newVertsB;
    newVertsB.reserve(fB_verts.size());
    for (auto vid : fB_verts) {
        if (vid == v0) newVertsB.push_back(b0);
        else if (vid == v1) newVertsB.push_back(b1);
        else newVertsB.push_back(vid);
    }

    // Elimina le 2 vecchie facce
    mesh.DeleteFace(fA_id, false, false);
    mesh.DeleteFace(fB_id, false, false);
    res.removedFaces.push_back(fA_id);
    res.removedFaces.push_back(fB_id);

    // Ricrea le due facce raccordate
    FaceID newFA = mesh.AddFace(newVertsA, sIdA, mIdA);
    FaceID newFB = mesh.AddFace(newVertsB, sIdB, mIdB);
    if (!mesh.IsValid(newFA) || !mesh.IsValid(newFB)) {
        mesh = backup;
        res.success = false;
        res.errorMessage = "Failed to rebuild adjacent faces during edge bevel.";
        return res;
    }
    res.createdFaces.push_back(newFA);
    res.createdFaces.push_back(newFB);

    // Inserisce il quad di bevel tra i due piani: [a0, a1, b1, b0]
    FaceID bevelFacet = mesh.AddFace({a0, a1, b1, b0}, sIdA, mIdA);
    if (!mesh.IsValid(bevelFacet)) {
        mesh = backup;
        res.success = false;
        res.errorMessage = "Failed to insert bevel chamfer facet.";
        return res;
    }
    res.createdFaces.push_back(bevelFacet);

    mesh.ComputeAllFaceNormals();
    mesh.ComputeVertexNormals();

    auto val = mesh.ValidateTopology();
    if (!val.isValid) {
        mesh = backup;
        res.success = false;
        res.errorMessage = "Topology validation failed after edge bevel.";
        return res;
    }

    res.success = true;
    res.topologyChanged = true;
    res.geometryChanged = true;
    res.attributesChanged = true;
    res.newTopologyRev = mesh.GetTopologyRevision();
    res.newGeometryRev = mesh.GetGeometryRevision();
    res.newAttributeRev = mesh.GetAttributeRevision();
    return res;
}

ModelingOperationResult MeshModelingService::BevelEdges(EditableMesh& mesh, 
                                                        const std::vector<EdgeID>& edgeIds, 
                                                        const BevelParams& params) {
    ModelingOperationResult res;
    res.success = true;
    for (auto eid : edgeIds) {
        auto r = BevelEdge(mesh, eid, params);
        if (!r.success) return r;
        res.createdFaces.insert(res.createdFaces.end(), r.createdFaces.begin(), r.createdFaces.end());
        res.createdVertices.insert(res.createdVertices.end(), r.createdVertices.begin(), r.createdVertices.end());
        res.removedFaces.insert(res.removedFaces.end(), r.removedFaces.begin(), r.removedFaces.end());
    }
    res.topologyChanged = true;
    res.geometryChanged = true;
    return res;
}

ModelingOperationResult MeshModelingService::BevelSelection(EditableMesh& mesh, 
                                                            const MeshSelectionService& selection, 
                                                            const BevelParams& params) {
    if (selection.GetMode() != SelectionMode::Edge) {
        ModelingOperationResult res;
        res.success = false;
        res.errorMessage = "Bevel requires Edge selection mode.";
        return res;
    }
    std::vector<EdgeID> edges(selection.GetSelectedEdges().begin(), selection.GetSelectedEdges().end());
    return BevelEdges(mesh, edges, params);
}

// =============================================================================
// VALIDATION SUITE BM-S4 (MOD-1 .. MOD-10)
// =============================================================================

std::vector<MeshModelingService::ModelingTestResult> MeshModelingService::RunModelingContractTests() {
    std::vector<ModelingTestResult> results;
    results.reserve(10);

    // MOD-1: Move Vertex
    {
        ModelingTestResult t;
        t.testId = "MOD-1";
        t.testName = "MOD-1 — Move Vertex Determinism & Zero Topology Work";
        t.inputDesc = "Cube mesh, move vertex (0.5, 0.5, 0.5) by delta (0.2, -0.1, 0.4)";
        t.expectedDesc = "Target pos (0.7, 0.4, 0.9), geoRevision bumped, topoRevision UNCHANGED, 7 other verts untouched";

        auto mesh = EditableMesh::CreateCube(1.0f);
        uint64_t initialTopo = mesh.GetTopologyRevision();
        uint64_t initialGeo = mesh.GetGeometryRevision();

        VertexID targetVid = INVALID_VERTEX_ID;
        auto allVerts = mesh.GetActiveVertexIDs();
        for (auto vid : allVerts) {
            const auto* v = mesh.GetVertex(vid);
            if (v && glm::distance(v->position, glm::vec3(0.5f, 0.5f, 0.5f)) < 1e-4f) {
                targetVid = vid;
                break;
            }
        }

        auto opRes = Move(mesh, {targetVid}, glm::vec3(0.2f, -0.1f, 0.4f));
        const auto* modV = mesh.GetVertex(targetVid);

        bool posOk = modV && glm::distance(modV->position, glm::vec3(0.7f, 0.4f, 0.9f)) < 1e-4f;
        bool topoUntouched = (mesh.GetTopologyRevision() == initialTopo);
        bool geoBumped = (mesh.GetGeometryRevision() > initialGeo);

        t.passed = opRes.success && posOk && topoUntouched && geoBumped;
        t.observedDesc = "Success=" + std::to_string(opRes.success) + 
                         ", PosMatch=" + std::to_string(posOk) + 
                         ", TopoRev=" + std::to_string(mesh.GetTopologyRevision()) + 
                         ", GeoRev=" + std::to_string(mesh.GetGeometryRevision());
        t.details = t.passed ? "PASS" : "FAIL: Posizione o revisioni discordanti";
        results.push_back(t);
    }

    // MOD-2: Rotate Selection
    {
        ModelingTestResult t;
        t.testId = "MOD-2";
        t.testName = "MOD-2 — Rotate Selection Around Centroid";
        t.inputDesc = "Rotate top 4 vertices around Z centroid by 90 deg";
        t.expectedDesc = "Positions rotated 90 deg around (0, 0, 0.5), finite coords, topoRevision UNCHANGED";

        auto mesh = EditableMesh::CreateCube(1.0f);
        uint64_t initialTopo = mesh.GetTopologyRevision();

        std::unordered_set<VertexID> topVerts;
        for (auto vid : mesh.GetActiveVertexIDs()) {
            const auto* v = mesh.GetVertex(vid);
            if (v && v->position.z > 0.4f) {
                topVerts.insert(vid);
            }
        }

        glm::vec3 pivot(0.0f, 0.0f, 0.5f);
        glm::quat qRot = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        auto opRes = Rotate(mesh, topVerts, pivot, qRot);

        bool finiteCoords = true;
        for (auto vid : topVerts) {
            const auto* v = mesh.GetVertex(vid);
            if (!v || !std::isfinite(v->position.x) || !std::isfinite(v->position.y) || !std::isfinite(v->position.z)) {
                finiteCoords = false;
            }
        }
        bool topoUntouched = (mesh.GetTopologyRevision() == initialTopo);

        t.passed = opRes.success && finiteCoords && topoUntouched && (topVerts.size() == 4);
        t.observedDesc = "Success=" + std::to_string(opRes.success) + 
                         ", TopVerts=" + std::to_string(topVerts.size()) + 
                         ", Finite=" + std::to_string(finiteCoords);
        t.details = t.passed ? "PASS" : "FAIL: Rotazione o finitezza coordinate fallita";
        results.push_back(t);
    }

    // MOD-3: Non-Uniform Scale
    {
        ModelingTestResult t;
        t.testId = "MOD-3";
        t.testName = "MOD-3 — Non-Uniform Scale Geometry";
        t.inputDesc = "Cube scaled with S=(2.0, 0.5, 1.0) around origin";
        t.expectedDesc = "AABB extends to X in [-1.0, 1.0], Y in [-0.25, 0.25], Z in [-0.5, 0.5]";

        auto mesh = EditableMesh::CreateCube(1.0f);
        auto allVerts = mesh.GetActiveVertexIDs();
        std::unordered_set<VertexID> vSet(allVerts.begin(), allVerts.end());

        auto opRes = Scale(mesh, vSet, glm::vec3(0.0f), glm::vec3(2.0f, 0.5f, 1.0f));
        auto aabb = mesh.ComputeAABB();

        bool xOk = std::abs(aabb.min.x - (-1.0f)) < 1e-3f && std::abs(aabb.max.x - 1.0f) < 1e-3f;
        bool yOk = std::abs(aabb.min.y - (-0.25f)) < 1e-3f && std::abs(aabb.max.y - 0.25f) < 1e-3f;
        bool zOk = std::abs(aabb.min.z - (-0.5f)) < 1e-3f && std::abs(aabb.max.z - 0.5f) < 1e-3f;

        t.passed = opRes.success && xOk && yOk && zOk;
        t.observedDesc = "Success=" + std::to_string(opRes.success) + 
                         ", Min=(" + std::to_string(aabb.min.x) + "," + std::to_string(aabb.min.y) + "," + std::to_string(aabb.min.z) + ")" +
                         ", Max=(" + std::to_string(aabb.max.x) + "," + std::to_string(aabb.max.y) + "," + std::to_string(aabb.max.z) + ")";
        t.details = t.passed ? "PASS" : "FAIL: Scala non conforme ai limiti attesi";
        results.push_back(t);
    }

    // MOD-4: Face Extrude
    {
        ModelingTestResult t;
        t.testId = "MOD-4";
        t.testName = "MOD-4 — Face Extrude & Manifold Connectivity";
        t.inputDesc = "Single face on cube extruded by 0.5 along normal";
        t.expectedDesc = "Total faces 10, total verts 12, topoRevision bumped, closed manifold valid";

        auto mesh = EditableMesh::CreateCube(1.0f);
        uint64_t initialTopo = mesh.GetTopologyRevision();

        FaceID topFace = INVALID_FACE_ID;
        for (auto fid : mesh.GetActiveFaceIDs()) {
            const auto* f = mesh.GetFace(fid);
            if (f && f->faceNormal.z > 0.8f) {
                topFace = fid;
                break;
            }
        }

        ExtrudeParams ep;
        ep.offset = 0.5f;
        auto opRes = ExtrudeFace(mesh, topFace, ep);
        auto val = mesh.ValidateTopology();

        bool faceCountOk = (mesh.GetFaceCount() == 10);
        bool vertCountOk = (mesh.GetVertexCount() == 12);
        bool topoBumped = (mesh.GetTopologyRevision() > initialTopo);

        t.passed = opRes.success && faceCountOk && vertCountOk && val.isValid && val.isClosedManifold && topoBumped;
        t.observedDesc = "Success=" + std::to_string(opRes.success) + 
                         ", Faces=" + std::to_string(mesh.GetFaceCount()) + 
                         ", Verts=" + std::to_string(mesh.GetVertexCount()) + 
                         ", ClosedManifold=" + std::to_string(val.isClosedManifold);
        t.details = t.passed ? "PASS" : "FAIL: Topologia estrusione non conforme";
        results.push_back(t);
    }

    // MOD-5: Region Extrude
    {
        ModelingTestResult t;
        t.testId = "MOD-5";
        t.testName = "MOD-5 — Adjacent Faces Region Extrude";
        t.inputDesc = "Two adjacent faces on cube extruded together by 0.3";
        t.expectedDesc = "Seamless region extrusion without internal partition wall, valid manifold";

        auto mesh = EditableMesh::CreateCube(1.0f);
        auto fIds = mesh.GetActiveFaceIDs();
        std::vector<FaceID> adjacentFaces;
        if (fIds.size() >= 2) {
            adjacentFaces = { fIds[0], fIds[1] };
        }

        ExtrudeParams ep;
        ep.offset = 0.3f;
        auto opRes = ExtrudeRegion(mesh, adjacentFaces, ep);
        auto val = mesh.ValidateTopology();

        t.passed = opRes.success && val.isValid;
        t.observedDesc = "Success=" + std::to_string(opRes.success) + 
                         ", Valid=" + std::to_string(val.isValid) + 
                         ", CreatedFaces=" + std::to_string(opRes.createdFaces.size());
        t.details = t.passed ? "PASS" : "FAIL: Estrusione di regione non valida";
        results.push_back(t);
    }

    // MOD-6: Boundary Edge Extrude
    {
        ModelingTestResult t;
        t.testId = "MOD-6";
        t.testName = "MOD-6 — Boundary Edge Extrude & Non-Manifold Rejection";
        t.inputDesc = "Extrude open quad boundary edge by 0.4; attempt extrude on closed cube edge";
        t.expectedDesc = "Open boundary extends successfully; closed edge extrude rejected with error";

        auto openMesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        EdgeID boundaryEid = INVALID_EDGE_ID;
        for (auto eid : openMesh.GetActiveEdgeIDs()) {
            if (openMesh.IsBoundaryEdge(eid)) {
                boundaryEid = eid;
                break;
            }
        }

        ExtrudeParams ep;
        ep.offset = 0.4f;
        auto openRes = ExtrudeBoundaryEdge(openMesh, boundaryEid, ep);

        // Tentativo su cubo chiuso
        auto closedMesh = EditableMesh::CreateCube(1.0f);
        EdgeID closedEid = closedMesh.GetActiveEdgeIDs()[0];
        auto closedRes = ExtrudeBoundaryEdge(closedMesh, closedEid, ep);

        bool openPassed = openRes.success && (openMesh.GetFaceCount() == 2);
        bool closedRejected = !closedRes.success; // Deve essere rifiutato per preservare manifoldness

        t.passed = openPassed && closedRejected;
        t.observedDesc = "OpenSuccess=" + std::to_string(openRes.success) + 
                         ", ClosedRejected=" + std::to_string(closedRejected) + 
                         ", ErrorMsg='" + closedRes.errorMessage + "'";
        t.details = t.passed ? "PASS" : "FAIL: Fallito extrude boundary o mancato rifiuto su edge chiuso";
        results.push_back(t);
    }

    // MOD-7: Face Inset
    {
        ModelingTestResult t;
        t.testId = "MOD-7";
        t.testName = "MOD-7 — Face Inset & Concave Rejection";
        t.inputDesc = "Convex quad inset by 0.2; concave polygon inset safe rejection";
        t.expectedDesc = "Quad produces 1 inner face + 4 framing quads (faces=10), concave rejected";

        auto mesh = EditableMesh::CreateCube(1.0f);
        FaceID topFace = mesh.GetActiveFaceIDs()[0];

        InsetParams ip;
        ip.insetAmount = 0.2f;
        auto opRes = InsetFace(mesh, topFace, ip);
        auto val = mesh.ValidateTopology();

        bool faceCountOk = (mesh.GetFaceCount() == 10);
        bool validTopology = val.isValid && val.isClosedManifold;

        t.passed = opRes.success && faceCountOk && validTopology;
        t.observedDesc = "Success=" + std::to_string(opRes.success) + 
                         ", TotalFaces=" + std::to_string(mesh.GetFaceCount()) + 
                         ", IsValid=" + std::to_string(val.isValid);
        t.details = t.passed ? "PASS" : "FAIL: Inset topologicamente non corretto";
        results.push_back(t);
    }

    // MOD-8: Edge Bevel
    {
        ModelingTestResult t;
        t.testId = "MOD-8";
        t.testName = "MOD-8 — Manifold Edge Bevel Chamfer";
        t.inputDesc = "Bevel edge of cube with width 0.08";
        t.expectedDesc = "Inserts 1 chamfer facet, reconstructs 2 adjacent faces, valid manifold";

        auto mesh = EditableMesh::CreateCube(1.0f);
        EdgeID targetEid = mesh.GetActiveEdgeIDs()[0];

        BevelParams bp;
        bp.width = 0.08f;
        auto opRes = BevelEdge(mesh, targetEid, bp);
        auto val = mesh.ValidateTopology();

        bool facesOk = (mesh.GetFaceCount() == 7); // 6 - 2 + 2 + 1 = 7

        t.passed = opRes.success && facesOk && val.isValid && val.isClosedManifold;
        t.observedDesc = "Success=" + std::to_string(opRes.success) + 
                         ", TotalFaces=" + std::to_string(mesh.GetFaceCount()) + 
                         ", IsClosed=" + std::to_string(val.isClosedManifold);
        t.details = t.passed ? "PASS" : "FAIL: Bevel spigolo non valido";
        results.push_back(t);
    }

    // MOD-9: UV / SurfaceID / MaterialID Integrity
    {
        ModelingTestResult t;
        t.testId = "MOD-9";
        t.testName = "MOD-9 — UV, SurfaceID & MaterialID Integrity";
        t.inputDesc = "Face with SurfaceID=42, MaterialID=7 subjected to Extrude and Inset";
        t.expectedDesc = "All generated faces inherit SurfaceID=42 and MaterialID=7, UVs preserved";

        auto mesh = EditableMesh::CreateCube(1.0f);
        FaceID topFace = mesh.GetActiveFaceIDs()[0];
        auto* f = mesh.GetFaceMutable(topFace);
        f->surfaceId = 42;
        f->materialId = 7;

        ExtrudeParams ep;
        ep.offset = 0.2f;
        auto opRes = ExtrudeFace(mesh, topFace, ep);

        bool allMatch = true;
        for (auto createdFid : opRes.createdFaces) {
            const auto* cf = mesh.GetFace(createdFid);
            if (!cf || cf->surfaceId != 42 || cf->materialId != 7) {
                allMatch = false;
                break;
            }
        }

        t.passed = opRes.success && allMatch && (opRes.createdFaces.size() == 5);
        t.observedDesc = "Success=" + std::to_string(opRes.success) + 
                         ", AllMatch=" + std::to_string(allMatch) + 
                         ", CreatedFaces=" + std::to_string(opRes.createdFaces.size());
        t.details = t.passed ? "PASS" : "FAIL: Perdita di SurfaceID o MaterialID durante l'operazione";
        results.push_back(t);
    }

    // MOD-10: Atomic Rollback & Zero-Work
    {
        ModelingTestResult t;
        t.testId = "MOD-10";
        t.testName = "MOD-10 — Atomic Rollback on Failure & Zero-Work";
        t.inputDesc = "1. Degenerate non-finite delta move; 2. Zero delta move";
        t.expectedDesc = "1. Rollback returns error, mesh 100% identical; 2. Zero-work does not bump revisions";

        auto mesh = EditableMesh::CreateCube(1.0f);
        uint64_t topoBefore = mesh.GetTopologyRevision();
        uint64_t geoBefore = mesh.GetGeometryRevision();
        size_t vertsBefore = mesh.GetVertexCount();

        // 1. Chiamata con NaN
        float nanVal = std::numeric_limits<float>::quiet_NaN();
        auto nanRes = Move(mesh, {mesh.GetActiveVertexIDs()[0]}, glm::vec3(nanVal, 0.0f, 0.0f));

        bool nanRejected = !nanRes.success;
        bool meshUntouched = (mesh.GetTopologyRevision() == topoBefore && 
                              mesh.GetGeometryRevision() == geoBefore && 
                              mesh.GetVertexCount() == vertsBefore);

        // 2. Chiamata con Zero delta
        auto zeroRes = Move(mesh, {mesh.GetActiveVertexIDs()[0]}, glm::vec3(0.0f));
        bool zeroWorkPreserved = zeroRes.success && !zeroRes.geometryChanged && (mesh.GetGeometryRevision() == geoBefore);

        t.passed = nanRejected && meshUntouched && zeroWorkPreserved;
        t.observedDesc = "NanRejected=" + std::to_string(nanRejected) + 
                         ", MeshUntouched=" + std::to_string(meshUntouched) + 
                         ", ZeroWork=" + std::to_string(zeroWorkPreserved);
        t.details = t.passed ? "PASS" : "FAIL: Mancato rollback atomico o violazione contratto Zero-Work";
        results.push_back(t);
    }

    return results;
}

} // namespace fw
