#include "SurfacePaintingAdapter.h"
#include <cmath>
#include <algorithm>
#include <iostream>

namespace fw {

// =============================================================================
// BM-TX4.4 — UV TO PIXEL MAPPING
// =============================================================================

float ApplyWrapModeScalar(float coord, TextureWrapMode mode) {
    switch (mode) {
        case TextureWrapMode::Repeat: {
            float c = std::fmod(coord, 1.0f);
            if (c < 0.0f) c += 1.0f;
            return c;
        }
        case TextureWrapMode::ClampToEdge:
            return std::clamp(coord, 0.0f, 1.0f);

        case TextureWrapMode::MirroredRepeat: {
            float c = std::fabs(coord);
            int period = static_cast<int>(std::floor(c));
            float rem = c - static_cast<float>(period);
            return (period % 2 == 0) ? rem : (1.0f - rem);
        }
        default:
            return std::clamp(coord, 0.0f, 1.0f);
    }
}

glm::vec2 MapUVToPixelCoords(const glm::vec2& uv, const UVToPixelConfig& cfg) {
    if (cfg.textureWidth == 0 || cfg.textureHeight == 0) {
        return glm::vec2(0.0f);
    }

    float u = ApplyWrapModeScalar(uv.x, cfg.wrapMode);
    float v = ApplyWrapModeScalar(uv.y, cfg.wrapMode);

    if (cfg.flipV) {
        v = 1.0f - v;
    }

    float px = u * static_cast<float>(cfg.textureWidth);
    float py = v * static_cast<float>(cfg.textureHeight);

    px = std::clamp(px, 0.0f, static_cast<float>(cfg.textureWidth - 1));
    py = std::clamp(py, 0.0f, static_cast<float>(cfg.textureHeight - 1));

    return glm::vec2(px, py);
}

// =============================================================================
// BM-TX4.5 — TEXTURE TARGET RESOLVER (COPY-ON-WRITE ISOLATION)
// =============================================================================

void TextureTargetResolver::RegisterMaterialTarget(uint32_t materialId, PaintChannel channel, std::shared_ptr<TexturePaintingService> service) {
    TargetKey key{materialId, channel};
    m_materialTargets[key] = std::move(service);
}

void TextureTargetResolver::RegisterSurfaceTarget(uint32_t surfaceId, PaintChannel channel, std::shared_ptr<TexturePaintingService> service) {
    TargetKey key{surfaceId, channel};
    m_surfaceTargets[key] = std::move(service);
}

ResolvedPaintTarget TextureTargetResolver::ResolveTarget(const SurfacePaintHit& hit, 
                                                         PaintChannel channel, 
                                                         bool requestIsolation,
                                                         uint32_t defaultWidth,
                                                         uint32_t defaultHeight) 
{
    ResolvedPaintTarget res;
    res.surfaceId = hit.surfaceId;
    res.materialId = hit.materialId;
    res.channel = channel;
    res.colorSpace = GetPaintChannelColorSpace(channel);
    res.wrapMode = TextureWrapMode::Repeat;
    res.assetId = hit.assetId;
    res.targetIdentifier = "surf_" + std::to_string(hit.surfaceId) + "_ch_" + std::to_string(static_cast<uint32_t>(channel));

    TargetKey surfKey{hit.surfaceId, channel};
    auto surfIt = m_surfaceTargets.find(surfKey);
    if (surfIt != m_surfaceTargets.end() && surfIt->second) {
        res.valid = true;
        res.ownership = PaintTargetOwnership::IsolatedSurface;
        res.paintingService = surfIt->second.get();
        res.width = res.paintingService->GetCanvasWidth();
        res.height = res.paintingService->GetCanvasHeight();
        res.requiresCOW = false;
        return res;
    }

    TargetKey matKey{hit.materialId, channel};
    auto matIt = m_materialTargets.find(matKey);

    if (requestIsolation) {
        // Trigger Copy-On-Write (COW): crea un canvas isolato dedicato a questa superficie
        auto isolatedService = std::make_shared<TexturePaintingService>();

        if (matIt != m_materialTargets.end() && matIt->second) {
            auto sharedService = matIt->second;
            uint32_t w = sharedService->GetCanvasWidth();
            uint32_t h = sharedService->GetCanvasHeight();
            isolatedService->InitializeCanvas(w, h, glm::vec4(1.0f));

            // Copia il contenuto compositato iniziale dal target condiviso
            ImageBitmap compSnapshot;
            sharedService->CompositeLayers(compSnapshot);
            if (isolatedService->GetLayerCount() > 0) {
                *isolatedService->GetLayerMutable(0)->GetBitmapMutable() = compSnapshot;
                isolatedService->GetLayerMutable(0)->SetName("COW_BaseLayer_Surf" + std::to_string(hit.surfaceId));
                isolatedService->IncrementRevision();
            }
            res.width = w;
            res.height = h;
        } else {
            glm::vec4 fill = (channel == PaintChannel::BaseColor) ? glm::vec4(1.0f) : glm::vec4(0.0f);
            isolatedService->InitializeCanvas(defaultWidth, defaultHeight, fill);
            res.width = defaultWidth;
            res.height = defaultHeight;
        }

        m_surfaceTargets[surfKey] = isolatedService;
        res.valid = true;
        res.ownership = PaintTargetOwnership::IsolatedSurface;
        res.paintingService = isolatedService.get();
        res.requiresCOW = true;
        return res;
    } else {
        // Condivisione esplicita con il materiale
        if (matIt != m_materialTargets.end() && matIt->second) {
            res.valid = true;
            res.ownership = PaintTargetOwnership::SharedMaterial;
            res.paintingService = matIt->second.get();
            res.width = res.paintingService->GetCanvasWidth();
            res.height = res.paintingService->GetCanvasHeight();
            res.requiresCOW = false;
            return res;
        }

        // Nessun target registrato: crea target di default condiviso per il materiale
        auto newMatService = std::make_shared<TexturePaintingService>();
        glm::vec4 fill = (channel == PaintChannel::BaseColor) ? glm::vec4(1.0f) : glm::vec4(0.0f);
        newMatService->InitializeCanvas(defaultWidth, defaultHeight, fill);
        m_materialTargets[matKey] = newMatService;

        res.valid = true;
        res.ownership = PaintTargetOwnership::SharedMaterial;
        res.paintingService = newMatService.get();
        res.width = defaultWidth;
        res.height = defaultHeight;
        res.requiresCOW = false;
        return res;
    }
}

bool TextureTargetResolver::HasIsolatedTarget(uint32_t surfaceId, PaintChannel channel) const {
    TargetKey key{surfaceId, channel};
    return m_surfaceTargets.find(key) != m_surfaceTargets.end();
}

std::shared_ptr<TexturePaintingService> TextureTargetResolver::GetSurfaceTarget(uint32_t surfaceId, PaintChannel channel) const {
    TargetKey key{surfaceId, channel};
    auto it = m_surfaceTargets.find(key);
    return (it != m_surfaceTargets.end()) ? it->second : nullptr;
}

void TextureTargetResolver::Clear() {
    m_materialTargets.clear();
    m_surfaceTargets.clear();
}

// =============================================================================
// BM-TX4.5 — SURFACE PAINTING ADAPTER CONSTRUCTOR
// =============================================================================

SurfacePaintingAdapter::SurfacePaintingAdapter(TexturePaintingService* paintingService)
    : m_paintingService(paintingService) {
    if (m_paintingService) {
        m_uvConfig.textureWidth = m_paintingService->GetCanvasWidth();
        m_uvConfig.textureHeight = m_paintingService->GetCanvasHeight();
    }
}

// =============================================================================
// BM-TX4.2 & BM-TX4.3 — RAYCAST OBJECT-SPACE E INTERPOLAZIONE UV PER-CORNER
// =============================================================================

SurfacePaintHit SurfacePaintingAdapter::RaycastMesh(const EditableMesh& mesh, 
                                                   const glm::vec3& rayOrigin, 
                                                   const glm::vec3& rayDirection, 
                                                   float maxDistance,
                                                   const glm::mat4& modelMatrix) 
{
    SurfacePaintHit bestHit;
    bestHit.valid = false;
    bestHit.distance = maxDistance;

    float dirLen = glm::length(rayDirection);
    if (dirLen < 1e-6f) return bestHit;
    glm::vec3 worldDirNorm = rayDirection / dirLen;

    // Gestione trasformazione oggetto e scala non uniforme
    bool isIdentity = (modelMatrix == glm::mat4(1.0f));
    glm::mat4 invModel(1.0f);
    glm::mat3 normalMatrix(1.0f);

    glm::vec3 localOrigin = rayOrigin;
    glm::vec3 localDir = worldDirNorm;

    if (!isIdentity) {
        invModel = glm::inverse(modelMatrix);
        normalMatrix = glm::transpose(glm::inverse(glm::mat3(modelMatrix)));
        localOrigin = glm::vec3(invModel * glm::vec4(rayOrigin, 1.0f));
        glm::vec3 lDir = glm::vec3(invModel * glm::vec4(worldDirNorm, 0.0f));
        float lDirLen = glm::length(lDir);
        if (lDirLen < 1e-6f) return bestHit;
        localDir = lDir / lDirLen;
    }

    const float EPSILON = 1e-7f;

    auto fIds = mesh.GetActiveFaceIDs();
    for (FaceID fId : fIds) {
        const auto* faceObj = mesh.GetFace(fId);
        if (!faceObj) continue;

        std::vector<VertexID> verts = mesh.GetFaceVertices(fId);
        std::vector<CornerAttributes> corners = mesh.GetFaceCorners(fId);
        std::vector<uint32_t> triIndices;
        if (!mesh.TriangulateFace(fId, triIndices)) continue;

        const size_t numTris = triIndices.size() / 3;
        for (size_t t = 0; t < numTris; ++t) {
            uint32_t i0 = triIndices[t * 3 + 0];
            uint32_t i1 = triIndices[t * 3 + 1];
            uint32_t i2 = triIndices[t * 3 + 2];

            if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size()) continue;

            const auto* v0 = mesh.GetVertex(verts[i0]);
            const auto* v1 = mesh.GetVertex(verts[i1]);
            const auto* v2 = mesh.GetVertex(verts[i2]);
            if (!v0 || !v1 || !v2) continue;

            const glm::vec3& P0 = v0->position;
            const glm::vec3& P1 = v1->position;
            const glm::vec3& P2 = v2->position;

            // Algoritmo di Möller-Trumbore
            glm::vec3 edge1 = P1 - P0;
            glm::vec3 edge2 = P2 - P0;
            glm::vec3 pvec = glm::cross(localDir, edge2);
            float det = glm::dot(edge1, pvec);

            if (det > -EPSILON && det < EPSILON) continue;
            float invDet = 1.0f / det;

            glm::vec3 tvec = localOrigin - P0;
            float u = glm::dot(tvec, pvec) * invDet;
            if (u < 0.0f || u > 1.0f) continue;

            glm::vec3 qvec = glm::cross(tvec, edge1);
            float v = glm::dot(localDir, qvec) * invDet;
            if (v < 0.0f || u + v > 1.0f) continue;

            float localDist = glm::dot(edge2, qvec) * invDet;
            if (localDist <= EPSILON) continue;

            glm::vec3 hitLocal = localOrigin + localDir * localDist;
            glm::vec3 hitWorld = isIdentity ? hitLocal : glm::vec3(modelMatrix * glm::vec4(hitLocal, 1.0f));
            float worldDist = glm::length(hitWorld - rayOrigin);

            if (worldDist < bestHit.distance) {
                bestHit.valid = true;
                bestHit.distance = worldDist;
                bestHit.faceId = fId;
                bestHit.triangleIndex = static_cast<uint32_t>(t);
                bestHit.localPosition = hitLocal;
                bestHit.worldPosition = hitWorld;

                float w = 1.0f - u - v;
                bestHit.barycentricCoords = glm::vec3(w, u, v);
                bestHit.surfaceId = faceObj->surfaceId;
                bestHit.materialId = faceObj->materialId;
                bestHit.localNormal = faceObj->faceNormal;

                // Interpolazione UV per-corner isolata: preserva al 100% le cuciture (seams)
                if (i0 < corners.size() && i1 < corners.size() && i2 < corners.size()) {
                    bestHit.interpolatedUV = w * corners[i0].uv + u * corners[i1].uv + v * corners[i2].uv;
                    glm::vec3 n = w * corners[i0].normal + u * corners[i1].normal + v * corners[i2].normal;
                    if (glm::dot(n, n) > 1e-6f) {
                        bestHit.localNormal = glm::normalize(n);
                    }
                } else {
                    bestHit.interpolatedUV = glm::vec2(0.0f);
                }

                bestHit.worldNormal = isIdentity ? bestHit.localNormal : glm::normalize(normalMatrix * bestHit.localNormal);
                bestHit.meshTopologyRevision = mesh.GetTopologyRevision();
                bestHit.meshAttributeRevision = mesh.GetAttributeRevision();
            }
        }
    }

