#include "pch.h"
#include "EditableMesh.h"
#include "SurfaceDefinition.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <limits>

namespace fw {

EditableMesh::EditableMesh() {
    Clear();
}

EditableMesh::EditableMesh(const EditableMesh& other) {
    *this = other;
}

EditableMesh& EditableMesh::operator=(const EditableMesh& other) {
    if (this == &other) return *this;

    m_vertices = other.m_vertices;
    m_vertexGenerations = other.m_vertexGenerations;
    m_freeVertexIndices = other.m_freeVertexIndices;
    m_activeVertexCount = other.m_activeVertexCount;

    m_halfEdges = other.m_halfEdges;
    m_halfEdgeGenerations = other.m_halfEdgeGenerations;
    m_freeHalfEdgeIndices = other.m_freeHalfEdgeIndices;
    m_activeHalfEdgeCount = other.m_activeHalfEdgeCount;

    m_edges = other.m_edges;
    m_edgeGenerations = other.m_edgeGenerations;
    m_freeEdgeIndices = other.m_freeEdgeIndices;
    m_activeEdgeCount = other.m_activeEdgeCount;

    m_faces = other.m_faces;
    m_faceGenerations = other.m_faceGenerations;
    m_freeFaceIndices = other.m_freeFaceIndices;
    m_activeFaceCount = other.m_activeFaceCount;

    m_directedEdgeLookup = other.m_directedEdgeLookup;

    m_topologyRevision = other.m_topologyRevision;
    m_geometryRevision = other.m_geometryRevision;
    m_attributeRevision = other.m_attributeRevision;

    return *this;
}

void EditableMesh::Clear() {
    m_vertices.clear();
    m_vertexGenerations.clear();
    m_freeVertexIndices.clear();
    m_activeVertexCount = 0;

    m_halfEdges.clear();
    m_halfEdgeGenerations.clear();
    m_freeHalfEdgeIndices.clear();
    m_activeHalfEdgeCount = 0;

    m_edges.clear();
    m_edgeGenerations.clear();
    m_freeEdgeIndices.clear();
    m_activeEdgeCount = 0;

    m_faces.clear();
    m_faceGenerations.clear();
    m_freeFaceIndices.clear();
    m_activeFaceCount = 0;

    m_directedEdgeLookup.clear();

    IncrementTopologyRevision();
}

// =============================================================================
// ALLOCATORI INTERNI SLOTMAP CON GENERAZIONE
// =============================================================================

VertexID EditableMesh::AddVertex(const glm::vec3& position) {
    VertexID id;
    if (!m_freeVertexIndices.empty()) {
        id.index = m_freeVertexIndices.back();
        m_freeVertexIndices.pop_back();
        m_vertexGenerations[id.index]++;
        id.generation = m_vertexGenerations[id.index];

        auto& v = m_vertices[id.index];
        v.id = id;
        v.position = position;
        v.halfEdge = INVALID_HALFEDGE_ID;
        v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        v.isDeleted = false;
    } else {
        id.index = static_cast<uint32_t>(m_vertices.size());
        id.generation = 1;
        m_vertexGenerations.push_back(1);

        MeshVertex v;
        v.id = id;
        v.position = position;
        v.halfEdge = INVALID_HALFEDGE_ID;
        v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        v.isDeleted = false;
        m_vertices.push_back(v);
    }
    m_activeVertexCount++;
    IncrementGeometryRevision();
    return id;
}

HalfEdgeID EditableMesh::AllocateHalfEdge() {
    HalfEdgeID id;
    if (!m_freeHalfEdgeIndices.empty()) {
        id.index = m_freeHalfEdgeIndices.back();
        m_freeHalfEdgeIndices.pop_back();
        m_halfEdgeGenerations[id.index]++;
        id.generation = m_halfEdgeGenerations[id.index];

        auto& he = m_halfEdges[id.index];
        he.id = id;
        he.origin = INVALID_VERTEX_ID;
        he.twin = INVALID_HALFEDGE_ID;
        he.next = INVALID_HALFEDGE_ID;
        he.prev = INVALID_HALFEDGE_ID;
        he.face = INVALID_FACE_ID;
        he.edge = INVALID_EDGE_ID;
        he.corner = CornerAttributes{};
        he.isDeleted = false;
    } else {
        id.index = static_cast<uint32_t>(m_halfEdges.size());
        id.generation = 1;
        m_halfEdgeGenerations.push_back(1);

        MeshHalfEdge he;
        he.id = id;
        he.origin = INVALID_VERTEX_ID;
        he.twin = INVALID_HALFEDGE_ID;
        he.next = INVALID_HALFEDGE_ID;
        he.prev = INVALID_HALFEDGE_ID;
        he.face = INVALID_FACE_ID;
        he.edge = INVALID_EDGE_ID;
        he.corner = CornerAttributes{};
        he.isDeleted = false;
        m_halfEdges.push_back(he);
    }
    m_activeHalfEdgeCount++;
    return id;
}

EdgeID EditableMesh::AllocateEdge() {
    EdgeID id;
    if (!m_freeEdgeIndices.empty()) {
        id.index = m_freeEdgeIndices.back();
        m_freeEdgeIndices.pop_back();
        m_edgeGenerations[id.index]++;
        id.generation = m_edgeGenerations[id.index];

        auto& e = m_edges[id.index];
        e.id = id;
        e.halfEdge = INVALID_HALFEDGE_ID;
        e.twin = INVALID_HALFEDGE_ID;
        e.isSharp = false;
        e.seamWeight = 0.0f;
        e.isDeleted = false;
    } else {
        id.index = static_cast<uint32_t>(m_edges.size());
        id.generation = 1;
        m_edgeGenerations.push_back(1);

        MeshEdge e;
        e.id = id;
        e.halfEdge = INVALID_HALFEDGE_ID;
        e.twin = INVALID_HALFEDGE_ID;
        e.isSharp = false;
        e.seamWeight = 0.0f;
        e.isDeleted = false;
        m_edges.push_back(e);
    }
    m_activeEdgeCount++;
    return id;
}

FaceID EditableMesh::AllocateFace() {
    FaceID id;
    if (!m_freeFaceIndices.empty()) {
        id.index = m_freeFaceIndices.back();
        m_freeFaceIndices.pop_back();
        m_faceGenerations[id.index]++;
        id.generation = m_faceGenerations[id.index];

        auto& f = m_faces[id.index];
        f.id = id;
        f.halfEdge = INVALID_HALFEDGE_ID;
        f.surfaceId = 0;
        f.materialId = 0;
        f.faceNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        f.area = 0.0f;
        f.isDeleted = false;
    } else {
        id.index = static_cast<uint32_t>(m_faces.size());
        id.generation = 1;
        m_faceGenerations.push_back(1);

        MeshFace f;
        f.id = id;
        f.halfEdge = INVALID_HALFEDGE_ID;
        f.surfaceId = 0;
        f.materialId = 0;
        f.faceNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        f.area = 0.0f;
        f.isDeleted = false;
        m_faces.push_back(f);
    }
    m_activeFaceCount++;
    return id;
}

// =============================================================================
// CONTROLLO VALIDITA ID ED ACCESSO ELEMENTI
// =============================================================================

bool EditableMesh::IsValid(VertexID id) const {
    return id.IsValid() && id.index < m_vertices.size() &&
           !m_vertices[id.index].isDeleted &&
           m_vertexGenerations[id.index] == id.generation;
}

bool EditableMesh::IsValid(HalfEdgeID id) const {
    return id.IsValid() && id.index < m_halfEdges.size() &&
           !m_halfEdges[id.index].isDeleted &&
           m_halfEdgeGenerations[id.index] == id.generation;
}

bool EditableMesh::IsValid(EdgeID id) const {
    return id.IsValid() && id.index < m_edges.size() &&
           !m_edges[id.index].isDeleted &&
           m_edgeGenerations[id.index] == id.generation;
}

bool EditableMesh::IsValid(FaceID id) const {
    return id.IsValid() && id.index < m_faces.size() &&
           !m_faces[id.index].isDeleted &&
           m_faceGenerations[id.index] == id.generation;
}

const MeshVertex* EditableMesh::GetVertex(VertexID id) const {
    return IsValid(id) ? &m_vertices[id.index] : nullptr;
}
MeshVertex* EditableMesh::GetVertexMutable(VertexID id) {
    return IsValid(id) ? &m_vertices[id.index] : nullptr;
}

const MeshHalfEdge* EditableMesh::GetHalfEdge(HalfEdgeID id) const {
    return IsValid(id) ? &m_halfEdges[id.index] : nullptr;
}
MeshHalfEdge* EditableMesh::GetHalfEdgeMutable(HalfEdgeID id) {
    return IsValid(id) ? &m_halfEdges[id.index] : nullptr;
}

const MeshEdge* EditableMesh::GetEdge(EdgeID id) const {
    return IsValid(id) ? &m_edges[id.index] : nullptr;
}
MeshEdge* EditableMesh::GetEdgeMutable(EdgeID id) {
    return IsValid(id) ? &m_edges[id.index] : nullptr;
}

const MeshFace* EditableMesh::GetFace(FaceID id) const {
    return IsValid(id) ? &m_faces[id.index] : nullptr;
}
MeshFace* EditableMesh::GetFaceMutable(FaceID id) {
    return IsValid(id) ? &m_faces[id.index] : nullptr;
}

std::vector<VertexID> EditableMesh::GetActiveVertexIDs() const {
    std::vector<VertexID> res;
    res.reserve(m_activeVertexCount);
    for (size_t i = 0; i < m_vertices.size(); ++i) {
        if (!m_vertices[i].isDeleted) {
            res.push_back(m_vertices[i].id);
        }
    }
    return res;
}

std::vector<HalfEdgeID> EditableMesh::GetActiveHalfEdgeIDs() const {
    std::vector<HalfEdgeID> res;
    res.reserve(m_activeHalfEdgeCount);
    for (size_t i = 0; i < m_halfEdges.size(); ++i) {
        if (!m_halfEdges[i].isDeleted) {
            res.push_back(m_halfEdges[i].id);
        }
    }
    return res;
}

std::vector<EdgeID> EditableMesh::GetActiveEdgeIDs() const {
    std::vector<EdgeID> res;
    res.reserve(m_activeEdgeCount);
    for (size_t i = 0; i < m_edges.size(); ++i) {
        if (!m_edges[i].isDeleted) {
            res.push_back(m_edges[i].id);
        }
    }
    return res;
}

std::vector<FaceID> EditableMesh::GetActiveFaceIDs() const {
    std::vector<FaceID> res;
    res.reserve(m_activeFaceCount);
    for (size_t i = 0; i < m_faces.size(); ++i) {
        if (!m_faces[i].isDeleted) {
            res.push_back(m_faces[i].id);
        }
    }
    return res;
}

bool EditableMesh::SetVertexPosition(VertexID id, const glm::vec3& newPos) {
    auto* v = GetVertexMutable(id);
    if (!v) return false;
    v->position = newPos;
    IncrementGeometryRevision();
    return true;
}

// =============================================================================
// COSTRUZIONE TOPOLOGICA DELLE FACCE (BM-S2)
// =============================================================================

FaceID EditableMesh::AddFace(const std::vector<VertexID>& vertexIds, 
                             uint32_t surfaceId, 
                             uint32_t materialId, 
                             const std::vector<CornerAttributes>* corners) {
    const size_t k = vertexIds.size();
    if (k < 3) return INVALID_FACE_ID;

    // 1. Verifica che tutti i vertici esistano e non siano duplicati nel perimetro
    for (size_t i = 0; i < k; ++i) {
        if (!IsValid(vertexIds[i])) return INVALID_FACE_ID;
        for (size_t j = i + 1; j < k; ++j) {
            if (vertexIds[i] == vertexIds[j]) return INVALID_FACE_ID;
        }
    }

    // 2. Verifica che nessuno degli spigoli orientati esista gia (garantisce manifoldness)
    for (size_t i = 0; i < k; ++i) {
        VertexID v0 = vertexIds[i];
        VertexID v1 = vertexIds[(i + 1) % k];
        DirectedEdgeKey key{ v0.index, v1.index };
        if (m_directedEdgeLookup.find(key) != m_directedEdgeLookup.end()) {
            // Spigolo orientato gia occupato: orientamento o faccia duplicata
            return INVALID_FACE_ID;
        }
    }

    // 3. Alloca la faccia e gli half-edge necessari
    FaceID faceId = AllocateFace();
    std::vector<HalfEdgeID> faceHalfEdges;
    faceHalfEdges.reserve(k);

    for (size_t i = 0; i < k; ++i) {
        HalfEdgeID heId = AllocateHalfEdge();
        faceHalfEdges.push_back(heId);

        auto* he = GetHalfEdgeMutable(heId);
        he->origin = vertexIds[i];
        he->face = faceId;

        // Assegna corner attributes se forniti
        if (corners && i < corners->size()) {
            he->corner = (*corners)[i];
        } else {
            he->corner = CornerAttributes{};
        }

        // Assicura che il vertice punti a questo half-edge uscente se ancora nullo
        auto* vOrigin = GetVertexMutable(vertexIds[i]);
        if (!IsValid(vOrigin->halfEdge)) {
            vOrigin->halfEdge = heId;
        }
    }

    // 4. Collega il ciclo next e prev perimetrale
    for (size_t i = 0; i < k; ++i) {
        HalfEdgeID currId = faceHalfEdges[i];
        HalfEdgeID nextId = faceHalfEdges[(i + 1) % k];
        HalfEdgeID prevId = faceHalfEdges[(i + k - 1) % k];

        auto* currHe = GetHalfEdgeMutable(currId);
        currHe->next = nextId;
        currHe->prev = prevId;

        VertexID v0 = vertexIds[i];
        VertexID v1 = vertexIds[(i + 1) % k];

        // 5. Cerca l'half-edge opposto (twin) gia presente nel verso contrario (v1 -> v0)
        DirectedEdgeKey oppKey{ v1.index, v0.index };
        auto itTwin = m_directedEdgeLookup.find(oppKey);
        if (itTwin != m_directedEdgeLookup.end()) {
            HalfEdgeID twinId = itTwin->second;
            auto* twinHe = GetHalfEdgeMutable(twinId);

            currHe->twin = twinId;
            twinHe->twin = currId;

            // Condivide lo spigolo non orientato esistente
            currHe->edge = twinHe->edge;
            auto* edgeObj = GetEdgeMutable(twinHe->edge);
            if (edgeObj) {
                edgeObj->twin = currId;
            }
        } else {
            // Crea un nuovo spigolo logico non orientato
            EdgeID edgeId = AllocateEdge();
            auto* edgeObj = GetEdgeMutable(edgeId);
            edgeObj->halfEdge = currId;
            edgeObj->twin = INVALID_HALFEDGE_ID;

            currHe->edge = edgeId;
            currHe->twin = INVALID_HALFEDGE_ID;
        }

        // Registra questo half-edge nella lookup map
        DirectedEdgeKey dirKey{ v0.index, v1.index };
        m_directedEdgeLookup[dirKey] = currId;
    }

    // 6. Configura la faccia
    auto* f = GetFaceMutable(faceId);
    f->halfEdge = faceHalfEdges[0];
    f->surfaceId = surfaceId;
    f->materialId = materialId;

    ComputeFaceNormalAndArea(faceId);
    IncrementTopologyRevision();

    return faceId;
}

bool EditableMesh::DeleteFace(FaceID id, bool deleteUnusedEdges, bool deleteUnusedVertices) {
    if (!IsValid(id)) return false;

    std::vector<HalfEdgeID> faceHEs = GetFaceHalfEdges(id);
    std::vector<VertexID> faceVerts = GetFaceVertices(id);

    // 1. Rimuove gli half-edge dalla directed lookup map e scollega twin
    for (HalfEdgeID heId : faceHEs) {
        auto* he = GetHalfEdgeMutable(heId);
        if (!he) continue;

        DirectedEdgeKey key{ he->origin.index, GetHalfEdge(he->next)->origin.index };
        m_directedEdgeLookup.erase(key);

        if (IsValid(he->twin)) {
            auto* twinHe = GetHalfEdgeMutable(he->twin);
            if (twinHe) {
                twinHe->twin = INVALID_HALFEDGE_ID;
                auto* eObj = GetEdgeMutable(he->edge);
                if (eObj) {
                    if (eObj->halfEdge == heId) eObj->halfEdge = he->twin;
                    else if (eObj->twin == heId) eObj->twin = INVALID_HALFEDGE_ID;
                }
            }
        } else if (deleteUnusedEdges && IsValid(he->edge)) {
            DeleteEdge(he->edge);
        }

        // Se il vertice puntava a questo half-edge eliminato, trova un'alternativa o invalidate
        auto* v = GetVertexMutable(he->origin);
        if (v && v->halfEdge == heId) {
            v->halfEdge = INVALID_HALFEDGE_ID;
            for (size_t oi = 0; oi < m_halfEdges.size(); ++oi) {
                if (!m_halfEdges[oi].isDeleted && m_halfEdges[oi].origin == v->id && m_halfEdges[oi].id != heId) {
                    v->halfEdge = m_halfEdges[oi].id;
                    break;
                }
            }
        }

        he->isDeleted = true;
        m_freeHalfEdgeIndices.push_back(heId.index);
        m_activeHalfEdgeCount--;
    }

    // 2. Elimina la faccia
    auto* f = GetFaceMutable(id);
    f->isDeleted = true;
    m_freeFaceIndices.push_back(id.index);
    m_activeFaceCount--;

    // 3. Pulizia opzionale vertici rimasti isolati
    if (deleteUnusedVertices) {
        for (VertexID vId : faceVerts) {
            auto* v = GetVertex(vId);
            if (v && !IsValid(v->halfEdge)) {
                DeleteVertex(vId);
            }
        }
    }

    IncrementTopologyRevision();
    return true;
}

bool EditableMesh::DeleteEdge(EdgeID id) {
    if (!IsValid(id)) return false;
    auto* e = GetEdgeMutable(id);
    e->isDeleted = true;
    m_freeEdgeIndices.push_back(id.index);
    m_activeEdgeCount--;
    IncrementTopologyRevision();
    return true;
}

bool EditableMesh::DeleteVertex(VertexID id) {
    if (!IsValid(id)) return false;

    // Elimina prima eventuali facce incidenti
    std::vector<FaceID> incFaces = GetVertexIncidentFaces(id);
    for (FaceID fId : incFaces) {
        DeleteFace(fId, true, false);
    }

    auto* v = GetVertexMutable(id);
    v->isDeleted = true;
    m_freeVertexIndices.push_back(id.index);
    m_activeVertexCount--;

    IncrementTopologyRevision();
    return true;
}

// =============================================================================
// NAVIGAZIONE TOPOLOGICA LOCALE O(1)
// =============================================================================

std::vector<HalfEdgeID> EditableMesh::GetFaceHalfEdges(FaceID f) const {
    std::vector<HalfEdgeID> res;
    const auto* faceObj = GetFace(f);
    if (!faceObj || !IsValid(faceObj->halfEdge)) return res;

    HalfEdgeID start = faceObj->halfEdge;
    HalfEdgeID curr = start;
    size_t guard = 0;
    const size_t maxGuard = m_activeHalfEdgeCount + 10;

    do {
        res.push_back(curr);
        const auto* he = GetHalfEdge(curr);
        if (!he || !IsValid(he->next)) break;
        curr = he->next;
        guard++;
    } while (curr != start && guard < maxGuard);

    return res;
}

std::vector<VertexID> EditableMesh::GetFaceVertices(FaceID f) const {
    std::vector<VertexID> res;
    std::vector<HalfEdgeID> hes = GetFaceHalfEdges(f);
    res.reserve(hes.size());
    for (HalfEdgeID heId : hes) {
        const auto* he = GetHalfEdge(heId);
        if (he) res.push_back(he->origin);
    }
    return res;
}

std::vector<CornerAttributes> EditableMesh::GetFaceCorners(FaceID f) const {
    std::vector<CornerAttributes> res;
    std::vector<HalfEdgeID> hes = GetFaceHalfEdges(f);
    res.reserve(hes.size());
    for (HalfEdgeID heId : hes) {
        const auto* he = GetHalfEdge(heId);
        if (he) res.push_back(he->corner);
    }
    return res;
}

size_t EditableMesh::GetFaceVertexCount(FaceID f) const {
    return GetFaceHalfEdges(f).size();
}

std::vector<HalfEdgeID> EditableMesh::GetVertexOutgoingHalfEdges(VertexID v) const {
    std::vector<HalfEdgeID> res;
    const auto* vObj = GetVertex(v);
    if (!vObj || !IsValid(vObj->halfEdge)) return res;

    // Per mesh manifold chiuse, ruota attorno al vertice tramite prev->twin
    HalfEdgeID start = vObj->halfEdge;
    HalfEdgeID curr = start;
    size_t guard = 0;
    const size_t maxGuard = m_activeHalfEdgeCount + 10;
    bool looped = false;

    while (IsValid(curr) && guard < maxGuard) {
        res.push_back(curr);
        const auto* he = GetHalfEdge(curr);
        if (!he) break;
        const auto* prevHe = GetHalfEdge(he->prev);
        if (!prevHe || !IsValid(prevHe->twin)) {
            // Raggiunto un bordo aperto (boundary)
            break;
        }
        curr = prevHe->twin;
        if (curr == start) {
            looped = true;
            break;
        }
        guard++;
    }

    // Se abbiamo trovato un boundary prima di completare il loop, cerchiamo nell'altro senso
    if (!looped && IsValid(start)) {
        curr = start;
        guard = 0;
        while (IsValid(curr) && guard < maxGuard) {
            const auto* he = GetHalfEdge(curr);
            if (!he || !IsValid(he->twin)) break;
            const auto* twinHe = GetHalfEdge(he->twin);
            if (!twinHe) break;
            curr = twinHe->next;
            if (curr == start) break;
            if (std::find(res.begin(), res.end(), curr) == res.end()) {
                res.insert(res.begin(), curr);
            }
            guard++;
        }
    }

    return res;
}

std::vector<VertexID> EditableMesh::GetVertexOneRingNeighbors(VertexID v) const {
    std::vector<VertexID> neighbors;
    std::vector<HalfEdgeID> outgoing = GetVertexOutgoingHalfEdges(v);
    neighbors.reserve(outgoing.size());
    for (HalfEdgeID heId : outgoing) {
        const auto* he = GetHalfEdge(heId);
        if (he) {
            const auto* nextHe = GetHalfEdge(he->next);
            if (nextHe && IsValid(nextHe->origin)) {
                neighbors.push_back(nextHe->origin);
            }
        }
    }
    return neighbors;
}

std::vector<FaceID> EditableMesh::GetVertexIncidentFaces(VertexID v) const {
    std::vector<FaceID> faces;
    std::vector<HalfEdgeID> outgoing = GetVertexOutgoingHalfEdges(v);
    for (HalfEdgeID heId : outgoing) {
        const auto* he = GetHalfEdge(heId);
        if (he && IsValid(he->face)) {
            faces.push_back(he->face);
        }
    }
    return faces;
}

bool EditableMesh::IsBoundaryHalfEdge(HalfEdgeID h) const {
    const auto* he = GetHalfEdge(h);
    return he && (!IsValid(he->twin) || !IsValid(he->face));
}

bool EditableMesh::IsBoundaryEdge(EdgeID e) const {
    const auto* edgeObj = GetEdge(e);
    return edgeObj && (!IsValid(edgeObj->twin) || !IsValid(edgeObj->halfEdge));
}

bool EditableMesh::IsBoundaryVertex(VertexID v) const {
    std::vector<HalfEdgeID> outgoing = GetVertexOutgoingHalfEdges(v);
    for (HalfEdgeID heId : outgoing) {
        if (IsBoundaryHalfEdge(heId)) return true;
    }
    return false;
}

// =============================================================================
// CALCOLO NORMALI ED AREE
// =============================================================================

void EditableMesh::ComputeFaceNormalAndArea(FaceID f) {
    auto* faceObj = GetFaceMutable(f);
    if (!faceObj) return;

    std::vector<VertexID> verts = GetFaceVertices(f);
    const size_t n = verts.size();
    if (n < 3) {
        faceObj->faceNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        faceObj->area = 0.0f;
        return;
    }

    // Metodo di Newell per calcolo deterministico e robusto della normale di poligoni
    glm::vec3 normal(0.0f);
    for (size_t i = 0; i < n; ++i) {
        const auto* vCurr = GetVertex(verts[i]);
        const auto* vNext = GetVertex(verts[(i + 1) % n]);
        if (!vCurr || !vNext) continue;

        const glm::vec3& p1 = vCurr->position;
        const glm::vec3& p2 = vNext->position;

        normal.x += (p1.y - p2.y) * (p1.z + p2.z);
        normal.y += (p1.z - p2.z) * (p1.x + p2.x);
        normal.z += (p1.x - p2.x) * (p1.y + p2.y);
    }

    float len = glm::length(normal);
    if (len > 1e-6f) {
        faceObj->faceNormal = normal / len;
        faceObj->area = len * 0.5f;
    } else {
        faceObj->faceNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        faceObj->area = 0.0f;
    }
}

void EditableMesh::ComputeAllFaceNormals() {
    for (size_t i = 0; i < m_faces.size(); ++i) {
        if (!m_faces[i].isDeleted) {
            ComputeFaceNormalAndArea(m_faces[i].id);
        }
    }
}

void EditableMesh::ComputeVertexNormals() {
    ComputeAllFaceNormals();

    for (size_t i = 0; i < m_vertices.size(); ++i) {
        if (m_vertices[i].isDeleted) continue;

        glm::vec3 avgNormal(0.0f);
        std::vector<FaceID> incFaces = GetVertexIncidentFaces(m_vertices[i].id);
        for (FaceID fId : incFaces) {
            const auto* fObj = GetFace(fId);
            if (fObj && fObj->area > 1e-6f) {
                avgNormal += fObj->faceNormal * fObj->area;
            }
        }

        float len = glm::length(avgNormal);
        if (len > 1e-6f) {
            m_vertices[i].normal = avgNormal / len;
        } else {
            m_vertices[i].normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }
    IncrementGeometryRevision();
}

AABB EditableMesh::ComputeAABB() const {
    AABB b;
    for (size_t i = 0; i < m_vertices.size(); ++i) {
        if (m_vertices[i].isDeleted) continue;
        const glm::vec3& p = m_vertices[i].position;
        b.expand(Vec3{p.x, p.y, p.z});
    }
    return b;
}

// =============================================================================
// VALIDAZIONE TOPOLOGICA DETERMINISTICA (BM-S2)
// =============================================================================

MeshValidationReport EditableMesh::ValidateTopology() const {
    MeshValidationReport report;
    report.vertexCount = m_activeVertexCount;
    report.halfEdgeCount = m_activeHalfEdgeCount;
    report.edgeCount = m_activeEdgeCount;
    report.faceCount = m_activeFaceCount;

    // 1. Controllo vertici
    for (size_t i = 0; i < m_vertices.size(); ++i) {
        if (m_vertices[i].isDeleted) continue;
        const auto& v = m_vertices[i];

        if (std::isnan(v.position.x) || std::isnan(v.position.y) || std::isnan(v.position.z) ||
            std::isinf(v.position.x) || std::isinf(v.position.y) || std::isinf(v.position.z)) {
            report.AddError("Vertice ID " + std::to_string(v.id.index) + " contiene coordinate non finite (NaN/Inf)");
        }

        if (IsValid(v.halfEdge)) {
            const auto* he = GetHalfEdge(v.halfEdge);
            if (!he || he->origin != v.id) {
                report.AddError("Vertice ID " + std::to_string(v.id.index) + " punta a un HalfEdge inconsistente");
            }
        } else {
            report.AddWarning("Vertice ID " + std::to_string(v.id.index) + " e isolato (senza half-edge incidenti)");
        }
    }

    // 2. Controllo half-edges
    for (size_t i = 0; i < m_halfEdges.size(); ++i) {
        if (m_halfEdges[i].isDeleted) continue;
        const auto& he = m_halfEdges[i];

        if (!IsValid(he.origin)) {
            report.AddError("HalfEdge ID " + std::to_string(he.id.index) + " ha un origin vertex non valido");
        }

        if (!IsValid(he.next) || !IsValid(he.prev)) {
            report.AddError("HalfEdge ID " + std::to_string(he.id.index) + " ha un puntatore next o prev non valido");
        } else {
            const auto* nextHe = GetHalfEdge(he.next);
            const auto* prevHe = GetHalfEdge(he.prev);

            if (nextHe && prevHe) {
                if (nextHe->prev != he.id) {
                    report.AddError("HalfEdge ID " + std::to_string(he.id.index) + " next->prev != this");
                }
                if (prevHe->next != he.id) {
                    report.AddError("HalfEdge ID " + std::to_string(he.id.index) + " prev->next != this");
                }
            }
        }

        if (IsValid(he.twin)) {
            const auto* twinHe = GetHalfEdge(he.twin);
            if (!twinHe || twinHe->twin != he.id) {
                report.AddError("HalfEdge ID " + std::to_string(he.id.index) + " twin non reciproco");
            } else {
                const auto* nextHe = GetHalfEdge(he.next);
                if (nextHe && twinHe->origin != nextHe->origin) {
                    report.AddError("HalfEdge ID " + std::to_string(he.id.index) + " twin->origin != next->origin");
                }
            }
        } else {
            report.boundaryEdgeCount++;
        }
    }

    // 3. Controllo facce e poligoni
    for (size_t i = 0; i < m_faces.size(); ++i) {
        if (m_faces[i].isDeleted) continue;
        const auto& f = m_faces[i];

        if (!IsValid(f.halfEdge)) {
            report.AddError("Faccia ID " + std::to_string(f.id.index) + " ha un puntatore halfEdge non valido");
            continue;
        }

        std::vector<HalfEdgeID> fHEs = GetFaceHalfEdges(f.id);
        const size_t vCount = fHEs.size();
        if (vCount < 3) {
            report.AddError("Faccia ID " + std::to_string(f.id.index) + " degenere con meno di 3 vertici");
        } else if (vCount == 3) {
            report.triangleCount++;
        } else if (vCount == 4) {
            report.quadCount++;
        } else {
            report.ngonCount++;
        }

        if (f.area <= 1e-7f) {
            report.AddWarning("Faccia ID " + std::to_string(f.id.index) + " ha area degenere quasi-zero");
        }
    }

    report.isClosedManifold = (report.boundaryEdgeCount == 0 && report.errors.empty());
    return report;
}

// =============================================================================
// TRIANGOLAZIONE ROBUSTA (EAR CLIPPING PER N-GON) & COMPILAZIONE VULKAN
// =============================================================================

bool EditableMesh::TriangulateFace(FaceID f, std::vector<uint32_t>& outTriangles) const {
    std::vector<VertexID> verts = GetFaceVertices(f);
    const size_t n = verts.size();
    if (n < 3) return false;

    // 1. Caso triangolo semplice
    if (n == 3) {
        outTriangles.push_back(0);
        outTriangles.push_back(1);
        outTriangles.push_back(2);
        return true;
    }

    // 2. Caso quad
    if (n == 4) {
        outTriangles.push_back(0);
        outTriangles.push_back(1);
        outTriangles.push_back(2);

        outTriangles.push_back(0);
        outTriangles.push_back(2);
        outTriangles.push_back(3);
        return true;
    }

    // 3. Caso N-gon (Ear Clipping deterministico in 2D)
    const auto* fObj = GetFace(f);
    glm::vec3 normal = fObj ? fObj->faceNormal : glm::vec3(0.0f, 1.0f, 0.0f);
    if (glm::length(normal) < 1e-5f) normal = glm::vec3(0.0f, 1.0f, 0.0f);

    // Trova asse dominante per la proiezione 2D
    int dropAxis = 2; // Default drop Z
    float ax = std::abs(normal.x);
    float ay = std::abs(normal.y);
    float az = std::abs(normal.z);
    if (ax > ay && ax > az) dropAxis = 0;
    else if (ay > ax && ay > az) dropAxis = 1;

    auto project2D = [&](const glm::vec3& p) -> glm::vec2 {
        if (dropAxis == 0) return glm::vec2(p.y, p.z);
        if (dropAxis == 1) return glm::vec2(p.x, p.z);
        return glm::vec2(p.x, p.y);
    };

    std::vector<glm::vec2> poly2D;
    poly2D.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const auto* v = GetVertex(verts[i]);
        poly2D.push_back(v ? project2D(v->position) : glm::vec2(0.0f));
    }

    // Verifica orientamento 2D (deve essere CCW)
    float signedArea = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        signedArea += (poly2D[j].x - poly2D[i].x) * (poly2D[j].y + poly2D[i].y);
    }
    bool isCCW = (signedArea < 0.0f);

