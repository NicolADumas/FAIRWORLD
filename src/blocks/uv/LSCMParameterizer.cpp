#include "pch.h"
#include "LSCMParameterizer.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <glm/gtc/constants.hpp>

namespace fw {

bool LSCMParameterizer::TriangulateIsland(const EditableMesh& mesh, 
                                         const UVIsland& island, 
                                         std::vector<LocalTriangle>& outTriangles, 
                                         std::vector<VertexID>& outVertices, 
                                         std::unordered_map<VertexID, uint32_t>& outVertexMap) {
    outTriangles.clear();
    outVertices.clear();
    outVertexMap.clear();

    std::unordered_set<VertexID> uniqueVerts;

    for (FaceID fId : island.faces) {
        const auto* face = mesh.GetFace(fId);
        if (!face || !IsValid(face->halfEdge)) continue;

        std::vector<HalfEdgeID> faceHes;
        std::vector<VertexID> faceVs;

        HalfEdgeID startHe = face->halfEdge;
        HalfEdgeID curHe = startHe;
        do {
            const auto* he = mesh.GetHalfEdge(curHe);
            if (!he) break;
            faceHes.push_back(curHe);
            faceVs.push_back(he->origin);
            if (uniqueVerts.insert(he->origin).second) {
                outVertexMap[he->origin] = static_cast<uint32_t>(outVertices.size());
                outVertices.push_back(he->origin);
            }
            curHe = he->next;
        } while (curHe != startHe && IsValid(curHe));

        if (faceVs.size() < 3) continue;

        // Triangolazione a ventaglio per poligoni (N-gons / quads)
        for (size_t i = 1; i + 1 < faceVs.size(); ++i) {
            LocalTriangle tri;
            tri.v[0] = faceVs[0];
            tri.v[1] = faceVs[i];
            tri.v[2] = faceVs[i + 1];

            tri.he[0] = faceHes[0];
            tri.he[1] = faceHes[i];
            tri.he[2] = faceHes[i + 1];

            const auto* v0 = mesh.GetVertex(tri.v[0]);
            const auto* v1 = mesh.GetVertex(tri.v[1]);
            const auto* v2 = mesh.GetVertex(tri.v[2]);
            if (!v0 || !v1 || !v2) continue;

            glm::vec3 p0 = v0->position;
            glm::vec3 p1 = v1->position;
            glm::vec3 p2 = v2->position;

            glm::vec3 e01 = p1 - p0;
            glm::vec3 e02 = p2 - p0;
            glm::vec3 crossNorm = glm::cross(e01, e02);
            float lenCross = glm::length(crossNorm);
            tri.area3D = std::max(0.5f * lenCross, 1e-7f);

            float d01 = glm::length(e01);
            if (d01 < 1e-7f) d01 = 1e-6f;
            glm::vec3 axisX = e01 / d01;
            glm::vec3 normN = (lenCross > 1e-7f) ? (crossNorm / lenCross) : glm::vec3(0, 1, 0);
            glm::vec3 axisY = glm::cross(normN, axisX);

            tri.local2D[0] = glm::vec2(0.0f, 0.0f);
            tri.local2D[1] = glm::vec2(d01, 0.0f);
            tri.local2D[2] = glm::vec2(glm::dot(e02, axisX), glm::dot(e02, axisY));

            outTriangles.push_back(tri);
        }
    }

    return !outTriangles.empty() && outVertices.size() >= 3;
}

bool LSCMParameterizer::SelectPinVertices(const EditableMesh& mesh, 
                                         const UVIsland& island, 
                                         VertexID& outPinA, 
                                         VertexID& outPinB) {
    outPinA = INVALID_VERTEX_ID;
    outPinB = INVALID_VERTEX_ID;

    std::vector<VertexID> candidates;
    for (HalfEdgeID heId : island.boundaryHalfEdges) {
        const auto* he = mesh.GetHalfEdge(heId);
        if (he && IsValid(he->origin)) {
            candidates.push_back(he->origin);
        }
    }

    if (candidates.size() < 2) {
        // Se non ci sono sufficienti boundary half-edges, usa i vertici delle facce
        std::unordered_set<VertexID> allVerts;
        for (FaceID fId : island.faces) {
            const auto* f = mesh.GetFace(fId);
            if (!f) continue;
            HalfEdgeID h = f->halfEdge;
            HalfEdgeID startH = h;
            do {
                const auto* he = mesh.GetHalfEdge(h);
                if (!he) break;
                allVerts.insert(he->origin);
                h = he->next;
            } while (h != startH && IsValid(h));
        }
        candidates.assign(allVerts.begin(), allVerts.end());
    }

    if (candidates.size() < 2) return false;

    float maxDistSq = -1.0f;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto* va = mesh.GetVertex(candidates[i]);
        if (!va) continue;
        for (size_t j = i + 1; j < candidates.size(); ++j) {
            const auto* vb = mesh.GetVertex(candidates[j]);
            if (!vb) continue;
            glm::vec3 delta = va->position - vb->position;
            float d2 = glm::dot(delta, delta);
            if (d2 > maxDistSq) {
                maxDistSq = d2;
                outPinA = candidates[i];
                outPinB = candidates[j];
            }
        }
    }

