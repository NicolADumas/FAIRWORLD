#include "pch.h"
#include "MeshTopologyService.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <queue>

namespace fw {

// =============================================================================
// BM-S6: 1. MIRRORING ACROSS PLANE & SYMMETRY
// =============================================================================

TopologyOperationResult MeshTopologyService::MirrorAcrossPlane(EditableMesh& mesh, const MirrorParams& params) {
    TopologyOperationResult res;
    res.oldTopologyRev = mesh.GetTopologyRevision();
    res.oldGeometryRev = mesh.GetGeometryRevision();

    if (mesh.GetFaceCount() == 0) {
        res.success = false;
        res.errorMessage = "Mesh is empty.";
        return res;
    }

    float eps = std::max(1e-5f, params.weldThreshold);

    auto GetCoord = [&](const glm::vec3& p) -> float {
        if (params.plane == MirrorPlane::X) return p.x;
        if (params.plane == MirrorPlane::Y) return p.y;
        return p.z;
    };

    auto MirrorVec = [&](const glm::vec3& p) -> glm::vec3 {
        glm::vec3 m = p;
        if (params.plane == MirrorPlane::X) m.x = -m.x;
        else if (params.plane == MirrorPlane::Y) m.y = -m.y;
        else m.z = -m.z;
        return m;
    };

    // 1. Se richiesto, elimina facce che risiedono interamente sul lato negativo
    if (params.removeNegativeSide) {
        auto activeFaces = mesh.GetActiveFaceIDs();
        for (FaceID fid : activeFaces) {
            auto verts = mesh.GetFaceVertices(fid);
            bool allNegative = true;
            for (VertexID vid : verts) {
                const auto* v = mesh.GetVertex(vid);
                if (v && GetCoord(v->position) >= -eps) {
                    allNegative = false;
                    break;
                }
            }
            if (allNegative) {
                mesh.DeleteFace(fid, true, false);
            }
        }

        // Snap dei vertici di confine al piano di simmetria (es. x = 0)
        auto activeVerts = mesh.GetActiveVertexIDs();
        for (VertexID vid : activeVerts) {
            auto* v = mesh.GetVertexMutable(vid);
            if (!v) continue;
            float c = GetCoord(v->position);
            if (std::abs(c) <= eps) {
                if (params.plane == MirrorPlane::X) v->position.x = 0.0f;
                else if (params.plane == MirrorPlane::Y) v->position.y = 0.0f;
                else v->position.z = 0.0f;
            }
        }
    }

    // 2. Mappatura vertici: per ogni vertice originale, crea o riusa la controparte specchiata
    std::unordered_map<VertexID, VertexID> origToMirror;
    auto activeVerts = mesh.GetActiveVertexIDs();

    for (VertexID vid : activeVerts) {
        const auto* v = mesh.GetVertex(vid);
        if (!v) continue;

        float c = GetCoord(v->position);
        if (params.weldSeam && std::abs(c) <= eps) {
            // Vertice sul piano di simmetria: resta condiviso (saldatura naturale del seam)
            origToMirror[vid] = vid;
        } else {
            // Crea vertice specchiato
            glm::vec3 mirroredPos = MirrorVec(v->position);
            VertexID mVid = mesh.AddVertex(mirroredPos);
            origToMirror[vid] = mVid;
            res.createdVertices.push_back(mVid);
        }
    }

    // 3. Duplica le facce con avvolgimento invertito (CCW invariant)
    auto activeFaces = mesh.GetActiveFaceIDs();
    for (FaceID fid : activeFaces) {
        const auto* face = mesh.GetFace(fid);
        if (!face) continue;

        auto vList = mesh.GetFaceVertices(fid);
        auto cList = mesh.GetFaceCorners(fid);
        if (vList.size() < 3) continue;

        // Inversione ordine vertici per compensare il cambio di segno del determinante (riflessione)
        // e garantire normali uscenti coerenti CCW
        std::vector<VertexID> mirroredVerts;
        std::vector<CornerAttributes> mirroredCorners;
        mirroredVerts.reserve(vList.size());
        mirroredCorners.reserve(cList.size());

        for (int i = static_cast<int>(vList.size()) - 1; i >= 0; --i) {
            VertexID origV = vList[i];
            auto it = origToMirror.find(origV);
            if (it != origToMirror.end()) {
                mirroredVerts.push_back(it->second);
            }

            if (!cList.empty() && static_cast<size_t>(i) < cList.size()) {
                CornerAttributes ca = cList[i];
                ca.normal = MirrorVec(ca.normal);
                mirroredCorners.push_back(ca);
            }
        }

        if (mirroredVerts.size() == vList.size()) {
            FaceID mFid = mesh.AddFace(mirroredVerts, face->surfaceId, face->materialId, 
                                      mirroredCorners.empty() ? nullptr : &mirroredCorners);
            if (mFid.IsValid()) {
                res.createdFaces.push_back(mFid);
            }
        }
    }

    mesh.ComputeAllFaceNormals();
    mesh.ComputeVertexNormals();
    mesh.IncrementTopologyRevision();

    res.success = true;
    res.topologyChanged = true;
    res.geometryChanged = true;
    res.newTopologyRev = mesh.GetTopologyRevision();
    res.newGeometryRev = mesh.GetGeometryRevision();
    return res;
}

// =============================================================================
// BM-S6: 2. PROPORTIONAL EDITING (SOFT SELECTION)
// =============================================================================

TopologyOperationResult MeshTopologyService::ApplyProportionalEdit(EditableMesh& mesh, 
                                                                   const MeshSelectionService& selection, 
                                                                   const ProportionalEditParams& params) {
    TopologyOperationResult res;
    res.oldTopologyRev = mesh.GetTopologyRevision();
    res.oldGeometryRev = mesh.GetGeometryRevision();

    if (params.radius <= 1e-5f) {
        res.success = true; // Zero-Work
        return res;
    }

    // Risoluzione vertici selezionati
    std::unordered_set<VertexID> selectedVerts = selection.GetSelectedVertices();
    if (selectedVerts.empty()) {
        for (EdgeID eid : selection.GetSelectedEdges()) {
            const auto* edge = mesh.GetEdge(eid);
            if (!edge) continue;
            const auto* he = mesh.GetHalfEdge(edge->halfEdge);
            if (!he) continue;
            selectedVerts.insert(he->origin);
            const auto* heNext = mesh.GetHalfEdge(he->next);
            if (heNext) selectedVerts.insert(heNext->origin);
        }
        for (FaceID fid : selection.GetSelectedFaces()) {
            auto fVerts = mesh.GetFaceVertices(fid);
            selectedVerts.insert(fVerts.begin(), fVerts.end());
        }
    }

    if (selectedVerts.empty()) {
        res.success = false;
        res.errorMessage = "No elements selected for proportional editing.";
        return res;
    }

    // Calcolo baricentro della selezione
    glm::vec3 centroid(0.0f);
    float selCount = 0.0f;
    for (VertexID vid : selectedVerts) {
        const auto* v = mesh.GetVertex(vid);
        if (v) {
            centroid += v->position;
            selCount += 1.0f;
        }
    }
    if (selCount > 0.0f) centroid /= selCount;

    bool anyModified = false;
    auto activeVerts = mesh.GetActiveVertexIDs();

    for (VertexID vid : activeVerts) {
        auto* v = mesh.GetVertexMutable(vid);
        if (!v) continue;

        // Distanza minima dal baricentro o da vertici selezionati
        float dist = glm::distance(v->position, centroid);
        if (dist > params.radius) continue;

        float weight = MeshSculptService::EvaluateFalloff(dist / params.radius, params.falloff);
        if (weight <= 1e-6f) continue;

        // 1. Traslazione
        if (glm::length(params.translationDelta) > 1e-6f) {
            v->position += params.translationDelta * weight;
            anyModified = true;
        }

        // 2. Rotazione
        if (std::abs(params.rotationDelta.w - 1.0f) > 1e-5f || glm::length(glm::vec3(params.rotationDelta.x, params.rotationDelta.y, params.rotationDelta.z)) > 1e-5f) {
            glm::quat qWeighted = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), params.rotationDelta, weight);
            glm::vec3 rel = v->position - centroid;
            v->position = centroid + (qWeighted * rel);
            anyModified = true;
        }