    return bestHit;
}

// =============================================================================
// BM-TX4.7 — CALCOLO METRICA SUPERFICIALE 3D (JACOBIANO POSIZIONE/UV)
// =============================================================================

float SurfacePaintingAdapter::ComputeSurfaceMetricPixels(const EditableMesh& mesh, 
                                                         const SurfacePaintHit& hit, 
                                                         float metricRadius, 
                                                         const UVToPixelConfig& cfg) 
{
    if (!hit.valid || !mesh.IsValid(hit.faceId)) {
        return metricRadius;
    }

    std::vector<VertexID> verts = mesh.GetFaceVertices(hit.faceId);
    std::vector<CornerAttributes> corners = mesh.GetFaceCorners(hit.faceId);
    std::vector<uint32_t> triIndices;
    if (!mesh.TriangulateFace(hit.faceId, triIndices)) {
        return metricRadius;
    }

    size_t t = hit.triangleIndex;
    if (t * 3 + 2 >= triIndices.size()) return metricRadius;

    uint32_t i0 = triIndices[t * 3 + 0];
    uint32_t i1 = triIndices[t * 3 + 1];
    uint32_t i2 = triIndices[t * 3 + 2];

    if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size()) return metricRadius;
    if (i0 >= corners.size() || i1 >= corners.size() || i2 >= corners.size()) return metricRadius;

    const auto* v0 = mesh.GetVertex(verts[i0]);
    const auto* v1 = mesh.GetVertex(verts[i1]);
    const auto* v2 = mesh.GetVertex(verts[i2]);
    if (!v0 || !v1 || !v2) return metricRadius;

    glm::vec3 dp1 = v1->position - v0->position;
    glm::vec3 dp2 = v2->position - v0->position;
    glm::vec2 duv1 = corners[i1].uv - corners[i0].uv;
    glm::vec2 duv2 = corners[i2].uv - corners[i0].uv;

    float det = duv1.x * duv2.y - duv1.y * duv2.x;
    if (std::abs(det) > 1e-7f) {
        glm::vec3 tangent = (dp1 * duv2.y - dp2 * duv1.y) / det;
        glm::vec3 bitangent = (dp2 * duv1.x - dp1 * duv2.x) / det;
        float lu = glm::length(tangent);
        float lv = glm::length(bitangent);
        if (lu > 1e-5f && lv > 1e-5f) {
            float pxPerUnitX = (1.0f / lu) * static_cast<float>(cfg.textureWidth);
            float pxPerUnitY = (1.0f / lv) * static_cast<float>(cfg.textureHeight);
            float avgPxPerUnit = 0.5f * (pxPerUnitX + pxPerUnitY);
            return std::max(1.0f, metricRadius * avgPxPerUnit);
        }
    }

    float edgeLen3D = glm::length(dp1);
    float edgeLenUV = glm::length(duv1);
    if (edgeLen3D > 1e-5f && edgeLenUV > 1e-5f) {
        float pxPerUnit = (edgeLenUV / edgeLen3D) * static_cast<float>(std::max(cfg.textureWidth, cfg.textureHeight));
        return std::max(1.0f, metricRadius * pxPerUnit);
    }

    return metricRadius;
}

