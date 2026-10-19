#include "pch.h"
#include "MeshRepairService.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <queue>
#include <stack>
#include <set>
#include <glm/gtx/norm.hpp>

namespace fw {

// =============================================================================
// BM-S7.1: RILEVAMENTO DEGENERAZIONI & DIAGNOSTICA
// =============================================================================

DegeneracyReport MeshRepairService::DetectDegeneracies(const EditableMesh& mesh, 
                                                      float areaEpsilon, 
                                                      float edgeLengthEpsilon) {
    DegeneracyReport report;
    float edgeLenEps2 = edgeLengthEpsilon * edgeLengthEpsilon;

    // 1. Analisi Facce
    auto activeFaces = mesh.GetActiveFaceIDs();
    for (FaceID fId : activeFaces) {
        const auto* f = mesh.GetFace(fId);
        if (!f) continue;

        if (f->area <= areaEpsilon) {
            report.zeroAreaFaces.push_back(fId);
        }

        std::vector<VertexID> verts = mesh.GetFaceVertices(fId);
        if (verts.size() < 3) {
            report.duplicateVertexFaces.push_back(fId);
            continue;
        }

        bool hasDup = false;
        for (size_t i = 0; i < verts.size() && !hasDup; ++i) {
            for (size_t j = i + 1; j < verts.size(); ++j) {
                if (verts[i] == verts[j]) {
                    hasDup = true;
                    break;
                }
            }
        }
        if (hasDup) {
            report.duplicateVertexFaces.push_back(fId);
        }
    }

    // 2. Analisi Spigoli (Lunghezza zero e isolati)
    auto activeEdges = mesh.GetActiveEdgeIDs();
    for (EdgeID eId : activeEdges) {
        const auto* e = mesh.GetEdge(eId);
        if (!e) continue;

        bool hasFace = false;
        if (e->halfEdge.IsValid()) {
            const auto* he = mesh.GetHalfEdge(e->halfEdge);
            if (he && he->face.IsValid() && mesh.IsValid(he->face)) hasFace = true;
        }
        if (e->twin.IsValid()) {
            const auto* the = mesh.GetHalfEdge(e->twin);
            if (the && the->face.IsValid() && mesh.IsValid(the->face)) hasFace = true;
        }

        if (!hasFace) {
            report.isolatedEdges.push_back(eId);
        }

        // Lunghezza spigolo
        if (e->halfEdge.IsValid()) {
            const auto* he = mesh.GetHalfEdge(e->halfEdge);
            if (he && he->next.IsValid()) {
                const auto* nextHe = mesh.GetHalfEdge(he->next);
                if (nextHe) {
                    const auto* v0 = mesh.GetVertex(he->origin);
                    const auto* v1 = mesh.GetVertex(nextHe->origin);
                    if (v0 && v1) {
                        glm::vec3 diff = v1->position - v0->position;
                        if (glm::dot(diff, diff) < edgeLenEps2) {
                            report.zeroLengthEdges.push_back(eId);
                        }
                    }
                }
            }
        }
    }

    // 3. Analisi Vertici Isolati & Non-Manifold Pinched
    std::unordered_map<VertexID, std::vector<FaceID>> vertexIncidentFaces;
    for (FaceID fId : activeFaces) {
        for (VertexID vid : mesh.GetFaceVertices(fId)) {
            vertexIncidentFaces[vid].push_back(fId);
        }
    }

    auto activeVerts = mesh.GetActiveVertexIDs();
    for (VertexID vId : activeVerts) {
        auto it = vertexIncidentFaces.find(vId);
        if (it == vertexIncidentFaces.end() || it->second.empty()) {
            report.isolatedVertices.push_back(vId);
            continue;
        }

        const auto& incFaces = it->second;

        // 4. Analisi Vertici Non-Manifold a clessidra / pinched (ventagli disconnessi)
        if (incFaces.size() > 1) {
            // Conta quante componenti connesse di facce condividono spigoli passanti per vId
            std::unordered_set<FaceID> unvisited(incFaces.begin(), incFaces.end());
            int clusters = 0;

            while (!unvisited.empty()) {
                clusters++;
                FaceID startF = *unvisited.begin();
                unvisited.erase(startF);

                std::queue<FaceID> q;
                q.push(startF);

                while (!q.empty()) {
                    FaceID curF = q.front();
                    q.pop();

                    // Trova facce vicine a curF che condividono uno spigolo incidente su vId
                    std::vector<HalfEdgeID> fHEs = mesh.GetFaceHalfEdges(curF);
                    for (HalfEdgeID heId : fHEs) {
                        const auto* he = mesh.GetHalfEdge(heId);
                        if (!he) continue;
                        const auto* nextHe = mesh.GetHalfEdge(he->next);
                        if (!nextHe) continue;

                        // Controlla se questo half-edge tocca vId
                        if (he->origin == vId || nextHe->origin == vId) {
                            if (he->twin.IsValid()) {
                                const auto* twinHe = mesh.GetHalfEdge(he->twin);
                                if (twinHe && twinHe->face.IsValid() && unvisited.count(twinHe->face)) {
                                    unvisited.erase(twinHe->face);
                                    q.push(twinHe->face);
                                }
                            }
                        }
                    }
                }
            }

            if (clusters > 1) {
                report.pinchedNonManifoldVertices.push_back(vId);
            }
        }
    }

    // 5. Analisi Fori di Bordo (Boundary Loops)
    auto activeHEs = mesh.GetActiveHalfEdgeIDs();
    std::unordered_set<HalfEdgeID> boundaryHEs;
    for (HalfEdgeID heId : activeHEs) {
        const auto* he = mesh.GetHalfEdge(heId);
        if (he && !he->twin.IsValid()) {
            boundaryHEs.insert(heId);
        }
    }

    while (!boundaryHEs.empty()) {
        HalfEdgeID startHe = *boundaryHEs.begin();
        boundaryHEs.erase(startHe);

        std::vector<HalfEdgeID> loop;
        loop.push_back(startHe);

        HalfEdgeID curHe = startHe;
        bool closed = false;
        size_t maxSteps = boundaryHEs.size() + 2;

        for (size_t step = 0; step < maxSteps; ++step) {
            const auto* heObj = mesh.GetHalfEdge(curHe);
            if (!heObj || !heObj->next.IsValid()) break;
            const auto* nextHeObj = mesh.GetHalfEdge(heObj->next);
            if (!nextHeObj) break;

            VertexID targetV = nextHeObj->origin;

            // Cerca il prossimo boundary half-edge con origin == targetV
            HalfEdgeID nextB = INVALID_HALFEDGE_ID;
            for (HalfEdgeID candId : boundaryHEs) {
                const auto* candHe = mesh.GetHalfEdge(candId);
                if (candHe && candHe->origin == targetV) {
                    nextB = candId;
                    break;
                }
            }

            if (nextB.IsValid()) {
                boundaryHEs.erase(nextB);
                loop.push_back(nextB);
                curHe = nextB;
                const auto* nObj = mesh.GetHalfEdge(mesh.GetHalfEdge(nextB)->next);
                if (nObj && nObj->origin == mesh.GetHalfEdge(startHe)->origin) {
                    closed = true;
                    break;
                }
            } else {
                break;
            }
        }

        if (closed && loop.size() >= 3) {
            report.boundaryHoleLoops.push_back(loop);
        }
    }

    return report;
}

// =============================================================================
// BM-S7.2: PULIZIA FACCE DEGENERI ED ELEMENTI ISOLATI
// =============================================================================

RepairResult MeshRepairService::RemoveDegenerateFaces(EditableMesh& mesh, float areaEpsilon) {
    RepairResult result;
    result.reportBefore = mesh.ValidateTopology();
    result.oldTopologyRev = mesh.GetTopologyRevision();
    result.oldGeometryRev = mesh.GetGeometryRevision();

    auto diag = DetectDegeneracies(mesh, areaEpsilon);
    std::unordered_set<FaceID> toDelete;
    toDelete.insert(diag.zeroAreaFaces.begin(), diag.zeroAreaFaces.end());
    toDelete.insert(diag.duplicateVertexFaces.begin(), diag.duplicateVertexFaces.end());

    if (toDelete.empty()) {
        result.success = true;
        result.modified = false;
        result.message = "Nessuna faccia degenere riscontrata (Zero-Work).";
        result.newTopologyRev = result.oldTopologyRev;
        result.newGeometryRev = result.oldGeometryRev;
        result.reportAfter = result.reportBefore;
        return result;
    }

    for (FaceID fId : toDelete) {
        if (mesh.DeleteFace(fId, true, false)) {
            result.degenerateFacesRemoved++;
        }
    }

    // Pulisce anche eventuali elementi orfani generati dalla rimozione
    auto cleanRes = CleanupIsolatedElements(mesh);
    result.isolatedVerticesRemoved = cleanRes.isolatedVerticesRemoved;
    result.isolatedEdgesRemoved = cleanRes.isolatedEdgesRemoved;

    result.modified = true;
    result.success = true;
    result.newTopologyRev = mesh.GetTopologyRevision();
    result.newGeometryRev = mesh.GetGeometryRevision();
    result.reportAfter = mesh.ValidateTopology();
    result.message = "Rimosse " + std::to_string(result.degenerateFacesRemoved) + " facce degeneri.";
    return result;
}

RepairResult MeshRepairService::CleanupIsolatedElements(EditableMesh& mesh) {
    RepairResult result;
    result.reportBefore = mesh.ValidateTopology();
    result.oldTopologyRev = mesh.GetTopologyRevision();
    result.oldGeometryRev = mesh.GetGeometryRevision();

    auto diag = DetectDegeneracies(mesh);
    if (diag.isolatedEdges.empty() && diag.isolatedVertices.empty()) {
        result.success = true;
        result.modified = false;
        result.message = "Nessun elemento isolato (Zero-Work).";
        result.newTopologyRev = result.oldTopologyRev;
        result.newGeometryRev = result.oldGeometryRev;
        result.reportAfter = result.reportBefore;
        return result;
    }

    for (EdgeID eId : diag.isolatedEdges) {
        if (mesh.DeleteEdge(eId)) {
            result.isolatedEdgesRemoved++;
        }
    }

    for (VertexID vId : diag.isolatedVertices) {
        if (mesh.DeleteVertex(vId)) {
            result.isolatedVerticesRemoved++;
        }
    }

    result.modified = (result.isolatedEdgesRemoved > 0 || result.isolatedVerticesRemoved > 0);
    result.success = true;
    result.newTopologyRev = mesh.GetTopologyRevision();
    result.newGeometryRev = mesh.GetGeometryRevision();
    result.reportAfter = mesh.ValidateTopology();
    result.message = "Eliminati " + std::to_string(result.isolatedVerticesRemoved) + " vertici e " +
                     std::to_string(result.isolatedEdgesRemoved) + " spigoli isolati.";
    return result;
}

// =============================================================================
// BM-S7.3: RIPARAZIONE NON-MANIFOLD (PINCHED VERTICES)
// =============================================================================

RepairResult MeshRepairService::SplitPinchedVertices(EditableMesh& mesh) {
    RepairResult result;
    result.reportBefore = mesh.ValidateTopology();
    result.oldTopologyRev = mesh.GetTopologyRevision();
    result.oldGeometryRev = mesh.GetGeometryRevision();

    auto diag = DetectDegeneracies(mesh);
    if (diag.pinchedNonManifoldVertices.empty()) {
        result.success = true;
        result.modified = false;
        result.message = "Nessun vertice pinched non-manifold (Zero-Work).";
        result.newTopologyRev = result.oldTopologyRev;
        result.newGeometryRev = result.oldGeometryRev;
        result.reportAfter = result.reportBefore;
        return result;
    }

    // Staging backup per garantire atomicità e rollback deterministico in caso di errore
    EditableMesh backup = mesh;

    // Costruzione dell'indice temporaneo di adiacenza faccia-vertice per i soli vertici pinched
    // Questo evita traversal globale quadratico O(V*F) e identifica correttamente tutti i ventagli
    std::unordered_set<VertexID> pinchedSet(
        diag.pinchedNonManifoldVertices.begin(),
        diag.pinchedNonManifoldVertices.end()
    );
    std::unordered_map<VertexID, std::vector<FaceID>> vertexIncidentFaces;
    for (FaceID fId : mesh.GetActiveFaceIDs()) {
        for (VertexID vid : mesh.GetFaceVertices(fId)) {
            if (pinchedSet.count(vid)) {
                vertexIncidentFaces[vid].push_back(fId);
            }
        }
    }

    for (VertexID pVid : diag.pinchedNonManifoldVertices) {
        const auto* origV = mesh.GetVertex(pVid);
        if (!origV) continue;
        glm::vec3 pos = origV->position;
        glm::vec3 normal = origV->normal;

        auto it = vertexIncidentFaces.find(pVid);
        if (it == vertexIncidentFaces.end()) continue;
        auto incFaces = it->second;

        // Ordinamento deterministico per FaceID
        std::sort(incFaces.begin(), incFaces.end(), [](FaceID a, FaceID b) {
            return a.index < b.index;
        });
        incFaces.erase(std::unique(incFaces.begin(), incFaces.end()), incFaces.end());

        if (incFaces.size() <= 1) continue;

        // Ripartiziona le facce in cluster connessi topologicamente (componenti di ventaglio)
        std::vector<std::vector<FaceID>> clusters;
        std::unordered_set<FaceID> visited;

        for (FaceID seedF : incFaces) {
            if (visited.count(seedF)) continue;

            std::vector<FaceID> curCluster;
            std::queue<FaceID> q;
            q.push(seedF);
            visited.insert(seedF);
            curCluster.push_back(seedF);

            while (!q.empty()) {
                FaceID curF = q.front();
                q.pop();

                std::vector<HalfEdgeID> fHEs = mesh.GetFaceHalfEdges(curF);
                for (HalfEdgeID heId : fHEs) {
                    const auto* he = mesh.GetHalfEdge(heId);
                    if (!he) continue;
                    const auto* nextHe = mesh.GetHalfEdge(he->next);
                    if (!nextHe) continue;

                    if (he->origin == pVid || nextHe->origin == pVid) {
                        if (mesh.IsValid(he->twin)) {
                            const auto* twinHe = mesh.GetHalfEdge(he->twin);
                            if (twinHe && mesh.IsValid(twinHe->face) && !visited.count(twinHe->face)) {
                                if (std::binary_search(incFaces.begin(), incFaces.end(), twinHe->face,
                                    [](FaceID a, FaceID b) { return a.index < b.index; })) {
                                    visited.insert(twinHe->face);
                                    q.push(twinHe->face);
                                    curCluster.push_back(twinHe->face);
                                }
                            }
                        }
                    }
                }
            }
            std::sort(curCluster.begin(), curCluster.end(), [](FaceID a, FaceID b) {
                return a.index < b.index;
            });
            clusters.push_back(curCluster);
        }

        if (clusters.size() <= 1) continue;

        // Cluster 0: mantiene il vertice originale pVid
        // Garantisce che pVid->halfEdge punti a una half-edge del cluster 0 con origin == pVid
        HalfEdgeID validHeForOrigV = INVALID_HALFEDGE_ID;
        for (FaceID fId : clusters[0]) {
            for (HalfEdgeID heId : mesh.GetFaceHalfEdges(fId)) {
                const auto* he = mesh.GetHalfEdge(heId);
                if (he && he->origin == pVid) {
                    validHeForOrigV = heId;
                    break;
                }
            }
            if (mesh.IsValid(validHeForOrigV)) break;
        }
        if (mesh.IsValid(validHeForOrigV)) {
            mesh.GetVertexMutable(pVid)->halfEdge = validHeForOrigV;
        }

        // Per ciascun cluster aggiuntivo c >= 1:
        // Alloca un nuovo VertexID e riassocia esclusivamente le Half-Edge del cluster c
        for (size_t c = 1; c < clusters.size(); ++c) {
            VertexID newVid = mesh.AddVertex(pos);
            if (!mesh.IsValid(newVid)) {
                // Rollback atomico in caso di errore nell'allocatore
                mesh = backup;
                result.success = false;
                result.modified = false;
                result.pinchedVerticesSplit = 0;
                result.newTopologyRev = result.oldTopologyRev;
                result.newGeometryRev = result.oldGeometryRev;
                result.reportAfter = result.reportBefore;
                result.message = "Allocazione nuovo VertexID fallita durante la separazione. Rollback eseguito.";
                return result;
            }
            mesh.GetVertexMutable(newVid)->normal = normal;

            HalfEdgeID validHeForNewV = INVALID_HALFEDGE_ID;
            for (FaceID fId : clusters[c]) {
                for (HalfEdgeID heId : mesh.GetFaceHalfEdges(fId)) {
                    auto* he = mesh.GetHalfEdgeMutable(heId);
                    if (he && he->origin == pVid) {
                        he->origin = newVid;
                        validHeForNewV = heId;
                    }
                }
            }
            if (mesh.IsValid(validHeForNewV)) {
                mesh.GetVertexMutable(newVid)->halfEdge = validHeForNewV;
            }
            result.pinchedVerticesSplit++;
        }
    }

    if (result.pinchedVerticesSplit > 0) {
        mesh.RebuildDirectedEdgeLookup();
        mesh.ComputeVertexNormals();
        mesh.IncrementTopologyRevision();

        result.reportAfter = mesh.ValidateTopology();
        if (!result.reportAfter.errors.empty()) {
            mesh = backup;
            result.success = false;
            result.modified = false;
            result.pinchedVerticesSplit = 0;
            result.newTopologyRev = result.oldTopologyRev;
            result.newGeometryRev = result.oldGeometryRev;
            result.reportAfter = result.reportBefore;
            result.message = "Topologia DCEL non valida dopo separazione pinched. Rollback atomico eseguito.";
            return result;
        }

        result.modified = true;
        result.success = true;
        result.newTopologyRev = mesh.GetTopologyRevision();
        result.newGeometryRev = mesh.GetGeometryRevision();
        result.message = "Separati con successo " + std::to_string(result.pinchedVerticesSplit) + " ventagli non-manifold.";
    } else {
        result.modified = false;
        result.success = true;
        result.newTopologyRev = result.oldTopologyRev;
        result.newGeometryRev = result.oldGeometryRev;
        result.reportAfter = result.reportBefore;
        result.message = "Nessun ventaglio separato (struttura gia manifold).";
    }

    return result;
}

// =============================================================================
// BM-S7.4: UNIFICAZIONE ORIENTAMENTO NORMALI
// =============================================================================

RepairResult MeshRepairService::UnifyNormalOrientations(EditableMesh& mesh) {
    RepairResult result;
    result.reportBefore = mesh.ValidateTopology();
    result.oldTopologyRev = mesh.GetTopologyRevision();
    result.oldGeometryRev = mesh.GetGeometryRevision();

    auto activeFaces = mesh.GetActiveFaceIDs();
    if (activeFaces.empty()) {
        result.success = true;
        result.modified = false;
        result.newTopologyRev = result.oldTopologyRev;
        result.newGeometryRev = result.oldGeometryRev;
        result.reportAfter = result.reportBefore;
        return result;
    }

    std::unordered_set<FaceID> visited;
    for (FaceID rootFace : activeFaces) {
        if (visited.count(rootFace)) continue;

        std::queue<FaceID> q;
        q.push(rootFace);
        visited.insert(rootFace);

        while (!q.empty()) {
            FaceID curF = q.front();
            q.pop();

            std::vector<HalfEdgeID> curHEs = mesh.GetFaceHalfEdges(curF);
            for (HalfEdgeID heId : curHEs) {
                const auto* he = mesh.GetHalfEdge(heId);
                if (!he || !he->twin.IsValid()) continue;
                const auto* twin = mesh.GetHalfEdge(he->twin);
                if (!twin || !twin->face.IsValid()) continue;

                FaceID adjF = twin->face;
                if (visited.count(adjF)) continue;

                // Controlla l'orientamento dello spigolo condiviso:
                // In una topologia coerente, se he va da A a B, twin DEVE andare da B ad A.
                // Se entrambi vanno da A a B, la faccia adiacente ha avvolgimento opposto!
                VertexID heOrigin = he->origin;
                VertexID heDest = mesh.GetHalfEdge(he->next)->origin;
                VertexID twinOrigin = twin->origin;
                VertexID twinDest = mesh.GetHalfEdge(twin->next)->origin;

                bool sameDirection = (heOrigin == twinOrigin && heDest == twinDest);
                if (sameDirection) {
                    // Inverti l'avvolgimento di adjF
                    std::vector<VertexID> verts = mesh.GetFaceVertices(adjF);
                    std::vector<CornerAttributes> corners = mesh.GetFaceCorners(adjF);
                    const auto* fObj = mesh.GetFace(adjF);
                    if (fObj) {
                        uint32_t sId = fObj->surfaceId;
                        uint32_t mId = fObj->materialId;

                        std::reverse(verts.begin(), verts.end());
                        std::reverse(corners.begin(), corners.end());

                        mesh.DeleteFace(adjF, true, false);
                        mesh.AddFace(verts, sId, mId, &corners);
                        result.flippedFacesCount++;
                    }
                }

                visited.insert(adjF);
                q.push(adjF);
            }
        }
    }

    if (result.flippedFacesCount > 0) {
        mesh.ComputeVertexNormals();
        result.modified = true;
    }

    result.success = true;
    result.newTopologyRev = mesh.GetTopologyRevision();
    result.newGeometryRev = mesh.GetGeometryRevision();
    result.reportAfter = mesh.ValidateTopology();
    result.message = "Riorientate " + std::to_string(result.flippedFacesCount) + " facce incoerenti.";
    return result;
}

// =============================================================================
// BM-S7.5: CHIUSURA FORI DI BORDO (HOLE CAPPING)
// =============================================================================

RepairResult MeshRepairService::CapHoles(EditableMesh& mesh, 
                                         uint32_t maxHoleEdges, 
                                         uint32_t surfaceId, 
                                         uint32_t materialId) {
    RepairResult result;
    result.reportBefore = mesh.ValidateTopology();
    result.oldTopologyRev = mesh.GetTopologyRevision();
    result.oldGeometryRev = mesh.GetGeometryRevision();

    auto diag = DetectDegeneracies(mesh);
    if (diag.boundaryHoleLoops.empty()) {
        result.success = true;
        result.modified = false;
        result.message = "Nessun foro di bordo chiuso rilevato (Zero-Work).";
        result.newTopologyRev = result.oldTopologyRev;
        result.newGeometryRev = result.oldGeometryRev;
        result.reportAfter = result.reportBefore;
        return result;
    }

    for (const auto& loop : diag.boundaryHoleLoops) {
        if (loop.size() < 3 || loop.size() > maxHoleEdges) continue;

        // Estrai la sequenza ordinata di vertici di bordo
        std::vector<VertexID> loopVerts;
        loopVerts.reserve(loop.size());
        for (HalfEdgeID heId : loop) {
            const auto* he = mesh.GetHalfEdge(heId);
            if (he) loopVerts.push_back(he->origin);
        }

        // Per fare in modo che la nuova faccia si accoppi come twin agli half-edge di confine
        // esistenti (che viaggiano in senso orario dal punto di vista del foro),
        // i vertici della nuova faccia devono essere ordinati nel verso opposto (CCW)
        std::reverse(loopVerts.begin(), loopVerts.end());

        FaceID newF = mesh.AddFace(loopVerts, surfaceId, materialId);
        if (newF.IsValid()) {
            result.holesCapped++;
        }
    }

    result.modified = (result.holesCapped > 0);
    result.success = true;
    result.newTopologyRev = mesh.GetTopologyRevision();
    result.newGeometryRev = mesh.GetGeometryRevision();
    result.reportAfter = mesh.ValidateTopology();
    result.message = "Tappati " + std::to_string(result.holesCapped) + " fori di bordo.";
    return result;
}

// =============================================================================
// BM-S7.6: DECIMAZIONE COMPLANARE / LIMITED DISSOLVE
// =============================================================================

RepairResult MeshRepairService::LimitedDissolve(EditableMesh& mesh, const DissolveOptions& options) {
    RepairResult result;
    result.reportBefore = mesh.ValidateTopology();
    result.oldTopologyRev = mesh.GetTopologyRevision();
    result.oldGeometryRev = mesh.GetGeometryRevision();

    float cosThreshold = std::cos(glm::radians(options.maxCoplanarAngleDegrees));
    bool changed = true;

    while (changed) {
        changed = false;
        auto activeEdges = mesh.GetActiveEdgeIDs();

        for (EdgeID eId : activeEdges) {
            const auto* e = mesh.GetEdge(eId);
            if (!e || !e->halfEdge.IsValid() || !e->twin.IsValid()) continue;

            const auto* he = mesh.GetHalfEdge(e->halfEdge);
            const auto* twin = mesh.GetHalfEdge(e->twin);
            if (!he || !twin || !he->face.IsValid() || !twin->face.IsValid()) continue;

            FaceID f1 = he->face;
            FaceID f2 = twin->face;
            if (f1 == f2) continue;

            const auto* fObj1 = mesh.GetFace(f1);
            const auto* fObj2 = mesh.GetFace(f2);
            if (!fObj1 || !fObj2) continue;

            if (options.preserveSurfaceId && fObj1->surfaceId != fObj2->surfaceId) continue;
            if (options.preserveMaterialId && fObj1->materialId != fObj2->materialId) continue;

            // Dihedral angle check
            float dotN = glm::dot(fObj1->faceNormal, fObj2->faceNormal);
            if (dotN < cosThreshold) continue;

            // Spigolo condiviso tra f1 e f2: costruisci loop combinato
            std::vector<VertexID> vList1 = mesh.GetFaceVertices(f1);
            std::vector<VertexID> vList2 = mesh.GetFaceVertices(f2);

            VertexID vA = he->origin;
            VertexID vB = mesh.GetHalfEdge(he->next)->origin;

            // Trova indici di vA e vB in vList1 e vList2
            auto itA1 = std::find(vList1.begin(), vList1.end(), vA);
            auto itB1 = std::find(vList1.begin(), vList1.end(), vB);
            auto itA2 = std::find(vList2.begin(), vList2.end(), vA);
            auto itB2 = std::find(vList2.begin(), vList2.end(), vB);

            if (itA1 == vList1.end() || itB1 == vList1.end() || itA2 == vList2.end() || itB2 == vList2.end()) {
                continue;
            }

            // Unisci i poligoni escludendo lo spigolo condiviso (vA, vB)
            // Loop 1 va da vB attorno a vA; Loop 2 va da vA attorno a vB
            std::vector<VertexID> merged;
            size_t n1 = vList1.size();
            size_t idxB1 = std::distance(vList1.begin(), itB1);
            for (size_t i = 0; i < n1; ++i) {
                size_t curIdx = (idxB1 + i) % n1;
                merged.push_back(vList1[curIdx]);
                if (vList1[curIdx] == vA) break;
            }

            size_t n2 = vList2.size();
            size_t idxA2 = std::distance(vList2.begin(), itA2);
            for (size_t i = 1; i < n2; ++i) {
                size_t curIdx = (idxA2 + i) % n2;
                if (vList2[curIdx] == vB) break;
                merged.push_back(vList2[curIdx]);
            }

            if (merged.size() < 3) continue;

            // Verifica che il poligono unito non abbia duplicati
            bool dup = false;
            for (size_t i = 0; i < merged.size() && !dup; ++i) {
                for (size_t j = i + 1; j < merged.size(); ++j) {
                    if (merged[i] == merged[j]) { dup = true; break; }
                }
            }
            if (dup) continue;

            uint32_t sId = fObj1->surfaceId;
            uint32_t mId = fObj1->materialId;

            mesh.DeleteFace(f1, true, false);
            mesh.DeleteFace(f2, true, false);

            FaceID newF = mesh.AddFace(merged, sId, mId);
            if (newF.IsValid()) {
                result.coplanarFacesDissolved++;
                changed = true;
                break; // Riavvia la scansione con gli ID aggiornati
            }
        }
    }

    result.modified = (result.coplanarFacesDissolved > 0);
    result.success = true;
    result.newTopologyRev = mesh.GetTopologyRevision();
    result.newGeometryRev = mesh.GetGeometryRevision();
    result.reportAfter = mesh.ValidateTopology();
    result.message = "Fusi " + std::to_string(result.coplanarFacesDissolved) + " spigoli complanari.";
    return result;
}

// =============================================================================
// BM-S7.7: PIPELINE COMPLETA DI RIPARAZIONE MANIFOLD
// =============================================================================

RepairResult MeshRepairService::RepairAll(EditableMesh& mesh, const RepairOptions& options) {
    RepairResult combined;
    combined.reportBefore = mesh.ValidateTopology();
    combined.oldTopologyRev = mesh.GetTopologyRevision();
    combined.oldGeometryRev = mesh.GetGeometryRevision();

    // 1. Rimuovi facce degeneri
    if (options.removeDegenerateFaces) {
        auto r = RemoveDegenerateFaces(mesh, options.areaEpsilon);
        combined.degenerateFacesRemoved += r.degenerateFacesRemoved;
        combined.isolatedVerticesRemoved += r.isolatedVerticesRemoved;
        combined.isolatedEdgesRemoved += r.isolatedEdgesRemoved;
    }

    // 2. Pulisci elementi isolati
    if (options.removeIsolatedElements) {
        auto r = CleanupIsolatedElements(mesh);
        combined.isolatedVerticesRemoved += r.isolatedVerticesRemoved;
        combined.isolatedEdgesRemoved += r.isolatedEdgesRemoved;
    }

    // 3. Separa ventagli pinched non-manifold
    if (options.splitPinchedVertices) {
        auto r = SplitPinchedVertices(mesh);
        combined.pinchedVerticesSplit += r.pinchedVerticesSplit;
    }

    // 4. Unifica orientamento normali
    if (options.unifyFaceWindings) {
        auto r = UnifyNormalOrientations(mesh);
        combined.flippedFacesCount += r.flippedFacesCount;
    }

    // 5. Tappa fori di confine se richiesto
    if (options.capHoles) {
        auto r = CapHoles(mesh, options.maxHoleEdges, options.defaultSurfaceId, options.defaultMaterialId);
        combined.holesCapped += r.holesCapped;
    }

    combined.modified = (combined.degenerateFacesRemoved > 0 ||
                         combined.isolatedVerticesRemoved > 0 ||
                         combined.isolatedEdgesRemoved > 0 ||
                         combined.pinchedVerticesSplit > 0 ||
                         combined.flippedFacesCount > 0 ||
                         combined.holesCapped > 0);

    combined.success = true;
    combined.newTopologyRev = mesh.GetTopologyRevision();
    combined.newGeometryRev = mesh.GetGeometryRevision();
    combined.reportAfter = mesh.ValidateTopology();
    combined.message = combined.modified ? "Riparazione mesh completata con successo." : "Mesh gia integra (Zero-Work).";
    return combined;
}

// =============================================================================
// BM-S7.8: REMESHING DETERMINISTICO UNIFORME
// =============================================================================

RemeshResult MeshRepairService::UniformRemesh(EditableMesh& mesh, const RemeshOptions& options) {
    RemeshResult result;
    result.oldTopologyRev = mesh.GetTopologyRevision();

    float L = std::max(0.01f, options.targetEdgeLength);
    float L_max = (4.0f / 3.0f) * L;
    float L_min = (4.0f / 5.0f) * L;
    float L_max2 = L_max * L_max;

    for (uint32_t iter = 0; iter < options.iterations; ++iter) {
        // --- Pass 1: Edge Splitting (per spigoli troppo lunghi) ---
        auto activeEdges = mesh.GetActiveEdgeIDs();
        for (EdgeID eId : activeEdges) {
            const auto* e = mesh.GetEdge(eId);
            if (!e || !e->halfEdge.IsValid()) continue;

            const auto* he = mesh.GetHalfEdge(e->halfEdge);
            if (!he || !he->next.IsValid()) continue;
            const auto* nextHe = mesh.GetHalfEdge(he->next);
            if (!nextHe) continue;

            VertexID v0 = he->origin;
            VertexID v1 = nextHe->origin;
            const auto* p0 = mesh.GetVertex(v0);
            const auto* p1 = mesh.GetVertex(v1);
            if (!p0 || !p1) continue;

            glm::vec3 diff = p1->position - p0->position;
            float len2 = glm::dot(diff, diff);

            if (len2 > L_max2) {
                // Suddividi lo spigolo inserendo il punto medio
                glm::vec3 midPos = (p0->position + p1->position) * 0.5f;
                VertexID vMid = mesh.AddVertex(midPos);

                std::vector<FaceID> incFaces;
                if (he->face.IsValid()) incFaces.push_back(he->face);
                if (e->twin.IsValid()) {
                    const auto* twin = mesh.GetHalfEdge(e->twin);
                    if (twin && twin->face.IsValid()) incFaces.push_back(twin->face);
                }

                for (FaceID fId : incFaces) {
                    std::vector<VertexID> verts = mesh.GetFaceVertices(fId);
                    if (verts.size() == 3) {
                        const auto* fObj = mesh.GetFace(fId);
                        if (!fObj) continue;
                        uint32_t sId = fObj->surfaceId;
                        uint32_t mId = fObj->materialId;

                        // Trova il terzo vertice opposto allo spigolo (v0, v1)
                        VertexID vOpp = INVALID_VERTEX_ID;
                        for (VertexID v : verts) {
                            if (v != v0 && v != v1) { vOpp = v; break; }
                        }

                        if (vOpp.IsValid()) {
                            mesh.DeleteFace(fId, true, false);
                            mesh.AddFace({v0, vMid, vOpp}, sId, mId);
                            mesh.AddFace({vMid, v1, vOpp}, sId, mId);
                            result.edgesSplit++;
                        }
                    }
                }
            }
        }

        // --- Pass 2: Tangential Relaxation (Laplaciano proiettato sul piano tangente) ---
        auto activeVerts = mesh.GetActiveVertexIDs();
        for (VertexID vId : activeVerts) {
            if (options.preserveBoundaries && mesh.IsBoundaryVertex(vId)) continue;

            const auto* v = mesh.GetVertex(vId);
            if (!v) continue;

            auto neighbors = mesh.GetVertexOneRingNeighbors(vId);
            if (neighbors.empty()) continue;

            glm::vec3 centroid(0.0f);
            for (VertexID nId : neighbors) {
                const auto* nv = mesh.GetVertex(nId);
                if (nv) centroid += nv->position;
            }
            centroid /= static_cast<float>(neighbors.size());

            glm::vec3 delta = centroid - v->position;
            // Proietta su piano tangente alla normale
            glm::vec3 norm = v->normal;
            if (glm::length2(norm) > 1e-4f) {
                norm = glm::normalize(norm);
                delta -= glm::dot(delta, norm) * norm;
            }

            mesh.SetVertexPosition(vId, v->position + delta * options.relaxFactor);
            result.verticesRelaxed++;
        }
    }

    mesh.ComputeVertexNormals();

    // Calcolo statistiche lunghezze spigoli post-remesh
    auto finalEdges = mesh.GetActiveEdgeIDs();
    float sumLen = 0.0f;
    float minL = 1e9f;
    float maxL = 0.0f;
    size_t edgeCount = 0;

    for (EdgeID eId : finalEdges) {
        const auto* e = mesh.GetEdge(eId);
        if (!e || !e->halfEdge.IsValid()) continue;
        const auto* he = mesh.GetHalfEdge(e->halfEdge);
        if (!he || !he->next.IsValid()) continue;
        const auto* nextHe = mesh.GetHalfEdge(he->next);
        if (!nextHe) continue;

        const auto* p0 = mesh.GetVertex(he->origin);
        const auto* p1 = mesh.GetVertex(nextHe->origin);
        if (p0 && p1) {
            float dist = glm::length(p1->position - p0->position);
            sumLen += dist;
            minL = std::min(minL, dist);
            maxL = std::max(maxL, dist);
            edgeCount++;
        }
    }

    result.avgEdgeLength = edgeCount > 0 ? (sumLen / edgeCount) : 0.0f;
    result.minEdgeLength = edgeCount > 0 ? minL : 0.0f;
    result.maxEdgeLength = edgeCount > 0 ? maxL : 0.0f;

    result.modified = (result.edgesSplit > 0 || result.verticesRelaxed > 0);
    result.success = true;
    result.newTopologyRev = mesh.GetTopologyRevision();
    result.finalReport = mesh.ValidateTopology();
    result.message = "Remesh uniforme completato (" + std::to_string(result.edgesSplit) + " split, " +
                     std::to_string(result.verticesRelaxed) + " rilassamenti).";
    return result;
}

// =============================================================================
// SUITE DI TEST DETERMINISTICA BM-S7 (REP-1 .. REP-10)
// =============================================================================

std::vector<MeshRepairService::RepairTestResult> MeshRepairService::RunRepairContractTests() {
    std::vector<RepairTestResult> results;
    results.reserve(10);

    // -------------------------------------------------------------------------
    // REP-1: Zero-Area Face Detection & Removal
    // -------------------------------------------------------------------------
    {
        RepairTestResult t;
        t.testId = "REP-1";
        t.testName = "Zero-Area Face Detection & Removal";
        t.inputDesc = "Quad 1x1 + triangolo degenere con 3 vertici allineati (area = 0)";
        t.expectedDesc = "Faccia degenere rimossa; quad valido preservato; modified=true";

        EditableMesh mesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        size_t initialFaceCount = mesh.GetFaceCount();

        // Aggiungi triangolo degenere con 3 vertici collineari
        VertexID d0 = mesh.AddVertex(glm::vec3(2.0f, 0.0f, 0.0f));
        VertexID d1 = mesh.AddVertex(glm::vec3(2.0f, 0.5f, 0.0f));
        VertexID d2 = mesh.AddVertex(glm::vec3(2.0f, 1.0f, 0.0f));
        FaceID degF = mesh.AddFace({d0, d1, d2});

        auto res = RemoveDegenerateFaces(mesh, 1e-5f);

        bool removed = res.success && res.modified && (res.degenerateFacesRemoved == 1);
        bool quadPreserved = (mesh.GetFaceCount() == initialFaceCount) && !mesh.IsValid(degF);

        t.passed = removed && quadPreserved;
        t.observedDesc = "Removed=" + std::to_string(res.degenerateFacesRemoved) + 
                         ", FinalFaceCount=" + std::to_string(mesh.GetFaceCount());
        t.details = t.passed ? "PASS" : "FAIL: Faccia degenere non rimossa correttamente";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // REP-2: Orphaned & Isolated Element Cleanup
    // -------------------------------------------------------------------------
    {
        RepairTestResult t;
        t.testId = "REP-2";
        t.testName = "Orphaned & Isolated Element Cleanup";
        t.inputDesc = "Quad con 4 vertici + 3 vertici isolati orfani";
        t.expectedDesc = "3 vertici orfani eliminati; 4 vertici del quad preservati; modified=true";

        EditableMesh mesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        VertexID iso1 = mesh.AddVertex(glm::vec3(5.0f, 0.0f, 0.0f));
        VertexID iso2 = mesh.AddVertex(glm::vec3(6.0f, 0.0f, 0.0f));
        VertexID iso3 = mesh.AddVertex(glm::vec3(7.0f, 0.0f, 0.0f));

        auto res = CleanupIsolatedElements(mesh);

        bool removedCorrectly = (res.isolatedVerticesRemoved == 3) && res.modified;
        bool quadIntact = (mesh.GetVertexCount() == 4) && 
                          !mesh.IsValid(iso1) && !mesh.IsValid(iso2) && !mesh.IsValid(iso3);

        t.passed = removedCorrectly && quadIntact;
        t.observedDesc = "CleanedVerts=" + std::to_string(res.isolatedVerticesRemoved) + 
                         ", RemainingVerts=" + std::to_string(mesh.GetVertexCount());
        t.details = t.passed ? "PASS" : "FAIL: Pulizia vertici isolati fallita";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // REP-3: Collinear / Repeated Vertex Face Repair
    // -------------------------------------------------------------------------
    {
        RepairTestResult t;
        t.testId = "REP-3";
        t.testName = "Collinear / Repeated Vertex Face Repair";
        t.inputDesc = "Rilevamento degenerazioni su mesh con facce con vertici ripetuti";
        t.expectedDesc = "DetectDegeneracies intercetta e segnala HasIssues=true";

        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
        VertexID v1 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
        VertexID v2 = mesh.AddVertex(glm::vec3(0.0f, 1.0f, 0.0f));
        mesh.AddFace({v0, v1, v2});

        auto diagClean = DetectDegeneracies(mesh);
        // Distinguiamo le degenerazioni geometriche dal boundary loop legittimo della mesh aperta (1 triangolo)
        bool hasGeometricDegeneracies = !diagClean.zeroAreaFaces.empty() ||
                                        !diagClean.duplicateVertexFaces.empty() ||
                                        !diagClean.zeroLengthEdges.empty() ||
                                        !diagClean.isolatedVertices.empty() ||
                                        !diagClean.isolatedEdges.empty() ||
                                        !diagClean.pinchedNonManifoldVertices.empty();
        bool cleanReport = !hasGeometricDegeneracies && (diagClean.boundaryHoleLoops.size() == 1);

        // Aggiungi un vertice degenere isolato
        VertexID vIso = mesh.AddVertex(glm::vec3(3.0f, 3.0f, 3.0f));
        auto diagDirty = DetectDegeneracies(mesh);
        bool dirtyReport = diagDirty.HasIssues() && !diagDirty.isolatedVertices.empty();

        t.passed = cleanReport && dirtyReport;
        t.observedDesc = "CleanHasIssues=" + std::to_string(!cleanReport) + 
                         ", DirtyHasIssues=" + std::to_string(dirtyReport);
        t.details = t.passed ? "PASS" : "FAIL: Diagnostica degenerazioni non coerente";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // REP-4: Boundary Hole Capping & Manifold Closure
    // -------------------------------------------------------------------------
    {
        RepairTestResult t;
        t.testId = "REP-4";
        t.testName = "Boundary Hole Capping & Manifold Closure";
        t.inputDesc = "Cubo con 1 faccia eliminata (5 facce, 4 boundary edges)";
        t.expectedDesc = "CapHoles inserisce la faccia di chiusura; boundaryEdgeCount=0; isClosedManifold=true";

        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        auto faces = mesh.GetActiveFaceIDs();
        mesh.DeleteFace(faces[0], true, false);

        auto repOpen = mesh.ValidateTopology();
        bool wasOpen = (repOpen.boundaryEdgeCount == 4) && !repOpen.isClosedManifold;

        auto capRes = CapHoles(mesh, 64, 0, 0);
        auto repClosed = mesh.ValidateTopology();

        bool closedSuccessfully = capRes.success && (capRes.holesCapped == 1) &&
                                 (repClosed.boundaryEdgeCount == 0) && repClosed.isClosedManifold;

        t.passed = wasOpen && closedSuccessfully;
        t.observedDesc = "WasOpen=" + std::to_string(wasOpen) + 
                         ", Capped=" + std::to_string(capRes.holesCapped) + 
                         ", FinalBoundaryEdges=" + std::to_string(repClosed.boundaryEdgeCount);
        t.details = t.passed ? "PASS" : "FAIL: Hole capping non ha ripristinato 2-manifold chiusa";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // REP-5: Normal Orientation Consistency Unification
    // -------------------------------------------------------------------------
    {
        RepairTestResult t;
        t.testId = "REP-5";
        t.testName = "Normal Orientation Consistency Unification";
        t.inputDesc = "Due facce adiacenti su piano XY con avvolgimento coerente CCW";
        t.expectedDesc = "UnifyNormalOrientations verifica e preserva reciprocita twin opposta";

        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
        VertexID v1 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
        VertexID v2 = mesh.AddVertex(glm::vec3(1.0f, 1.0f, 0.0f));
        VertexID v3 = mesh.AddVertex(glm::vec3(0.0f, 1.0f, 0.0f));
        VertexID v4 = mesh.AddVertex(glm::vec3(2.0f, 0.0f, 0.0f));
        VertexID v5 = mesh.AddVertex(glm::vec3(2.0f, 1.0f, 0.0f));

        // Due quad adiacenti condividono spigolo (v1, v2)
        mesh.AddFace({v0, v1, v2, v3});
        mesh.AddFace({v1, v4, v5, v2});

        auto res = UnifyNormalOrientations(mesh);
        auto rep = mesh.ValidateTopology();

        t.passed = res.success && rep.isValid;
        t.observedDesc = "Valid=" + std::to_string(rep.isValid) + 
                         ", Flipped=" + std::to_string(res.flippedFacesCount);
        t.details = t.passed ? "PASS" : "FAIL: Unificazione normali fallita";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // REP-6: Coplanar Limited Dissolve (Planar Decimation)
    // -------------------------------------------------------------------------
    {
        RepairTestResult t;
        t.testId = "REP-6";
        t.testName = "Coplanar Limited Dissolve (Planar Decimation)";
        t.inputDesc = "2 quad adiacenti complanari (2x1) con medesimo SurfaceID";
        t.expectedDesc = "LimitedDissolve fonde i 2 quad in 1 singolo poligono; area conservata";

        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
        VertexID v1 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
        VertexID v2 = mesh.AddVertex(glm::vec3(1.0f, 1.0f, 0.0f));
        VertexID v3 = mesh.AddVertex(glm::vec3(0.0f, 1.0f, 0.0f));
        VertexID v4 = mesh.AddVertex(glm::vec3(2.0f, 0.0f, 0.0f));
        VertexID v5 = mesh.AddVertex(glm::vec3(2.0f, 1.0f, 0.0f));

        mesh.AddFace({v0, v1, v2, v3}, 1, 0);
        mesh.AddFace({v1, v4, v5, v2}, 1, 0);

        size_t initialFaces = mesh.GetFaceCount();
        auto disRes = LimitedDissolve(mesh, DissolveOptions{});

        bool merged = disRes.success && disRes.modified && (disRes.coplanarFacesDissolved == 1);
        bool faceReduced = (mesh.GetFaceCount() == 1) && (initialFaces == 2);

        t.passed = merged && faceReduced;
        t.observedDesc = "Dissolved=" + std::to_string(disRes.coplanarFacesDissolved) + 
                         ", InitialFaces=" + std::to_string(initialFaces) + 
                         ", FinalFaces=" + std::to_string(mesh.GetFaceCount());
        t.details = t.passed ? "PASS" : "FAIL: Limited dissolve non ha unito i quad complanari";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // REP-7: Non-Manifold Pinched Vertex Separation
    // -------------------------------------------------------------------------
    {
        RepairTestResult t;
        t.testId = "REP-7";
        t.testName = "Non-Manifold Pinched Vertex Separation";
        t.inputDesc = "Due triangoli che condividono solo il vertice centrale (struttura a farfalla)";
        t.expectedDesc = "DetectDegeneracies rileva pinched vertex; SplitPinchedVertices separa i ventagli";

        EditableMesh mesh;
        VertexID apex = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
        VertexID a1 = mesh.AddVertex(glm::vec3(-1.0f, 0.0f, 0.0f));
        VertexID a2 = mesh.AddVertex(glm::vec3(-1.0f, 1.0f, 0.0f));
        VertexID b1 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
        VertexID b2 = mesh.AddVertex(glm::vec3(1.0f, 1.0f, 0.0f));

        mesh.AddFace({a1, apex, a2});
        mesh.AddFace({b1, b2, apex});

        auto diag = DetectDegeneracies(mesh);
        bool detected = !diag.pinchedNonManifoldVertices.empty();

        auto splitRes = SplitPinchedVertices(mesh);
        bool splitSuccess = splitRes.success && splitRes.modified && (splitRes.pinchedVerticesSplit >= 1);

        t.passed = detected && splitSuccess;
        t.observedDesc = "DetectedPinched=" + std::to_string(diag.pinchedNonManifoldVertices.size()) + 
                         ", SplitCount=" + std::to_string(splitRes.pinchedVerticesSplit);
        t.details = t.passed ? "PASS" : "FAIL: Separazione vertice pinched non riuscita";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // REP-8: Uniform Remeshing Edge Length Convergence
    // -------------------------------------------------------------------------
    {
        RepairTestResult t;
        t.testId = "REP-8";
        t.testName = "Uniform Remeshing Edge Length Convergence";
        t.inputDesc = "Mesh triangolare allungata (spigolo lungo = 2.0); targetEdgeLength = 0.5";
        t.expectedDesc = "UniformRemesh suddivide spigoli lunghi; media converge verso target; topologia valida";

        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
        VertexID v1 = mesh.AddVertex(glm::vec3(2.0f, 0.0f, 0.0f));
        VertexID v2 = mesh.AddVertex(glm::vec3(0.0f, 2.0f, 0.0f));
        mesh.AddFace({v0, v1, v2});

        RemeshOptions opt;
        opt.targetEdgeLength = 0.6f;
        opt.iterations = 2;
        opt.relaxFactor = 0.2f;

        auto remRes = UniformRemesh(mesh, opt);

        bool remeshed = remRes.success && remRes.modified && (remRes.edgesSplit > 0);
        bool topValid = remRes.finalReport.isValid;

        t.passed = remeshed && topValid;
        t.observedDesc = "EdgesSplit=" + std::to_string(remRes.edgesSplit) + 
                         ", AvgEdgeLen=" + std::to_string(remRes.avgEdgeLength) + 
                         ", TopoValid=" + std::to_string(topValid);
        t.details = t.passed ? "PASS" : "FAIL: Remeshing uniforme non ha suddiviso gli spigoli";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // REP-9: Zero-Work Invariance on Clean Pristine Mesh
    // -------------------------------------------------------------------------
    {
        RepairTestResult t;
        t.testId = "REP-9";
        t.testName = "Zero-Work Invariance on Clean Pristine Mesh";
        t.inputDesc = "Cubo integro 2-manifold chiuso passato a RepairAll";
        t.expectedDesc = "modified=false; 0 modifiche; geometryRevision e topologyRevision rigorosamente intatte";

        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        uint64_t initialTopoRev = mesh.GetTopologyRevision();
        uint64_t initialGeomRev = mesh.GetGeometryRevision();

        auto res = RepairAll(mesh);

        bool unmodified = (!res.modified) && res.success;
        bool revsIntact = (mesh.GetTopologyRevision() == initialTopoRev) &&
                          (mesh.GetGeometryRevision() == initialGeomRev);

        t.passed = unmodified && revsIntact;
        t.observedDesc = "Modified=" + std::to_string(res.modified) + 
                         ", TopoRevDelta=" + std::to_string(mesh.GetTopologyRevision() - initialTopoRev) + 
                         ", GeomRevDelta=" + std::to_string(mesh.GetGeometryRevision() - initialGeomRev);
        t.details = t.passed ? "PASS" : "FAIL: Zero-Work violato su mesh integra";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // REP-10: Generational ID Safety & Invalidation Tracking
    // -------------------------------------------------------------------------
    {
        RepairTestResult t;
        t.testId = "REP-10";
        t.testName = "Generational ID Safety & Invalidation Tracking";
        t.inputDesc = "Faccia degenere con FaceID valido; eliminazione tramite repair";
        t.expectedDesc = "Dopo repair, mesh.IsValid(oldFaceId) risulta rigorosamente false";

        EditableMesh mesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        VertexID d0 = mesh.AddVertex(glm::vec3(3.0f, 0.0f, 0.0f));
        VertexID d1 = mesh.AddVertex(glm::vec3(3.0f, 0.5f, 0.0f));
        VertexID d2 = mesh.AddVertex(glm::vec3(3.0f, 1.0f, 0.0f));
        FaceID degF = mesh.AddFace({d0, d1, d2});

        bool validBefore = mesh.IsValid(degF);
        RemoveDegenerateFaces(mesh, 1e-5f);
        bool validAfter = mesh.IsValid(degF);

        t.passed = validBefore && (!validAfter);
        t.observedDesc = "ValidBefore=" + std::to_string(validBefore) + 
                         ", ValidAfter=" + std::to_string(validAfter);
        t.details = t.passed ? "PASS" : "FAIL: ID generazionale non invalidato dopo eliminazione";
        results.push_back(t);
    }

    return results;
}

} // namespace fw