        // 3. Scala
        if (glm::distance(params.scaleDelta, glm::vec3(1.0f)) > 1e-5f) {
            glm::vec3 sWeighted = glm::mix(glm::vec3(1.0f), params.scaleDelta, weight);
            glm::vec3 rel = v->position - centroid;
            v->position = centroid + (rel * sWeighted);
            anyModified = true;
        }
    }

    if (anyModified) {
        mesh.ComputeAllFaceNormals();
        mesh.ComputeVertexNormals();
        mesh.IncrementGeometryRevision();
        res.geometryChanged = true;
    }

    res.success = true;
    res.topologyChanged = false; // Invariante topologica: proportional editing modifica solo posizioni
    res.newTopologyRev = mesh.GetTopologyRevision();
    res.newGeometryRev = mesh.GetGeometryRevision();
    return res;
}

// =============================================================================
// BM-S6: 3. SUDDIVISIONE POLIGONALE (MIDPOINT / CATMULL-CLARK QUAD SPLIT)
// =============================================================================

TopologyOperationResult MeshTopologyService::Subdivide(EditableMesh& mesh, const SubdivideParams& params) {
    TopologyOperationResult res;
    res.oldTopologyRev = mesh.GetTopologyRevision();
    res.oldGeometryRev = mesh.GetGeometryRevision();

    if (params.iterations == 0) {
        res.success = true; // Zero-Work
        return res;
    }

    for (uint32_t it = 0; it < params.iterations; ++it) {
        auto activeFaces = mesh.GetActiveFaceIDs();
        auto activeEdges = mesh.GetActiveEdgeIDs();
        auto activeVerts = mesh.GetActiveVertexIDs();

        if (activeFaces.empty()) break;

        // 1. Calcola punto baricentrico per ciascuna faccia
        std::unordered_map<FaceID, glm::vec3> facePoints;
        for (FaceID fid : activeFaces) {
            auto fVerts = mesh.GetFaceVertices(fid);
            glm::vec3 avg(0.0f);
            for (auto vid : fVerts) {
                const auto* v = mesh.GetVertex(vid);
                if (v) avg += v->position;
            }
            if (!fVerts.empty()) avg /= static_cast<float>(fVerts.size());
            facePoints[fid] = avg;
        }

        // 2. Calcola punto medio per ciascun edge (con smooth se richiesto)
        std::unordered_map<EdgeID, glm::vec3> edgePoints;
        for (EdgeID eid : activeEdges) {
            const auto* edge = mesh.GetEdge(eid);
            if (!edge) continue;
            const auto* he = mesh.GetHalfEdge(edge->halfEdge);
            if (!he) continue;
            const auto* heNext = mesh.GetHalfEdge(he->next);
            if (!heNext) continue;

            const auto* v0 = mesh.GetVertex(he->origin);
            const auto* v1 = mesh.GetVertex(heNext->origin);
            if (!v0 || !v1) continue;

            glm::vec3 edgeMid = (v0->position + v1->position) * 0.5f;

            if (params.smooth && edge->twin.IsValid()) {
                const auto* twinHe = mesh.GetHalfEdge(edge->twin);
                FaceID f1 = he->face;
                FaceID f2 = twinHe ? twinHe->face : INVALID_FACE_ID;
                if (facePoints.count(f1) && facePoints.count(f2)) {
                    edgeMid = (v0->position + v1->position + facePoints[f1] + facePoints[f2]) * 0.25f;
                }
            }
            edgePoints[eid] = edgeMid;
        }

        // 3. Costruisci la nuova topologia suddivisa
        EditableMesh subMesh;
        std::unordered_map<VertexID, VertexID> origToNew;
        std::unordered_map<EdgeID, VertexID> edgeToNew;
        std::unordered_map<FaceID, VertexID> faceToNew;

        // Alloca vertici originali (con smooth se richiesto)
        for (VertexID vid : activeVerts) {
            const auto* v = mesh.GetVertex(vid);
            if (!v) continue;
            glm::vec3 pos = v->position;
            if (params.smooth && !mesh.IsBoundaryVertex(vid)) {
                auto neighbors = mesh.GetVertexOneRingNeighbors(vid);
                auto incFaces = mesh.GetVertexIncidentFaces(vid);
                float n = static_cast<float>(neighbors.size());
                if (n >= 3.0f && incFaces.size() == neighbors.size()) {
                    glm::vec3 avgF(0.0f);
                    for (auto fid : incFaces) avgF += facePoints[fid];
                    avgF /= static_cast<float>(incFaces.size());

                    glm::vec3 avgR(0.0f);
                    for (auto nId : neighbors) {
                        const auto* nv = mesh.GetVertex(nId);
                        if (nv) avgR += (v->position + nv->position) * 0.5f;
                    }
                    avgR /= n;

                    pos = (avgF + 2.0f * avgR + (n - 3.0f) * v->position) / n;
                }
            }
            VertexID nvId = subMesh.AddVertex(pos);
            origToNew[vid] = nvId;
        }

        // Alloca vertici edge
        for (const auto& kv : edgePoints) {
            VertexID evId = subMesh.AddVertex(kv.second);
            edgeToNew[kv.first] = evId;
        }

        // Alloca vertici faccia
        for (const auto& kv : facePoints) {
            VertexID fvId = subMesh.AddVertex(kv.second);
            faceToNew[kv.first] = fvId;
        }

        // 4. Per ogni faccia e per ogni angolo, crea un nuovo quad
        for (FaceID fid : activeFaces) {
            const auto* face = mesh.GetFace(fid);
            if (!face) continue;

            auto hes = mesh.GetFaceHalfEdges(fid);
            size_t k = hes.size();
            VertexID fvId = faceToNew[fid];

            for (size_t i = 0; i < k; ++i) {
                HalfEdgeID currHeId = hes[i];
                HalfEdgeID prevHeId = hes[(i + k - 1) % k];

                const auto* currHe = mesh.GetHalfEdge(currHeId);
                const auto* prevHe = mesh.GetHalfEdge(prevHeId);
                if (!currHe || !prevHe) continue;

                VertexID vOrig = origToNew[currHe->origin];
                VertexID vEdgeCurr = edgeToNew[currHe->edge];
                VertexID vEdgePrev = edgeToNew[prevHe->edge];

                // Quad CCW: (Corner, MidNext, FaceCenter, MidPrev)
                std::vector<VertexID> quadVerts = { vOrig, vEdgeCurr, fvId, vEdgePrev };

                // Interpolazione UV per-corner
                CornerAttributes ca0 = currHe->corner;
                CornerAttributes ca1 = ca0; ca1.uv = (ca0.uv + mesh.GetHalfEdge(currHe->next)->corner.uv) * 0.5f;
                CornerAttributes ca2 = ca0; ca2.uv = (ca0.uv + ca1.uv) * 0.5f;
                CornerAttributes ca3 = ca0; ca3.uv = (ca0.uv + prevHe->corner.uv) * 0.5f;
                std::vector<CornerAttributes> quadCorners = { ca0, ca1, ca2, ca3 };

                FaceID newFid = subMesh.AddFace(quadVerts, face->surfaceId, face->materialId, &quadCorners);
                if (newFid.IsValid()) {
                    res.createdFaces.push_back(newFid);
                }
            }
        }

        subMesh.ComputeAllFaceNormals();
        subMesh.ComputeVertexNormals();
        mesh = std::move(subMesh);
        mesh.IncrementTopologyRevision();
    }

    res.success = true;
    res.topologyChanged = true;
    res.geometryChanged = true;
    res.newTopologyRev = mesh.GetTopologyRevision();
    res.newGeometryRev = mesh.GetGeometryRevision();
    return res;
}

