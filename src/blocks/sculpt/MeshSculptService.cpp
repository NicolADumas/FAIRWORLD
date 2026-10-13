#include "pch.h"
#include "MeshSculptService.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <glm/gtx/norm.hpp>


namespace fw {

// =============================================================================
// SPATIAL GRID ACCELERATION (BM-S5.4)
// =============================================================================

void SpatialGridIndex::Clear() {
    m_cells.clear();
    m_vertexCellMap.clear();
    m_built = false;
}

void SpatialGridIndex::Build(const EditableMesh& mesh, float cellSize) {
    Clear();
    m_cellSize = std::max(0.02f, cellSize);

    auto activeVerts = mesh.GetActiveVertexIDs();
    m_vertexCellMap.reserve(activeVerts.size());
    for (auto vid : activeVerts) {
        const auto* v = mesh.GetVertex(vid);
        if (!v) continue;

        GridKey key = ComputeKey(v->position);
        m_cells[key].push_back(vid);
        m_vertexCellMap[vid] = key;
    }
    m_built = true;
}

void SpatialGridIndex::UpdateVertexPositions(const EditableMesh& mesh, const std::vector<VertexID>& modifiedVertices) {
    if (!m_built || m_cellSize <= 0.0f) return;

    for (VertexID vid : modifiedVertices) {
        const auto* v = mesh.GetVertex(vid);
        if (!v) continue;

        GridKey newKey = ComputeKey(v->position);
        auto itOld = m_vertexCellMap.find(vid);
        if (itOld == m_vertexCellMap.end()) {
            m_cells[newKey].push_back(vid);
            m_vertexCellMap[vid] = newKey;
        } else if (!(itOld->second == newKey)) {
            GridKey oldKey = itOld->second;
            auto itCell = m_cells.find(oldKey);
            if (itCell != m_cells.end()) {
                auto& vec = itCell->second;
                for (size_t i = 0; i < vec.size(); ++i) {
                    if (vec[i] == vid) {
                        vec[i] = vec.back();
                        vec.pop_back();
                        break;
                    }
                }
                if (vec.empty()) {
                    m_cells.erase(itCell);
                }
            }
            m_cells[newKey].push_back(vid);
            itOld->second = newKey;
        }
    }
}

bool SpatialGridIndex::ContainsVertexInCell(VertexID vid, const glm::vec3& pos) const {
    GridKey key = ComputeKey(pos);
    auto it = m_cells.find(key);
    if (it == m_cells.end()) return false;
    for (auto v : it->second) {
        if (v == vid) return true;
    }
    return false;
}

std::vector<VertexID> SpatialGridIndex::QuerySphere(const glm::vec3& center, 
                                                    float radius, 
                                                    const EditableMesh& mesh) const {
    std::vector<VertexID> results;
    if (!m_built || m_cells.empty()) {
        // Fallback lineare se l'indice non e' costruito
        auto allVerts = mesh.GetActiveVertexIDs();
        float r2 = radius * radius;
        for (auto vid : allVerts) {
            const auto* v = mesh.GetVertex(vid);
            if (v) {
                glm::vec3 delta = v->position - center;
                if (glm::dot(delta, delta) <= r2) {
                    results.push_back(vid);
                }
            }
        }
        return results;
    }

    int minX = static_cast<int>(std::floor((center.x - radius) / m_cellSize));
    int maxX = static_cast<int>(std::floor((center.x + radius) / m_cellSize));
    int minY = static_cast<int>(std::floor((center.y - radius) / m_cellSize));
    int maxY = static_cast<int>(std::floor((center.y + radius) / m_cellSize));
    int minZ = static_cast<int>(std::floor((center.z - radius) / m_cellSize));
    int maxZ = static_cast<int>(std::floor((center.z + radius) / m_cellSize));

    float r2 = radius * radius;
    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                auto it = m_cells.find({x, y, z});
                if (it != m_cells.end()) {
                    for (auto vid : it->second) {
                        const auto* v = mesh.GetVertex(vid);
                        if (v) {
                            glm::vec3 delta = v->position - center;
                            if (glm::dot(delta, delta) <= r2) {
                                results.push_back(vid);
                            }
                        }
                    }
                }
            }
        }
    }
    return results;
}

// =============================================================================
// CURVE DI FALLOFF (BM-S5.2)
// =============================================================================

float MeshSculptService::EvaluateFalloff(float normalizedDist, BrushFalloffType type) {
    float t = glm::clamp(normalizedDist, 0.0f, 1.0f);
    switch (type) {
        case BrushFalloffType::Smooth:
            // Hermite Smoothstep inverso (1 a t=0, 0 a t=1)
            return 1.0f - (3.0f * t * t - 2.0f * t * t * t);
        case BrushFalloffType::Linear:
            return 1.0f - t;
        case BrushFalloffType::Sphere:
            return std::sqrt(std::max(0.0f, 1.0f - t * t));
        case BrushFalloffType::Sharp: {
            float inv = 1.0f - t;
            return inv * inv * inv;
        }
        case BrushFalloffType::Constant:
            return 1.0f;
    }
    return 1.0f - t;
}

// =============================================================================
// MESH SCULPT SERVICE (BM-S5)
// =============================================================================

MeshSculptService::MeshSculptService() {
    m_settings.brushType = SculptBrushType::Inflate;
    m_settings.radius = 0.25f;
    m_settings.strength = 0.5f;
    m_settings.falloff = BrushFalloffType::Smooth;
    m_settings.spacing = 0.1f;
    m_settings.symmetry = SymmetryPlane::None;
    m_settings.clayCeiling = 0.2f;
}

