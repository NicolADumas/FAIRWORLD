#include "TextureReprojectionService.h"
#include "UVEditorDocument.h"

#include <cmath>
#include <algorithm>
#include <iostream>

namespace fw {

// =============================================================================
// REPROJECTION STRATEGY DEDUCTION
// =============================================================================

ReprojectionStrategy TextureReprojectionService::DeduceStrategy(
    const EditableMesh& sourceMesh,
    const UVLayout& sourceLayout,
    const EditableMesh& destMesh,
    const UVLayout& destLayout,
    const ReprojectionOptions& options) const 
{
    if (options.strategy != ReprojectionStrategy::Auto) {
        return options.strategy;
    }

    // Se la topologia e cambiata o il numero di facce differisce -> TopologyChanged
    if (sourceMesh.GetTopologyRevision() != destMesh.GetTopologyRevision() ||
        sourceMesh.GetFaceCount() != destMesh.GetFaceCount()) 
    {
        return ReprojectionStrategy::TopologyChanged;
    }

    // Se il numero di isole UV e cambiato -> SeamReconfiguration
    if (sourceLayout.islands.size() != destLayout.islands.size()) {
        return ReprojectionStrategy::SeamReconfiguration;
    }

    // Se le isole hanno solo rotazione/traslazione/scala diversa -> AtlasRepack
    bool onlyAtlasPackingChanged = true;
    for (size_t i = 0; i < sourceLayout.islands.size(); ++i) {
        const auto& srcIsl = sourceLayout.islands[i];
        const auto& dstIsl = destLayout.islands[i];

        if (srcIsl.faces.size() != dstIsl.faces.size()) {
            onlyAtlasPackingChanged = false;
            break;
        }
    }

    if (onlyAtlasPackingChanged) {
        return ReprojectionStrategy::AtlasRepack;
    }

    return ReprojectionStrategy::SameTopologyUVChange;
}

// =============================================================================
// REVISION TRACKING & ZERO-WORK FINGERPRINT
// =============================================================================

uint64_t TextureReprojectionService::ComputeReprojectionFingerprint(
    const UVLayout& sourceLayout,
    const UVLayout& destLayout,
    const TexturePaintingService& paintingService,
    const ReprojectionOptions& options) const 
{
    // Algoritmo FNV-1a 64-bit deterministico
    uint64_t hash = 14695981039346656037ULL;
    auto hashCombine = [&hash](uint64_t val) {
        hash ^= val;
        hash *= 1099511628211ULL;
    };

    hashCombine(sourceLayout.uvRevision);
    hashCombine(sourceLayout.atlasRevision);
    hashCombine(destLayout.uvRevision);
    hashCombine(destLayout.atlasRevision);
    hashCombine(paintingService.ComputeCombinedFingerprint());
    hashCombine(static_cast<uint64_t>(options.strategy));
    hashCombine(static_cast<uint64_t>(options.samplingMode));
    hashCombine(static_cast<uint64_t>(options.edgeDilationPixels));
    hashCombine(static_cast<uint64_t>(options.overrideWidth));
    hashCombine(static_cast<uint64_t>(options.overrideHeight));

    return hash;
}

// =============================================================================
// TRIANGLE RASTERIZATION & RESAMPLING
// =============================================================================

void TextureReprojectionService::RasterizeTriangle(
    const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2,
    const glm::vec2& srcUV0, const glm::vec2& srcUV1, const glm::vec2& srcUV2,
    const ImageBitmap& srcBitmap,
    ImageBitmap& dstBitmap,
    std::vector<bool>& coverageMask,
    uint32_t dstW, uint32_t dstH,
    ReprojectionSamplingMode mode,
    ReprojectionDiagnosticReport& report) 
{
    if (dstW == 0 || dstH == 0 || !srcBitmap.IsValid() || !dstBitmap.IsValid()) {
        return;
    }

    // Determinante 2D per orientamento e area triangolo destinazione
    float det = (uv1.x - uv0.x) * (uv2.y - uv0.y) - (uv1.y - uv0.y) * (uv2.x - uv0.x);
    if (std::abs(det) < 1e-8f) {
        report.degenerateTrianglesSkipped++;
        return;
    }

    if (det < 0.0f) {
        report.invertedTrianglesHandled++;
    }

    // Bounding Box del triangolo in coordinate pixel destinazione
    glm::vec2 p0 = uv0 * glm::vec2(static_cast<float>(dstW), static_cast<float>(dstH));
    glm::vec2 p1 = uv1 * glm::vec2(static_cast<float>(dstW), static_cast<float>(dstH));
    glm::vec2 p2 = uv2 * glm::vec2(static_cast<float>(dstW), static_cast<float>(dstH));

    int minX = std::max(0, static_cast<int>(std::floor(std::min({p0.x, p1.x, p2.x}))));
    int maxX = std::min(static_cast<int>(dstW) - 1, static_cast<int>(std::ceil(std::max({p0.x, p1.x, p2.x}))));
    int minY = std::max(0, static_cast<int>(std::floor(std::min({p0.y, p1.y, p2.y}))));
    int maxY = std::min(static_cast<int>(dstH) - 1, static_cast<int>(std::ceil(std::max({p0.y, p1.y, p2.y}))));

    if (minX > maxX || minY > maxY) {
        return;
    }

    // Configurazione sottocampionamento
    int sampleGrid = 1;
    if (mode == ReprojectionSamplingMode::Supersampling2x) sampleGrid = 2;
    else if (mode == ReprojectionSamplingMode::Supersampling4x) sampleGrid = 4;

    float invGrid = 1.0f / static_cast<float>(sampleGrid);
    float invDet = 1.0f / det;
    glm::vec2 v01 = uv1 - uv0;
    glm::vec2 v02 = uv2 - uv0;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            glm::vec4 accumulatedColor(0.0f);
            int validSubSamples = 0;

            for (int sy = 0; sy < sampleGrid; ++sy) {
                for (int sx = 0; sx < sampleGrid; ++sx) {
                    float subPixelX = static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) * invGrid;
                    float subPixelY = static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) * invGrid;

                    glm::vec2 p(subPixelX / static_cast<float>(dstW), subPixelY / static_cast<float>(dstH));
                    glm::vec2 v0p = p - uv0;

                    // Calcolo baricentrico analitico
                    float lambda1 = (v0p.x * v02.y - v0p.y * v02.x) * invDet;
                    float lambda2 = (v01.x * v0p.y - v01.y * v0p.x) * invDet;
                    float lambda0 = 1.0f - lambda1 - lambda2;

                    // Test di inclusione con tolleranza per cuciture condivise
                    constexpr float eps = -1e-4f;
                    if (lambda0 >= eps && lambda1 >= eps && lambda2 >= eps) {
                        float l0 = std::clamp(lambda0, 0.0f, 1.0f);
                        float l1 = std::clamp(lambda1, 0.0f, 1.0f);
                        float l2 = std::clamp(lambda2, 0.0f, 1.0f);
                        float sum = l0 + l1 + l2;
                        if (sum > 1e-6f) {
                            l0 /= sum; l1 /= sum; l2 /= sum;
                        }

                        // UV sorgente interpolata nello spazio della sorgente
                        glm::vec2 srcUV = l0 * srcUV0 + l1 * srcUV1 + l2 * srcUV2;

                        glm::vec4 sampleColor;
                        if (mode == ReprojectionSamplingMode::Nearest) {
                            sampleColor = srcBitmap.SampleNearest(srcUV, TextureWrapMode::ClampToEdge);
                        } else {
                            sampleColor = srcBitmap.SampleBilinear(srcUV, TextureWrapMode::ClampToEdge);
                        }

                        accumulatedColor += sampleColor;
                        validSubSamples++;
                    }
                }
            }

            if (validSubSamples > 0) {
                accumulatedColor /= static_cast<float>(validSubSamples);
                dstBitmap.SetPixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y), accumulatedColor);

                size_t maskIdx = static_cast<size_t>(y) * dstW + static_cast<size_t>(x);
                if (maskIdx < coverageMask.size()) {
                    coverageMask[maskIdx] = true;
                }
                report.totalTexelsWritten++;
            }
        }
    }
}