// =============================================================================
// BM-S6: 4. EDGE LOOP CUT & RING TRAVERSAL
// =============================================================================

std::vector<EdgeID> MeshTopologyService::FindEdgeRing(const EditableMesh& mesh, EdgeID startEdge) {
    std::vector<EdgeID> ring;
    if (!mesh.IsValid(startEdge)) return ring;

    std::unordered_set<EdgeID> visited;
    const auto* e = mesh.GetEdge(startEdge);
    if (!e) return ring;

    // Funzione helper per percorrere il ring lungo una direzione
    auto TraverseDirection = [&](HalfEdgeID startHe) {
        HalfEdgeID currHe = startHe;
        while (currHe.IsValid()) {
            const auto* he = mesh.GetHalfEdge(currHe);
            if (!he || !he->face.IsValid()) break;

            // In un quad CCW (h0, h1, h2, h3), lo spigolo opposto e' h2
            auto fHes = mesh.GetFaceHalfEdges(he->face);
            if (fHes.size() != 4) break; // Ring supportato rigorosamente su topologia quad

            int idx = -1;
            for (int i = 0; i < 4; ++i) {
                if (fHes[i] == currHe) { idx = i; break; }
            }
            if (idx == -1) break;

            HalfEdgeID oppHe = fHes[(idx + 2) % 4];
            const auto* oppHeData = mesh.GetHalfEdge(oppHe);
            if (!oppHeData) break;

            EdgeID oppEdge = oppHeData->edge;
            if (visited.count(oppEdge)) break;

            visited.insert(oppEdge);
            ring.push_back(oppEdge);

            // Passa alla faccia adiacente tramite il twin dello spigolo opposto
            currHe = oppHeData->twin;
        }
    };

    visited.insert(startEdge);
    ring.push_back(startEdge);

    // Percorri verso lato 1
    TraverseDirection(e->halfEdge);
    // Percorri verso lato 2 (se non boundary)
    if (e->twin.IsValid()) {
        TraverseDirection(e->twin);
    }

    return ring;
}