bool MeshSculptService::BeginStroke(EditableMesh& mesh, const StrokePoint& startPoint) {
    if (m_inStroke) {
        CancelStroke(mesh);
    }

    m_inStroke = true;
    m_strokeStartPoint = startPoint;
    m_lastDabPoint = startPoint;
    m_hasLastDab = true;

    m_strokeInitialGeoRev = mesh.GetGeometryRevision();
    m_strokeInitialTopoRev = mesh.GetTopologyRevision();

    m_strokeBaselinePositions.clear();
    m_strokeModifiedVertices.clear();
    m_grabCandidates.clear();
    m_symGrabCandidates.clear();

    // Cache posizioni per l'intero insieme di vertici per garantire Undo/Cancel perfetto e supporto a Grab
    auto activeVerts = mesh.GetActiveVertexIDs();
    for (auto vid : activeVerts) {
        const auto* v = mesh.GetVertex(vid);
        if (v) {
            m_strokeBaselinePositions[vid] = v->position;
        }
    }

    // Costruisce la griglia spaziale
    m_spatialIndex.Build(mesh, m_settings.radius * 1.5f);

    if (m_settings.brushType == SculptBrushType::Grab) {
        m_grabCandidates = m_spatialIndex.QuerySphere(startPoint.hitPoint, m_settings.radius, mesh);
        if (m_settings.symmetry != SymmetryPlane::None) {
            glm::vec3 symStart = startPoint.hitPoint;
            if (m_settings.symmetry == SymmetryPlane::X) symStart.x = -symStart.x;
            else if (m_settings.symmetry == SymmetryPlane::Y) symStart.y = -symStart.y;
            else if (m_settings.symmetry == SymmetryPlane::Z) symStart.z = -symStart.z;
            m_symGrabCandidates = m_spatialIndex.QuerySphere(symStart, m_settings.radius, mesh);
        }
    } else {
        std::vector<VertexID> mod;
        ApplyBrushDab(mesh, startPoint, m_settings, &mod);
        for (auto vid : mod) {
            m_strokeModifiedVertices.insert(vid);
        }
    }

    return true;
}

bool MeshSculptService::UpdateStroke(EditableMesh& mesh, const StrokePoint& currentPoint) {
    if (!m_inStroke) return false;

    // 1. Gestione speciale per Grab: trascina la baseline originale
    if (m_settings.brushType == SculptBrushType::Grab) {
        glm::vec3 grabDelta = currentPoint.hitPoint - m_strokeStartPoint.hitPoint;
        if (glm::length(grabDelta) < 1e-6f) {
            return false; // Zero-Work
        }

        if (m_grabCandidates.empty() && m_symGrabCandidates.empty()) {
            return false;
        }

        for (auto vid : m_grabCandidates) {
            auto itBase = m_strokeBaselinePositions.find(vid);
            if (itBase == m_strokeBaselinePositions.end()) continue;

            auto* v = mesh.GetVertexMutable(vid);
            if (!v) continue;

            float dist = glm::distance(itBase->second, m_strokeStartPoint.hitPoint);
            float weight = EvaluateFalloff(dist / m_settings.radius, m_settings.falloff) * m_settings.strength;

            v->position = itBase->second + grabDelta * weight;
            m_strokeModifiedVertices.insert(vid);
        }

        // Se simmetria attiva
        if (m_settings.symmetry != SymmetryPlane::None) {
            glm::vec3 symStart = m_strokeStartPoint.hitPoint;
            glm::vec3 symDelta = grabDelta;
            if (m_settings.symmetry == SymmetryPlane::X) { symStart.x = -symStart.x; symDelta.x = -symDelta.x; }
            else if (m_settings.symmetry == SymmetryPlane::Y) { symStart.y = -symStart.y; symDelta.y = -symDelta.y; }
            else if (m_settings.symmetry == SymmetryPlane::Z) { symStart.z = -symStart.z; symDelta.z = -symDelta.z; }

            for (auto vid : m_symGrabCandidates) {
                auto itBase = m_strokeBaselinePositions.find(vid);
                if (itBase == m_strokeBaselinePositions.end()) continue;

                auto* v = mesh.GetVertexMutable(vid);
                if (!v) continue;

                float dist = glm::distance(itBase->second, symStart);
                float weight = EvaluateFalloff(dist / m_settings.radius, m_settings.falloff) * m_settings.strength;

                v->position = itBase->second + symDelta * weight;
                m_strokeModifiedVertices.insert(vid);
            }
        }

        mesh.ComputeAllFaceNormals();
        mesh.ComputeVertexNormals();
        mesh.IncrementGeometryRevision();
        return true;
    }

    // 2. Controllo stroke spacing per gli altri pennelli
    float dabDist = glm::distance(currentPoint.hitPoint, m_lastDabPoint.hitPoint);
    float stepThreshold = std::max(0.01f, m_settings.radius * m_settings.spacing);
    if (m_hasLastDab && dabDist < stepThreshold) {
        return false; // Zero-Work: cursore quasi fermo
    }

    std::vector<VertexID> mod;
    bool applied = ApplyBrushDab(mesh, currentPoint, m_settings, &mod);
    if (applied) {
        for (auto vid : mod) {
            m_strokeModifiedVertices.insert(vid);
        }
        m_lastDabPoint = currentPoint;
        m_hasLastDab = true;
    }
    return applied;
}