// =============================================================================
// EDGE DILATION (GUTTER SEAM PADDING)
// =============================================================================

void TextureReprojectionService::DilateEdges(
    ImageBitmap& bitmap, 
    std::vector<bool>& coverageMask, 
    uint32_t width, uint32_t height, 
    uint32_t iterations) 
{
    if (iterations == 0 || width == 0 || height == 0 || !bitmap.IsValid()) {
        return;
    }

    for (uint32_t it = 0; it < iterations; ++it) {
        std::vector<bool> nextMask = coverageMask;
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                size_t idx = static_cast<size_t>(y) * width + x;
                if (coverageMask[idx]) {
                    continue; // Gia coperto da geometria UV diretta
                }

                glm::vec4 neighborSum(0.0f);
                int neighborCount = 0;

                // Controlla 4 vicini
                if (x > 0 && coverageMask[idx - 1]) {
                    neighborSum += bitmap.GetPixel(x - 1, y);
                    neighborCount++;
                }
                if (x + 1 < width && coverageMask[idx + 1]) {
                    neighborSum += bitmap.GetPixel(x + 1, y);
                    neighborCount++;
                }
                if (y > 0 && coverageMask[idx - width]) {
                    neighborSum += bitmap.GetPixel(x, y - 1);
                    neighborCount++;
                }
                if (y + 1 < height && coverageMask[idx + width]) {
                    neighborSum += bitmap.GetPixel(x, y + 1);
                    neighborCount++;
                }

                if (neighborCount > 0) {
                    bitmap.SetPixel(x, y, neighborSum / static_cast<float>(neighborCount));
                    nextMask[idx] = true;
                }
            }
        }
        coverageMask = std::move(nextMask);
    }
}