// =============================================================================
// BM-TX4.5, BM-TX4.6 & BM-TX4.7 — STROKE 3D & SURFACE ISOLATION
// =============================================================================

bool SurfacePaintingAdapter::BeginStroke3D(const EditableMesh& mesh, 
                                          const SurfacePaintHit& hit, 
                                          const PaintBrush& brush, 
                                          uint32_t targetLayerId, 
                                          bool targetMask) 
{
    if (!hit.valid) return false;

    if (m_lockToActiveFace) {
        if (m_lockedFaceId.IsValid() && hit.faceId != m_lockedFaceId) {
            return false;
        }
        m_lockedFaceId = hit.faceId;
    }

    // Risoluzione Target con Copy-On-Write se disponibile
    if (m_targetResolver) {
        auto resolved = m_targetResolver->ResolveTarget(hit, m_activeChannel, m_isolateBySurfaceId);
        if (resolved.valid && resolved.paintingService) {
            m_paintingService = resolved.paintingService;
        }
    }

    if (!m_paintingService) return false;

    // Sincronizza dimensioni canvas con il servizio pittura
    m_uvConfig.textureWidth = m_paintingService->GetCanvasWidth();
    m_uvConfig.textureHeight = m_paintingService->GetCanvasHeight();

    glm::vec2 pxCoords = MapUVToPixelCoords(hit.interpolatedUV, m_uvConfig);

    // Calcolo raggio effettivo del pennello (UV vs Surface-Space)
    PaintBrush effectiveBrush = brush;
    if (m_brushSpaceMode == BrushSpaceMode::SurfaceSpace) {
        float metricR = (brush.radiusMetric > 0.0f) ? brush.radiusMetric : (brush.radiusPixels * 0.05f);
        effectiveBrush.radiusPixels = ComputeSurfaceMetricPixels(mesh, hit, metricR, m_uvConfig);
    }

    uint32_t resolvedLayerId = targetLayerId;
    if (resolvedLayerId == 0 && m_paintingService->GetLayerCount() > 0) {
        resolvedLayerId = m_paintingService->GetLayers()[0].layerId;
    }

    // Inizia la transazione sul painting service
    if (!m_paintingService->BeginStroke(resolvedLayerId, targetMask)) {
        return false;
    }

    // BM-PAINTER-PERF-02: Maschera raster cached per isolamento isola UV
    if (m_uvLayout) {
        auto it = m_uvLayout->faceToIslandMap.find(hit.faceId);
        if (it != m_uvLayout->faceToIslandMap.end() && it->second < m_uvLayout->islands.size()) {
            auto mask = GetOrCreateIslandRasterMask(mesh, *m_uvLayout, it->second, m_uvConfig.textureWidth, m_uvConfig.textureHeight, m_uvConfig.flipV);
            m_paintingService->SetStrokeClipMask(mask);
        } else {
            m_paintingService->SetStrokeClipMask(nullptr);
        }
    }

    m_paintingService->ApplyDab(pxCoords, effectiveBrush);

    float metricR = (brush.radiusMetric > 0.0f) ? brush.radiusMetric : (brush.radiusPixels * 0.05f);
    PropagateCrossSeamDab(mesh, hit, effectiveBrush, metricR);

    m_isStroking = true;
    m_raycastLost = false;
    m_strokeOriginNormal = hit.worldNormal;
    m_lastHit = hit;
    m_prevPixelCoords = pxCoords;
    m_activeSurfaceId = hit.surfaceId;
    m_activeMaterialId = hit.materialId;
    m_activeLayerId = resolvedLayerId;
    m_activeTargetMask = targetMask;

    return true;
}

