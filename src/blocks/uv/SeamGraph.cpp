#include "pch.h"
#include "SeamGraph.h"
#include <queue>
#include <cmath>
#include <algorithm>
#include <glm/gtc/constants.hpp>

namespace fw {

void SeamGraph::Reset() {
    m_edgeMap.clear();
    m_seamEdges.clear();
    m_boundaryEdges.clear();
    m_manualSeamEdges.clear();
    m_autoSeamEdges.clear();
    m_diagnostics.clear();
}

bool SeamGraph::BuildFromMesh(const EditableMesh& mesh, const SeamOptions& options) {
    Reset();

    if (mesh.GetEdgeCount() == 0 || mesh.GetFaceCount() == 0) {
        m_diagnostics.push_back("SeamGraph: Mesh priva di spigoli o facce.");
        return false;
    }

    // 1. Rileva confini aperti geometrici
    DetectBoundaries(mesh);

    // 2. Rileva cuciture manuali preesistenti
    DetectManualSeams(mesh);

    // 3. Rileva spigoli vivi, cambi superficie e soglie di curvatura diedrica
    DetectFeatureSeams(mesh, options);

    // 4. Se richiesto, taglia componenti chiuse per renderle sviluppabili su disco
    if (options.autoCutClosedComponents) {
        CutClosedManifoldComponents(mesh, options);
    }

    // Costruisci le liste indicizzate
    for (const auto& [eId, info] : m_edgeMap) {
        if (info.isBoundary) {
            m_boundaryEdges.push_back(eId);
        }
        if (info.type != SeamType::None && info.type != SeamType::OpenBoundary) {
            m_seamEdges.push_back(eId);
            if (info.isManual) {
                m_manualSeamEdges.push_back(eId);
            } else {
                m_autoSeamEdges.push_back(eId);
            }
        }
    }

    return true;
}

void SeamGraph::DetectBoundaries(const EditableMesh& mesh) {
    for (EdgeID eId : mesh.GetActiveEdgeIDs()) {
        const auto* e = mesh.GetEdge(eId);
        if (!e) continue;

        bool isBnd = false;
        if (!IsValid(e->halfEdge) || !IsValid(e->twin)) {
            isBnd = true;
        } else {
            const auto* h0 = mesh.GetHalfEdge(e->halfEdge);
            const auto* h1 = mesh.GetHalfEdge(e->twin);
            if (!h0 || !h1 || !IsValid(h0->face) || !IsValid(h1->face)) {
                isBnd = true;
            }
        }

        if (isBnd) {
            SeamEdgeInfo info;
            info.edgeId = eId;
            info.isBoundary = true;
            info.type = SeamType::OpenBoundary;
            m_edgeMap[eId] = info;
        }
    }
}

void SeamGraph::DetectManualSeams(const EditableMesh& mesh) {
    for (EdgeID eId : mesh.GetActiveEdgeIDs()) {
        const auto* e = mesh.GetEdge(eId);
        if (!e) continue;

        // SeamWeight >= 0.99f identifica una cucitura marcata esplicitamente dall'utente
        if (e->seamWeight >= 0.99f) {
            auto it = m_edgeMap.find(eId);
            if (it == m_edgeMap.end()) {
                SeamEdgeInfo info;
                info.edgeId = eId;
                info.isManual = true;
                info.type = SeamType::Manual;
                m_edgeMap[eId] = info;
            } else {
                it->second.isManual = true;
                if (!it->second.isBoundary) {
                    it->second.type = SeamType::Manual;
                }
            }
        }
    }
}

void SeamGraph::DetectFeatureSeams(const EditableMesh& mesh, const SeamOptions& options) {
    for (EdgeID eId : mesh.GetActiveEdgeIDs()) {
        const auto* e = mesh.GetEdge(eId);
        if (!e) continue;

        auto it = m_edgeMap.find(eId);
        if (it != m_edgeMap.end() && (it->second.isBoundary || it->second.isManual)) {
            continue; // Non sovrascrivere confini aperti o tagli manuali utente
        }

        const auto* h0 = mesh.GetHalfEdge(e->halfEdge);
        const auto* h1 = mesh.GetHalfEdge(e->twin);
        if (!h0 || !h1 || !IsValid(h0->face) || !IsValid(h1->face)) continue;

        const auto* f0 = mesh.GetFace(h0->face);
        const auto* f1 = mesh.GetFace(h1->face);
        if (!f0 || !f1) continue;

        bool markAsSeam = false;

        // 1. Spigolo vivo dichiarato nella mesh
        if (options.seamOnSharpEdges && e->isSharp) {
            markAsSeam = true;
        }

        // 2. Confine fra diverse superfici semantiche
        if (options.seamOnSurfaceBoundary && (f0->surfaceId != f1->surfaceId)) {
            markAsSeam = true;
        }

        // 3. Confine fra diversi materiali
        if (options.seamOnMaterialBoundary && (f0->materialId != f1->materialId)) {
            markAsSeam = true;
        }

        // 4. Analisi angolo diedrico fra le normali delle facce
        float dotNorm = glm::clamp(glm::dot(f0->faceNormal, f1->faceNormal), -1.0f, 1.0f);
        float angleDeg = glm::degrees(std::acos(dotNorm));

        if (angleDeg >= options.dihedralAngleThresholdDegrees) {
            markAsSeam = true;
        }

        if (markAsSeam) {
            SeamEdgeInfo info;
            info.edgeId = eId;
            info.type = SeamType::Automatic;
            info.dihedralAngleDegrees = angleDeg;
            info.isSharp = e->isSharp;
            m_edgeMap[eId] = info;
        }
    }
}

void SeamGraph::CutClosedManifoldComponents(const EditableMesh& mesh, const SeamOptions& options) {
    // Rileva componenti connesse di facce che non sono separate da cuciture o confini
    std::unordered_set<FaceID> visited;
    const auto& activeFaces = mesh.GetActiveFaceIDs();

    for (FaceID fId : activeFaces) {
        if (visited.count(fId)) continue;

        std::vector<FaceID> componentFaces;
        std::queue<FaceID> queue;
        queue.push(fId);
        visited.insert(fId);

        bool hasBoundaryOrSeamEdge = false;

        while (!queue.empty()) {
            FaceID cur = queue.front();
            queue.pop();
            componentFaces.push_back(cur);

            const auto* f = mesh.GetFace(cur);
            if (!f || !IsValid(f->halfEdge)) continue;

            HalfEdgeID startHe = f->halfEdge;
            HalfEdgeID curHe = startHe;
            do {
                const auto* he = mesh.GetHalfEdge(curHe);
                if (!he) break;

                EdgeID eId = he->edge;
                if (IsSeamOrBoundary(eId)) {
                    hasBoundaryOrSeamEdge = true;
                } else if (IsValid(he->twin)) {
                    const auto* twinHe = mesh.GetHalfEdge(he->twin);
                    if (twinHe && IsValid(twinHe->face)) {
                        FaceID neighborFace = twinHe->face;
                        if (visited.find(neighborFace) == visited.end()) {
                            visited.insert(neighborFace);
                            queue.push(neighborFace);
                        }
                    }
                }
                curHe = he->next;
            } while (curHe != startHe && IsValid(curHe));
        }

        // Se la componente e completamente priva di tagli (componente chiusa manifold, es. sfera liscia)
        if (!hasBoundaryOrSeamEdge && componentFaces.size() > 1) {
            // Trova i vertici estremi (min e max lungo l'asse Y o Z)
            VertexID minV = INVALID_VERTEX_ID;
            VertexID maxV = INVALID_VERTEX_ID;
            float minY = 1e20f, maxY = -1e20f;

            for (FaceID compF : componentFaces) {
                const auto* f = mesh.GetFace(compF);
                if (!f) continue;
                HalfEdgeID h = f->halfEdge;
                HalfEdgeID startH = h;
                do {
                    const auto* he = mesh.GetHalfEdge(h);
                    if (!he) break;
                    const auto* v = mesh.GetVertex(he->origin);
                    if (v) {
                        if (v->position.y < minY) { minY = v->position.y; minV = v->id; }
                        if (v->position.y > maxY) { maxY = v->position.y; maxV = v->id; }
                    }
                    h = he->next;
                } while (h != startH && IsValid(h));
            }

            if (IsValid(minV) && IsValid(maxV) && minV != maxV) {
                auto path = FindShortestEdgePath(mesh, minV, maxV);
                for (EdgeID pathEdge : path) {
                    auto& info = m_edgeMap[pathEdge];
                    info.edgeId = pathEdge;
                    info.type = SeamType::Automatic;
                }
            }
        }
    }
}

std::vector<EdgeID> SeamGraph::FindShortestEdgePath(const EditableMesh& mesh, VertexID startV, VertexID endV) const {
    if (!IsValid(startV) || !IsValid(endV) || startV == endV) return {};

    std::unordered_map<VertexID, EdgeID> edgeToVertex;
    std::unordered_map<VertexID, VertexID> parent;
    std::queue<VertexID> q;
    std::unordered_set<VertexID> visited;

    q.push(startV);
    visited.insert(startV);

    bool found = false;
    while (!q.empty()) {
        VertexID cur = q.front();
        q.pop();

        if (cur == endV) {
            found = true;
            break;
        }

        const auto* v = mesh.GetVertex(cur);
        if (!v || !IsValid(v->halfEdge)) continue;

        // Itera gli half-edge uscenti dal vertice
        HalfEdgeID startHe = v->halfEdge;
        HalfEdgeID curHe = startHe;
        do {
            const auto* he = mesh.GetHalfEdge(curHe);
            if (!he) break;

            if (IsValid(he->next)) {
                const auto* nextHe = mesh.GetHalfEdge(he->next);
                if (nextHe && IsValid(nextHe->origin)) {
                    VertexID neighbor = nextHe->origin;
                    if (visited.find(neighbor) == visited.end()) {
                        visited.insert(neighbor);
                        parent[neighbor] = cur;
                        edgeToVertex[neighbor] = he->edge;
                        q.push(neighbor);
                    }
                }
            }

            // Ruota attorno al vertice tramite twin->next
            if (IsValid(he->twin)) {
                const auto* twinHe = mesh.GetHalfEdge(he->twin);
                if (twinHe) curHe = twinHe->next;
                else break;
            } else {
                break;
            }
        } while (curHe != startHe && IsValid(curHe));
    }

    if (!found) return {};

    std::vector<EdgeID> result;
    VertexID curr = endV;
    while (curr != startV) {
        result.push_back(edgeToVertex[curr]);
        curr = parent[curr];
    }
    std::reverse(result.begin(), result.end());
    return result;
}

bool SeamGraph::IsSeamOrBoundary(EdgeID edgeId) const {
    auto it = m_edgeMap.find(edgeId);
    if (it == m_edgeMap.end()) return false;
    return it->second.type != SeamType::None;
}

bool SeamGraph::IsSeam(EdgeID edgeId) const {
    auto it = m_edgeMap.find(edgeId);
    if (it == m_edgeMap.end()) return false;
    return it->second.type != SeamType::None && it->second.type != SeamType::OpenBoundary;
}

bool SeamGraph::IsBoundary(EdgeID edgeId) const {
    auto it = m_edgeMap.find(edgeId);
    if (it == m_edgeMap.end()) return false;
    return it->second.isBoundary;
}

SeamType SeamGraph::GetSeamType(EdgeID edgeId) const {
    auto it = m_edgeMap.find(edgeId);
    if (it == m_edgeMap.end()) return SeamType::None;
    return it->second.type;
}

const SeamEdgeInfo* SeamGraph::GetEdgeInfo(EdgeID edgeId) const {
    auto it = m_edgeMap.find(edgeId);
    if (it == m_edgeMap.end()) return nullptr;
    return &it->second;
}

void SeamGraph::ApplySeamsToMesh(EditableMesh& mesh) const {
    for (const auto& [eId, info] : m_edgeMap) {
        auto* e = mesh.GetEdgeMutable(eId);
        if (!e) continue;

        if (info.isManual) {
            e->seamWeight = 1.0f;
        } else if (info.type == SeamType::Automatic) {
            if (e->seamWeight < 0.99f) {
                e->seamWeight = 0.5f;
            }
        }
    }
}

void SeamGraph::SetManualSeam(EditableMesh& mesh, EdgeID edgeId, bool isSeam) {
    auto* e = mesh.GetEdgeMutable(edgeId);
    if (e) {
        e->seamWeight = isSeam ? 1.0f : 0.0f;
    }
}

void SeamGraph::ClearAutomaticSeams(EditableMesh& mesh) {
    for (EdgeID eId : mesh.GetActiveEdgeIDs()) {
        auto* e = mesh.GetEdgeMutable(eId);
        if (e && e->seamWeight > 0.0f && e->seamWeight < 0.99f) {
            e->seamWeight = 0.0f;
        }
    }
}

} // namespace fw
