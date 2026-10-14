#include "pch.h"
#include "TexelDensityCalculator.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace fw {

float TexelDensityCalculator::ComputeScaledArea3D(const EditableMesh& mesh, 
                                                 const UVIsland& island, 
                                                 const glm::vec3& scale) {
    float totalArea = 0.0f;
    for (FaceID fId : island.faces) {
        const auto* face = mesh.GetFace(fId);
        if (!face || !IsValid(face->halfEdge)) continue;

        std::vector<glm::vec3> scaledPositions;
        HalfEdgeID startHe = face->halfEdge;
        HalfEdgeID curHe = startHe;
        do {
            const auto* he = mesh.GetHalfEdge(curHe);
            if (!he) break;
            const auto* v = mesh.GetVertex(he->origin);
            if (v) {
                scaledPositions.push_back(v->position * scale);
            }
            curHe = he->next;
        } while (curHe != startHe && IsValid(curHe));

        if (scaledPositions.size() < 3) continue;

        // Triangolazione a ventaglio per computo area 3D scalata
        for (size_t i = 1; i + 1 < scaledPositions.size(); ++i) {
            glm::vec3 e1 = scaledPositions[i] - scaledPositions[0];
            glm::vec3 e2 = scaledPositions[i + 1] - scaledPositions[0];
            totalArea += 0.5f * glm::length(glm::cross(e1, e2));
        }
    }
    return std::max(totalArea, 1e-6f);
}

float TexelDensityCalculator::ComputeIslandUVArea(const EditableMesh& mesh, const UVIsland& island) {
    float totalUVArea = 0.0f;
    for (FaceID fId : island.faces) {
        const auto* face = mesh.GetFace(fId);
        if (!face || !IsValid(face->halfEdge)) continue;

        std::vector<glm::vec2> uvs;
        HalfEdgeID startHe = face->halfEdge;
        HalfEdgeID curHe = startHe;
        do {
            const auto* he = mesh.GetHalfEdge(curHe);
            if (!he) break;
            uvs.push_back(he->corner.uv);
            curHe = he->next;
        } while (curHe != startHe && IsValid(curHe));

        if (uvs.size() < 3) continue;

        for (size_t i = 1; i + 1 < uvs.size(); ++i) {
            glm::vec2 a = uvs[0], b = uvs[i], c = uvs[i + 1];
            float triArea = 0.5f * std::abs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y));
            totalUVArea += triArea;
        }
    }
    return std::max(totalUVArea, 1e-7f);
}