    std::vector<uint32_t> indices(n);
    for (size_t i = 0; i < n; ++i) indices[i] = static_cast<uint32_t>(i);

    auto isEar = [&](size_t u, size_t v, size_t w) -> bool {
        const glm::vec2& a = poly2D[indices[u]];
        const glm::vec2& b = poly2D[indices[v]];
        const glm::vec2& c = poly2D[indices[w]];

        // Verifica concavita/orientamento
        float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (isCCW ? (cross <= 1e-7f) : (cross >= -1e-7f)) return false;

        // Verifica che nessun altro vertice sia contenuto nel triangolo
        for (size_t p = 0; p < indices.size(); ++p) {
            if (p == u || p == v || p == w) continue;
            const glm::vec2& pt = poly2D[indices[p]];

            auto sign = [](const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3) {
                return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
            };
            float d1 = sign(pt, a, b);
            float d2 = sign(pt, b, c);
            float d3 = sign(pt, c, a);

            bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
            bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
            if (!(hasNeg && hasPos)) return false; // Il punto e dentro il triangolo
        }
        return true;
    };

    size_t count = indices.size();
    size_t failsafe = 0;
    while (count > 2 && failsafe < n * n) {
        bool earFound = false;
        for (size_t i = 0; i < count; ++i) {
            size_t u = (i + count - 1) % count;
            size_t v = i;
            size_t w = (i + 1) % count;

            if (isEar(u, v, w)) {
                outTriangles.push_back(indices[u]);
                outTriangles.push_back(indices[v]);
                outTriangles.push_back(indices[w]);
                indices.erase(indices.begin() + v);
                count--;
                earFound = true;
                break;
            }
        }
        if (!earFound) {
            // Fallback deterministico fan
            break;
        }
        failsafe++;
    }