    return IsValid(outPinA) && IsValid(outPinB) && outPinA != outPinB;
}

LSCMResult LSCMParameterizer::ParameterizeIsland(EditableMesh& mesh, 
                                                UVIsland& island, 
                                                const LSCMOptions& options) {
    LSCMResult result;
    if (island.faces.empty()) {
        result.diagnosticMessage = "Isola vuota.";
        return result;
    }

    std::vector<LocalTriangle> triangles;
    std::vector<VertexID> vertices;
    std::unordered_map<VertexID, uint32_t> vertexMap;

    if (!TriangulateIsland(mesh, island, triangles, vertices, vertexMap)) {
        if (options.fallbackToPlanarOnFailure) {
            return ParameterizePlanarFallback(mesh, island);
        }
        result.diagnosticMessage = "Triangolazione dell'isola fallita.";
        return result;
    }

    VertexID pinA = INVALID_VERTEX_ID;
    VertexID pinB = INVALID_VERTEX_ID;
    if (!SelectPinVertices(mesh, island, pinA, pinB)) {
        if (options.fallbackToPlanarOnFailure) {
            return ParameterizePlanarFallback(mesh, island);
        }
        result.diagnosticMessage = "Impossibile selezionare vertici di ancoraggio per LSCM.";
        return result;
    }

    uint32_t pinAIdx = vertexMap[pinA];
    uint32_t pinBIdx = vertexMap[pinB];

    const auto* posA = mesh.GetVertex(pinA);
    const auto* posB = mesh.GetVertex(pinB);
    float pinDist = (posA && posB) ? glm::distance(posA->position, posB->position) : 1.0f;
    if (pinDist < 1e-5f) pinDist = 1.0f;

    glm::vec2 pinAUV(0.0f, 0.0f);
    glm::vec2 pinBUV(pinDist * options.pinDistanceScale, 0.0f);

    uint32_t N = static_cast<uint32_t>(vertices.size());
    std::vector<int32_t> vertexToFreeIndex(N, -1);
    uint32_t freeCount = 0;
    for (uint32_t i = 0; i < N; ++i) {
        if (i != pinAIdx && i != pinBIdx) {
            vertexToFreeIndex[i] = freeCount++;
        }
    }

    if (freeCount == 0) {
        return ParameterizePlanarFallback(mesh, island);
    }

    uint32_t numUnknowns = freeCount * 2;
    std::vector<std::unordered_map<uint32_t, float>> AtA(numUnknowns);
    std::vector<float> Atb(numUnknowns, 0.0f);

    auto addAtA = [&](uint32_t row, uint32_t col, float val) {
        AtA[row][col] += val;
    };

    // Assemblaggio del sistema conforme di Cauchy-Riemann (BM-UV3.4)
    for (const auto& tri : triangles) {
        glm::vec2 z0 = tri.local2D[0];
        glm::vec2 z1 = tri.local2D[1];
        glm::vec2 z2 = tri.local2D[2];

        // Vettori di Cauchy-Riemann: W0 = z1 - z2, W1 = z2 - z0, W2 = z0 - z1
        glm::vec2 W[3] = { z1 - z2, z2 - z0, z0 - z1 };
        float invSqrt2Area = 1.0f / std::sqrt(std::max(2.0f * tri.area3D, 1e-7f));

        float coeffX[3], coeffY[3];
        for (int j = 0; j < 3; ++j) {
            coeffX[j] = W[j].x * invSqrt2Area;
            coeffY[j] = W[j].y * invSqrt2Area;
        }

        // 2 equazioni per triangolo:
        // Eq 0: sum(coeffX[j] * u[j] - coeffY[j] * v[j]) = 0
        // Eq 1: sum(coeffY[j] * u[j] + coeffX[j] * v[j]) = 0
        struct Entry { uint32_t col; float val; };
        std::vector<Entry> row0, row1;
        float b0 = 0.0f, b1 = 0.0f;

        for (int j = 0; j < 3; ++j) {
            uint32_t vIdx = vertexMap[tri.v[j]];
            if (vIdx == pinAIdx) {
                b0 -= (coeffX[j] * pinAUV.x - coeffY[j] * pinAUV.y);
                b1 -= (coeffY[j] * pinAUV.x + coeffX[j] * pinAUV.y);
            } else if (vIdx == pinBIdx) {
                b0 -= (coeffX[j] * pinBUV.x - coeffY[j] * pinBUV.y);
                b1 -= (coeffY[j] * pinBUV.x + coeffX[j] * pinBUV.y);
            } else {
                uint32_t fIdx = vertexToFreeIndex[vIdx];
                uint32_t uCol = fIdx * 2;
                uint32_t vCol = fIdx * 2 + 1;

                row0.push_back({ uCol, coeffX[j] });
                row0.push_back({ vCol, -coeffY[j] });

                row1.push_back({ uCol, coeffY[j] });
                row1.push_back({ vCol, coeffX[j] });
            }
        }

        // Accumula in AtA e Atb per row0
        for (const auto& e : row0) {
            Atb[e.col] += e.val * b0;
            for (const auto& e2 : row0) {
                addAtA(e.col, e2.col, e.val * e2.val);
            }
        }
        // Accumula in AtA e Atb per row1
        for (const auto& e : row1) {
            Atb[e.col] += e.val * b1;
            for (const auto& e2 : row1) {
                addAtA(e.col, e2.col, e.val * e2.val);
            }
        }
    }

    // Risoluzione tramite Preconditioned Conjugate Gradient (Jacobi diagonal preconditioner)
    std::vector<float> P(numUnknowns, 1.0f);
    for (uint32_t i = 0; i < numUnknowns; ++i) {
        auto it = AtA[i].find(i);
        if (it != AtA[i].end() && std::abs(it->second) > 1e-12f) {
            P[i] = 1.0f / it->second;
        }
    }

    std::vector<float> x(numUnknowns, 0.0f);
    std::vector<float> r = Atb; // poiche x_0 = 0, r_0 = Atb - A*0 = Atb
    std::vector<float> z(numUnknowns, 0.0f);
    for (uint32_t i = 0; i < numUnknowns; ++i) z[i] = P[i] * r[i];
    std::vector<float> p = z;

    auto dot = [](const std::vector<float>& a, const std::vector<float>& b) {
        float sum = 0.0f;
        for (size_t i = 0; i < a.size(); ++i) sum += a[i] * b[i];
        return sum;
    };

    float rho = dot(r, z);
    float initialResNorm = std::sqrt(dot(r, r));
    uint32_t iters = 0;
    bool converged = (initialResNorm < options.tolerance);

    while (!converged && iters < options.maxIterations) {
        iters++;

        // q = AtA * p
        std::vector<float> q(numUnknowns, 0.0f);
        for (uint32_t i = 0; i < numUnknowns; ++i) {
            float qi = 0.0f;
            for (const auto& [col, val] : AtA[i]) {
                qi += val * p[col];
            }
            q[i] = qi;
        }

        float pDotQ = dot(p, q);
        if (std::abs(pDotQ) < 1e-25f) break;

        float alpha = rho / pDotQ;
        for (uint32_t i = 0; i < numUnknowns; ++i) {
            x[i] += alpha * p[i];
            r[i] -= alpha * q[i];
        }

        float resNorm = std::sqrt(dot(r, r));
        if (resNorm <= options.tolerance * initialResNorm || resNorm < 1e-7f) {
            converged = true;
            break;
        }

        for (uint32_t i = 0; i < numUnknowns; ++i) z[i] = P[i] * r[i];
        float rhoNew = dot(r, z);
        float beta = rhoNew / rho;
        for (uint32_t i = 0; i < numUnknowns; ++i) {
            p[i] = z[i] + beta * p[i];
        }
        rho = rhoNew;
    }

    result.iterationsExecuted = iters;
    result.finalResidual = std::sqrt(dot(r, r));

    // Recupera le coordinate UV per tutti i vertici dell'isola
    std::vector<glm::vec2> vertexUVs(N);
    for (uint32_t i = 0; i < N; ++i) {
        if (i == pinAIdx) {
            vertexUVs[i] = pinAUV;
        } else if (i == pinBIdx) {
            vertexUVs[i] = pinBUV;
        } else {
            uint32_t fIdx = vertexToFreeIndex[i];
            float u = x[fIdx * 2];
            float v = x[fIdx * 2 + 1];
            if (std::isnan(u) || std::isnan(v) || std::isinf(u) || std::isinf(v)) {
                if (options.fallbackToPlanarOnFailure) {
                    return ParameterizePlanarFallback(mesh, island);
                }
                result.diagnosticMessage = "LSCM ha prodotto coordinate numeriche NaN/Inf.";
                return result;
            }
            vertexUVs[i] = glm::vec2(u, v);
        }
    }

    // Normalizzazione UV nello spazio locale [0, 1] dell'isola
    float minU = 1e20f, maxU = -1e20f;
    float minV = 1e20f, maxV = -1e20f;
    for (const auto& uv : vertexUVs) {
        minU = std::min(minU, uv.x);
        maxU = std::max(maxU, uv.x);
        minV = std::min(minV, uv.y);
        maxV = std::max(maxV, uv.y);
    }

    float width = maxU - minU;
    float height = maxV - minV;
    if (width < 1e-6f) width = 1.0f;
    if (height < 1e-6f) height = 1.0f;

    island.localUVBounds = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);

    // Scrittura delle coordinate UV nei corner autoritativi di EditableMesh
    for (const auto& tri : triangles) {
        for (int j = 0; j < 3; ++j) {
            auto* he = mesh.GetHalfEdgeMutable(tri.he[j]);
            if (!he) continue;
            uint32_t vIdx = vertexMap[tri.v[j]];
            glm::vec2 rawUV = vertexUVs[vIdx];
            glm::vec2 normUV((rawUV.x - minU) / width, (rawUV.y - minV) / height);
            he->corner.uv = normUV;
        }
    }

    // Calcolo distorsione e verifica flipped triangles
    island.distortion = ComputeDistortion(mesh, island);
    result.distortion = island.distortion;
    result.pinnedVertexCount = 2;
    result.success = true;

    // Se l'unwrapping presenta un numero eccessivo di triangoli invertiti, prova il fallback planare
    if (island.distortion.flippedTriangleCount > (triangles.size() / 4) && options.fallbackToPlanarOnFailure) {
        return ParameterizePlanarFallback(mesh, island);
    }

    return result;
}

