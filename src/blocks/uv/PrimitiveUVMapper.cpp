#include "pch.h"
#include "PrimitiveUVMapper.h"
#include <cmath>
#include <algorithm>
#include <unordered_set>
#include <sstream>
#include <glm/gtc/constants.hpp>

namespace fw {

// =============================================================================
// METRICHE E HELPER INTERNI (BM-UV2.10)
// =============================================================================

float PrimitiveUVMapper::ComputeTriangleArea2D(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
    return 0.5f * ((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y));
}

void PrimitiveUVMapper::CorrectSeamWrapQuad(glm::vec2& uv0, glm::vec2& uv1, glm::vec2& uv2, glm::vec2& uv3) {
    float minU = std::min({uv0.x, uv1.x, uv2.x, uv3.x});
    float maxU = std::max({uv0.x, uv1.x, uv2.x, uv3.x});
    if ((maxU - minU) > 0.5f) {
        if (uv0.x < 0.5f) uv0.x += 1.0f;
        if (uv1.x < 0.5f) uv1.x += 1.0f;
        if (uv2.x < 0.5f) uv2.x += 1.0f;
        if (uv3.x < 0.5f) uv3.x += 1.0f;
    }
}

void PrimitiveUVMapper::MarkSeamEdge(EditableMesh& mesh, EdgeID edgeId, float weight) {
    auto* e = mesh.GetEdgeMutable(edgeId);
    if (e) {
        e->seamWeight = weight;
    }
}

void PrimitiveUVMapper::ClearDerivedSeams(EditableMesh& mesh) {
    for (EdgeID eId : mesh.GetActiveEdgeIDs()) {
        auto* e = mesh.GetEdgeMutable(eId);
        if (e && e->seamWeight > 0.0f && e->seamWeight < 0.999f) {
            e->seamWeight = 0.0f; // Azzera solo le seam derivate, preservando quelle manuali = 1.0f esplicite
        }
    }
}

// =============================================================================
// VALIDAZIONE UV E RILEVAMENTO DEGENERAZIONI (BM-UV2.10)
// =============================================================================

bool PrimitiveUVMapper::ValidateGeneratedUV(const EditableMesh& mesh, 
                                            const UVLayout& layout, 
                                            UVValidationReport& report) {
    report.Reset();
    if (mesh.GetFaceCount() == 0) {
        report.AddWarning("Mesh priva di facce attive.");
        return true;
    }

    report.minU = std::numeric_limits<float>::infinity();
    report.maxU = -std::numeric_limits<float>::infinity();
    report.minV = std::numeric_limits<float>::infinity();
    report.maxV = -std::numeric_limits<float>::infinity();

    float totalAreaDist = 0.0f;
    uint32_t faceCountEvaluated = 0;

    for (FaceID fId : mesh.GetActiveFaceIDs()) {
        const auto* face = mesh.GetFace(fId);
        if (!face) continue;

        // 1. Verifica appartenenza a un'isola
        auto itIsland = layout.faceToIslandMap.find(fId);
        if (itIsland == layout.faceToIslandMap.end()) {
            report.unmappedFaceCount++;
            report.AddError("Faccia #" + std::to_string(fId.index) + " non associata ad alcuna UVIsland.");
        }

        // 2. Controllo corner UV e assenza di NaN/Inf
        std::vector<HalfEdgeID> hes = mesh.GetFaceHalfEdges(fId);
        std::vector<glm::vec2> faceUVs;
        faceUVs.reserve(hes.size());

        for (HalfEdgeID heId : hes) {
            const auto* he = mesh.GetHalfEdge(heId);
            if (!he) continue;
            report.totalCornersChecked++;
            glm::vec2 uv = he->corner.uv;

            if (std::isnan(uv.x) || std::isnan(uv.y) || std::isinf(uv.x) || std::isinf(uv.y)) {
                report.nanInfCount++;
                report.AddError("Corner UV NaN/Inf rilevato su HalfEdge #" + std::to_string(heId.index));
            } else {
                report.minU = std::min(report.minU, uv.x);
                report.maxU = std::max(report.maxU, uv.x);
                report.minV = std::min(report.minV, uv.y);
                report.maxV = std::max(report.maxV, uv.y);
            }
            faceUVs.push_back(uv);
        }

        // 3. Triangolazione e verifica triangoli degeneri / invertiti
        std::vector<uint32_t> triIndices;
        if (mesh.TriangulateFace(fId, triIndices) && !faceUVs.empty()) {
            for (size_t t = 0; t + 2 < triIndices.size(); t += 3) {
                uint32_t i0 = triIndices[t];
                uint32_t i1 = triIndices[t + 1];
                uint32_t i2 = triIndices[t + 2];
                if (i0 < faceUVs.size() && i1 < faceUVs.size() && i2 < faceUVs.size()) {
                    float a2D = ComputeTriangleArea2D(faceUVs[i0], faceUVs[i1], faceUVs[i2]);
                    if (std::abs(a2D) < 1e-7f) {
                        report.degenerateUVTriangles++;
                    }
                    if (a2D < -1e-6f) {
                        report.invertedUVTriangles++;
                    }
                }
            }
        }

        faceCountEvaluated++;
    }

    if (faceCountEvaluated > 0) {
        report.averageAreaDistortion = totalAreaDist / static_cast<float>(faceCountEvaluated);
    }

    report.isValid = report.errors.empty() && (report.nanInfCount == 0) && (report.unmappedFaceCount == 0);
    return report.isValid;
}

// =============================================================================
// ANALISI PRIMITIVA (BM-UV2.3)
// =============================================================================

UVMappingStrategy PrimitiveUVMapper::AnalyzePrimitive(const EditableMesh& mesh, 
                                                     ShapeType shapeHint, 
                                                     const ShapeParameters* /*params*/) {
    if (shapeHint != ShapeType::None) {
        switch (shapeHint) {
            case ShapeType::Cube:
            case ShapeType::Cuboid:
            case ShapeType::Parallelepiped:
            case ShapeType::Ungula:
            case ShapeType::ConvexHull:
                return UVMappingStrategy::Box;
            case ShapeType::Pyramid:
            case ShapeType::PyramidFrustum:
                return UVMappingStrategy::Planar;
            case ShapeType::Cylinder:
            case ShapeType::HollowCylinder:
            case ShapeType::SlicedCylinder:
            case ShapeType::Barrel:
                return UVMappingStrategy::Cylindrical;
            case ShapeType::Cone:
            case ShapeType::ConeFrustum:
                return UVMappingStrategy::Conical;
            case ShapeType::Sphere:
            case ShapeType::SphericalZone:
            case ShapeType::SphericalSegment:
            case ShapeType::SphericalSector:
            case ShapeType::Capsule:
            case ShapeType::LegacySuperSphere:
                return UVMappingStrategy::Hemisphere;
            default:
                return UVMappingStrategy::Parametric;
        }
    }

    // Analisi euristica basata sulla topologia di EditableMesh
    size_t fCount = mesh.GetFaceCount();
    if (fCount == 6) {
        return UVMappingStrategy::Box; // Cubo o parallelepipedo canonico
    }
    return UVMappingStrategy::Planar;
}

// =============================================================================
// 1. PROIEZIONE PLANARE (BM-UV2.3)
// =============================================================================

PrimitiveMappingResult PrimitiveUVMapper::GeneratePlanarMapping(EditableMesh& mesh, 
                                                                UVLayout& outLayout, 
                                                                const glm::vec3& planeNormal,
                                                                const PrimitiveUVOptions& options) {
    PrimitiveMappingResult res;
    outLayout.Clear();
    outLayout.strategyUsed = UVMappingStrategy::Planar;
    outLayout.meshTopologyRevision = mesh.GetTopologyRevision();

    glm::vec3 n = glm::length(planeNormal) > 1e-5f ? glm::normalize(planeNormal) : glm::vec3(0, 1, 0);

    // Costruisce una terna ortonormale (T, B, N)
    glm::vec3 up = (std::abs(n.y) > 0.9f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    glm::vec3 tangent = glm::normalize(glm::cross(up, n));
    glm::vec3 bitangent = glm::normalize(glm::cross(n, tangent));

    AABB bounds = mesh.ComputeAABB();
    Vec3 bC = bounds.center();
    Vec3 bS = bounds.size();
    glm::vec3 center(bC.x, bC.y, bC.z);
    glm::vec3 size(bS.x, bS.y, bS.z);
    float extentU = std::max(0.001f, std::abs(glm::dot(size, tangent)));
    float extentV = std::max(0.001f, std::abs(glm::dot(size, bitangent)));

    UVIsland island;
    island.id = 0;
    island.semanticName = "PlanarIsland";
    island.localUVBounds = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    island.atlasTransform = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);

    for (FaceID fId : mesh.GetActiveFaceIDs()) {
        island.faces.push_back(fId);
        outLayout.faceToIslandMap[fId] = island.id;

        for (HalfEdgeID heId : mesh.GetFaceHalfEdges(fId)) {
            auto* he = mesh.GetHalfEdgeMutable(heId);
            if (!he) continue;
            const auto* v = mesh.GetVertex(he->origin);
            if (!v) continue;

            glm::vec3 p = v->position - center;
            float u = (glm::dot(p, tangent) / extentU) + 0.5f;
            float vCoord = (glm::dot(p, bitangent) / extentV) + 0.5f;
            he->corner.uv = glm::clamp(glm::vec2(u, vCoord), 0.0f, 1.0f);
        }
    }

    outLayout.islands.push_back(island);
    mesh.IncrementAttributeRevision();
    outLayout.meshAttributeRevision = mesh.GetAttributeRevision();

    ValidateGeneratedUV(mesh, outLayout, res.validation);
    res.layout = outLayout;
    res.status = res.validation.isValid ? PrimitiveMappingStatus::SUCCESS : PrimitiveMappingStatus::PARTIAL_SUCCESS;
    res.success = res.validation.isValid;
    return res;
}

// =============================================================================
// 2. FORME POLIGONALI: BOX MAPPING (BM-UV2.4)
// Cube, Cuboid, Parallelepiped, Ungula, ConvexHull
// =============================================================================

PrimitiveMappingResult PrimitiveUVMapper::GenerateBoxMapping(EditableMesh& mesh, 
                                                             UVLayout& outLayout, 
                                                             const glm::vec3& size,
                                                             const PrimitiveUVOptions& options) {
    PrimitiveMappingResult res;
    outLayout.Clear();
    outLayout.strategyUsed = UVMappingStrategy::Box;
    outLayout.meshTopologyRevision = mesh.GetTopologyRevision();

    // 6 semantic islands disposte in griglia 3x2 non-overlapping
    // 0: Back (-Z), 1: Front (+Z), 2: Left (-X), 3: Right (+X), 4: Bottom (-Y), 5: Top (+Y)
    const char* names[6] = { "Back (-Z)", "Front (+Z)", "Left (-X)", "Right (+X)", "Bottom (-Y)", "Top (+Y)" };
    for (uint32_t i = 0; i < 6; ++i) {
        UVIsland isl;
        isl.id = i;
        isl.semanticName = names[i];
        isl.semanticSurfaceId = i;
        isl.localUVBounds = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        float col = static_cast<float>(i % 3);
        float row = static_cast<float>(i / 3);
        isl.atlasTransform = glm::vec4(1.0f / 3.0f, 1.0f / 2.0f, col / 3.0f, row / 2.0f);
        outLayout.islands.push_back(isl);
    }

    AABB bounds = mesh.ComputeAABB();
    Vec3 bC = bounds.center();
    Vec3 bS = bounds.size();
    glm::vec3 bCenter(bC.x, bC.y, bC.z);
    glm::vec3 bSize(bS.x, bS.y, bS.z);
    if (bSize.x < 1e-4f) bSize.x = size.x > 0 ? size.x : 1.0f;
    if (bSize.y < 1e-4f) bSize.y = size.y > 0 ? size.y : 1.0f;
    if (bSize.z < 1e-4f) bSize.z = size.z > 0 ? size.z : 1.0f;

    for (FaceID fId : mesh.GetActiveFaceIDs()) {
        const auto* face = mesh.GetFace(fId);
        if (!face) continue;

        // Determina a quale delle 6 direzioni appartiene la faccia
        glm::vec3 n = face->faceNormal;
        uint32_t islandIdx = 0;
        if (face->surfaceId < 6) {
            islandIdx = face->surfaceId;
        } else {
            glm::vec3 absN = glm::abs(n);
            if (absN.z >= absN.x && absN.z >= absN.y) {
                islandIdx = (n.z >= 0.0f) ? 1 : 0; // Front (+Z) / Back (-Z)
            } else if (absN.x >= absN.y && absN.x >= absN.z) {
                islandIdx = (n.x >= 0.0f) ? 3 : 2; // Right (+X) / Left (-X)
            } else {
                islandIdx = (n.y >= 0.0f) ? 5 : 4; // Top (+Y) / Bottom (-Y)
            }
        }

        outLayout.islands[islandIdx].faces.push_back(fId);
        outLayout.faceToIslandMap[fId] = islandIdx;

        float col = static_cast<float>(islandIdx % 3);
        float row = static_cast<float>(islandIdx / 3);
        float tileW = 1.0f / 3.0f;
        float tileH = 1.0f / 2.0f;

        for (HalfEdgeID heId : mesh.GetFaceHalfEdges(fId)) {
            auto* he = mesh.GetHalfEdgeMutable(heId);
            if (!he) continue;
            const auto* v = mesh.GetVertex(he->origin);
            if (!v) continue;

            glm::vec3 p = v->position;
            float uLoc = 0.0f;
            float vLoc = 0.0f;

            switch (islandIdx) {
                case 0: // Back (-Z)
                    uLoc = 1.0f - ((p.x - bounds.min.x) / bSize.x);
                    vLoc = (p.y - bounds.min.y) / bSize.y;
                    break;
                case 1: // Front (+Z)
                    uLoc = (p.x - bounds.min.x) / bSize.x;
                    vLoc = (p.y - bounds.min.y) / bSize.y;
                    break;
                case 2: // Left (-X)
                    uLoc = (p.z - bounds.min.z) / bSize.z;
                    vLoc = (p.y - bounds.min.y) / bSize.y;
                    break;
                case 3: // Right (+X)
                    uLoc = 1.0f - ((p.z - bounds.min.z) / bSize.z);
                    vLoc = (p.y - bounds.min.y) / bSize.y;
                    break;
                case 4: // Bottom (-Y)
                    uLoc = (p.x - bounds.min.x) / bSize.x;
                    vLoc = 1.0f - ((p.z - bounds.min.z) / bSize.z);
                    break;
                case 5: // Top (+Y)
                    uLoc = (p.x - bounds.min.x) / bSize.x;
                    vLoc = (p.z - bounds.min.z) / bSize.z;
                    break;
            }

            uLoc = glm::clamp(uLoc, 0.0f, 1.0f);
            vLoc = glm::clamp(vLoc, 0.0f, 1.0f);

            // Mappatura nello slot globale non-overlapping
            he->corner.uv = glm::vec2(
                (col * tileW) + (uLoc * tileW),
                (row * tileH) + (vLoc * tileH)
            );
        }
    }

    // Marcatura cuciture: tutti gli spigoli tra facce di isole differenti sono cuciture
    if (options.markSeamEdges) {
        for (EdgeID eId : mesh.GetActiveEdgeIDs()) {
            const auto* e = mesh.GetEdge(eId);
            if (!e) continue;
            const auto* he0 = mesh.GetHalfEdge(e->halfEdge);
            const auto* he1 = mesh.GetHalfEdge(e->twin);
            if (he0 && he1 && IsValid(he0->face) && IsValid(he1->face)) {
                if (outLayout.faceToIslandMap[he0->face] != outLayout.faceToIslandMap[he1->face]) {
                    MarkSeamEdge(mesh, eId, 1.0f);
                }
            } else if (he0 && !IsValid(e->twin)) {
                MarkSeamEdge(mesh, eId, 1.0f); // Bordo aperto
            }
        }
    }

    mesh.IncrementAttributeRevision();
    outLayout.meshAttributeRevision = mesh.GetAttributeRevision();

    ValidateGeneratedUV(mesh, outLayout, res.validation);
    res.layout = outLayout;
    res.status = res.validation.isValid ? PrimitiveMappingStatus::SUCCESS : PrimitiveMappingStatus::PARTIAL_SUCCESS;
    res.success = res.validation.isValid;
    return res;
}

// =============================================================================
// 3. FORME PIRAMIDALI: PYRAMID & FRUSTUM (BM-UV2.4)
// =============================================================================

PrimitiveMappingResult PrimitiveUVMapper::GeneratePyramidMapping(EditableMesh& mesh, 
                                                                 UVLayout& outLayout, 
                                                                 float width, float depth, float height,
                                                                 const glm::vec2& apexOffset,
                                                                 bool isFrustum,
                                                                 float topW, float topD,
                                                                 const PrimitiveUVOptions& options) {
    PrimitiveMappingResult res;
    outLayout.Clear();
    outLayout.strategyUsed = UVMappingStrategy::Planar;
    outLayout.meshTopologyRevision = mesh.GetTopologyRevision();

    // Isole: Base (0), 4 Facce Laterali (1..4), Tappo Superiore se Frustum (5)
    uint32_t numIslands = isFrustum ? 6 : 5;
    for (uint32_t i = 0; i < numIslands; ++i) {
        UVIsland isl;
        isl.id = i;
        isl.semanticSurfaceId = i;
        isl.localUVBounds = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        isl.atlasTransform = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
        outLayout.islands.push_back(isl);
    }
    outLayout.islands[0].semanticName = "Base Cap (-Y)";
    outLayout.islands[1].semanticName = "Lateral Face (North)";
    outLayout.islands[2].semanticName = "Lateral Face (East)";
    outLayout.islands[3].semanticName = "Lateral Face (South)";
    outLayout.islands[4].semanticName = "Lateral Face (West)";
    if (isFrustum) outLayout.islands[5].semanticName = "Top Cap (+Y)";

    AABB bounds = mesh.ComputeAABB();
    float halfH = (height > 1e-4f ? height : bounds.size().y) * 0.5f;

    for (FaceID fId : mesh.GetActiveFaceIDs()) {
        const auto* face = mesh.GetFace(fId);
        if (!face) continue;

        glm::vec3 n = face->faceNormal;
        uint32_t islandIdx = 0;

        if (n.y < -0.6f) {
            islandIdx = 0; // Base
        } else if (isFrustum && n.y > 0.6f) {
            islandIdx = 5; // Top Cap
        } else {
            // Faccia laterale inclinata
            if (std::abs(n.z) >= std::abs(n.x)) {
                islandIdx = (n.z >= 0.0f) ? 1 : 3;
            } else {
                islandIdx = (n.x >= 0.0f) ? 2 : 4;
            }
        }

        outLayout.islands[islandIdx].faces.push_back(fId);
        outLayout.faceToIslandMap[fId] = islandIdx;

        // Distribuzione nell'atlas: riga inferiore per basi, riga superiore per facce laterali
        float uOffset = 0.0f, vOffset = 0.0f, uScale = 0.25f, vScale = 0.5f;
        if (islandIdx == 0) {
            uOffset = 0.0f; vOffset = 0.0f; uScale = 0.5f; vScale = 0.5f;
        } else if (islandIdx == 5) {
            uOffset = 0.5f; vOffset = 0.0f; uScale = 0.5f; vScale = 0.5f;
        } else {
            uint32_t latIdx = islandIdx - 1;
            uOffset = latIdx * 0.25f;
            vOffset = 0.5f;
            uScale = 0.25f;
            vScale = 0.5f;
        }

        for (HalfEdgeID heId : mesh.GetFaceHalfEdges(fId)) {
            auto* he = mesh.GetHalfEdgeMutable(heId);
            if (!he) continue;
            const auto* v = mesh.GetVertex(he->origin);
            if (!v) continue;

            glm::vec3 p = v->position;
            float uLoc = 0.5f, vLoc = 0.5f;

            if (islandIdx == 0 || islandIdx == 5) {
                // Proiezione planare orizzontale
                float w = (islandIdx == 0 ? width : topW);
                float d = (islandIdx == 0 ? depth : topD);
                if (w < 1e-4f) w = 1.0f;
                if (d < 1e-4f) d = 1.0f;
                uLoc = (p.x / w) + 0.5f;
                vLoc = (p.z / d) + 0.5f;
            } else {
                // Sviluppo conforme della faccia laterale
                float hRatio = glm::clamp((p.y + halfH) / (2.0f * halfH), 0.0f, 1.0f);
                vLoc = hRatio;
                // Lungo il perimetro orizzontale
                if (islandIdx == 1 || islandIdx == 3) {
                    uLoc = (p.x / (width > 1e-4f ? width : 1.0f)) + 0.5f;
                } else {
                    uLoc = (p.z / (depth > 1e-4f ? depth : 1.0f)) + 0.5f;
                }
            }

            uLoc = glm::clamp(uLoc, 0.0f, 1.0f);
            vLoc = glm::clamp(vLoc, 0.0f, 1.0f);

            he->corner.uv = glm::vec2(uOffset + uLoc * uScale, vOffset + vLoc * vScale);
        }
    }

    if (options.markSeamEdges) {
        for (EdgeID eId : mesh.GetActiveEdgeIDs()) {
            const auto* e = mesh.GetEdge(eId);
            if (!e) continue;
            const auto* he0 = mesh.GetHalfEdge(e->halfEdge);
            const auto* he1 = mesh.GetHalfEdge(e->twin);
            if (he0 && he1 && IsValid(he0->face) && IsValid(he1->face)) {
                if (outLayout.faceToIslandMap[he0->face] != outLayout.faceToIslandMap[he1->face]) {
                    MarkSeamEdge(mesh, eId, 1.0f);
                }
            }
        }
    }

    mesh.IncrementAttributeRevision();
    outLayout.meshAttributeRevision = mesh.GetAttributeRevision();

    ValidateGeneratedUV(mesh, outLayout, res.validation);
    res.layout = outLayout;
    res.status = res.validation.isValid ? PrimitiveMappingStatus::SUCCESS : PrimitiveMappingStatus::PARTIAL_SUCCESS;
    res.success = res.validation.isValid;
    return res;
}

// =============================================================================
// 4. FORME CILINDRICHE E CONICHE (BM-UV2.5)
// Cylinder, HollowCylinder, SlicedCylinder, Barrel
// =============================================================================

PrimitiveMappingResult PrimitiveUVMapper::GenerateCylindricalMapping(EditableMesh& mesh, 
                                                                     UVLayout& outLayout, 
                                                                     float radius, float height,
                                                                     bool hasInnerWall, float innerRadius,
                                                                     bool isSliced, const glm::vec3& cutNormal,
                                                                     const PrimitiveUVOptions& options) {
    PrimitiveMappingResult res;
    outLayout.Clear();
    outLayout.strategyUsed = UVMappingStrategy::Cylindrical;
    outLayout.meshTopologyRevision = mesh.GetTopologyRevision();

    // 0: Mantello esterno, 1: Top Cap, 2: Bottom Cap, 3: Mantello interno (se Hollow)
    UVIsland islMantle;
    islMantle.id = 0; islMantle.semanticName = "Outer Mantle"; islMantle.semanticSurfaceId = 0;
    outLayout.islands.push_back(islMantle);

    UVIsland islTop;
    islTop.id = 1; islTop.semanticName = "Top Cap (+Y)"; islTop.semanticSurfaceId = 1;
    outLayout.islands.push_back(islTop);

    UVIsland islBot;
    islBot.id = 2; islBot.semanticName = "Bottom Cap (-Y)"; islBot.semanticSurfaceId = 2;
    outLayout.islands.push_back(islBot);

    if (hasInnerWall) {
        UVIsland islInner;
        islInner.id = 3; islInner.semanticName = "Inner Mantle (Cavity)"; islInner.semanticSurfaceId = 3;
        outLayout.islands.push_back(islInner);
    }

    AABB bounds = mesh.ComputeAABB();
    float safeH = (height > 1e-4f) ? height : std::max(0.001f, bounds.size().y);
    float safeR = (radius > 1e-4f) ? radius : std::max(0.001f, bounds.size().x * 0.5f);

    glm::vec3 cNorm = glm::normalize(cutNormal);
    if (std::abs(cNorm.y) < 1e-4f) cNorm = glm::vec3(0, 1, 0);

    for (FaceID fId : mesh.GetActiveFaceIDs()) {
        const auto* face = mesh.GetFace(fId);
        if (!face) continue;

        glm::vec3 n = face->faceNormal;
        uint32_t islandIdx = 0;

        // Classificazione semantica della faccia
        if (n.y < -0.8f) {
            islandIdx = 2; // Bottom Cap
        } else if (isSliced ? (glm::dot(n, cNorm) > 0.8f) : (n.y > 0.8f)) {
            islandIdx = 1; // Top Cap
        } else {
            // Mantello laterale
            if (hasInnerWall && innerRadius > 0.0f) {
                // Calcola raggio medio della faccia
                float avgR = 0.0f;
                auto fVerts = mesh.GetFaceVertices(fId);
                for (VertexID vId : fVerts) {
                    const auto* v = mesh.GetVertex(vId);
                    if (v) avgR += std::sqrt(v->position.x * v->position.x + v->position.z * v->position.z);
                }
                avgR /= std::max(1.0f, static_cast<float>(fVerts.size()));
                islandIdx = (avgR < (safeR + innerRadius) * 0.5f) ? 3 : 0;
            } else {
                islandIdx = 0; // Outer Mantle
            }
        }

        outLayout.islands[islandIdx].faces.push_back(fId);
        outLayout.faceToIslandMap[fId] = islandIdx;

        // Raccolta corner per correzione salto cucitura
        std::vector<HalfEdgeID> fHEs = mesh.GetFaceHalfEdges(fId);
        std::vector<glm::vec2> localUVs;
        localUVs.reserve(fHEs.size());

        for (HalfEdgeID heId : fHEs) {
            const auto* he = mesh.GetHalfEdge(heId);
            if (!he) continue;
            const auto* v = mesh.GetVertex(he->origin);
            if (!v) continue;

            glm::vec3 p = v->position;
            float u = 0.5f, vCoord = 0.5f;

            if (islandIdx == 0 || islandIdx == 3) {
                // Mantello cilindrico: u = angle / 2pi, v = y / h
                float angle = std::atan2(p.z, p.x); // [-pi, pi]
                u = (angle / glm::two_pi<float>()) + 0.5f;
                vCoord = (p.y / safeH) + 0.5f;
            } else if (islandIdx == 1 && isSliced) {
                // Tappo obliquo tagliato (SlicedCylinder): proiezione nel piano del taglio
                glm::vec3 upRef = (std::abs(cNorm.y) > 0.99f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
                glm::vec3 tAxis = glm::normalize(glm::cross(upRef, cNorm));
                glm::vec3 bAxis = glm::normalize(glm::cross(cNorm, tAxis));
                u = (glm::dot(p, tAxis) / (safeR * 2.0f)) + 0.5f;
                vCoord = (glm::dot(p, bAxis) / (safeR * 2.0f)) + 0.5f;
            } else {
                // Tappi piatti orizzontali
                u = (p.x / (safeR * 2.0f)) + 0.5f;
                vCoord = (islandIdx == 1 ? p.z : -p.z) / (safeR * 2.0f) + 0.5f;
            }

            localUVs.push_back(glm::vec2(glm::clamp(u, 0.0f, 1.0f), glm::clamp(vCoord, 0.0f, 1.0f)));
        }

        // Correzione salto cucitura (U=1.0 <-> U=0.0) se la faccia è sul mantello
        if (options.preventSeamWrapStretch && (islandIdx == 0 || islandIdx == 3) && localUVs.size() == 4) {
            CorrectSeamWrapQuad(localUVs[0], localUVs[1], localUVs[2], localUVs[3]);
        }

        // Distribuzione nell'atlas
        // Mantello superiore [0, 1] x [0.35, 1.0], Tappi inferiori [0, 0.5] e [0.5, 1.0] x [0, 0.35]
        float uOff = 0.0f, vOff = 0.0f, uSc = 1.0f, vSc = 0.65f;
        if (islandIdx == 1) { // Top Cap
            uOff = 0.0f; vOff = 0.0f; uSc = 0.5f; vSc = 0.35f;
        } else if (islandIdx == 2) { // Bottom Cap
            uOff = 0.5f; vOff = 0.0f; uSc = 0.5f; vSc = 0.35f;
        } else if (islandIdx == 3) { // Inner Mantle
            uOff = 0.0f; vOff = 0.35f; uSc = 1.0f; vSc = 0.30f;
        } else { // Outer Mantle
            uOff = 0.0f; vOff = (hasInnerWall ? 0.65f : 0.35f); uSc = 1.0f; vSc = (hasInnerWall ? 0.35f : 0.65f);
        }

        for (size_t k = 0; k < fHEs.size(); ++k) {
            auto* heMut = mesh.GetHalfEdgeMutable(fHEs[k]);
            if (heMut) {
                float finalU = uOff + std::fmod(localUVs[k].x, 1.0f) * uSc;
                float finalV = vOff + localUVs[k].y * vSc;
                heMut->corner.uv = glm::vec2(finalU, finalV);
            }
        }
    }

    // Seam longitudinali e dei perimetri dei tappi
    if (options.markSeamEdges) {
        for (EdgeID eId : mesh.GetActiveEdgeIDs()) {
            const auto* e = mesh.GetEdge(eId);
            if (!e) continue;
            const auto* he0 = mesh.GetHalfEdge(e->halfEdge);
            const auto* he1 = mesh.GetHalfEdge(e->twin);
            if (he0 && he1 && IsValid(he0->face) && IsValid(he1->face)) {
                if (outLayout.faceToIslandMap[he0->face] != outLayout.faceToIslandMap[he1->face]) {
                    MarkSeamEdge(mesh, eId, 1.0f);
                } else if (outLayout.faceToIslandMap[he0->face] == 0) {
                    // Controllo salto cucitura sul mantello (longitudinal seam)
                    const auto* v0 = mesh.GetVertex(he0->origin);
                    const auto* v1 = mesh.GetVertex(he1->origin);
                    if (v0 && v1 && v0->position.x < 0.0f && std::abs(v0->position.z) < 0.05f) {
                        MarkSeamEdge(mesh, eId, 1.0f);
                    }
                }
            }
        }
    }

    mesh.IncrementAttributeRevision();
    outLayout.meshAttributeRevision = mesh.GetAttributeRevision();

    ValidateGeneratedUV(mesh, outLayout, res.validation);
    res.layout = outLayout;
    res.status = res.validation.isValid ? PrimitiveMappingStatus::SUCCESS : PrimitiveMappingStatus::PARTIAL_SUCCESS;
    res.success = res.validation.isValid;
    return res;
}

// =============================================================================
// 5. FORME CONICHE: CONE & CONE FRUSTUM (BM-UV2.5)
// =============================================================================

PrimitiveMappingResult PrimitiveUVMapper::GenerateConicalMapping(EditableMesh& mesh, 
                                                                 UVLayout& outLayout, 
                                                                 float botRadius, float topRadius, float height,
                                                                 const PrimitiveUVOptions& options) {
    PrimitiveMappingResult res;
    outLayout.Clear();
    outLayout.strategyUsed = UVMappingStrategy::Conical;
    outLayout.meshTopologyRevision = mesh.GetTopologyRevision();

    // 0: Mantello conico, 1: Base Cap, 2: Top Cap (se frustum con topRadius > 0)
    UVIsland islBody;
    islBody.id = 0; islBody.semanticName = "Conical Body"; islBody.semanticSurfaceId = 0;
    outLayout.islands.push_back(islBody);

    UVIsland islBase;
    islBase.id = 1; islBase.semanticName = "Base Cap"; islBase.semanticSurfaceId = 1;
    outLayout.islands.push_back(islBase);

    bool isFrustum = (topRadius > 1e-4f);
    if (isFrustum) {
        UVIsland islTop;
        islTop.id = 2; islTop.semanticName = "Top Cap"; islTop.semanticSurfaceId = 2;
        outLayout.islands.push_back(islTop);
    }

    float safeH = (height > 1e-4f) ? height : 1.0f;
    float safeR = (botRadius > 1e-4f) ? botRadius : 1.0f;

    for (FaceID fId : mesh.GetActiveFaceIDs()) {
        const auto* face = mesh.GetFace(fId);
        if (!face) continue;

        glm::vec3 n = face->faceNormal;
        uint32_t islandIdx = 0;

        if (n.y < -0.8f) {
            islandIdx = 1; // Base Cap
        } else if (isFrustum && n.y > 0.8f) {
            islandIdx = 2; // Top Cap
        } else {
            islandIdx = 0; // Conical Body
        }

        outLayout.islands[islandIdx].faces.push_back(fId);
        outLayout.faceToIslandMap[fId] = islandIdx;

        float uOff = (islandIdx == 0) ? 0.0f : ((islandIdx == 1) ? 0.0f : 0.5f);
        float vOff = (islandIdx == 0) ? 0.35f : 0.0f;
        float uSc = (islandIdx == 0) ? 1.0f : 0.5f;
        float vSc = (islandIdx == 0) ? 0.65f : 0.35f;

        for (HalfEdgeID heId : mesh.GetFaceHalfEdges(fId)) {
            auto* he = mesh.GetHalfEdgeMutable(heId);
            if (!he) continue;
            const auto* v = mesh.GetVertex(he->origin);
            if (!v) continue;

            glm::vec3 p = v->position;
            float u = 0.5f, vCoord = 0.5f;

            if (islandIdx == 0) {
                // Mantello: gestione singolarità apice
                float hRatio = glm::clamp((p.y / safeH) + 0.5f, 0.0f, 1.0f);
                vCoord = hRatio;

                float rAtY = std::sqrt(p.x * p.x + p.z * p.z);
                if (rAtY < 1e-4f) {
                    // Vertice all'apice: il raggio collassa a 0.
                    // Evita NaN mediando l'orientamento della faccia
                    glm::vec3 fn = face->faceNormal;
                    float angle = std::atan2(fn.z, fn.x);
                    u = (angle / glm::two_pi<float>()) + 0.5f;
                } else {
                    float angle = std::atan2(p.z, p.x);
                    u = (angle / glm::two_pi<float>()) + 0.5f;
                }
            } else {
                u = (p.x / (safeR * 2.0f)) + 0.5f;
                vCoord = (p.z / (safeR * 2.0f)) + 0.5f;
            }

            u = glm::clamp(u, 0.0f, 1.0f);
            vCoord = glm::clamp(vCoord, 0.0f, 1.0f);
            he->corner.uv = glm::vec2(uOff + u * uSc, vOff + vCoord * vSc);
        }
    }

    if (options.markSeamEdges) {
        for (EdgeID eId : mesh.GetActiveEdgeIDs()) {
            const auto* e = mesh.GetEdge(eId);
            if (!e) continue;
            const auto* he0 = mesh.GetHalfEdge(e->halfEdge);
            const auto* he1 = mesh.GetHalfEdge(e->twin);
            if (he0 && he1 && IsValid(he0->face) && IsValid(he1->face)) {
                if (outLayout.faceToIslandMap[he0->face] != outLayout.faceToIslandMap[he1->face]) {
                    MarkSeamEdge(mesh, eId, 1.0f);
                }
            }
        }
    }

    mesh.IncrementAttributeRevision();
    outLayout.meshAttributeRevision = mesh.GetAttributeRevision();

    ValidateGeneratedUV(mesh, outLayout, res.validation);
    res.layout = outLayout;
    res.status = res.validation.isValid ? PrimitiveMappingStatus::SUCCESS : PrimitiveMappingStatus::PARTIAL_SUCCESS;
    res.success = res.validation.isValid;
    return res;
}

// =============================================================================
// 6. FORME SFERICHE E CURVE (BM-UV2.6)
// Sphere, SphericalZone, Segment, Sector, Capsule, SuperSphere
// =============================================================================

PrimitiveMappingResult PrimitiveUVMapper::GenerateSphericalMapping(EditableMesh& mesh, 
                                                                   UVLayout& outLayout, 
                                                                   float radius, 
                                                                   UVMappingStrategy strat,
                                                                   float bottomY, float topY,
                                                                   float thetaRange, float phiRange,
                                                                   const PrimitiveUVOptions& options) {
    PrimitiveMappingResult res;
    outLayout.Clear();
    outLayout.strategyUsed = (strat == UVMappingStrategy::Hemisphere) ? UVMappingStrategy::Hemisphere : UVMappingStrategy::Spherical;
    outLayout.meshTopologyRevision = mesh.GetTopologyRevision();

    float safeR = (radius > 1e-4f) ? radius : 1.0f;

    if (strat == UVMappingStrategy::Hemisphere) {
        // Dual-Hemisphere Chart: 2 calotte disgiunte (Nord e Sud) con proiezione conforme
        UVIsland islNorth; islNorth.id = 0; islNorth.semanticName = "North Hemisphere (+Y)"; islNorth.semanticSurfaceId = 0;
        UVIsland islSouth; islSouth.id = 1; islSouth.semanticName = "South Hemisphere (-Y)"; islSouth.semanticSurfaceId = 1;
        outLayout.islands.push_back(islNorth);
        outLayout.islands.push_back(islSouth);

        for (FaceID fId : mesh.GetActiveFaceIDs()) {
            const auto* face = mesh.GetFace(fId);
            if (!face) continue;

            uint32_t islandIdx = (face->faceNormal.y >= 0.0f) ? 0 : 1;
            outLayout.islands[islandIdx].faces.push_back(fId);
            outLayout.faceToIslandMap[fId] = islandIdx;

            float uOff = (islandIdx == 0) ? 0.0f : 0.5f;

            for (HalfEdgeID heId : mesh.GetFaceHalfEdges(fId)) {
                auto* he = mesh.GetHalfEdgeMutable(heId);
                if (!he) continue;
                const auto* v = mesh.GetVertex(he->origin);
                if (!v) continue;

                glm::vec3 n = glm::normalize(v->position);
                float denom = 1.0f + std::abs(n.y);
                float uDisc = 0.5f + 0.5f * (n.x / denom);
                float vDisc = 0.5f + 0.5f * (n.z / denom);

                uDisc = glm::clamp(uDisc, 0.0f, 1.0f);
                vDisc = glm::clamp(vDisc, 0.0f, 1.0f);

                he->corner.uv = glm::vec2(uOff + uDisc * 0.5f, vDisc);
            }
        }
    } else {
        // Strategia sferica polare equirettangolare
        UVIsland islSphere;
        islSphere.id = 0;
        islSphere.semanticName = "Spherical Unroll";
        islSphere.semanticSurfaceId = 0;
        outLayout.islands.push_back(islSphere);

        for (FaceID fId : mesh.GetActiveFaceIDs()) {
            outLayout.islands[0].faces.push_back(fId);
            outLayout.faceToIslandMap[fId] = 0;

            for (HalfEdgeID heId : mesh.GetFaceHalfEdges(fId)) {
                auto* he = mesh.GetHalfEdgeMutable(heId);
                if (!he) continue;
                const auto* v = mesh.GetVertex(he->origin);
                if (!v) continue;

                glm::vec3 p = v->position;
                glm::vec3 n = glm::length(p) > 1e-4f ? glm::normalize(p) : glm::vec3(0, 1, 0);

                float u = 0.5f + (std::atan2(n.z, n.x) / glm::two_pi<float>());
                float vCoord = 0.5f - (std::asin(glm::clamp(n.y, -1.0f, 1.0f)) / glm::pi<float>());

                // Protezione poli per evitare degenerazione
                if (std::abs(n.y) > 0.999f) {
                    glm::vec3 fn = mesh.GetFace(fId)->faceNormal;
                    u = 0.5f + (std::atan2(fn.z, fn.x) / glm::two_pi<float>());
                }

                he->corner.uv = glm::vec2(glm::clamp(u, 0.0f, 1.0f), glm::clamp(vCoord, 0.0f, 1.0f));
            }
        }
    }

    if (options.markSeamEdges) {
        for (EdgeID eId : mesh.GetActiveEdgeIDs()) {
            const auto* e = mesh.GetEdge(eId);
            if (!e) continue;
            const auto* he0 = mesh.GetHalfEdge(e->halfEdge);
            const auto* he1 = mesh.GetHalfEdge(e->twin);
            if (he0 && he1 && IsValid(he0->face) && IsValid(he1->face)) {
                if (outLayout.faceToIslandMap[he0->face] != outLayout.faceToIslandMap[he1->face]) {
                    MarkSeamEdge(mesh, eId, 1.0f);
                }
            }
        }
    }

    mesh.IncrementAttributeRevision();
    outLayout.meshAttributeRevision = mesh.GetAttributeRevision();

    ValidateGeneratedUV(mesh, outLayout, res.validation);
    res.layout = outLayout;
    res.status = res.validation.isValid ? PrimitiveMappingStatus::SUCCESS : PrimitiveMappingStatus::PARTIAL_SUCCESS;
    res.success = res.validation.isValid;
    return res;
}

// =============================================================================
// 7. FORME CON CAVITA (BM-UV2.7)
// SphereWithCylindricalBore, SphereWithConicalCavities
// =============================================================================

PrimitiveMappingResult PrimitiveUVMapper::GenerateCavityMapping(EditableMesh& mesh, 
                                                                UVLayout& outLayout, 
                                                                ShapeType type, 
                                                                const ShapeParameters& params,
                                                                const PrimitiveUVOptions& options) {
    PrimitiveMappingResult res;
    outLayout.Clear();
    outLayout.strategyUsed = UVMappingStrategy::Parametric;
    outLayout.meshTopologyRevision = mesh.GetTopologyRevision();

    if (type == ShapeType::SphereWithCylindricalBore) {
        // 4 isole: 0: Outer Sphere Zone, 1: Inner Bore Wall, 2: Top Annular Cap, 3: Bottom Annular Cap
        UVIsland iOuter; iOuter.id = 0; iOuter.semanticName = "Outer Sphere Zone";
        UVIsland iBore;  iBore.id = 1;  iBore.semanticName = "Inner Cylindrical Bore";
        UVIsland iTop;   iTop.id = 2;   iTop.semanticName = "Top Annular Cap (+Y)";
        UVIsland iBot;   iBot.id = 3;   iBot.semanticName = "Bottom Annular Cap (-Y)";
        outLayout.islands.push_back(iOuter);
        outLayout.islands.push_back(iBore);
        outLayout.islands.push_back(iTop);
        outLayout.islands.push_back(iBot);

        AABB bounds = mesh.ComputeAABB();
        float yMax = bounds.max.y;

        for (FaceID fId : mesh.GetActiveFaceIDs()) {
            const auto* face = mesh.GetFace(fId);
            if (!face) continue;

            glm::vec3 n = face->faceNormal;
            uint32_t islandIdx = 0;

            if (n.y > 0.8f) {
                islandIdx = 2; // Top Cap
            } else if (n.y < -0.8f) {
                islandIdx = 3; // Bottom Cap
            } else {
                // Parete: distingui tra mantello esterno (normale uscente) e foro interno (normale centripeta)
                auto vList = mesh.GetFaceVertices(fId);
                glm::vec3 center(0.0f);
                for (VertexID vid : vList) {
                    const auto* v = mesh.GetVertex(vid);
                    if (v) center += v->position;
                }
                center /= std::max(1.0f, static_cast<float>(vList.size()));
                islandIdx = (glm::dot(n, glm::vec3(center.x, 0, center.z)) < 0.0f) ? 1 : 0;
            }

            outLayout.islands[islandIdx].faces.push_back(fId);
            outLayout.faceToIslandMap[fId] = islandIdx;

            // Quadranti non-overlapping nell'atlas: 2x2
            float col = static_cast<float>(islandIdx % 2);
            float row = static_cast<float>(islandIdx / 2);

            for (HalfEdgeID heId : mesh.GetFaceHalfEdges(fId)) {
                auto* he = mesh.GetHalfEdgeMutable(heId);
                if (!he) continue;
                const auto* v = mesh.GetVertex(he->origin);
                if (!v) continue;

                glm::vec3 p = v->position;
                float u = 0.5f, vCoord = 0.5f;

                if (islandIdx == 0 || islandIdx == 1) {
                    float angle = std::atan2(p.z, p.x);
                    u = (angle / glm::two_pi<float>()) + 0.5f;
                    vCoord = (p.y / (2.0f * (yMax > 1e-4f ? yMax : 1.0f))) + 0.5f;
                } else {
                    u = (p.x / (bounds.size().x > 1e-4f ? bounds.size().x : 1.0f)) + 0.5f;
                    vCoord = (p.z / (bounds.size().z > 1e-4f ? bounds.size().z : 1.0f)) + 0.5f;
                }

                u = glm::clamp(u, 0.0f, 1.0f);
                vCoord = glm::clamp(vCoord, 0.0f, 1.0f);
                he->corner.uv = glm::vec2((col * 0.5f) + u * 0.5f, (row * 0.5f) + vCoord * 0.5f);
            }
        }
    } else if (type == ShapeType::SphereWithConicalCavities) {
        // 3 isole: 0: Outer Sphere Zone, 1: Top Conical Cavity, 2: Bottom Conical Cavity
        UVIsland iOuter; iOuter.id = 0; iOuter.semanticName = "Outer Sphere Zone";
        UVIsland iTopC;  iTopC.id = 1;  iTopC.semanticName = "Top Conical Cavity";
        UVIsland iBotC;  iBotC.id = 2;  iBotC.semanticName = "Bottom Conical Cavity";
        outLayout.islands.push_back(iOuter);
        outLayout.islands.push_back(iTopC);
        outLayout.islands.push_back(iBotC);

        for (FaceID fId : mesh.GetActiveFaceIDs()) {
            const auto* face = mesh.GetFace(fId);
            if (!face) continue;

            auto vList = mesh.GetFaceVertices(fId);
            glm::vec3 center(0.0f);
            for (VertexID vid : vList) {
                const auto* v = mesh.GetVertex(vid);
                if (v) center += v->position;
            }
            center /= std::max(1.0f, static_cast<float>(vList.size()));

            uint32_t islandIdx = 0;
            if (center.y > 0.3f && face->faceNormal.y < 0.0f) {
                islandIdx = 1; // Top Cavity
            } else if (center.y < -0.3f && face->faceNormal.y > 0.0f) {
                islandIdx = 2; // Bottom Cavity
            } else {
                islandIdx = 0; // Outer Sphere
            }

            outLayout.islands[islandIdx].faces.push_back(fId);
            outLayout.faceToIslandMap[fId] = islandIdx;

            float uOff = (islandIdx == 0) ? 0.0f : ((islandIdx == 1) ? 0.0f : 0.5f);
            float vOff = (islandIdx == 0) ? 0.4f : 0.0f;
            float uSc = (islandIdx == 0) ? 1.0f : 0.5f;
            float vSc = (islandIdx == 0) ? 0.6f : 0.4f;

            for (HalfEdgeID heId : mesh.GetFaceHalfEdges(fId)) {
                auto* he = mesh.GetHalfEdgeMutable(heId);
                if (!he) continue;
                const auto* v = mesh.GetVertex(he->origin);
                if (!v) continue;

                glm::vec3 p = v->position;
                float angle = std::atan2(p.z, p.x);
                float u = (angle / glm::two_pi<float>()) + 0.5f;
                float vCoord = (p.y * 0.5f) + 0.5f;

                he->corner.uv = glm::vec2(uOff + glm::clamp(u, 0.0f, 1.0f) * uSc, 
                                          vOff + glm::clamp(vCoord, 0.0f, 1.0f) * vSc);
            }
        }
    } else {
        res.status = PrimitiveMappingStatus::NEEDS_BM_UV3;
        res.diagnosticMessage = "Geometria con cavita non riconosciuta analiticamente: delegata a BM-UV3.";
        return res;
    }

    if (options.markSeamEdges) {
        for (EdgeID eId : mesh.GetActiveEdgeIDs()) {
            const auto* e = mesh.GetEdge(eId);
            if (!e) continue;
            const auto* he0 = mesh.GetHalfEdge(e->halfEdge);
            const auto* he1 = mesh.GetHalfEdge(e->twin);
            if (he0 && he1 && IsValid(he0->face) && IsValid(he1->face)) {
                if (outLayout.faceToIslandMap[he0->face] != outLayout.faceToIslandMap[he1->face]) {
                    MarkSeamEdge(mesh, eId, 1.0f);
                }
            }
        }
    }

    mesh.IncrementAttributeRevision();
    outLayout.meshAttributeRevision = mesh.GetAttributeRevision();

    ValidateGeneratedUV(mesh, outLayout, res.validation);
    res.layout = outLayout;
    res.status = res.validation.isValid ? PrimitiveMappingStatus::SUCCESS : PrimitiveMappingStatus::PARTIAL_SUCCESS;
    res.success = res.validation.isValid;
    return res;
}

// =============================================================================
// 8. MASTER DISPATCHER: ANALYZE AND MAP (BM-UV2.1 / BM-UV2.11)
// =============================================================================

PrimitiveMappingResult PrimitiveUVMapper::AnalyzeAndMap(EditableMesh& mesh, 
                                                       ShapeType type, 
                                                       const ShapeParameters& params, 
                                                       const PrimitiveUVOptions& options) {
    // 1. Classificazione anticipata per forme non poligonali, composite o astratte
    // Queste forme non possiedono una mesh poligonale classica da mappare analiticamente in BM-UV2
    if (type == ShapeType::None) {
        PrimitiveMappingResult res;
        res.status = PrimitiveMappingStatus::NEEDS_BM_UV3;
        res.diagnosticMessage = "ShapeType::None non definisce alcuna geometria: mappatura analitica non supportata (delegata a BM-UV3).";
        return res;
    }

    if (type == ShapeType::Heightfield) {
        PrimitiveMappingResult res;
        res.status = PrimitiveMappingStatus::NEEDS_BM_UV3;
        res.diagnosticMessage = "Forma volumetrica (Heightfield): richiede meshing dedicato o triplanar mapping BM-UV3.";
        return res;
    }

    if (type == ShapeType::SignedDistanceField) {
        PrimitiveMappingResult res;
        res.status = PrimitiveMappingStatus::NEEDS_BM_UV3;
        res.diagnosticMessage = "Forma procedurale implicita (SignedDistanceField): richiede raymarching o polygonizer BM-UV3.";
        return res;
    }

    if (type == ShapeType::Compound) {
        PrimitiveMappingResult res;
        res.status = PrimitiveMappingStatus::NEEDS_BM_UV3;
        res.diagnosticMessage = "Forma composta (Compound): l'unwrapping gerarchico dei nodi foglia richiede la pipeline di composizione BM-UV3.";
        return res;
    }

    // 2. Classificazione per forme poliedriche o mesh arbitrarie prive di elementi geometrici compilati
    if ((type == ShapeType::ConvexHull || type == ShapeType::TriangleMesh) &&
        (mesh.GetFaceCount() == 0 || mesh.GetVertexCount() == 0)) {
        PrimitiveMappingResult res;
        res.status = PrimitiveMappingStatus::NEEDS_BM_UV3;
        res.diagnosticMessage = "Forma poliedrica/mesh arbitraria priva di elementi geometrici compilati: delegata a BM-UV3.";
        return res;
    }

    // 3. Controllo rigoroso di validità topologica d'ingresso per primitive analitiche
    if (mesh.GetFaceCount() == 0 || mesh.GetVertexCount() == 0) {
        PrimitiveMappingResult res;
        res.status = PrimitiveMappingStatus::INVALID_TOPOLOGY;
        res.diagnosticMessage = "Mesh priva di elementi geometrici (0 vertici o 0 facce).";
        return res;
    }

    // 2. Dispatching verso l'algoritmo geometrico dedicato
    UVLayout layout;
    layout.assetId = "ShapeAsset_" + std::to_string(static_cast<uint32_t>(type));

    switch (type) {
        case ShapeType::Cube:
            return GenerateBoxMapping(mesh, layout, glm::vec3(1.0f), options);

        case ShapeType::Cuboid: {
            glm::vec3 sz(1.0f);
            if (const auto* p = std::get_if<ShapeParamsCuboid>(&params)) sz = p->size;
            return GenerateBoxMapping(mesh, layout, sz, options);
        }

        case ShapeType::Parallelepiped: {
            glm::vec3 sz(1.0f);
            if (const auto* p = std::get_if<ShapeParamsParallelepiped>(&params)) {
                sz = glm::vec3(glm::length(p->basisX), glm::length(p->basisY), glm::length(p->basisZ));
            }
            return GenerateBoxMapping(mesh, layout, sz, options);
        }

        case ShapeType::Ungula: {
            float r = 1.0f, h = 1.0f;
            if (const auto* p = std::get_if<ShapeParamsUngula>(&params)) { r = p->radius; h = p->height; }
            return GenerateBoxMapping(mesh, layout, glm::vec3(r * 2.0f, h, r * 2.0f), options);
        }

        case ShapeType::Pyramid: {
            float w = 1.0f, d = 1.0f, h = 1.0f;
            glm::vec2 apex(0.0f);
            if (const auto* p = std::get_if<ShapeParamsPyramid>(&params)) {
                w = p->baseWidth; d = p->baseDepth; h = p->height; apex = p->apexOffset;
            }
            return GeneratePyramidMapping(mesh, layout, w, d, h, apex, false, 0.0f, 0.0f, options);
        }

        case ShapeType::PyramidFrustum: {
            float bW = 1.0f, bD = 1.0f, tW = 0.5f, tD = 0.5f, h = 1.0f;
            glm::vec2 apex(0.0f);
            if (const auto* p = std::get_if<ShapeParamsPyramidFrustum>(&params)) {
                bW = p->bottomWidth; bD = p->bottomDepth; tW = p->topWidth; tD = p->topDepth; h = p->height; apex = p->apexOffset;
            }
            return GeneratePyramidMapping(mesh, layout, bW, bD, h, apex, true, tW, tD, options);
        }

        case ShapeType::Cylinder: {
            float r = 0.5f, h = 1.0f;
            if (const auto* p = std::get_if<ShapeParamsCylinder>(&params)) { r = p->radius; h = p->height; }
            return GenerateCylindricalMapping(mesh, layout, r, h, false, 0.0f, false, glm::vec3(0, 1, 0), options);
        }

        case ShapeType::HollowCylinder: {
            float rOut = 1.0f, rIn = 0.5f, h = 1.0f;
            if (const auto* p = std::get_if<ShapeParamsHollowCylinder>(&params)) {
                rOut = p->outerRadius; rIn = p->innerRadius; h = p->height;
            }
            return GenerateCylindricalMapping(mesh, layout, rOut, h, true, rIn, false, glm::vec3(0, 1, 0), options);
        }

        case ShapeType::SlicedCylinder: {
            float r = 0.5f, h = 1.0f;
            glm::vec3 cNorm(0, 1, 1);
            if (const auto* p = std::get_if<ShapeParamsSlicedCylinder>(&params)) {
                r = p->radius; h = p->height; cNorm = p->cutNormal;
            }
            return GenerateCylindricalMapping(mesh, layout, r, h, false, 0.0f, true, cNorm, options);
        }

        case ShapeType::Barrel: {
            float midR = 1.0f, endR = 0.8f, h = 2.0f;
            if (const auto* p = std::get_if<ShapeParamsBarrel>(&params)) {
                midR = p->midRadius; endR = p->endRadius; h = p->height;
            }
            return GenerateCylindricalMapping(mesh, layout, midR, h, false, 0.0f, false, glm::vec3(0, 1, 0), options);
        }

        case ShapeType::Cone: {
            float r = 0.5f, h = 1.0f;
            if (const auto* p = std::get_if<ShapeParamsCone>(&params)) { r = p->radius; h = p->height; }
            return GenerateConicalMapping(mesh, layout, r, 0.0f, h, options);
        }

        case ShapeType::ConeFrustum: {
            float bR = 1.0f, tR = 0.5f, h = 1.0f;
            if (const auto* p = std::get_if<ShapeParamsConeFrustum>(&params)) {
                bR = p->bottomRadius; tR = p->topRadius; h = p->height;
            }
            return GenerateConicalMapping(mesh, layout, bR, tR, h, options);
        }

        case ShapeType::Sphere: {
            float r = 1.0f;
            if (const auto* p = std::get_if<ShapeParamsSphere>(&params)) r = p->radius;
            return GenerateSphericalMapping(mesh, layout, r, options.strategy, -r, r, glm::two_pi<float>(), glm::pi<float>(), options);
        }

        case ShapeType::SphericalZone: {
            float r = 1.0f, botY = -0.5f, topY = 0.5f;
            if (const auto* p = std::get_if<ShapeParamsSphericalZone>(&params)) {
                r = p->radius; botY = p->bottomY; topY = p->topY;
            }
            return GenerateSphericalMapping(mesh, layout, r, UVMappingStrategy::Spherical, botY, topY, glm::two_pi<float>(), glm::pi<float>(), options);
        }

        case ShapeType::SphericalSegment: {
            float r = 1.0f, baseY = 0.0f;
            if (const auto* p = std::get_if<ShapeParamsSphericalSegment>(&params)) {
                r = p->radius; baseY = p->baseY;
            }
            return GenerateSphericalMapping(mesh, layout, r, UVMappingStrategy::Spherical, baseY, r, glm::two_pi<float>(), glm::pi<float>(), options);
        }

        case ShapeType::SphericalSector: {
            float r = 1.0f, thR = glm::pi<float>(), phiR = glm::half_pi<float>();
            if (const auto* p = std::get_if<ShapeParamsSphericalSector>(&params)) {
                r = p->radius; thR = p->thetaRange; phiR = p->phiRange;
            }
            return GenerateSphericalMapping(mesh, layout, r, UVMappingStrategy::Spherical, -r, r, thR, phiR, options);
        }

        case ShapeType::Capsule: {
            float r = 0.5f, hh = 1.0f;
            if (const auto* p = std::get_if<ShapeParamsCapsule>(&params)) { r = p->radius; hh = p->halfHeight; }
            return GenerateCylindricalMapping(mesh, layout, r, hh * 2.0f, false, 0.0f, false, glm::vec3(0, 1, 0), options);
        }

        case ShapeType::LegacySuperSphere: {
            return GenerateSphericalMapping(mesh, layout, 1.0f, UVMappingStrategy::Spherical, -1.0f, 1.0f, glm::two_pi<float>(), glm::pi<float>(), options);
        }

        case ShapeType::SphereWithCylindricalBore:
        case ShapeType::SphereWithConicalCavities:
            return GenerateCavityMapping(mesh, layout, type, params, options);

        case ShapeType::ConvexHull:
            // Proiezione planare poliedrica
            return GenerateBoxMapping(mesh, layout, glm::vec3(1.0f), options);

        case ShapeType::TriangleMesh: {
            // Controlla se la mesh importata ha gia corner UV validi
            UVValidationReport vRep;
            if (ValidateGeneratedUV(mesh, layout, vRep) && vRep.isValid) {
                PrimitiveMappingResult res;
                res.status = PrimitiveMappingStatus::SUCCESS;
                res.success = true;
                res.layout = layout;
                res.validation = vRep;
                return res;
            }
            return GenerateBoxMapping(mesh, layout, glm::vec3(1.0f), options);
        }

        case ShapeType::Compound: {
            PrimitiveMappingResult res;
            res.status = PrimitiveMappingStatus::NEEDS_BM_UV3;
            res.diagnosticMessage = "Forma composta (Compound): l'unwrapping gerarchico dei nodi foglia richiede la pipeline di composizione BM-UV3.";
            return res;
        }

        case ShapeType::Heightfield:
        case ShapeType::SignedDistanceField:
        case ShapeType::None:
        default: {
            PrimitiveMappingResult res;
            res.status = PrimitiveMappingStatus::NEEDS_BM_UV3;
            res.diagnosticMessage = "Forma volumetrica o non-parametrica: delegata al solver generale BM-UV3.";
            return res;
        }
    }
}

PrimitiveMappingResult PrimitiveUVMapper::MapEditableMesh(EditableMesh& mesh, 
                                                          const PrimitiveUVOptions& options) {
    UVMappingStrategy strat = AnalyzePrimitive(mesh, ShapeType::None, nullptr);
    UVLayout layout;
    if (strat == UVMappingStrategy::Box) {
        return GenerateBoxMapping(mesh, layout, glm::vec3(1.0f), options);
    }
    return GeneratePlanarMapping(mesh, layout, glm::vec3(0, 1, 0), options);
}

} // namespace fw