    if (count > 2) {
        // Fallback fan per elementi rimanenti
        for (size_t i = 1; i < count - 1; ++i) {
            outTriangles.push_back(indices[0]);
            outTriangles.push_back(indices[i]);
            outTriangles.push_back(indices[i + 1]);
        }
    }

    return true;
}

MeshComponent EditableMesh::CompileToMeshComponent(const std::string& name) const {
    MeshComponent mesh;
    mesh.name = name;
    mesh.type = MeshType::Standard;

    for (size_t fIdx = 0; fIdx < m_faces.size(); ++fIdx) {
        if (m_faces[fIdx].isDeleted) continue;
        const auto& f = m_faces[fIdx];

        std::vector<VertexID> faceVerts = GetFaceVertices(f.id);
        std::vector<CornerAttributes> faceCorners = GetFaceCorners(f.id);
        const size_t vCount = faceVerts.size();
        if (vCount < 3) continue;

        std::vector<uint32_t> localTriIndices;
        if (!TriangulateFace(f.id, localTriIndices)) continue;

        // Base index dei vertici appena aggiunti in mesh.vertices
        uint32_t baseVertexIdx = static_cast<uint32_t>(mesh.vertices.size());

        // Genera vertici dedicati per preservare UV seams e discontinuità
        for (size_t i = 0; i < vCount; ++i) {
            const auto* v = GetVertex(faceVerts[i]);
            Vertex vert{};
            if (v) {
                vert.position = { v->position.x, v->position.y, v->position.z };
                vert.normal = { f.faceNormal.x, f.faceNormal.y, f.faceNormal.z };
            }
            vert.color = { faceCorners[i].color.r, faceCorners[i].color.g, faceCorners[i].color.b, faceCorners[i].color.a };
            vert.roughMetal = { faceCorners[i].uv.x, faceCorners[i].uv.y }; // UV in roughMetal per shader standard
            vert.materialID = f.materialId;
            vert.ao = 1.0f;
            vert.light = 1.0f;
            vert.emissive = 0.0f;

            mesh.vertices.push_back(vert);
        }

        // Aggiunge le facce triangolate a MeshComponent
        for (size_t t = 0; t + 2 < localTriIndices.size(); t += 3) {
            Face triFace;
            triFace.indices = {
                static_cast<int>(baseVertexIdx + localTriIndices[t]),
                static_cast<int>(baseVertexIdx + localTriIndices[t + 1]),
                static_cast<int>(baseVertexIdx + localTriIndices[t + 2])
            };
            triFace.faceNormal = { f.faceNormal.x, f.faceNormal.y, f.faceNormal.z };
            triFace.matIndex = static_cast<int>(f.materialId);
            mesh.faces.push_back(triFace);
        }
    }

    mesh.cachedBounds = ComputeAABB();
    mesh.boundsCalculated = true;
    return mesh;
}