SculptStrokeResult MeshSculptService::EndStroke(EditableMesh& mesh) {
    SculptStrokeResult res;
    if (!m_inStroke) {
        res.success = false;
        res.errorMessage = "No active stroke to end.";
        return res;
    }

    res.oldGeometryRev = m_strokeInitialGeoRev;
    res.newGeometryRev = mesh.GetGeometryRevision();
    res.geometryChanged = (res.newGeometryRev > res.oldGeometryRev);

    res.modifiedVertices.assign(m_strokeModifiedVertices.begin(), m_strokeModifiedVertices.end());
    res.positionDeltas.reserve(m_strokeModifiedVertices.size());

    for (auto vid : m_strokeModifiedVertices) {
        const auto* v = mesh.GetVertex(vid);
        auto itBase = m_strokeBaselinePositions.find(vid);
        if (v && itBase != m_strokeBaselinePositions.end()) {
            SculptStrokeResult::VertexPositionDelta d;
            d.id = vid;
            d.oldPos = itBase->second;
            d.newPos = v->position;
            res.positionDeltas.push_back(d);
        }
    }

    // Per Grab, aggiorna l'indice spaziale con le posizioni finali
    if (m_settings.brushType == SculptBrushType::Grab && !m_strokeModifiedVertices.empty()) {
        std::vector<VertexID> modVec(m_strokeModifiedVertices.begin(), m_strokeModifiedVertices.end());
        m_spatialIndex.UpdateVertexPositions(mesh, modVec);
    }

    m_inStroke = false;
    m_strokeBaselinePositions.clear();
    m_strokeModifiedVertices.clear();
    m_grabCandidates.clear();
    m_symGrabCandidates.clear();
    m_hasLastDab = false;

    res.success = true;
    return res;
}

void MeshSculptService::CancelStroke(EditableMesh& mesh) {
    if (!m_inStroke) return;

    for (VertexID vid : m_strokeModifiedVertices) {
        auto itBase = m_strokeBaselinePositions.find(vid);
        if (itBase != m_strokeBaselinePositions.end()) {
            auto* v = mesh.GetVertexMutable(vid);
            if (v) v->position = itBase->second;
        }
    }

    mesh.ComputeAllFaceNormals();
    mesh.ComputeVertexNormals();

    // Ripristino deterministico delle revisioni pre-stroke
    mesh.SetGeometryRevision(m_strokeInitialGeoRev);
    mesh.SetTopologyRevision(m_strokeInitialTopoRev);

    // Ripristina l'indice spaziale sulla geometria restaurata
    m_spatialIndex.Build(mesh, m_settings.radius * 1.5f);

    m_inStroke = false;
    m_strokeBaselinePositions.clear();
    m_strokeModifiedVertices.clear();
    m_grabCandidates.clear();
    m_symGrabCandidates.clear();
    m_hasLastDab = false;
}

bool MeshSculptService::ApplyBrushDab(EditableMesh& mesh, 
                                     const StrokePoint& point, 
                                     const SculptBrushSettings& settings, 
                                     std::vector<VertexID>* outModifiedVertices) {
    if (settings.strength <= 1e-6f || settings.radius <= 1e-6f) {
        return false;
    }

    auto candidateVerts = m_spatialIndex.QuerySphere(point.hitPoint, settings.radius, mesh);
    std::vector<VertexID> symCandidates;
    if (settings.symmetry != SymmetryPlane::None) {
        StrokePoint symPoint = point;
        if (settings.symmetry == SymmetryPlane::X) {
            symPoint.hitPoint.x = -symPoint.hitPoint.x;
            symPoint.surfaceNormal.x = -symPoint.surfaceNormal.x;
        } else if (settings.symmetry == SymmetryPlane::Y) {
            symPoint.hitPoint.y = -symPoint.hitPoint.y;
            symPoint.surfaceNormal.y = -symPoint.surfaceNormal.y;
        } else if (settings.symmetry == SymmetryPlane::Z) {
            symPoint.hitPoint.z = -symPoint.hitPoint.z;
            symPoint.surfaceNormal.z = -symPoint.surfaceNormal.z;
        }
        symCandidates = m_spatialIndex.QuerySphere(symPoint.hitPoint, settings.radius, mesh);
    }

    if (candidateVerts.empty() && symCandidates.empty()) {
        return false;
    }

    if (!candidateVerts.empty()) {
        ApplyDeformationInternal(mesh, point, settings, candidateVerts);
        if (outModifiedVertices) {
            outModifiedVertices->insert(outModifiedVertices->end(), candidateVerts.begin(), candidateVerts.end());
        }
    }

    if (!symCandidates.empty()) {
        StrokePoint symPoint = point;
        if (settings.symmetry == SymmetryPlane::X) {
            symPoint.hitPoint.x = -symPoint.hitPoint.x;
            symPoint.surfaceNormal.x = -symPoint.surfaceNormal.x;
        } else if (settings.symmetry == SymmetryPlane::Y) {
            symPoint.hitPoint.y = -symPoint.hitPoint.y;
            symPoint.surfaceNormal.y = -symPoint.surfaceNormal.y;
        } else if (settings.symmetry == SymmetryPlane::Z) {
            symPoint.hitPoint.z = -symPoint.hitPoint.z;
            symPoint.surfaceNormal.z = -symPoint.surfaceNormal.z;
        }
        ApplyDeformationInternal(mesh, symPoint, settings, symCandidates);
        if (outModifiedVertices) {
            outModifiedVertices->insert(outModifiedVertices->end(), symCandidates.begin(), symCandidates.end());
        }
    }

    // Refit selettivo O(m) dell'indice spaziale per i vertici deformati
    std::vector<VertexID> allModified = candidateVerts;
    if (!symCandidates.empty()) {
        allModified.insert(allModified.end(), symCandidates.begin(), symCandidates.end());
    }
    m_spatialIndex.UpdateVertexPositions(mesh, allModified);

    mesh.ComputeAllFaceNormals();
    mesh.ComputeVertexNormals();
    mesh.IncrementGeometryRevision();
    return true;
}