bool SurfacePaintingAdapter::BeginStroke3D(const EditableMesh& mesh, 
                                          const glm::vec3& rayOrigin, 
                                          const glm::vec3& rayDirection, 
                                          const PaintBrush& brush, 
                                          uint32_t targetLayerId, 
                                          bool targetMask,
                                          const glm::mat4& modelMatrix) 
{
    SurfacePaintHit hit = RaycastMesh(mesh, rayOrigin, rayDirection, 1000.0f, modelMatrix);
    return BeginStroke3D(mesh, hit, brush, targetLayerId, targetMask);
}

bool SurfacePaintingAdapter::UpdateStroke3D(const EditableMesh& mesh, 
                                           const SurfacePaintHit& hit, 
                                           const PaintBrush& brush) 
{
    if (!m_isStroking || !m_paintingService) return false;

    if (!hit.valid) {
        // Cursore temporaneamente fuori dalla mesh: impostiamo raycastLost per evitare linee spurie al rientro
        m_raycastLost = true;
        return false;
    }

    // Hard Face Lock 3D: impedisce la pittura su isole/superfici diverse da quella autorizzata
    if (m_lockToActiveFace && m_lockedFaceId.IsValid() && hit.faceId != m_lockedFaceId) {
        bool sameIsland = false;
        if (m_uvLayout) {
            auto itA = m_uvLayout->faceToIslandMap.find(m_lockedFaceId);
            auto itB = m_uvLayout->faceToIslandMap.find(hit.faceId);
            if (itA != m_uvLayout->faceToIslandMap.end() && itB != m_uvLayout->faceToIslandMap.end()) {
                sameIsland = (itA->second == itB->second);
            }
        }
        if (sameIsland) {
            m_lockedFaceId = hit.faceId;
        } else {
            return false;
        }
    }

    // Normal Culling su superfici curve e pareti opposte (BM-TX4.8)
    float dotN = glm::dot(hit.worldNormal, m_strokeOriginNormal);
    if (dotN < m_minNormalDot) {
        return false;
    }

    // BM-TX4.6: Isolamento superficiale semantico (SurfaceID)
    if (m_isolateBySurfaceId && hit.surfaceId != m_activeSurfaceId) {
        return false;
    }

    glm::vec2 curPx = MapUVToPixelCoords(hit.interpolatedUV, m_uvConfig);

    // Calcolo raggio effettivo del pennello
    PaintBrush effectiveBrush = brush;
    if (m_brushSpaceMode == BrushSpaceMode::SurfaceSpace) {
        float metricR = (brush.radiusMetric > 0.0f) ? brush.radiusMetric : (brush.radiusPixels * 0.05f);
        effectiveBrush.radiusPixels = ComputeSurfaceMetricPixels(mesh, hit, metricR, m_uvConfig);
    }

    // Gestione rientro dopo perdita temporanea del raycast (BM-TX4.11)
    if (m_raycastLost) {
        m_paintingService->ApplyDab(curPx, effectiveBrush);
        m_raycastLost = false;
        m_prevPixelCoords = curPx;
        m_lastHit = hit;
        return true;
    }

    // BM-TX4.7: Rilevamento cuciture UV (Seam Crossing Discontinuity)
    glm::vec2 uvDelta = glm::abs(hit.interpolatedUV - m_lastHit.interpolatedUV);
    bool crossedSeam = (hit.faceId != m_lastHit.faceId) && 
                       (uvDelta.x > m_seamDiscontinuityThreshold || uvDelta.y > m_seamDiscontinuityThreshold);

    if (crossedSeam) {
        if (m_seamMode == SeamMode::StopAtSeam) {
            return false;
        } else {
            // Segmentazione dello stroke: applica dab isolato senza strisciare attraverso l'atlas
            m_paintingService->ApplyDab(curPx, effectiveBrush);
        }
    } else {
        // Superficie continua: interpola il segmento normalmente
        m_paintingService->UpdateStroke(m_prevPixelCoords, curPx, effectiveBrush);
    }

    float metricR = (brush.radiusMetric > 0.0f) ? brush.radiusMetric : (brush.radiusPixels * 0.05f);
    PropagateCrossSeamDab(mesh, hit, effectiveBrush, metricR);

    m_lastHit = hit;
    m_prevPixelCoords = curPx;
    return true;
}