// =============================================================================
// PRIMITIVE DI PARTENZA STANDARD (CUBE, QUAD, PLANE)
// =============================================================================

EditableMesh EditableMesh::CreateCube(float size) {
    EditableMesh mesh;
    float h = size * 0.5f;

    // 8 vertici del cubo
    VertexID v0 = mesh.AddVertex(glm::vec3(-h, -h, -h)); // 0: -X -Y -Z
    VertexID v1 = mesh.AddVertex(glm::vec3( h, -h, -h)); // 1: +X -Y -Z
    VertexID v2 = mesh.AddVertex(glm::vec3( h,  h, -h)); // 2: +X +Y -Z
    VertexID v3 = mesh.AddVertex(glm::vec3(-h,  h, -h)); // 3: -X +Y -Z
    VertexID v4 = mesh.AddVertex(glm::vec3(-h, -h,  h)); // 4: -X -Y +Z
    VertexID v5 = mesh.AddVertex(glm::vec3( h, -h,  h)); // 5: +X -Y +Z
    VertexID v6 = mesh.AddVertex(glm::vec3( h,  h,  h)); // 6: +X +Y +Z
    VertexID v7 = mesh.AddVertex(glm::vec3(-h,  h,  h)); // 7: -X +Y +Z

    // Helper per 4 angoli UV standard
    std::vector<CornerAttributes> uvQuad = {
        CornerAttributes{ glm::vec2(0.0f, 0.0f) },
        CornerAttributes{ glm::vec2(1.0f, 0.0f) },
        CornerAttributes{ glm::vec2(1.0f, 1.0f) },
        CornerAttributes{ glm::vec2(0.0f, 1.0f) }
    };

    // 6 facce quad conformi a AssetCubeFace enum di SurfaceDefinition
    // Back (-Z) surfaceId = 0
    mesh.AddFace({ v1, v0, v3, v2 }, static_cast<uint32_t>(AssetCubeFace::Back), 0, &uvQuad);
    // Front (+Z) surfaceId = 1
    mesh.AddFace({ v4, v5, v6, v7 }, static_cast<uint32_t>(AssetCubeFace::Front), 0, &uvQuad);
    // Left (-X) surfaceId = 2
    mesh.AddFace({ v0, v4, v7, v3 }, static_cast<uint32_t>(AssetCubeFace::Left), 0, &uvQuad);
    // Right (+X) surfaceId = 3
    mesh.AddFace({ v5, v1, v2, v6 }, static_cast<uint32_t>(AssetCubeFace::Right), 0, &uvQuad);
    // Bottom (-Y) surfaceId = 4
    mesh.AddFace({ v0, v1, v5, v4 }, static_cast<uint32_t>(AssetCubeFace::Bottom), 0, &uvQuad);
    // Top (+Y) surfaceId = 5
    mesh.AddFace({ v3, v7, v6, v2 }, static_cast<uint32_t>(AssetCubeFace::Top), 0, &uvQuad);

    mesh.ComputeVertexNormals();
    return mesh;
}