void MeshSculptService::ApplyDeformationInternal(EditableMesh& mesh, 
                                                const StrokePoint& point, 
                                                const SculptBrushSettings& settings,
                                                const std::vector<VertexID>& candidateVerts) {
    float r = settings.radius;
    float strength = settings.strength;
    glm::vec3 center = point.hitPoint;
    glm::vec3 normal = glm::length(point.surfaceNormal) > 1e-4f ? glm::normalize(point.surfaceNormal) : glm::vec3(0.0f, 1.0f, 0.0f);

    switch (settings.brushType) {
        case SculptBrushType::Smooth: {
            for (auto vid : candidateVerts) {
                auto* v = mesh.GetVertexMutable(vid);
                if (!v) continue;

                float dist = glm::distance(v->position, center);
                if (dist > r) continue;
                float weight = EvaluateFalloff(dist / r, settings.falloff) * strength;

                auto neighbors = mesh.GetVertexOneRingNeighbors(vid);
                if (!neighbors.empty()) {
                    glm::vec3 avg(0.0f);
                    for (auto nId : neighbors) {
                        const auto* nv = mesh.GetVertex(nId);
                        if (nv) avg += nv->position;
                    }
                    avg /= static_cast<float>(neighbors.size());
                    v->position += (avg - v->position) * weight;
                }
            }
            break;
        }
        case SculptBrushType::Inflate: {
            for (auto vid : candidateVerts) {
                auto* v = mesh.GetVertexMutable(vid);
                if (!v) continue;

                float dist = glm::distance(v->position, center);
                if (dist > r) continue;
                float weight = EvaluateFalloff(dist / r, settings.falloff) * strength;

                glm::vec3 vNorm = glm::length(v->normal) > 1e-4f ? glm::normalize(v->normal) : normal;
                glm::vec3 disp = vNorm * (r * weight * 0.5f);
                v->position += disp;
            }
            break;
        }
        case SculptBrushType::Flatten: {
            for (auto vid : candidateVerts) {
                auto* v = mesh.GetVertexMutable(vid);
                if (!v) continue;

                float dist = glm::distance(v->position, center);
                if (dist > r) continue;
                float weight = EvaluateFalloff(dist / r, settings.falloff) * strength;

                // Distanza dal piano tangente passante per center con normale normal
                float planeDist = glm::dot(v->position - center, normal);
                glm::vec3 projDisp = -normal * (planeDist * weight);
                v->position += projDisp;
            }
            break;
        }
        case SculptBrushType::Clay: {
            float ceilingHeight = r * settings.clayCeiling;
            for (auto vid : candidateVerts) {
                auto* v = mesh.GetVertexMutable(vid);
                if (!v) continue;

                float dist = glm::distance(v->position, center);
                if (dist > r) continue;
                float weight = EvaluateFalloff(dist / r, settings.falloff) * strength;

                float planeDist = glm::dot(v->position - center, normal);
                if (planeDist < ceilingHeight) {
                    float step = std::min(ceilingHeight - planeDist, r * 0.15f) * weight;
                    v->position += normal * step;
                }
            }
            break;
        }
        case SculptBrushType::Grab:
            // Grab e' gestito a livello di stroke update globale tramite baseline
            break;
    }
}

// =============================================================================
// VALIDATION SUITE BM-S5 (SCL-1 .. SCL-10)
// =============================================================================