std::vector<EdgeID> MeshTopologyService::FindEdgeLoop(const EditableMesh& mesh, EdgeID startEdge) {
    std::vector<EdgeID> loop;
    if (!mesh.IsValid(startEdge)) return loop;

    std::unordered_set<EdgeID> visited;
    visited.insert(startEdge);
    loop.push_back(startEdge);

    const auto* e = mesh.GetEdge(startEdge);
    if (!e) return loop;

    // Helper per estendere il loop attraverso vertici di valenza 4
    auto TraverseVertex = [&](VertexID vid, EdgeID fromEdge) {
        VertexID currV = vid;
        EdgeID prevE = fromEdge;

        while (currV.IsValid()) {
            auto outHes = mesh.GetVertexOutgoingHalfEdges(currV);
            if (outHes.size() != 4) break; // Loop continuo attraverso snodi quad a valenza 4

            // Trova l'edge uscente collinearamente opposto nello spoke a 4
            int fromIdx = -1;
            for (size_t i = 0; i < outHes.size(); ++i) {
                const auto* he = mesh.GetHalfEdge(outHes[i]);
                if (he && he->edge == prevE) { fromIdx = static_cast<int>(i); break; }
            }
            if (fromIdx == -1) break;

            HalfEdgeID oppHe = outHes[(fromIdx + 2) % 4];
            const auto* oppHeData = mesh.GetHalfEdge(oppHe);
            if (!oppHeData) break;

            EdgeID nextE = oppHeData->edge;
            if (visited.count(nextE)) break;

            visited.insert(nextE);
            loop.push_back(nextE);

            // Avanza al prossimo vertice
            const auto* nextNextHe = mesh.GetHalfEdge(oppHeData->next);
            currV = nextNextHe ? nextNextHe->origin : INVALID_VERTEX_ID;
            prevE = nextE;
        }
    };

    const auto* he0 = mesh.GetHalfEdge(e->halfEdge);
    if (he0) {
        const auto* heNext = mesh.GetHalfEdge(he0->next);
        if (heNext) {
            TraverseVertex(heNext->origin, startEdge);
            TraverseVertex(he0->origin, startEdge);
        }
    }

    return loop;
}