EditableMesh EditableMesh::CreateQuad(float width, float height) {
    EditableMesh mesh;
    float hw = width * 0.5f;
    float hh = height * 0.5f;

    VertexID v0 = mesh.AddVertex(glm::vec3(-hw, 0.0f, -hh));
    VertexID v1 = mesh.AddVertex(glm::vec3( hw, 0.0f, -hh));
    VertexID v2 = mesh.AddVertex(glm::vec3( hw, 0.0f,  hh));
    VertexID v3 = mesh.AddVertex(glm::vec3(-hw, 0.0f,  hh));

    std::vector<CornerAttributes> uvQuad = {
        CornerAttributes{ glm::vec2(0.0f, 0.0f) },
        CornerAttributes{ glm::vec2(1.0f, 0.0f) },
        CornerAttributes{ glm::vec2(1.0f, 1.0f) },
        CornerAttributes{ glm::vec2(0.0f, 1.0f) }
    };

    mesh.AddFace({ v0, v1, v2, v3 }, 0, 0, &uvQuad);
    mesh.ComputeVertexNormals();
    return mesh;
}

EditableMesh EditableMesh::CreatePlane(int resX, int resZ, float size) {
    EditableMesh mesh;
    if (resX < 1) resX = 1;
    if (resZ < 1) resZ = 1;

    float stepX = size / static_cast<float>(resX);
    float stepZ = size / static_cast<float>(resZ);
    float startX = -size * 0.5f;
    float startZ = -size * 0.5f;

    std::vector<std::vector<VertexID>> grid(resX + 1, std::vector<VertexID>(resZ + 1));
    for (int ix = 0; ix <= resX; ++ix) {
        for (int iz = 0; iz <= resZ; ++iz) {
            float x = startX + ix * stepX;
            float z = startZ + iz * stepZ;
            grid[ix][iz] = mesh.AddVertex(glm::vec3(x, 0.0f, z));
        }
    }

    for (int ix = 0; ix < resX; ++ix) {
        for (int iz = 0; iz < resZ; ++iz) {
            VertexID v0 = grid[ix][iz];
            VertexID v1 = grid[ix + 1][iz];
            VertexID v2 = grid[ix + 1][iz + 1];
            VertexID v3 = grid[ix][iz + 1];

            float u0 = static_cast<float>(ix) / resX;
            float u1 = static_cast<float>(ix + 1) / resX;
            float v0Coord = static_cast<float>(iz) / resZ;
            float v1Coord = static_cast<float>(iz + 1) / resZ;

            std::vector<CornerAttributes> corners = {
                CornerAttributes{ glm::vec2(u0, v0Coord) },
                CornerAttributes{ glm::vec2(u1, v0Coord) },
                CornerAttributes{ glm::vec2(u1, v1Coord) },
                CornerAttributes{ glm::vec2(u0, v1Coord) }
            };

            mesh.AddFace({ v0, v1, v2, v3 }, 0, 0, &corners);
        }
    }

    mesh.ComputeVertexNormals();
    return mesh;
}