// =============================================================================
// REPROJECTION DI SINGOLA BITMAP
// =============================================================================

ReprojectionDiagnosticReport TextureReprojectionService::ReprojectBitmap(
    const EditableMesh& sourceMesh,
    const UVLayout& sourceLayout,
    const ImageBitmap& sourceBitmap,
    uint32_t sourceAtlasPage,
    const EditableMesh& destMesh,
    const UVLayout& destLayout,
    uint32_t destAtlasPage,
    ImageBitmap& outDestBitmap,
    const ReprojectionOptions& options,
    SurfaceCorrespondenceProvider correspondenceProvider) 
{
    std::unordered_map<uint32_t, ImageBitmap> srcBitmaps;
    srcBitmaps[sourceAtlasPage] = sourceBitmap;

    std::unordered_map<uint32_t, ImageBitmap> dstBitmaps;

    auto report = ReprojectMultiPageBitmaps(sourceMesh, sourceLayout, srcBitmaps,
                                           destMesh, destLayout, dstBitmaps,
                                           options, correspondenceProvider);

    if (dstBitmaps.find(destAtlasPage) != dstBitmaps.end()) {
        outDestBitmap = std::move(dstBitmaps[destAtlasPage]);
    } else {
        uint32_t w = options.overrideWidth > 0 ? options.overrideWidth : destLayout.atlasTargetWidth;
        uint32_t h = options.overrideHeight > 0 ? options.overrideHeight : destLayout.atlasTargetHeight;
        outDestBitmap = ImageBitmap(w, h, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.0f));
    }

    return report;
}

// =============================================================================
// REPROJECTION MULTI-PAGINA ATLAS (BM-UV6.5)
// =============================================================================