bool SurfacePaintingAdapter::UpdateStroke3D(const EditableMesh& mesh, 
                                           const glm::vec3& rayOrigin, 
                                           const glm::vec3& rayDirection, 
                                           const PaintBrush& brush,
                                           const glm::mat4& modelMatrix) 
{
    if (!m_isStroking || !m_paintingService) return false;

    SurfacePaintHit hit = RaycastMesh(mesh, rayOrigin, rayDirection, 1000.0f, modelMatrix);
    return UpdateStroke3D(mesh, hit, brush);
}

PaintDirtyRect SurfacePaintingAdapter::EndStroke3D() {
    if (!m_isStroking || !m_paintingService) {
        m_isStroking = false;
        m_raycastLost = false;
        return PaintDirtyRect{};
    }

    PaintDirtyRect dr = m_paintingService->EndStroke();
    m_isStroking = false;
    m_raycastLost = false;
    m_lockedFaceId = FaceID{};
    return dr;
}

bool SurfacePaintingAdapter::CancelStroke3D() {
    if (!m_isStroking || !m_paintingService) {
        m_isStroking = false;
        m_raycastLost = false;
        return false;
    }

    bool ok = m_paintingService->CancelStroke();
    m_isStroking = false;
    m_raycastLost = false;
    m_lockedFaceId = FaceID{};
    return ok;
}