std::vector<glm::vec2> TexelDensityCalculator::ComputeIslandTexelDimensions(const EditableMesh& mesh, 
                                                                           std::vector<UVIsland>& islands, 
                                                                           const TexelDensityOptions& options, 
                                                                           uint32_t atlasW, 
                                                                           uint32_t atlasH, 
                                                                           float& outCalculatedGlobalDensity) {
    std::vector<glm::vec2> dimensions;
    dimensions.reserve(islands.size());

    if (islands.empty()) {
        outCalculatedGlobalDensity = 0.0f;
        return dimensions;
    }

    std::vector<float> physicalAreas(islands.size());
    std::vector<float> weightFactors(islands.size(), 1.0f);
    float totalWeighted3DArea = 0.0f;

    for (size_t i = 0; i < islands.size(); ++i) {
        float a3D = ComputeScaledArea3D(mesh, islands[i], options.physicalScale);
        physicalAreas[i] = a3D;
        islands[i].surfaceArea3D = a3D;

        float w = 1.0f;
        if (options.mode == TexelDensityMode::PerSurface) {
            auto it = options.surfaceWeights.find(islands[i].semanticSurfaceId);
            if (it != options.surfaceWeights.end()) w = it->second;
        } else if (options.mode == TexelDensityMode::PerIsland || options.mode == TexelDensityMode::CustomWeighted) {
            auto it = options.islandWeights.find(islands[i].id);
            if (it != options.islandWeights.end()) w = it->second;
        }
        w = std::clamp(w, 0.05f, 20.0f);
        weightFactors[i] = w;

        // Area pesata = Area3D * (w^2)
        totalWeighted3DArea += a3D * (w * w);
    }

    float targetLinearDensity = options.targetTexelDensity;

    // Se auto-calcolata: punta a coprire ~65% dell'atlas totale lasciando spazio al gutter
    if (options.autoCalculateTargetDensity && totalWeighted3DArea > 1e-6f) {
        float targetOccupancyArea = static_cast<float>(atlasW * atlasH) * 0.65f;
        float arealDensity = targetOccupancyArea / totalWeighted3DArea;
        targetLinearDensity = std::sqrt(arealDensity);
    }

    outCalculatedGlobalDensity = targetLinearDensity;

    for (size_t i = 0; i < islands.size(); ++i) {
        float localW = islands[i].localUVBounds.z - islands[i].localUVBounds.x;
        float localH = islands[i].localUVBounds.w - islands[i].localUVBounds.y;
        if (localW < 1e-4f) localW = 1.0f;
        if (localH < 1e-4f) localH = 1.0f;

        // Calcola scala metrica per isola in base a sqrt(Area3D)
        float side3D = std::sqrt(physicalAreas[i]);
        float islandLinearDensity = targetLinearDensity * weightFactors[i];
        islands[i].texelDensity = islandLinearDensity;

        float texelW = std::max(localW * side3D * islandLinearDensity, 4.0f);
        float texelH = std::max(localH * side3D * islandLinearDensity, 4.0f);

        dimensions.push_back(glm::vec2(texelW, texelH));
    }

    return dimensions;
}

TexelDensityReport TexelDensityCalculator::AnalyzeLayoutDensity(const EditableMesh& mesh, 
                                                               const UVLayout& layout, 
                                                               const glm::vec3& physicalScale) {
    TexelDensityReport report;
    if (layout.islands.empty()) return report;

    std::vector<float> linearDensities;
    linearDensities.reserve(layout.islands.size());

    report.minLinearDensity = 1e20f;
    report.maxLinearDensity = -1e20f;
    report.minArealDensity = 1e20f;
    report.maxArealDensity = -1e20f;

    float sumLinear = 0.0f;
    float sumAreal = 0.0f;

    for (const auto& island : layout.islands) {
        float a3D = ComputeScaledArea3D(mesh, island, physicalScale);
        float uvAreaNorm = ComputeIslandUVArea(mesh, island);

        // Area effettiva dell'isola in texel atlas
        float atlasScaleFactor = island.atlasTransform.x * island.atlasTransform.y;
        float islandTexelArea = uvAreaNorm * atlasScaleFactor * static_cast<float>(layout.atlasTargetWidth * layout.atlasTargetHeight);

        float areal = (a3D > 1e-6f) ? (islandTexelArea / a3D) : 0.0f;
        float linear = std::sqrt(areal);

        report.minLinearDensity = std::min(report.minLinearDensity, linear);
        report.maxLinearDensity = std::max(report.maxLinearDensity, linear);
        report.minArealDensity = std::min(report.minArealDensity, areal);
        report.maxArealDensity = std::max(report.maxArealDensity, areal);

        sumLinear += linear;
        sumAreal += areal;
        linearDensities.push_back(linear);
    }

    size_t count = layout.islands.size();
    report.averageLinearDensity = sumLinear / count;
    report.averageArealDensity = sumAreal / count;

    // Calcolo deviazione standard
    float variance = 0.0f;
    for (float d : linearDensities) {
        float diff = d - report.averageLinearDensity;
        variance += diff * diff;
    }
    report.standardDeviation = std::sqrt(variance / count);

    // Se deviazione standard < 10% della media, e considerata uniforme
    report.isUniform = (report.averageLinearDensity > 0.0f) && 
                       ((report.standardDeviation / report.averageLinearDensity) < 0.10f);

    if (!report.isUniform) {
        report.warnings.push_back("Texel density non uniforme (deviazione standard: " + 
            std::to_string(report.standardDeviation) + " px/m).");
    }

    return report;
}

} // namespace fw