LSCMResult LSCMParameterizer::ParameterizePlanarFallback(EditableMesh& mesh, UVIsland& island) {
    LSCMResult res;
    res.usedFallback = true;

    if (island.faces.empty()) {
        res.diagnosticMessage = "Isola vuota nel fallback planare.";
        return res;
    }

    // Calcola il centroide e la normale media ponderata dell'isola
    glm::vec3 centroid(0.0f);
    glm::vec3 avgNormal(0.0f);
    float totalArea = 0.0f;

    for (FaceID fId : island.faces) {
        const auto* f = mesh.GetFace(fId);
        if (!f) continue;
        centroid += f->faceNormal * f->area;
        avgNormal += f->faceNormal * f->area;
        totalArea += f->area;
    }

    if (totalArea > 1e-6f) {
        avgNormal = glm::normalize(avgNormal);
    } else {
        avgNormal = glm::vec3(0, 1, 0);
    }

    // Costruisci base ortonormale (T, B, N)
    glm::vec3 up = (std::abs(avgNormal.y) < 0.9f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 tangent = glm::normalize(glm::cross(up, avgNormal));
    glm::vec3 bitangent = glm::normalize(glm::cross(avgNormal, tangent));

    float minU = 1e20f, maxU = -1e20f;
    float minV = 1e20f, maxV = -1e20f;

    // Prima passata per calcolare i bounds planari
    for (FaceID fId : island.faces) {
        const auto* f = mesh.GetFace(fId);
        if (!f || !IsValid(f->halfEdge)) continue;
        HalfEdgeID h = f->halfEdge;
        HalfEdgeID startH = h;
        do {
            const auto* he = mesh.GetHalfEdge(h);
            if (!he) break;
            const auto* v = mesh.GetVertex(he->origin);
            if (v) {
                float u = glm::dot(v->position, tangent);
                float vCoord = glm::dot(v->position, bitangent);
                minU = std::min(minU, u);
                maxU = std::max(maxU, u);
                minV = std::min(minV, vCoord);
                maxV = std::max(maxV, vCoord);
            }
            h = he->next;
        } while (h != startH && IsValid(h));
    }

    float width = maxU - minU;
    float height = maxV - minV;
    if (width < 1e-6f) width = 1.0f;
    if (height < 1e-6f) height = 1.0f;

    // Seconda passata: scrittura corner UV normalizzati
    for (FaceID fId : island.faces) {
        const auto* f = mesh.GetFace(fId);
        if (!f || !IsValid(f->halfEdge)) continue;
        HalfEdgeID h = f->halfEdge;
        HalfEdgeID startH = h;
        do {
            auto* he = mesh.GetHalfEdgeMutable(h);
            if (!he) break;
            const auto* v = mesh.GetVertex(he->origin);
            if (v) {
                float u = (glm::dot(v->position, tangent) - minU) / width;
                float vCoord = (glm::dot(v->position, bitangent) - minV) / height;
                he->corner.uv = glm::vec2(u, vCoord);
            }
            h = he->next;
        } while (h != startH && IsValid(h));
    }

    island.localUVBounds = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    island.distortion = ComputeDistortion(mesh, island);

    res.success = true;
    res.distortion = island.distortion;
    res.diagnosticMessage = "Completato con successo tramite Planar Fallback.";
    return res;
}

IslandDistortion LSCMParameterizer::ComputeDistortion(const EditableMesh& mesh, const UVIsland& island) {
    IslandDistortion dist;
    dist.areaDistortion = 0.0f;
    dist.angleDistortion = 0.0f;
    dist.maxStretch = 1.0f;
    dist.flippedTriangleCount = 0;
    dist.isAcceptable = true;

    float totalArea2D = 0.0f;
    float totalArea3D = 0.0f;
    uint32_t triangleCount = 0;

    for (FaceID fId : island.faces) {
        const auto* f = mesh.GetFace(fId);
        if (!f || !IsValid(f->halfEdge)) continue;

        std::vector<glm::vec3> pos;
        std::vector<glm::vec2> uvs;

        HalfEdgeID h = f->halfEdge;
        HalfEdgeID startH = h;
        do {
            const auto* he = mesh.GetHalfEdge(h);
            if (!he) break;
            const auto* v = mesh.GetVertex(he->origin);
            if (v) {
                pos.push_back(v->position);
                uvs.push_back(he->corner.uv);
            }
            h = he->next;
        } while (h != startH && IsValid(h));

        if (pos.size() < 3) continue;

        // Valuta ogni triangolo della triangolazione perimetrale
        for (size_t i = 1; i + 1 < pos.size(); ++i) {
            triangleCount++;
            glm::vec3 p0 = pos[0], p1 = pos[i], p2 = pos[i + 1];
            glm::vec2 uv0 = uvs[0], uv1 = uvs[i], uv2 = uvs[i + 1];

            float a3D = 0.5f * glm::length(glm::cross(p1 - p0, p2 - p0));
            float a2D = 0.5f * ((uv1.x - uv0.x) * (uv2.y - uv0.y) - (uv2.x - uv0.x) * (uv1.y - uv0.y));

            totalArea3D += a3D;
            totalArea2D += std::abs(a2D);

            // Rilevamento orientamento UV invertito
            if (a2D < -1e-7f) {
                dist.flippedTriangleCount++;
            }

            // Stima approssimata dello stretch del Jacobiano 3D->2D
            float l3D = glm::distance(p1, p0) + glm::distance(p2, p1) + glm::distance(p0, p2);
            float l2D = glm::distance(uv1, uv0) + glm::distance(uv2, uv1) + glm::distance(uv0, uv2);
            if (l3D > 1e-6f) {
                float localStretch = l2D / l3D;
                dist.maxStretch = std::max(dist.maxStretch, localStretch);
            }
        }
    }

    if (totalArea3D > 1e-6f && totalArea2D > 1e-6f) {
        dist.areaDistortion = std::abs((totalArea2D / totalArea3D) - 1.0f);
    }

    if (dist.flippedTriangleCount > 0) {
        dist.isAcceptable = false;
    }

    return dist;
}

} // namespace fw