PaintGPUStagingToken SurfacePaintingAdapter::GenerateGPUStagingToken(const std::string& assetId) const {
    PaintGPUStagingToken token;
    token.textureAssetId = assetId;
    token.paintTargetId = "surf_" + std::to_string(m_activeSurfaceId);
    token.colorSpace = GetPaintChannelColorSpace(m_activeChannel);
    token.readyForUpload = true;
    token.uploadGeneration = 1;
    token.lifetimeToken = 1001;
    if (m_paintingService) {
        token.textureRevision = m_paintingService->GetRevision();
    }
    return token;
}

void SurfacePaintingAdapter::PropagateCrossSeamDab(const EditableMesh& mesh, 
                                                   const SurfacePaintHit& hit, 
                                                   const PaintBrush& brush, 
                                                   float metricRadius) {
    if (!m_paintingService || !mesh.IsValid(hit.faceId)) return;
    if (m_lockToActiveFace) return; // BM-UV-FACE-SYNC-05: Non propagare oltre le cuciture se e attivo il blocco della faccia!

    const auto* face = mesh.GetFace(hit.faceId);
    if (!face || !face->halfEdge.IsValid()) return;

    HalfEdgeID startHe = face->halfEdge;
    HalfEdgeID curHe = startHe;
    do {
        const auto* he = mesh.GetHalfEdge(curHe);
        if (!he) break;

        const auto* edge = mesh.GetEdge(he->edge);
        bool isSeamEdge = edge && (edge->seamWeight > 0.0f);

        // Se l'edge e marcato come cucitura ed ha un gemello valido dall'altra parte
        if (isSeamEdge && he->twin.IsValid()) {
            const auto* twinHe = mesh.GetHalfEdge(he->twin);
            if (twinHe && twinHe->face.IsValid()) {
                const auto* neighborFace = mesh.GetFace(twinHe->face);
                if (neighborFace) {
                    if (m_isolateBySurfaceId && neighborFace->surfaceId != m_activeSurfaceId) {
                        curHe = he->next;
                        continue;
                    }

                    const auto* v0 = mesh.GetVertex(he->origin);
                    const auto* nextHe = mesh.GetHalfEdge(he->next);
                    const auto* v1 = nextHe ? mesh.GetVertex(nextHe->origin) : nullptr;
                    if (v0 && v1) {
                        glm::vec3 segA = v0->position;
                        glm::vec3 segB = v1->position;
                        glm::vec3 p = hit.localPosition;
                        glm::vec3 ab = segB - segA;
                        float t = glm::clamp(glm::dot(p - segA, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
                        glm::vec3 closestP = segA + t * ab;
                        float distToEdge = glm::distance(p, closestP);

                        if (distToEdge <= metricRadius) {
                            glm::vec2 neighborUV = twinHe->corner.uv;
                            glm::vec2 neighborPx = MapUVToPixelCoords(neighborUV, m_uvConfig);
                            PaintBrush crossBrush = brush;
                            crossBrush.opacity *= (1.0f - (distToEdge / std::max(metricRadius, 1e-4f)));
                            m_paintingService->ApplyDab(neighborPx, crossBrush);
                        }
                    }
                }
            }
        }
        curHe = he->next;
    } while (curHe != startHe && curHe.IsValid());
}

// =============================================================================
// BM-PAINT-STAMP-04 — 3D SURFACE STAMP TOOL INTEGRATION
// =============================================================================

static inline bool PointInTriangle2D(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
    float d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
    float d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
    float d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
    bool has_neg = (d1 < -1e-5f) || (d2 < -1e-5f) || (d3 < -1e-5f);
    bool has_pos = (d1 > 1e-5f) || (d2 > 1e-5f) || (d3 > 1e-5f);
    return !(has_neg && has_pos);
}

static inline bool IsPointInFaceUV(const EditableMesh& mesh, FaceID fId, const glm::vec2& pUV) {
    if (!mesh.IsValid(fId)) return false;
    const auto* f = mesh.GetFace(fId);
    if (!f || !f->halfEdge.IsValid()) return false;

    HalfEdgeID h0 = f->halfEdge;
    const auto* he0 = mesh.GetHalfEdge(h0);
    if (!he0) return false;
    glm::vec2 uv0 = he0->corner.uv;

    HalfEdgeID hPrev = he0->next;
    while (hPrev.IsValid()) {
        const auto* hePrev = mesh.GetHalfEdge(hPrev);
        if (!hePrev) break;
        HalfEdgeID hCurr = hePrev->next;
        if (!hCurr.IsValid() || hCurr == h0) break;
        const auto* heCurr = mesh.GetHalfEdge(hCurr);
        if (!heCurr) break;

        glm::vec2 uv1 = hePrev->corner.uv;
        glm::vec2 uv2 = heCurr->corner.uv;

        if (PointInTriangle2D(pUV, uv0, uv1, uv2)) {
            return true;
        }

        hPrev = hCurr;
    }
    return false;
}

bool SurfacePaintingAdapter::ApplyStamp3D(const EditableMesh& mesh, 
                                         const SurfacePaintHit& hit, 
                                         const StampDefinition& stamp, 
                                         uint32_t targetLayerId) {
    // 1. Validazione hit e topologia mesh (Requisito 8)
    if (!hit.valid || !mesh.IsValid(hit.faceId)) {
        std::cout << "[STAMP-DIAG-02] TARGET: Valid=false Path=3D Rejection='hit.valid=false or mesh face invalid'\n";
        return false;
    }

    if (std::isnan(hit.interpolatedUV.x) || std::isnan(hit.interpolatedUV.y)) {
        std::cout << "[STAMP-DIAG-02] TARGET: Valid=false Path=3D Rejection='hit.interpolatedUV has NaN'\n";
        return false;
    }

    // Rispetto del blocco faccia attiva se configurato (consente continuita' sulla stessa isola UV)
    if (m_lockToActiveFace && m_lockedFaceId.IsValid() && hit.faceId != m_lockedFaceId) {
        bool sameIsland = false;
        if (m_uvLayout) {
            auto itA = m_uvLayout->faceToIslandMap.find(m_lockedFaceId);
            auto itB = m_uvLayout->faceToIslandMap.find(hit.faceId);
            if (itA != m_uvLayout->faceToIslandMap.end() && itB != m_uvLayout->faceToIslandMap.end()) {
                sameIsland = (itA->second == itB->second);
            }
        }
        if (sameIsland) {
            m_lockedFaceId = hit.faceId;
        } else {
            std::cout << "[STAMP-DIAG-02] TARGET: Valid=false Path=3D FaceID=" << hit.faceId.index
                      << " LockedFaceID=" << m_lockedFaceId.index << " Rejection='Face is locked to another face'\n";
            return false;
        }
    }

    // 2. Risoluzione Target con Copy-On-Write se disponibile
    if (m_targetResolver) {
        auto resolved = m_targetResolver->ResolveTarget(hit, m_activeChannel, m_isolateBySurfaceId);
        if (resolved.valid && resolved.paintingService) {
            m_paintingService = resolved.paintingService;
        }
    }

    if (!m_paintingService) {
        std::cout << "[STAMP-DIAG-02] TARGET: Valid=false Path=3D Rejection='paintingService is null'\n";
        return false;
    }

    // 3. Validazione PaintLayer attivo (Requisito 4 & 8)
    if (targetLayerId == 0) {
        std::cout << "[STAMP-DIAG-02] TARGET: Valid=false Path=3D Rejection='targetLayerId is 0'\n";
        return false;
    }
    const PaintLayer* targetLayer = m_paintingService->GetLayer(targetLayerId);
    if (!targetLayer || targetLayer->isLocked) {
        std::cout << "[STAMP-DIAG-02] TARGET: Valid=false Path=3D LayerID=" << targetLayerId 
                  << " Rejection='Layer does not exist or is locked'\n";
        return false;
    }

    // 4. Sincronizzazione dimensioni canvas e conversione UV -> Pixel (Requisiti 1 & 3)
    m_uvConfig.textureWidth = m_paintingService->GetCanvasWidth();
    m_uvConfig.textureHeight = m_paintingService->GetCanvasHeight();
    if (m_uvConfig.textureWidth == 0 || m_uvConfig.textureHeight == 0) {
        std::cout << "[STAMP-DIAG-02] TARGET: Valid=false Path=3D Rejection='Canvas dimensions are 0'\n";
        return false;
    }

    glm::vec2 pxCoords = MapUVToPixelCoords(hit.interpolatedUV, m_uvConfig);

    // 5. Costruzione del PixelClipCallback per l'isolamento della UV island (Requisiti 5 & 8)
    // Impedisce di dipingere sulle altre UV islands anche quando lo stampo supera i bordi della faccia
    UVIslandID targetIsland = INVALID_UVISLAND_ID;
    if (m_uvLayout) {
        auto it = m_uvLayout->faceToIslandMap.find(hit.faceId);
        if (it != m_uvLayout->faceToIslandMap.end()) {
            targetIsland = it->second;
        }
        if (targetIsland >= m_uvLayout->islands.size()) {
            std::cout << "[STAMP-DIAG-02] TARGET: Valid=false Path=3D FaceID=" << hit.faceId.index 
                      << " Rejection='targetIsland out of bounds (" << targetIsland << " >= " << m_uvLayout->islands.size() << ")'\n";
            return false; // UV island non valida
        }
    }

    std::cout << "[STAMP-DIAG-02] TARGET: Valid=true Path=3D FaceID=" << hit.faceId.index
              << " TargetIsland=" << (targetIsland != INVALID_UVISLAND_ID ? std::to_string(targetIsland) : "NONE")
              << " UV=(" << hit.interpolatedUV.x << "," << hit.interpolatedUV.y << ")"
              << " Pixel=(" << pxCoords.x << "," << pxCoords.y << ")"
              << " CanvasSize=(" << m_uvConfig.textureWidth << "x" << m_uvConfig.textureHeight << ")\n";

    uint32_t texW = m_uvConfig.textureWidth;
    uint32_t texH = m_uvConfig.textureHeight;
    bool flipV = m_uvConfig.flipV;

    PixelClipCallback clipCb = nullptr;
    if (m_uvLayout && targetIsland < m_uvLayout->islands.size()) {
        // BM-PAINTER-PERF-02: Fast O(1) mask lookup replaces per-pixel geometric face scans
        auto mask = GetOrCreateIslandRasterMask(mesh, *m_uvLayout, targetIsland, texW, texH, flipV);
        m_paintingService->SetStrokeClipMask(mask);
    } else {
        m_paintingService->SetStrokeClipMask(nullptr);
        // Fallback: se nessun UVLayout e' associato, vincola rigorosamente alla sola faccia di impatto
        FaceID hitFace = hit.faceId;
        clipCb = [&mesh, hitFace, texW, texH, flipV](int32_t px, int32_t py) -> bool {
            float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(texW);
            float v = (static_cast<float>(py) + 0.5f) / static_cast<float>(texH);
            if (flipV) v = 1.0f - v;
            glm::vec2 pUV(u, v);
            return IsPointInFaceUV(mesh, hitFace, pUV);
        };
    }

    // 6. Applicazione atomica one-shot sul TexturePaintingService (Requisiti 6 & 7)
    bool applied = m_paintingService->ApplyStampClipped(targetLayerId, pxCoords, stamp, clipCb);
    m_paintingService->SetStrokeClipMask(nullptr);
    return applied;
}

bool SurfacePaintingAdapter::ApplyStamp3D(const EditableMesh& mesh, 
                                         const glm::vec3& rayOrigin, 
                                         const glm::vec3& rayDirection, 
                                         const StampDefinition& stamp, 
                                         uint32_t targetLayerId,
                                         const glm::mat4& modelMatrix) {
    SurfacePaintHit hit = RaycastMesh(mesh, rayOrigin, rayDirection, 1000.0f, modelMatrix);
    return ApplyStamp3D(mesh, hit, stamp, targetLayerId);
}

} // namespace fw