ReprojectionDiagnosticReport TextureReprojectionService::ReprojectMultiPageBitmaps(
    const EditableMesh& sourceMesh,
    const UVLayout& sourceLayout,
    const std::unordered_map<uint32_t, ImageBitmap>& sourceBitmaps,
    const EditableMesh& destMesh,
    const UVLayout& destLayout,
    std::unordered_map<uint32_t, ImageBitmap>& outDestBitmaps,
    const ReprojectionOptions& options,
    SurfaceCorrespondenceProvider correspondenceProvider) 
{
    ReprojectionDiagnosticReport report;
    report.strategyUsed = DeduceStrategy(sourceMesh, sourceLayout, destMesh, destLayout, options);

    // Validazione layout
    if (sourceLayout.islands.empty() && sourceMesh.GetFaceCount() > 0) {
        report.AddError("Source UVLayout contains no islands.");
        report.status = ReprojectionStatus::InvalidSourceLayout;
        return report;
    }
    if (destLayout.islands.empty() && destMesh.GetFaceCount() > 0) {
        report.AddError("Destination UVLayout contains no islands.");
        report.status = ReprojectionStatus::InvalidDestinationLayout;
        return report;
    }

    // Se la topologia e alterata ma manca il provider di corrispondenza esplicito
    if (report.strategyUsed == ReprojectionStrategy::TopologyChanged && correspondenceProvider == nullptr) {
        report.AddError("Topology changed requires an explicit SurfaceCorrespondenceProvider.");
        report.status = ReprojectionStatus::RequiresCorrespondence;
        return report;
    }

    // Risoluzione dimensioni target destinazione
    uint32_t targetW = options.overrideWidth > 0 ? options.overrideWidth : destLayout.atlasTargetWidth;
    uint32_t targetH = options.overrideHeight > 0 ? options.overrideHeight : destLayout.atlasTargetHeight;
    if (targetW == 0) targetW = 1024;
    if (targetH == 0) targetH = 1024;

    uint32_t totalPages = std::max(1u, destLayout.totalAtlasPages);
    outDestBitmaps.clear();
    std::unordered_map<uint32_t, std::vector<bool>> coverageMasks;

    for (uint32_t p = 0; p < totalPages; ++p) {
        outDestBitmaps[p] = ImageBitmap(targetW, targetH, TexturePixelFormat::RGBA8, 
                                        TextureColorSpace::sRGB, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
        coverageMasks[p] = std::vector<bool>(static_cast<size_t>(targetW) * targetH, false);
    }

    // Iterazione su tutte le facce della mesh di destinazione
    auto activeFaceIDs = destMesh.GetActiveFaceIDs();
    for (FaceID fId : activeFaceIDs) {
        auto itDst = destLayout.faceToIslandMap.find(fId);
        if (itDst == destLayout.faceToIslandMap.end()) {
            continue;
        }

        UVIslandID dstIslandId = itDst->second;
        if (dstIslandId >= destLayout.islands.size()) continue;
        const auto& dstIsland = destLayout.islands[dstIslandId];
        uint32_t dstPage = dstIsland.atlasPage;

        // Triangolazione della faccia destinazione
        std::vector<CornerAttributes> dstCorners = destMesh.GetFaceCorners(fId);
        if (dstCorners.size() < 3) continue;

        // Recupero corrispondenza sorgente
        std::vector<CornerAttributes> srcCorners;
        uint32_t srcPage = 0;

        if (report.strategyUsed != ReprojectionStrategy::TopologyChanged) {
            if (!sourceMesh.IsValid(fId)) continue;
            auto itSrc = sourceLayout.faceToIslandMap.find(fId);
            if (itSrc == sourceLayout.faceToIslandMap.end()) continue;
            UVIslandID srcIslandId = itSrc->second;
            if (srcIslandId >= sourceLayout.islands.size()) continue;
            srcPage = sourceLayout.islands[srcIslandId].atlasPage;
            srcCorners = sourceMesh.GetFaceCorners(fId);
            if (srcCorners.size() != dstCorners.size()) continue;
        }

        auto itSrcBitmap = sourceBitmaps.find(srcPage);
        if (itSrcBitmap == sourceBitmaps.end() || !itSrcBitmap->second.IsValid()) {
            report.AddWarning("Missing source texture for atlas page " + std::to_string(srcPage));
            continue;
        }
        const ImageBitmap& srcBitmap = itSrcBitmap->second;

        // Triangolazione a ventaglio (Fan Triangulation) per N-gon convessi/quad
        size_t triCount = dstCorners.size() - 2;
        for (size_t t = 0; t < triCount; ++t) {
            glm::vec2 dstUV0 = dstCorners[0].uv;
            glm::vec2 dstUV1 = dstCorners[t + 1].uv;
            glm::vec2 dstUV2 = dstCorners[t + 2].uv;

            glm::vec2 srcUV0(0.0f), srcUV1(0.0f), srcUV2(0.0f);
            if (report.strategyUsed != ReprojectionStrategy::TopologyChanged) {
                srcUV0 = srcCorners[0].uv;
                srcUV1 = srcCorners[t + 1].uv;
                srcUV2 = srcCorners[t + 2].uv;
            }

            RasterizeTriangle(dstUV0, dstUV1, dstUV2,
                              srcUV0, srcUV1, srcUV2,
                              srcBitmap,
                              outDestBitmaps[dstPage],
                              coverageMasks[dstPage],
                              targetW, targetH,
                              options.samplingMode,
                              report);
        }
    }

    // Dilation per ciascuna pagina
    if (options.edgeDilationPixels > 0) {
        for (uint32_t p = 0; p < totalPages; ++p) {
            DilateEdges(outDestBitmaps[p], coverageMasks[p], targetW, targetH, options.edgeDilationPixels);
        }
    }

    report.status = ReprojectionStatus::Success;
    return report;
}

// =============================================================================
// REPROJECTION SU TEXTURE PAINTING SERVICE (BM-UV6.2 & BM-UV6.5)
// =============================================================================

ReprojectionDiagnosticReport TextureReprojectionService::ReprojectPaintingService(
    const EditableMesh& sourceMesh,
    const UVLayout& sourceLayout,
    TexturePaintingService& paintingService,
    const EditableMesh& destMesh,
    const UVLayout& destLayout,
    const ReprojectionOptions& options,
    SurfaceCorrespondenceProvider correspondenceProvider) 
{
    ReprojectionDiagnosticReport report;

    // Controllo Zero-Work
    uint64_t currentFingerprint = ComputeReprojectionFingerprint(sourceLayout, destLayout, paintingService, options);
    if (m_lastFingerprint != 0 && m_lastFingerprint == currentFingerprint) {
        report.status = ReprojectionStatus::ZeroWorkNoOp;
        report.zeroWorkHit = true;
        return report;
    }

    if (paintingService.GetLayerCount() == 0) {
        report.status = ReprojectionStatus::Success;
        return report;
    }

    uint32_t targetW = options.overrideWidth > 0 ? options.overrideWidth : destLayout.atlasTargetWidth;
    uint32_t targetH = options.overrideHeight > 0 ? options.overrideHeight : destLayout.atlasTargetHeight;
    if (targetW == 0) targetW = 1024;
    if (targetH == 0) targetH = 1024;

    // Preservazione layer individuali (BM-UV6.5)
    auto& layers = paintingService.GetLayers();
    for (auto& layer : layers) {
        if (!layer.pixelBuffer.IsValid()) continue;

        ImageBitmap reprojectedBitmap;
        auto singleReport = ReprojectBitmap(sourceMesh, sourceLayout, layer.pixelBuffer, 0,
                                            destMesh, destLayout, 0,
                                            reprojectedBitmap, options, correspondenceProvider);

        if (singleReport.status != ReprojectionStatus::Success) {
            report = singleReport;
            return report;
        }

        layer.pixelBuffer = std::move(reprojectedBitmap);
        layer.revision++;
        report.totalLayersProcessed++;
        report.totalTexelsWritten += singleReport.totalTexelsWritten;
        report.degenerateTrianglesSkipped += singleReport.degenerateTrianglesSkipped;
        report.invertedTrianglesHandled += singleReport.invertedTrianglesHandled;

        // Se presente una maschera di livello, riproietta anch'essa
        if (layer.HasMask()) {
            ImageBitmap maskBitmap = layer.mask->GetBitmap();
            ImageBitmap reprojectedMask;
            ReprojectBitmap(sourceMesh, sourceLayout, maskBitmap, 0,
                            destMesh, destLayout, 0,
                            reprojectedMask, options, correspondenceProvider);
            layer.mask->SetBitmap(reprojectedMask);
        }
    }

    paintingService.SetCanvasDimensions(targetW, targetH);
    paintingService.IncrementRevision();
    m_lastFingerprint = ComputeReprojectionFingerprint(sourceLayout, destLayout, paintingService, options);
    report.status = ReprojectionStatus::Success;
    return report;
}

// =============================================================================
// INTEGRAZIONE UV EDITOR DOCUMENT (BM-UV6.9)
// =============================================================================

ReprojectionDiagnosticReport TextureReprojectionService::ReprojectDocument(
    UVEditorDocument& document,
    const UVLayout& snapshotLayout,
    TexturePaintingService& paintingService,
    const ReprojectionOptions& options) 
{
    ReprojectionDiagnosticReport report = ReprojectPaintingService(
        document.GetMesh(),
        snapshotLayout,
        paintingService,
        document.GetMesh(),
        document.GetLayout(),
        options);

    if (report.status == ReprojectionStatus::Success || report.status == ReprojectionStatus::ZeroWorkNoOp) {
        // Azzeramento flag di sicurezza autoritativo post-migrazione riuscita
        document.AcknowledgeTextureMigration();
    }

    return report;
}

// =============================================================================
// TRANSAZIONE ATOMICA DI MIGRAZIONE (BM-UV6.7)
// =============================================================================

ReprojectionTransaction TextureReprojectionService::BeginTransaction(const std::string& id) {
    ReprojectionTransaction tx;
    tx.phase = ReprojectionTransaction::Phase::Prepared;
    tx.transactionId = id;
    return tx;
}

bool TextureReprojectionService::ValidateTransaction(
    ReprojectionTransaction& tx,
    const EditableMesh& sourceMesh, const UVLayout& sourceLayout,
    const EditableMesh& destMesh, const UVLayout& destLayout,
    const ReprojectionOptions& options) 
{
    tx.phase = ReprojectionTransaction::Phase::Validated;
    if (sourceLayout.islands.empty() && sourceMesh.GetFaceCount() > 0) {
        tx.report.AddError("Validation failed: Source UVLayout has no islands.");
        return false;
    }
    if (destLayout.islands.empty() && destMesh.GetFaceCount() > 0) {
        tx.report.AddError("Validation failed: Destination UVLayout has no islands.");
        return false;
    }
    return true;
}

bool TextureReprojectionService::ExecuteReprojection(
    ReprojectionTransaction& tx,
    const EditableMesh& sourceMesh, const UVLayout& sourceLayout,
    const std::unordered_map<uint32_t, ImageBitmap>& sourceBitmaps,
    const EditableMesh& destMesh, const UVLayout& destLayout,
    const ReprojectionOptions& options,
    SurfaceCorrespondenceProvider correspondenceProvider) 
{
    tx.backupPageBitmaps = sourceBitmaps;
    tx.report = ReprojectMultiPageBitmaps(sourceMesh, sourceLayout, sourceBitmaps,
                                         destMesh, destLayout, tx.stagedPageBitmaps,
                                         options, correspondenceProvider);

    if (tx.report.status == ReprojectionStatus::Success) {
        tx.phase = ReprojectionTransaction::Phase::Reprojected;
        return true;
    } else {
        RollbackTransaction(tx);
        return false;
    }
}

bool TextureReprojectionService::VerifyTransaction(ReprojectionTransaction& tx) {
    if (tx.phase != ReprojectionTransaction::Phase::Reprojected) return false;
    if (tx.stagedPageBitmaps.empty()) return false;

    for (const auto& [page, bmp] : tx.stagedPageBitmaps) {
        if (!bmp.IsValid()) return false;
    }
    tx.phase = ReprojectionTransaction::Phase::Verified;
    return true;
}

bool TextureReprojectionService::CommitTransaction(
    ReprojectionTransaction& tx, 
    std::unordered_map<uint32_t, ImageBitmap>& targetBitmaps) 
{
    if (tx.phase != ReprojectionTransaction::Phase::Verified) {
        RollbackTransaction(tx);
        return false;
    }

    targetBitmaps = std::move(tx.stagedPageBitmaps);
    tx.phase = ReprojectionTransaction::Phase::Committed;
    return true;
}

bool TextureReprojectionService::RollbackTransaction(ReprojectionTransaction& tx) {
    tx.stagedPageBitmaps.clear();
    tx.phase = ReprojectionTransaction::Phase::RolledBack;
    tx.report.status = ReprojectionStatus::RollbackExecuted;
    return true;
}

} // namespace fw