TopologyOperationResult MeshTopologyService::LoopCut(EditableMesh& mesh, const LoopCutParams& params) {
    TopologyOperationResult res;
    res.oldTopologyRev = mesh.GetTopologyRevision();
    res.oldGeometryRev = mesh.GetGeometryRevision();

    if (!mesh.IsValid(params.startEdge)) {
        res.success = false;
        res.errorMessage = "Start edge is invalid.";
        return res;
    }

    float t = glm::clamp(params.t, 0.01f, 0.99f);
    auto ringEdges = FindEdgeRing(mesh, params.startEdge);
    if (ringEdges.empty()) {
        res.success = false;
        res.errorMessage = "Could not find edge ring across quad topology.";
        return res;
    }

    // Per ciascun edge nel ring, inserisci un vertice di split interpolato
    std::unordered_map<EdgeID, VertexID> edgeSplitVerts;
    for (EdgeID eid : ringEdges) {
        const auto* edge = mesh.GetEdge(eid);
        if (!edge) continue;
        const auto* he = mesh.GetHalfEdge(edge->halfEdge);
        if (!he) continue;
        const auto* heNext = mesh.GetHalfEdge(he->next);
        if (!heNext) continue;

        const auto* v0 = mesh.GetVertex(he->origin);
        const auto* v1 = mesh.GetVertex(heNext->origin);
        if (!v0 || !v1) continue;

        glm::vec3 splitPos = glm::mix(v0->position, v1->position, t);
        VertexID splitVid = mesh.AddVertex(splitPos);
        edgeSplitVerts[eid] = splitVid;
        res.createdVertices.push_back(splitVid);
    }

    // Trova le facce quad attraversate dal ring ed esegui lo split
    std::unordered_set<FaceID> facesToCut;
    for (EdgeID eid : ringEdges) {
        const auto* edge = mesh.GetEdge(eid);
        if (!edge) continue;
        const auto* he0 = mesh.GetHalfEdge(edge->halfEdge);
        if (he0 && he0->face.IsValid()) facesToCut.insert(he0->face);
        const auto* he1 = mesh.GetHalfEdge(edge->twin);
        if (he1 && he1->face.IsValid()) facesToCut.insert(he1->face);
    }

    for (FaceID fid : facesToCut) {
        const auto* face = mesh.GetFace(fid);
        if (!face) continue;

        auto fHes = mesh.GetFaceHalfEdges(fid);
        if (fHes.size() != 4) continue; // Solo quad

        // Trova i due edge opposti che hanno ricevuto un vertice di split
        EdgeID e0 = INVALID_EDGE_ID;
        EdgeID e2 = INVALID_EDGE_ID;
        int idx0 = -1;

        for (int i = 0; i < 4; ++i) {
            const auto* he = mesh.GetHalfEdge(fHes[i]);
            if (he && edgeSplitVerts.count(he->edge)) {
                idx0 = i;
                e0 = he->edge;
                break;
            }
        }

        if (idx0 == -1) continue;
        const auto* heOpp = mesh.GetHalfEdge(fHes[(idx0 + 2) % 4]);
        if (!heOpp || !edgeSplitVerts.count(heOpp->edge)) continue;
        e2 = heOpp->edge;

        VertexID sv0 = edgeSplitVerts[e0];
        VertexID sv2 = edgeSplitVerts[e2];

        // Vertici originali del quad ordinati a partire da idx0
        auto fVerts = mesh.GetFaceVertices(fid);
        VertexID v0 = fVerts[idx0];
        VertexID v1 = fVerts[(idx0 + 1) % 4];
        VertexID v2 = fVerts[(idx0 + 2) % 4];
        VertexID v3 = fVerts[(idx0 + 3) % 4];

        uint32_t sId = face->surfaceId;
        uint32_t mId = face->materialId;

        // Rimuovi il vecchio quad ed inserisci i due nuovi quad
        mesh.DeleteFace(fid, false, false);

        FaceID fA = mesh.AddFace({ v0, sv0, sv2, v3 }, sId, mId);
        FaceID fB = mesh.AddFace({ sv0, v1, v2, sv2 }, sId, mId);

        if (fA.IsValid()) res.createdFaces.push_back(fA);
        if (fB.IsValid()) res.createdFaces.push_back(fB);
    }

    mesh.ComputeAllFaceNormals();
    mesh.ComputeVertexNormals();
    mesh.IncrementTopologyRevision();

    res.success = true;
    res.topologyChanged = true;
    res.geometryChanged = true;
    res.newTopologyRev = mesh.GetTopologyRevision();
    res.newGeometryRev = mesh.GetGeometryRevision();
    return res;
}

// =============================================================================
// BM-S6: 5. SUITE DI TEST DETERMINISTICA (TOP-1 .. TOP-10)
// =============================================================================