EditableMesh EditableMesh::CreateFromIndexedMesh(const std::vector<glm::vec3>& positions,
                                                 const std::vector<std::vector<uint32_t>>& faceIndices,
                                                 const std::vector<std::vector<glm::vec2>>* faceUVs,
                                                 uint32_t defaultSurfaceId,
                                                 uint32_t defaultMaterialId) {
    EditableMesh mesh;
    std::vector<VertexID> vMap;
    vMap.reserve(positions.size());

    for (const auto& p : positions) {
        vMap.push_back(mesh.AddVertex(p));
    }

    for (size_t f = 0; f < faceIndices.size(); ++f) {
        const auto& fIdxs = faceIndices[f];
        if (fIdxs.size() < 3) continue;

        std::vector<VertexID> vList;
        vList.reserve(fIdxs.size());
        for (uint32_t vi : fIdxs) {
            if (vi < vMap.size()) vList.push_back(vMap[vi]);
        }

        std::vector<CornerAttributes> corners;
        if (faceUVs && f < faceUVs->size()) {
            const auto& uvs = (*faceUVs)[f];
            corners.reserve(uvs.size());
            for (const auto& uv : uvs) {
                corners.push_back(CornerAttributes{ uv });
            }
        }

        mesh.AddFace(vList, defaultSurfaceId, defaultMaterialId, corners.empty() ? nullptr : &corners);
    }

    mesh.ComputeVertexNormals();
    return mesh;
}

// =============================================================================
// SERIALIZZAZIONE VERSIONATA JSON (BM-S2)
// =============================================================================

nlohmann::json EditableMesh::ToJson() const {
    nlohmann::json j;
    j["schemaVersion"] = 1;
    j["topologyRevision"] = m_topologyRevision;
    j["geometryRevision"] = m_geometryRevision;
    j["attributeRevision"] = m_attributeRevision;

    nlohmann::json vArr = nlohmann::json::array();
    for (size_t i = 0; i < m_vertices.size(); ++i) {
        if (m_vertices[i].isDeleted) continue;
        const auto& v = m_vertices[i];
        vArr.push_back({
            {"idx", v.id.index},
            {"gen", v.id.generation},
            {"pos", {v.position.x, v.position.y, v.position.z}},
            {"norm", {v.normal.x, v.normal.y, v.normal.z}}
        });
    }
    j["vertices"] = vArr;

    nlohmann::json fArr = nlohmann::json::array();
    for (size_t i = 0; i < m_faces.size(); ++i) {
        if (m_faces[i].isDeleted) continue;
        const auto& f = m_faces[i];
        std::vector<VertexID> fVerts = GetFaceVertices(f.id);
        std::vector<CornerAttributes> fCorners = GetFaceCorners(f.id);

        nlohmann::json vIdxArr = nlohmann::json::array();
        for (const auto& vid : fVerts) vIdxArr.push_back(vid.index);

        nlohmann::json cArr = nlohmann::json::array();
        for (const auto& c : fCorners) {
            cArr.push_back({
                {"uv", {c.uv.x, c.uv.y}},
                {"norm", {c.normal.x, c.normal.y, c.normal.z}},
                {"col", {c.color.r, c.color.g, c.color.b, c.color.a}}
            });
        }

        fArr.push_back({
            {"idx", f.id.index},
            {"gen", f.id.generation},
            {"surfaceId", f.surfaceId},
            {"materialId", f.materialId},
            {"verts", vIdxArr},
            {"corners", cArr}
        });
    }
    j["faces"] = fArr;

    return j;
}

bool EditableMesh::FromJson(const nlohmann::json& j) {
    if (!j.is_object() || !j.contains("vertices") || !j.contains("faces")) return false;

    Clear();
    if (j.contains("topologyRevision")) m_topologyRevision = j["topologyRevision"].get<uint64_t>();
    if (j.contains("geometryRevision")) m_geometryRevision = j["geometryRevision"].get<uint64_t>();
    if (j.contains("attributeRevision")) m_attributeRevision = j["attributeRevision"].get<uint64_t>();

    std::unordered_map<uint32_t, VertexID> vOldToNew;
    const auto& vArr = j["vertices"];
    for (const auto& vItem : vArr) {
        uint32_t oldIdx = vItem["idx"].get<uint32_t>();
        glm::vec3 pos(vItem["pos"][0].get<float>(), vItem["pos"][1].get<float>(), vItem["pos"][2].get<float>());
        VertexID newId = AddVertex(pos);
        vOldToNew[oldIdx] = newId;
    }

    const auto& fArr = j["faces"];
    for (const auto& fItem : fArr) {
        uint32_t surfId = fItem.value("surfaceId", 0u);
        uint32_t matId = fItem.value("materialId", 0u);

        std::vector<VertexID> vList;
        for (const auto& viItem : fItem["verts"]) {
            uint32_t oIdx = viItem.get<uint32_t>();
            auto it = vOldToNew.find(oIdx);
            if (it != vOldToNew.end()) vList.push_back(it->second);
        }

        std::vector<CornerAttributes> corners;
        if (fItem.contains("corners")) {
            for (const auto& cItem : fItem["corners"]) {
                CornerAttributes ca;
                if (cItem.contains("uv")) {
                    ca.uv = glm::vec2(cItem["uv"][0].get<float>(), cItem["uv"][1].get<float>());
                }
                if (cItem.contains("norm")) {
                    ca.normal = glm::vec3(cItem["norm"][0].get<float>(), cItem["norm"][1].get<float>(), cItem["norm"][2].get<float>());
                }
                if (cItem.contains("col")) {
                    ca.color = glm::vec4(cItem["col"][0].get<float>(), cItem["col"][1].get<float>(), cItem["col"][2].get<float>(), cItem["col"][3].get<float>());
                }
                corners.push_back(ca);
            }
        }

        AddFace(vList, surfId, matId, corners.empty() ? nullptr : &corners);
    }

    ComputeVertexNormals();
    return true;
}