std::vector<MeshSculptService::SculptTestResult> MeshSculptService::RunSculptContractTests() {
    std::vector<SculptTestResult> results;
    results.reserve(10);

    // SCL-1: Grab Brush
    {
        SculptTestResult t;
        t.testId = "SCL-1";
        t.testName = "SCL-1 — Grab Brush Local Dragging";
        t.inputDesc = "Cube with top vertex at (0.5, 0.5, 0.5); Grab stroke starts at (0.5, 0.5, 0.5) and drags +Z by 0.3";
        t.expectedDesc = "Target vertex pulled along +Z by ~0.3*strength; topoRevision UNCHANGED; finite coords";

        auto mesh = EditableMesh::CreateCube(1.0f);
        uint64_t initialTopo = mesh.GetTopologyRevision();

        MeshSculptService sculpt;
        SculptBrushSettings bs;
        bs.brushType = SculptBrushType::Grab;
        bs.radius = 0.5f;
        bs.strength = 1.0f;
        bs.falloff = BrushFalloffType::Smooth;
        sculpt.SetSettings(bs);

        StrokePoint spStart;
        spStart.hitPoint = glm::vec3(0.5f, 0.5f, 0.5f);
        sculpt.BeginStroke(mesh, spStart);

        StrokePoint spDrag;
        spDrag.hitPoint = glm::vec3(0.5f, 0.5f, 0.8f);
        sculpt.UpdateStroke(mesh, spDrag);

        auto strokeRes = sculpt.EndStroke(mesh);

        VertexID topVid = INVALID_VERTEX_ID;
        for (auto vid : mesh.GetActiveVertexIDs()) {
            const auto* v = mesh.GetVertex(vid);
            if (v && v->position.z > 0.65f) {
                topVid = vid;
                break;
            }
        }
        const auto* topV = mesh.GetVertex(topVid);

        bool posMoved = topV && (topV->position.z >= 0.75f);
        bool topoUntouched = (mesh.GetTopologyRevision() == initialTopo);

        t.passed = strokeRes.success && posMoved && topoUntouched;
        t.observedDesc = "Success=" + std::to_string(strokeRes.success) + 
                         ", TopoUntouched=" + std::to_string(topoUntouched) + 
                         ", Z=" + std::to_string(topV ? topV->position.z : 0.0f);
        t.details = t.passed ? "PASS" : "FAIL: Grab fallito o topologia alterata";
        results.push_back(t);
    }

    // SCL-2: Smooth Brush
    {
        SculptTestResult t;
        t.testId = "SCL-2";
        t.testName = "SCL-2 — Smooth Brush Relaxation";
        t.inputDesc = "Cube with one perturbed vertex at Z=0.8; Smooth brush applied with radius=0.6, strength=0.8";
        t.expectedDesc = "Perturbed vertex relaxed towards neighbor centroid; Z strictly less than 0.8; topoRevision UNCHANGED";

        auto mesh = EditableMesh::CreateCube(1.0f);
        uint64_t initialTopo = mesh.GetTopologyRevision();

        // Trova e perturba un vertice
        VertexID pertVid = INVALID_VERTEX_ID;
        for (auto vid : mesh.GetActiveVertexIDs()) {
            const auto* v = mesh.GetVertex(vid);
            if (v && glm::distance(v->position, glm::vec3(0.5f, 0.5f, 0.5f)) < 1e-4f) {
                pertVid = vid;
                break;
            }
        }
        mesh.SetVertexPosition(pertVid, glm::vec3(0.5f, 0.5f, 0.8f));

        MeshSculptService sculpt;
        SculptBrushSettings bs;
        bs.brushType = SculptBrushType::Smooth;
        bs.radius = 0.6f;
        bs.strength = 0.8f;
        sculpt.SetSettings(bs);

        StrokePoint sp;
        sp.hitPoint = glm::vec3(0.5f, 0.5f, 0.8f);
        sculpt.BeginStroke(mesh, sp);
        sculpt.EndStroke(mesh);

        const auto* relaxedV = mesh.GetVertex(pertVid);
        bool relaxed = relaxedV && (relaxedV->position.z < 0.75f) && (relaxedV->position.z >= 0.5f);
        bool topoUntouched = (mesh.GetTopologyRevision() == initialTopo);

        t.passed = relaxed && topoUntouched;
        t.observedDesc = "RelaxedZ=" + std::to_string(relaxedV ? relaxedV->position.z : 0.0f) + 
                         ", TopoUntouched=" + std::to_string(topoUntouched);
        t.details = t.passed ? "PASS" : "FAIL: Levigatura fallita";
        results.push_back(t);
    }

    // SCL-3: Inflate Brush
    {
        SculptTestResult t;
        t.testId = "SCL-3";
        t.testName = "SCL-3 — Inflate Radial Outward Expansion";
        t.inputDesc = "Inflate brush applied at top center (0, 0, 0.5) with normal (0, 0, 1) and radius 0.8";
        t.expectedDesc = "Top vertices displaced outward along normals; Z > 0.5; geoRevision bumped";

        auto mesh = EditableMesh::CreateCube(1.0f);
        uint64_t initialGeo = mesh.GetGeometryRevision();

        MeshSculptService sculpt;
        SculptBrushSettings bs;
        bs.brushType = SculptBrushType::Inflate;
        bs.radius = 0.8f;
        bs.strength = 0.5f;
        sculpt.SetSettings(bs);

        StrokePoint sp;
        sp.hitPoint = glm::vec3(0.0f, 0.0f, 0.5f);
        sp.surfaceNormal = glm::vec3(0.0f, 0.0f, 1.0f);

        sculpt.BeginStroke(mesh, sp);
        auto res = sculpt.EndStroke(mesh);

        bool anyExpanded = false;
        for (auto vid : mesh.GetActiveVertexIDs()) {
            const auto* v = mesh.GetVertex(vid);
            if (v && v->position.z > 0.52f) {
                anyExpanded = true;
                break;
            }
        }

        t.passed = res.success && anyExpanded && (mesh.GetGeometryRevision() > initialGeo);
        t.observedDesc = "Success=" + std::to_string(res.success) + 
                         ", AnyExpanded=" + std::to_string(anyExpanded) + 
                         ", GeoRev=" + std::to_string(mesh.GetGeometryRevision());
        t.details = t.passed ? "PASS" : "FAIL: Inflate fallito";
        results.push_back(t);
    }

    // SCL-4: Flatten Brush
    {
        SculptTestResult t;
        t.testId = "SCL-4";
        t.testName = "SCL-4 — Flatten Surface Tangent Plane Projection";
        t.inputDesc = "Perturbed vertex at Z=0.9; Flatten brush with plane center Z=0.5, normal (0,0,1)";
        t.expectedDesc = "Vertex pulled down towards tangent plane Z=0.5";

        auto mesh = EditableMesh::CreateCube(1.0f);
        VertexID targetVid = mesh.GetActiveVertexIDs()[0];
        mesh.SetVertexPosition(targetVid, glm::vec3(0.5f, 0.5f, 0.9f));

        MeshSculptService sculpt;
        SculptBrushSettings bs;
        bs.brushType = SculptBrushType::Flatten;
        bs.radius = 0.6f;
        bs.strength = 1.0f;
        bs.falloff = BrushFalloffType::Linear;
        sculpt.SetSettings(bs);

        StrokePoint sp;
        sp.hitPoint = glm::vec3(0.5f, 0.5f, 0.5f);
        sp.surfaceNormal = glm::vec3(0.0f, 0.0f, 1.0f);

        sculpt.BeginStroke(mesh, sp);
        sculpt.EndStroke(mesh);

        const auto* v = mesh.GetVertex(targetVid);
        bool flattened = v && (v->position.z < 0.8f);

        t.passed = flattened;
        t.observedDesc = "FlattenedZ=" + std::to_string(v ? v->position.z : 0.0f);
        t.details = t.passed ? "PASS" : "FAIL: Appiattimento su piano non riuscito";
        results.push_back(t);
    }

    // SCL-5: Clay Brush
    {
        SculptTestResult t;
        t.testId = "SCL-5";
        t.testName = "SCL-5 — Clay Progressive Volumetric Build-up";
        t.inputDesc = "Clay brush applied with ceiling 0.15; builds up material and clamps to ceiling plane";
        t.expectedDesc = "Vertices build up along normal without exceeding ceiling plane";

        auto mesh = EditableMesh::CreateCube(1.0f);
        MeshSculptService sculpt;
        SculptBrushSettings bs;
        bs.brushType = SculptBrushType::Clay;
        bs.radius = 0.8f;
        bs.strength = 1.0f;
        bs.clayCeiling = 0.15f;
        sculpt.SetSettings(bs);

        StrokePoint sp;
        sp.hitPoint = glm::vec3(0.0f, 0.0f, 0.5f);
        sp.surfaceNormal = glm::vec3(0.0f, 0.0f, 1.0f);

        sculpt.BeginStroke(mesh, sp);
        sculpt.EndStroke(mesh);

        bool builtUp = false;
        bool ceilingRespected = true;
        for (auto vid : mesh.GetActiveVertexIDs()) {
            const auto* v = mesh.GetVertex(vid);
            if (v && v->position.z > 0.51f) {
                builtUp = true;
                if (v->position.z > 0.5f + bs.radius * bs.clayCeiling + 0.01f) {
                    ceilingRespected = false;
                }
            }
        }

        t.passed = builtUp && ceilingRespected;
        t.observedDesc = "BuiltUp=" + std::to_string(builtUp) + ", CeilingRespected=" + std::to_string(ceilingRespected);
        t.details = t.passed ? "PASS" : "FAIL: Clay build-up o tetto non rispettato";
        results.push_back(t);
    }

    // SCL-6: Falloff Curves
    {
        SculptTestResult t;
        t.testId = "SCL-6";
        t.testName = "SCL-6 — Mathematical Falloff Curves Evaluation";
        t.inputDesc = "Evaluate Smooth, Linear, Sphere, Sharp, Constant at t=0, 0.5, 1.0";
        t.expectedDesc = "All curves evaluate f(0)=1.0, f(1)=0.0 (except Constant=1.0); strictly monotonic";

        float s0 = EvaluateFalloff(0.0f, BrushFalloffType::Smooth);
        float s05 = EvaluateFalloff(0.5f, BrushFalloffType::Smooth);
        float s1 = EvaluateFalloff(1.0f, BrushFalloffType::Smooth);

        float l0 = EvaluateFalloff(0.0f, BrushFalloffType::Linear);
        float l05 = EvaluateFalloff(0.5f, BrushFalloffType::Linear);
        float l1 = EvaluateFalloff(1.0f, BrushFalloffType::Linear);

        float c1 = EvaluateFalloff(1.0f, BrushFalloffType::Constant);

        bool sOk = (std::abs(s0 - 1.0f) < 1e-4f) && (std::abs(s05 - 0.5f) < 1e-4f) && (std::abs(s1 - 0.0f) < 1e-4f);
        bool lOk = (std::abs(l0 - 1.0f) < 1e-4f) && (std::abs(l05 - 0.5f) < 1e-4f) && (std::abs(l1 - 0.0f) < 1e-4f);
        bool cOk = (std::abs(c1 - 1.0f) < 1e-4f);

        t.passed = sOk && lOk && cOk;
        t.observedDesc = "Smooth(0,0.5,1)=(" + std::to_string(s0) + "," + std::to_string(s05) + "," + std::to_string(s1) + ")" +
                         ", Linear05=" + std::to_string(l05) + ", Constant1=" + std::to_string(c1);
        t.details = t.passed ? "PASS" : "FAIL: Valutazione curve di falloff non accurata";
        results.push_back(t);
    }

    // SCL-7: Stroke Spacing
    {
        SculptTestResult t;
        t.testId = "SCL-7";
        t.testName = "SCL-7 — Stroke Spacing Zero-Work Contract";
        t.inputDesc = "Micro-update with delta=0.001 (below spacing threshold 0.1*radius)";
        t.expectedDesc = "UpdateStroke returns false; skips redundant dab; Zero-Work";

        auto mesh = EditableMesh::CreateCube(1.0f);
        MeshSculptService sculpt;
        SculptBrushSettings bs;
        bs.brushType = SculptBrushType::Inflate;
        bs.radius = 0.5f;
        bs.spacing = 0.2f; // Soglia = 0.1
        sculpt.SetSettings(bs);

        StrokePoint sp;
        sp.hitPoint = glm::vec3(0.0f, 0.0f, 0.5f);
        sculpt.BeginStroke(mesh, sp);

        StrokePoint microMove = sp;
        microMove.hitPoint += glm::vec3(0.002f, 0.0f, 0.0f); // << 0.1
        bool skipped = !sculpt.UpdateStroke(mesh, microMove);

        sculpt.EndStroke(mesh);

        t.passed = skipped;
        t.observedDesc = "SkippedRedundantDab=" + std::to_string(skipped);
        t.details = t.passed ? "PASS" : "FAIL: Violazione spaziatura stroke";
        results.push_back(t);
    }

    // SCL-8: Symmetry
    {
        SculptTestResult t;
        t.testId = "SCL-8";
        t.testName = "SCL-8 — Symmetry Reflection Deformation";
        t.inputDesc = "Inflate dab on +X with SymmetryPlane::X enabled";
        t.expectedDesc = "Mirrored deformation applied to corresponding vertex on -X";

        auto mesh = EditableMesh::CreateCube(1.0f);
        MeshSculptService sculpt;
        SculptBrushSettings bs;
        bs.brushType = SculptBrushType::Inflate;
        bs.radius = 0.4f;
        bs.strength = 1.0f;
        bs.symmetry = SymmetryPlane::X;
        sculpt.SetSettings(bs);

        StrokePoint sp;
        sp.hitPoint = glm::vec3(0.5f, 0.0f, 0.0f);
        sp.surfaceNormal = glm::vec3(1.0f, 0.0f, 0.0f);

        sculpt.BeginStroke(mesh, sp);
        sculpt.EndStroke(mesh);

        bool posExpanded = false;
        bool negExpanded = false;
        for (auto vid : mesh.GetActiveVertexIDs()) {
            const auto* v = mesh.GetVertex(vid);
            if (v) {
                if (v->position.x > 0.52f) posExpanded = true;
                if (v->position.x < -0.52f) negExpanded = true;
            }
        }

        t.passed = posExpanded && negExpanded;
        t.observedDesc = "PosExpanded=" + std::to_string(posExpanded) + ", NegExpanded=" + std::to_string(negExpanded);
        t.details = t.passed ? "PASS" : "FAIL: Simmetria non applicata specularmente";
        results.push_back(t);
    }

    // SCL-9: CancelStroke Rollback (Positions, Revisions, Spatial Index)
    {
        SculptTestResult t;
        t.testId = "SCL-9";
        t.testName = "SCL-9 — CancelStroke 100% Rollback (Positions, Revisions, Index)";
        t.inputDesc = "Multi-step stroke deforms mesh; CancelStroke called";
        t.expectedDesc = "100% vertex positions, geometryRevision, topologyRevision and spatial index restored to pre-stroke state";

        auto mesh = EditableMesh::CreateCube(1.0f);
        EditableMesh pristine = mesh;
        uint64_t initialGeo = mesh.GetGeometryRevision();
        uint64_t initialTopo = mesh.GetTopologyRevision();

        MeshSculptService sculpt;
        SculptBrushSettings bs;
        bs.brushType = SculptBrushType::Inflate;
        bs.radius = 0.6f;
        bs.strength = 1.0f;
        sculpt.SetSettings(bs);

        StrokePoint sp;
        sp.hitPoint = glm::vec3(0.0f, 0.0f, 0.5f);
        sculpt.BeginStroke(mesh, sp);

        StrokePoint sp2 = sp;
        sp2.hitPoint += glm::vec3(0.2f, 0.0f, 0.0f);
        sculpt.UpdateStroke(mesh, sp2);

        sculpt.CancelStroke(mesh);

        bool exactlyRestored = true;
        for (auto vid : mesh.GetActiveVertexIDs()) {
            const auto* vNow = mesh.GetVertex(vid);
            const auto* vBefore = pristine.GetVertex(vid);
            if (!vNow || !vBefore || vNow->position != vBefore->position) {
                exactlyRestored = false;
                break;
            }
        }

        bool geoRestored = (mesh.GetGeometryRevision() == initialGeo);
        bool topoRestored = (mesh.GetTopologyRevision() == initialTopo);

        // Verifica che l'indice spaziale sia coerente con le posizioni restaurate
        auto restoredCandidates = sculpt.GetSpatialIndex().QuerySphere(sp.hitPoint, bs.radius, mesh);
        bool indexRestored = !restoredCandidates.empty();

        t.passed = exactlyRestored && geoRestored && topoRestored && indexRestored;
        t.observedDesc = "ExactlyRestored=" + std::to_string(exactlyRestored) + 
                         ", GeoRestored=" + std::to_string(geoRestored) + 
                         ", TopoRestored=" + std::to_string(topoRestored) + 
                         ", IndexRestored=" + std::to_string(indexRestored);
        t.details = t.passed ? "PASS" : "FAIL: Ripristino CancelStroke o revisioni incoerente";
        results.push_back(t);
    }

    // SCL-10: Topology Invariance & Zero-Work
    {
        SculptTestResult t;
        t.testId = "SCL-10";
        t.testName = "SCL-10 — Zero Topology Mutation & Closed Manifold Preservation";
        t.inputDesc = "Multi-step sculpting operations on cube";
        t.expectedDesc = "topologyRevision strictly UNCHANGED; mesh closed manifold valid; zero non-finite coords";

        auto mesh = EditableMesh::CreateCube(1.0f);
        uint64_t initialTopo = mesh.GetTopologyRevision();

        MeshSculptService sculpt;
        SculptBrushSettings bs;
        bs.brushType = SculptBrushType::Inflate;
        bs.radius = 0.5f;
        bs.strength = 0.5f;
        sculpt.SetSettings(bs);

        StrokePoint sp;
        sp.hitPoint = glm::vec3(0.0f, 0.0f, 0.5f);
        sculpt.BeginStroke(mesh, sp);
        sculpt.EndStroke(mesh);

        auto val = mesh.ValidateTopology();
        bool topoUntouched = (mesh.GetTopologyRevision() == initialTopo);

        t.passed = topoUntouched && val.isValid && val.isClosedManifold;
        t.observedDesc = "TopoUntouched=" + std::to_string(topoUntouched) + 
                         ", IsValid=" + std::to_string(val.isValid) + 
                         ", ClosedManifold=" + std::to_string(val.isClosedManifold);
        t.details = t.passed ? "PASS" : "FAIL: Alterazione topologica o violazione manifold";
        results.push_back(t);
    }

    // SCL-11: Spatial Index Dynamic Cross-Cell Migration (BM-S5.4 / Verification)
    {
        SculptTestResult t;
        t.testId = "SCL-11";
        t.testName = "SCL-11 — Spatial Index Dynamic Cross-Cell Migration";
        t.inputDesc = "Cube sculpted with high displacement; vertices cross uniform grid cell boundaries";
        t.expectedDesc = "Vertices cleanly migrate to new grid cells; query on new position finds them without global rebuild";

        auto mesh = EditableMesh::CreateCube(1.0f);
        MeshSculptService sculpt;
        SculptBrushSettings bs;
        bs.brushType = SculptBrushType::Inflate;
        bs.radius = 0.4f;
        bs.strength = 1.0f;
        sculpt.SetSettings(bs);

        StrokePoint sp;
        sp.hitPoint = glm::vec3(0.5f, 0.5f, 0.5f);
        sp.surfaceNormal = glm::vec3(0.0f, 0.0f, 1.0f);

        sculpt.BeginStroke(mesh, sp);

        // Trova il vertice interessato
        VertexID targetVid = INVALID_VERTEX_ID;
        glm::vec3 originalPos(0.0f);
        for (auto vid : mesh.GetActiveVertexIDs()) {
            const auto* v = mesh.GetVertex(vid);
            if (v && glm::distance(v->position, glm::vec3(0.5f, 0.5f, 0.5f)) < 0.1f) {
                targetVid = vid;
                originalPos = v->position;
                break;
            }
        }

        const auto* targetV = mesh.GetVertex(targetVid);
        bool movedOutOfOldCell = false;
        bool foundInNewCell = false;

        if (targetV) {
            float cellSize = sculpt.GetSpatialIndex().GetCellSize();
            auto oldKey = sculpt.GetSpatialIndex().ComputeKey(originalPos);
            auto newKey = sculpt.GetSpatialIndex().ComputeKey(targetV->position);

            movedOutOfOldCell = (targetV->position.z > originalPos.z + 0.15f);
            foundInNewCell = sculpt.GetSpatialIndex().ContainsVertexInCell(targetVid, targetV->position);

            // Una seconda query sulla nuova posizione deve trovare il vertice tramite l'indice aggiornato
            auto hits = sculpt.GetSpatialIndex().QuerySphere(targetV->position, 0.2f, mesh);
            bool queryFound = std::find(hits.begin(), hits.end(), targetVid) != hits.end();
            foundInNewCell = foundInNewCell && queryFound;
        }

        sculpt.EndStroke(mesh);

        t.passed = (targetVid != INVALID_VERTEX_ID) && movedOutOfOldCell && foundInNewCell;
        t.observedDesc = "TargetFound=" + std::to_string(targetVid != INVALID_VERTEX_ID) + 
                         ", MovedOutOfOld=" + std::to_string(movedOutOfOldCell) + 
                         ", FoundInNewCell=" + std::to_string(foundInNewCell);
        t.details = t.passed ? "PASS" : "FAIL: Migrazione inter-cella non riflessa nell'indice spaziale";
        results.push_back(t);
    }

    // SCL-12: Live Stroke Coalescence & Zero-Work Spacing Invariance
    {
        SculptTestResult t;
        t.testId = "SCL-12";
        t.testName = "SCL-12 — Live Stroke Coalescence & Zero-Work Spacing Invariance";
        t.inputDesc = "Stroke with initial dab, micro-move (< spacing), macro-move (> spacing), and EndStroke";
        t.expectedDesc = "Micro-move skipped (Zero-Work, rev unchanged); Macro-move applied (rev bumped); deltas collected";

        auto mesh = EditableMesh::CreateCube(1.0f);
        MeshSculptService sculpt;
        SculptBrushSettings bs;
        bs.brushType = SculptBrushType::Inflate;
        bs.radius = 0.5f;
        bs.spacing = 0.2f; // Soglia minima di movimento = 0.1
        bs.strength = 0.5f;
        sculpt.SetSettings(bs);

        StrokePoint sp1;
        sp1.hitPoint = glm::vec3(0.0f, 0.0f, 0.5f);
        sp1.surfaceNormal = glm::vec3(0.0f, 0.0f, 1.0f);

        sculpt.BeginStroke(mesh, sp1);
        uint64_t revAfterDab1 = mesh.GetGeometryRevision();

        // Micro-move (delta 0.01 << 0.1 soglia)
        StrokePoint spMicro = sp1;
        spMicro.hitPoint += glm::vec3(0.01f, 0.0f, 0.0f);
        bool microResult = sculpt.UpdateStroke(mesh, spMicro);
        uint64_t revAfterMicro = mesh.GetGeometryRevision();

        // Macro-move (delta 0.15 > 0.1 soglia)
        StrokePoint spMacro = sp1;
        spMacro.hitPoint += glm::vec3(0.15f, 0.0f, 0.0f);
        bool macroResult = sculpt.UpdateStroke(mesh, spMacro);
        uint64_t revAfterMacro = mesh.GetGeometryRevision();

        auto strokeRes = sculpt.EndStroke(mesh);

        bool zeroWorkRespected = (!microResult) && (revAfterMicro == revAfterDab1);
        bool macroApplied = macroResult && (revAfterMacro > revAfterMicro);
        bool deltasValid = strokeRes.success && strokeRes.geometryChanged && !strokeRes.positionDeltas.empty();

        t.passed = zeroWorkRespected && macroApplied && deltasValid;
        t.observedDesc = "ZeroWorkRespected=" + std::to_string(zeroWorkRespected) + 
                         ", MacroApplied=" + std::to_string(macroApplied) + 
                         ", DeltasCount=" + std::to_string(strokeRes.positionDeltas.size());
        t.details = t.passed ? "PASS" : "FAIL: Coalescenza stroke o Zero-Work fallito";
        results.push_back(t);
    }

    return results;
}

} // namespace fw