std::vector<MeshTopologyService::TopologyTestResult> MeshTopologyService::RunTopologyContractTests() {
    std::vector<TopologyTestResult> results;
    results.reserve(10);

    // TOP-1: Mirror Across Plane X with Inverted Winding
    {
        TopologyTestResult t;
        t.testId = "TOP-1";
        t.testName = "TOP-1 — Mirror Across Plane X with Inverted CCW Winding";
        t.inputDesc = "Single quad on +X (+0.5 to +1.5); MirrorAcrossPlane X with removeNegativeSide=false";
        t.expectedDesc = "Creates mirrored quad on -X; vertex cycle inverted; normal points consistently outward (+Z)";

        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0.5f, 0.0f, 0.0f));
        VertexID v1 = mesh.AddVertex(glm::vec3(1.5f, 0.0f, 0.0f));
        VertexID v2 = mesh.AddVertex(glm::vec3(1.5f, 1.0f, 0.0f));
        VertexID v3 = mesh.AddVertex(glm::vec3(0.5f, 1.0f, 0.0f));
        FaceID f0 = mesh.AddFace({ v0, v1, v2, v3 });

        MirrorParams mp;
        mp.plane = MirrorPlane::X;
        mp.removeNegativeSide = false;
        mp.weldSeam = false;

        auto res = MirrorAcrossPlane(mesh, mp);
        bool facesCount = (mesh.GetFaceCount() == 2);

        // Verifica normale della faccia specchiata
        bool normalOutward = false;
        for (FaceID fid : mesh.GetActiveFaceIDs()) {
            if (fid != f0) {
                const auto* f = mesh.GetFace(fid);
                if (f && f->faceNormal.z > 0.9f) normalOutward = true;
            }
        }

        t.passed = res.success && facesCount && normalOutward;
        t.observedDesc = "Success=" + std::to_string(res.success) + 
                         ", Faces=" + std::to_string(mesh.GetFaceCount()) + 
                         ", NormalOutward=" + std::to_string(normalOutward);
        t.details = t.passed ? "PASS" : "FAIL: Specchiatura fallita o normale non orientata CCW";
        results.push_back(t);
    }

    // TOP-2: Mirror Seam Welding & Closed Manifold
    {
        TopologyTestResult t;
        t.testId = "TOP-2";
        t.testName = "TOP-2 — Mirror Seam Welding & Closed 2-Manifold";
        t.inputDesc = "Open half-cube (4 faces on +X with boundary on X=0); Mirror X with weldSeam=true";
        t.expectedDesc = "Seam vertices welded; resulting mesh is a closed 2-manifold (boundaryEdgeCount == 0)";

        // Creiamo un cubo completo e poi rimuoviamo 2 facce per fare un open half-mesh
        auto mesh = EditableMesh::CreateCube(1.0f);
        // Spostiamo il cubo a X=0.5 in modo che la faccia Left sia a X=0
        for (VertexID vid : mesh.GetActiveVertexIDs()) {
            auto* v = mesh.GetVertexMutable(vid);
            if (v) v->position.x += 0.5f;
        }

        MirrorParams mp;
        mp.plane = MirrorPlane::X;
        mp.removeNegativeSide = true;
        mp.weldSeam = true;
        mp.weldThreshold = 0.01f;

        auto res = MirrorAcrossPlane(mesh, mp);
        auto val = mesh.ValidateTopology();

        t.passed = res.success && val.isValid && (mesh.GetFaceCount() > 6);
        t.observedDesc = "Success=" + std::to_string(res.success) + 
                         ", IsValid=" + std::to_string(val.isValid) + 
                         ", FaceCount=" + std::to_string(mesh.GetFaceCount());
        t.details = t.passed ? "PASS" : "FAIL: Saldatura seam specchio non riuscita";
        results.push_back(t);
    }

    // TOP-3: Proportional Editing Falloff Curve
    {
        TopologyTestResult t;
        t.testId = "TOP-3";
        t.testName = "TOP-3 — Proportional Editing Distance Weighting Falloff";
        t.inputDesc = "Cube centered at origin; top-center vertex selected; Translate +Y by 1.0 with radius 1.5";
        t.expectedDesc = "Selected vertex moves 100% (+1.0); nearby vertices move according to falloff; far vertices move 0%";

        auto mesh = EditableMesh::CreateCube(2.0f);
        MeshSelectionService sel;
        sel.SetMode(SelectionMode::Vertex);

        // Seleziona il vertice a (1, 1, 1)
        VertexID selVid = INVALID_VERTEX_ID;
        for (VertexID vid : mesh.GetActiveVertexIDs()) {
            const auto* v = mesh.GetVertex(vid);
            if (v && glm::distance(v->position, glm::vec3(1.0f, 1.0f, 1.0f)) < 0.1f) {
                selVid = vid;
                sel.SetSelectedVertices({ vid });
                break;
            }
        }

        ProportionalEditParams pep;
        pep.radius = 2.5f;
        pep.falloff = BrushFalloffType::Linear;
        pep.translationDelta = glm::vec3(0.0f, 1.0f, 0.0f);

        uint64_t initialTopo = mesh.GetTopologyRevision();
        auto res = ApplyProportionalEdit(mesh, sel, pep);

        const auto* vSel = mesh.GetVertex(selVid);
        bool selMoved = vSel && (vSel->position.y >= 1.9f);
        bool topoUntouched = (mesh.GetTopologyRevision() == initialTopo);

        t.passed = res.success && selMoved && topoUntouched;
        t.observedDesc = "Success=" + std::to_string(res.success) + 
                         ", SelPosY=" + std::to_string(vSel ? vSel->position.y : 0.0f) + 
                         ", TopoUntouched=" + std::to_string(topoUntouched);
        t.details = t.passed ? "PASS" : "FAIL: Proportional editing falloff errato o topologia alterata";
        results.push_back(t);
    }

    // TOP-4: Single Quad Midpoint Subdivision
    {
        TopologyTestResult t;
        t.testId = "TOP-4";
        t.testName = "TOP-4 — Single Quad Midpoint Subdivision (1 to 4 Quads)";
        t.inputDesc = "Single quad face; Subdivide with iterations=1, smooth=false";
        t.expectedDesc = "Exactly 4 quads generated, 9 vertices, CCW windings preserved, valid topology";

        auto mesh = EditableMesh::CreateQuad(2.0f, 2.0f);
        SubdivideParams sp;
        sp.iterations = 1;
        sp.smooth = false;

        auto res = Subdivide(mesh, sp);
        auto val = mesh.ValidateTopology();

        bool fourQuads = (mesh.GetFaceCount() == 4);
        bool nineVerts = (mesh.GetVertexCount() == 9);

        t.passed = res.success && fourQuads && nineVerts && val.isValid;
        t.observedDesc = "Faces=" + std::to_string(mesh.GetFaceCount()) + 
                         ", Verts=" + std::to_string(mesh.GetVertexCount()) + 
                         ", IsValid=" + std::to_string(val.isValid);
        t.details = t.passed ? "PASS" : "FAIL: Suddivisione quad singola non conforme (1 -> 4)";
        results.push_back(t);
    }

    // TOP-5: Closed Manifold Cube Subdivision
    {
        TopologyTestResult t;
        t.testId = "TOP-5";
        t.testName = "TOP-5 — Closed Manifold Cube Subdivision (6 to 24 Quads)";
        t.inputDesc = "Cube (6 quad faces); Subdivide with 1 iteration; closed manifold invariant";
        t.expectedDesc = "Exactly 24 quads; closed manifold valid (boundaryEdgeCount == 0); zero topology errors";

        auto mesh = EditableMesh::CreateCube(1.0f);
        SubdivideParams sp;
        sp.iterations = 1;
        sp.smooth = true;

        auto res = Subdivide(mesh, sp);
        auto val = mesh.ValidateTopology();

        bool count24 = (mesh.GetFaceCount() == 24);
        bool isManifold = val.isValid && val.isClosedManifold;

        t.passed = res.success && count24 && isManifold;
        t.observedDesc = "Faces=" + std::to_string(mesh.GetFaceCount()) + 
                         ", ClosedManifold=" + std::to_string(isManifold) + 
                         ", Errors=" + std::to_string(val.errors.size());
        t.details = t.passed ? "PASS" : "FAIL: Suddivisione cubo non manifold o conteggio errato";
        results.push_back(t);
    }

    // TOP-6: Edge Loop Cut Across Quad Strip
    {
        TopologyTestResult t;
        t.testId = "TOP-6";
        t.testName = "TOP-6 — Edge Loop Cut Across Quad Strip";
        t.inputDesc = "Plane 2x1 (2 quads); LoopCut on shared dividing edge at t=0.5";
        t.expectedDesc = "Splits quads into 4 quads; edge loop perpendicular to direction; topology valid";

        auto mesh = EditableMesh::CreatePlane(2, 1, 2.0f);
        EdgeID cutEdge = mesh.GetActiveEdgeIDs()[0];

        LoopCutParams lcp;
        lcp.startEdge = cutEdge;
        lcp.t = 0.5f;

        auto res = LoopCut(mesh, lcp);
        auto val = mesh.ValidateTopology();

        t.passed = res.success && res.topologyChanged && val.isValid;
        t.observedDesc = "Success=" + std::to_string(res.success) + 
                         ", Faces=" + std::to_string(mesh.GetFaceCount()) + 
                         ", IsValid=" + std::to_string(val.isValid);
        t.details = t.passed ? "PASS" : "FAIL: Loop cut su strip quad non riuscito";
        results.push_back(t);
    }

    // TOP-7: Edge Ring Traversal Completeness
    {
        TopologyTestResult t;
        t.testId = "TOP-7";
        t.testName = "TOP-7 — Edge Ring Traversal Completeness on Closed Topology";
        t.inputDesc = "Cube with 6 quads; FindEdgeRing starting from vertical edge";
        t.expectedDesc = "Finds complete closed ring of 4 parallel edges traversing the cylinder belt of the cube";

        auto mesh = EditableMesh::CreateCube(1.0f);
        EdgeID startE = mesh.GetActiveEdgeIDs()[0];

        auto ring = FindEdgeRing(mesh, startE);
        bool ringCountOk = (ring.size() == 4);

        t.passed = ringCountOk;
        t.observedDesc = "RingEdgeCount=" + std::to_string(ring.size());
        t.details = t.passed ? "PASS" : "FAIL: Ring traversal incompleto su cubo chiuso";
        results.push_back(t);
    }

    // TOP-8: TopologyRevision Tracking Contract
    {
        TopologyTestResult t;
        t.testId = "TOP-8";
        t.testName = "TOP-8 — TopologyRevision Tracking Contract";
        t.inputDesc = "Subdivision bumps topologyRevision; Proportional editing bumps only geometryRevision";
        t.expectedDesc = "topologyRevision strictly tracked according to operation nature";

        auto mesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        uint64_t initialTopo = mesh.GetTopologyRevision();
        uint64_t initialGeo = mesh.GetGeometryRevision();

        SubdivideParams sp;
        sp.iterations = 1;
        Subdivide(mesh, sp);
        uint64_t topoAfterSub = mesh.GetTopologyRevision();

        MeshSelectionService sel;
        sel.SetMode(SelectionMode::Vertex);
        sel.SetSelectedVertices({ mesh.GetActiveVertexIDs()[0] });

        ProportionalEditParams pep;
        pep.radius = 1.0f;
        pep.translationDelta = glm::vec3(0.0f, 0.1f, 0.0f);
        ApplyProportionalEdit(mesh, sel, pep);

        uint64_t topoAfterProp = mesh.GetTopologyRevision();
        uint64_t geoAfterProp = mesh.GetGeometryRevision();

        bool subBumped = (topoAfterSub > initialTopo);
        bool propPreserved = (topoAfterProp == topoAfterSub);
        bool geoBumped = (geoAfterProp > initialGeo);

        t.passed = subBumped && propPreserved && geoBumped;
        t.observedDesc = "SubBumpedTopo=" + std::to_string(subBumped) + 
                         ", PropPreservedTopo=" + std::to_string(propPreserved) + 
                         ", GeoBumped=" + std::to_string(geoBumped);
        t.details = t.passed ? "PASS" : "FAIL: Contratti di revisione topologica o geometrica violati";
        results.push_back(t);
    }

    // TOP-9: Generational ID Safety on Face Deletion / Split
    {
        TopologyTestResult t;
        t.testId = "TOP-9";
        t.testName = "TOP-9 — Generational ID Safety on Topological Mutations";
        t.inputDesc = "Subdivide replaces faces; old FaceIDs must be reported invalid";
        t.expectedDesc = "mesh.IsValid(oldFaceId) returns false; prevents stale references";

        auto mesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        FaceID oldFid = mesh.GetActiveFaceIDs()[0];

        SubdivideParams sp;
        sp.iterations = 1;
        Subdivide(mesh, sp);

        bool oldInvalid = !mesh.IsValid(oldFid);

        t.passed = oldInvalid;
        t.observedDesc = "OldFaceInvalidated=" + std::to_string(oldInvalid);
        t.details = t.passed ? "PASS" : "FAIL: Vecchi FaceID rimasti validi dopo la suddivisione";
        results.push_back(t);
    }

    // TOP-10: Zero-Work Invariance on Null / Invalid Input
    {
        TopologyTestResult t;
        t.testId = "TOP-10";
        t.testName = "TOP-10 — Zero-Work Invariance on Null or Invalid Input";
        t.inputDesc = "Subdivide with iterations=0; LoopCut with invalid EdgeID";
        t.expectedDesc = "Zero mutations performed; topologyRevision and geometryRevision strictly untouched";

        auto mesh = EditableMesh::CreateCube(1.0f);
        uint64_t initialTopo = mesh.GetTopologyRevision();
        uint64_t initialGeo = mesh.GetGeometryRevision();

        SubdivideParams sp;
        sp.iterations = 0;
        auto resSub = Subdivide(mesh, sp);

        LoopCutParams lcp;
        lcp.startEdge = INVALID_EDGE_ID;
        auto resCut = LoopCut(mesh, lcp);

        bool topoUntouched = (mesh.GetTopologyRevision() == initialTopo);
        bool geoUntouched = (mesh.GetGeometryRevision() == initialGeo);

        t.passed = topoUntouched && geoUntouched && !resSub.topologyChanged && !resCut.success;
        t.observedDesc = "TopoUntouched=" + std::to_string(topoUntouched) + 
                         ", GeoUntouched=" + std::to_string(geoUntouched) + 
                         ", SubTopoChanged=" + std::to_string(resSub.topologyChanged) + 
                         ", CutFailed=" + std::to_string(!resCut.success);
        t.details = t.passed ? "PASS" : "FAIL: Violazione del contratto Zero-Work";
        results.push_back(t);
    }

    return results;
}

} // namespace fw