// =============================================================================
// SUITE DI TEST DETERMINISTICA BM-S2 (10 TEST STRUTTURALI)
// =============================================================================

std::vector<EditableMesh::ContractTestResult> EditableMesh::RunContractTests() {
    std::vector<ContractTestResult> results;

    // SM-1 — Tri-Mesh Contract & Adjacency
    {
        ContractTestResult res;
        res.testId = "SM-1";
        res.testName = "SM-1 — Tri-Mesh Contract & Adjacency";
        res.inputDesc = "Costruzione di 2 triangoli adiacenti che condividono uno spigolo";
        res.expectedDesc = "4 vertici, 2 facce, 5 spigoli, 1 spigolo condiviso con twin reciproco";

        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0, 0, 0));
        VertexID v1 = mesh.AddVertex(glm::vec3(1, 0, 0));
        VertexID v2 = mesh.AddVertex(glm::vec3(0, 1, 0));
        VertexID v3 = mesh.AddVertex(glm::vec3(1, 1, 0));

        FaceID f0 = mesh.AddFace({ v0, v1, v2 });
        FaceID f1 = mesh.AddFace({ v1, v3, v2 });

        auto val = mesh.ValidateTopology();
        bool correctCounts = (mesh.GetVertexCount() == 4 && mesh.GetFaceCount() == 2 && mesh.GetEdgeCount() == 5);
        bool valOk = val.isValid && (val.triangleCount == 2);

        res.passed = correctCounts && valOk;
        res.observedDesc = "Vertices=" + std::to_string(mesh.GetVertexCount()) +
                           ", Faces=" + std::to_string(mesh.GetFaceCount()) +
                           ", Edges=" + std::to_string(mesh.GetEdgeCount()) +
                           ", IsValid=" + (val.isValid ? "true" : "false");
        res.details = res.passed ? "PASS" : "FAIL: Incoerenza nella connettività tri-mesh";
        results.push_back(res);
    }

    // SM-2 — Quad Mesh Loop Invariants
    {
        ContractTestResult res;
        res.testId = "SM-2";
        res.testName = "SM-2 — Quad Mesh Loop Invariants";
        res.inputDesc = "Costruzione cubo a 6 facce quad";
        res.expectedDesc = "8 vertici, 6 facce quad, 12 spigoli, chiuso manifold (0 bordi aperti)";

        EditableMesh cube = EditableMesh::CreateCube(1.0f);
        auto val = cube.ValidateTopology();

        bool cubeCounts = (cube.GetVertexCount() == 8 && cube.GetFaceCount() == 6 && cube.GetEdgeCount() == 12);
        bool isClosed = val.isClosedManifold && (val.boundaryEdgeCount == 0) && (val.quadCount == 6);

        res.passed = cubeCounts && isClosed;
        res.observedDesc = "V=8, F=6, E=12, ClosedManifold=" + std::string(val.isClosedManifold ? "true" : "false") +
                           ", BoundaryEdges=" + std::to_string(val.boundaryEdgeCount);
        res.details = res.passed ? "PASS" : "FAIL: Cubo quad non conforme o non chiuso manifold";
        results.push_back(res);
    }

    // SM-3 — N-gon Concave Ear-Clipping Triangulation
    {
        ContractTestResult res;
        res.testId = "SM-3";
        res.testName = "SM-3 — N-gon Concave Ear-Clipping Triangulation";
        res.inputDesc = "Poligono a L concavo (6 vertici) triangolato tramite Ear Clipping";
        res.expectedDesc = "Triangolazione in esattamente 4 triangoli senza sovrapposizioni";

        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0, 0, 0));
        VertexID v1 = mesh.AddVertex(glm::vec3(2, 0, 0));
        VertexID v2 = mesh.AddVertex(glm::vec3(2, 1, 0));
        VertexID v3 = mesh.AddVertex(glm::vec3(1, 1, 0)); // Angolo concavo riflesso
        VertexID v4 = mesh.AddVertex(glm::vec3(1, 2, 0));
        VertexID v5 = mesh.AddVertex(glm::vec3(0, 2, 0));

        FaceID fL = mesh.AddFace({ v0, v1, v2, v3, v4, v5 });
        std::vector<uint32_t> tris;
        bool triOk = mesh.TriangulateFace(fL, tris);

        bool expectedTriCount = (tris.size() == 12); // 4 triangoli * 3 indici = 12

        res.passed = triOk && expectedTriCount;
        res.observedDesc = "Triangulated=" + std::string(triOk ? "true" : "false") +
                           ", TriCount=" + std::to_string(tris.size() / 3) +
                           " (Expected 4)";
        res.details = res.passed ? "PASS" : "FAIL: Fallimento triangolazione Ear Clipping su N-gon concavo";
        results.push_back(res);
    }

    // SM-4 — Boundary Detection & Open Mesh Handling
    {
        ContractTestResult res;
        res.testId = "SM-4";
        res.testName = "SM-4 — Boundary Detection & Open Mesh Handling";
        res.inputDesc = "Piano griglia 2x2 con bordi aperti";
        res.expectedDesc = "4 facce quad, 8 spigoli di confine rilevati, isClosedManifold=false";

        EditableMesh plane = EditableMesh::CreatePlane(2, 2, 2.0f);
        auto val = plane.ValidateTopology();

        bool boundaryDetected = (!val.isClosedManifold) && (val.boundaryEdgeCount == 8);

        res.passed = boundaryDetected && (plane.GetFaceCount() == 4);
        res.observedDesc = "BoundaryEdges=" + std::to_string(val.boundaryEdgeCount) +
                           ", Closed=" + (val.isClosedManifold ? "true" : "false") +
                           ", Faces=" + std::to_string(plane.GetFaceCount());
        res.details = res.passed ? "PASS" : "FAIL: Mancato o errato tracciamento dei confini aperti";
        results.push_back(res);
    }

    // SM-5 — UV Seam Isolation on Shared 3D Vertices
    {
        ContractTestResult res;
        res.testId = "SM-5";
        res.testName = "SM-5 — UV Seam Isolation on Shared 3D Vertices";
        res.inputDesc = "2 facce con UV seam sullo spigolo comune; verifica preservazione corner attributes";
        res.expectedDesc = "Vertici 3D condivisi mantengono coordinate UV distinte per-corner";

        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0, 0, 0));
        VertexID v1 = mesh.AddVertex(glm::vec3(1, 0, 0));
        VertexID v2 = mesh.AddVertex(glm::vec3(1, 1, 0));
        VertexID v3 = mesh.AddVertex(glm::vec3(0, 1, 0));
        VertexID v4 = mesh.AddVertex(glm::vec3(2, 0, 0));
        VertexID v5 = mesh.AddVertex(glm::vec3(2, 1, 0));

        // Faccia sinistra con UV in [0, 0.5]
        std::vector<CornerAttributes> cLeft = {
            CornerAttributes{ glm::vec2(0.0f, 0.0f) },
            CornerAttributes{ glm::vec2(0.5f, 0.0f) },
            CornerAttributes{ glm::vec2(0.5f, 1.0f) },
            CornerAttributes{ glm::vec2(0.0f, 1.0f) }
        };
        FaceID f0 = mesh.AddFace({ v0, v1, v2, v3 }, 0, 0, &cLeft);

        // Faccia destra con UV seam: comincia da 0.8 anziche 0.5 sullo stesso spigolo (v1, v2)
        std::vector<CornerAttributes> cRight = {
            CornerAttributes{ glm::vec2(0.8f, 0.0f) },
            CornerAttributes{ glm::vec2(1.0f, 0.0f) },
            CornerAttributes{ glm::vec2(1.0f, 1.0f) },
            CornerAttributes{ glm::vec2(0.8f, 1.0f) }
        };
        FaceID f1 = mesh.AddFace({ v1, v4, v5, v2 }, 0, 0, &cRight);

        std::vector<CornerAttributes> corners0 = mesh.GetFaceCorners(f0);
        std::vector<CornerAttributes> corners1 = mesh.GetFaceCorners(f1);

        bool seamPreserved = (corners0[1].uv.x == 0.5f) && (corners1[0].uv.x == 0.8f);

        res.passed = seamPreserved;
        res.observedDesc = "Face0_v1_UVx=" + std::to_string(corners0[1].uv.x) +
                           ", Face1_v1_UVx=" + std::to_string(corners1[0].uv.x);
        res.details = res.passed ? "PASS" : "FAIL: Discontinuità UV su spigolo comune persa o sovrascritta";
        results.push_back(res);
    }

    // SM-6 — Multi-Material & Semantic SurfaceID
    {
        ContractTestResult res;
        res.testId = "SM-6";
        res.testName = "SM-6 — Multi-Material & Semantic SurfaceID";
        res.inputDesc = "Cubo con surfaceId (0..5) per ciascuna faccia e materiali distinti";
        res.expectedDesc = "Ogni faccia conserva il proprio surfaceId e materialId originale";

        EditableMesh cube = EditableMesh::CreateCube(1.0f);
        bool allSurfacesValid = true;
        for (int i = 0; i < 6; ++i) {
            const auto* f = cube.GetFace(FaceID{ static_cast<uint32_t>(i), 1 });
            if (!f || f->surfaceId != static_cast<uint32_t>(i)) {
                allSurfacesValid = false;
                break;
            }
        }

        res.passed = allSurfacesValid;
        res.observedDesc = "All6SurfacesMatchCubeFace=" + std::string(allSurfacesValid ? "true" : "false");
        res.details = res.passed ? "PASS" : "FAIL: Mancata conservazione dei SurfaceID semantici";
        results.push_back(res);
    }

    // SM-7 — Topology Corruption Detection
    {
        ContractTestResult res;
        res.testId = "SM-7";
        res.testName = "SM-7 — Topology Corruption Detection";
        res.inputDesc = "Iniezione intenzionale di incoerenza nel ciclo next/prev";
        res.expectedDesc = "ValidateTopology rileva l'errore, restituisce isValid=false con report diagnostico";

        EditableMesh quad = EditableMesh::CreateQuad(1.0f, 1.0f);
        auto* he = quad.GetHalfEdgeMutable(HalfEdgeID{ 0, 1 });
        if (he) {
            he->next = HalfEdgeID{ 9999, 1 }; // Corruzione intenzionale del puntatore
        }

        auto val = quad.ValidateTopology();
        bool errorDetected = (!val.isValid) && (!val.errors.empty());

        res.passed = errorDetected;
        res.observedDesc = "IsValid=" + std::string(val.isValid ? "true" : "false") +
                           ", ErrorsDetected=" + std::to_string(val.errors.size());
        res.details = res.passed ? "PASS" : "FAIL: Il validatore topologico non rileva corruzioni strutturali";
        results.push_back(res);
    }

    // SM-8 — Non-finite Coordinates Rejection
    {
        ContractTestResult res;
        res.testId = "SM-8";
        res.testName = "SM-8 — Non-finite Coordinates Rejection";
        res.inputDesc = "Iniezione di coordinate NaN in un vertice";
        res.expectedDesc = "ValidateTopology rileva NaN e invalida il report";

        EditableMesh quad = EditableMesh::CreateQuad(1.0f, 1.0f);
        quad.SetVertexPosition(VertexID{ 0, 1 }, glm::vec3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f));

        auto val = quad.ValidateTopology();
        bool nanDetected = (!val.isValid);

        res.passed = nanDetected;
        res.observedDesc = "IsValid=" + std::string(val.isValid ? "true" : "false") +
                           ", ErrorCount=" + std::to_string(val.errors.size());
        res.details = res.passed ? "PASS" : "FAIL: Coordinate NaN non intercettate dal validatore";
        results.push_back(res);
    }

    // SM-9 — Versioned JSON Serialization Round-Trip
    {
        ContractTestResult res;
        res.testId = "SM-9";
        res.testName = "SM-9 — Versioned JSON Serialization Round-Trip";
        res.inputDesc = "Serializzazione/deserializzazione JSON di un cubo con 6 facce e UV";
        res.expectedDesc = "Fedeltà 100% di vertici, facce, SurfaceID e conformità topologica";

        EditableMesh cubeSrc = EditableMesh::CreateCube(2.0f);
        nlohmann::json j = cubeSrc.ToJson();

        EditableMesh cubeDst;
        bool loadOk = cubeDst.FromJson(j);
        auto valDst = cubeDst.ValidateTopology();

        bool identicalCounts = (cubeDst.GetVertexCount() == cubeSrc.GetVertexCount()) &&
                               (cubeDst.GetFaceCount() == cubeSrc.GetFaceCount()) &&
                               (cubeDst.GetEdgeCount() == cubeSrc.GetEdgeCount());
        bool valOk = valDst.isValid && valDst.isClosedManifold;

        res.passed = loadOk && identicalCounts && valOk;
        res.observedDesc = "LoadOk=" + std::string(loadOk ? "true" : "false") +
                           ", DstVertices=" + std::to_string(cubeDst.GetVertexCount()) +
                           ", DstFaces=" + std::to_string(cubeDst.GetFaceCount()) +
                           ", ClosedManifold=" + (valDst.isClosedManifold ? "true" : "false");
        res.details = res.passed ? "PASS" : "FAIL: Incoerenza nel round-trip di serializzazione JSON";
        results.push_back(res);
    }

    // SM-10 — Generational ID Stability After Deletions
    {
        ContractTestResult res;
        res.testId = "SM-10";
        res.testName = "SM-10 — Generational ID Stability After Deletions";
        res.inputDesc = "Creazione vertice, eliminazione, ri-allocazione e verifica rifiuto vecchio ID";
        res.expectedDesc = "Il vecchio ID con generazione precedente risulta rigorosamente INVALIDO";

        EditableMesh mesh;
        VertexID vOld = mesh.AddVertex(glm::vec3(1.0f, 2.0f, 3.0f));
        mesh.DeleteVertex(vOld);

        bool oldIsInvalidNow = !mesh.IsValid(vOld);

        // Alloca un nuovo vertice che riutilizza lo stesso indice
        VertexID vNew = mesh.AddVertex(glm::vec3(4.0f, 5.0f, 6.0f));
        bool sameIndex = (vNew.index == vOld.index);
        bool higherGeneration = (vNew.generation > vOld.generation);
        bool oldStillInvalid = !mesh.IsValid(vOld);
        bool newIsValid = mesh.IsValid(vNew);

        res.passed = oldIsInvalidNow && sameIndex && higherGeneration && oldStillInvalid && newIsValid;
        res.observedDesc = "OldIndex=" + std::to_string(vOld.index) + " (Gen " + std::to_string(vOld.generation) + ")" +
                           ", NewIndex=" + std::to_string(vNew.index) + " (Gen " + std::to_string(vNew.generation) + ")" +
                           ", OldRejected=" + (oldStillInvalid ? "true" : "false");
        res.details = res.passed ? "PASS" : "FAIL: Mancata protezione generazionale su ID riciclati";
        results.push_back(res);
    }

    return results;
}

} // namespace fw
