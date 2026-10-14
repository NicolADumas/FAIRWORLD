#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <glm/glm.hpp>

#include "blocks/sculpt/EditableMesh.h"
#include "blocks/uv/UVLayout.h"
#include "blocks/uv/UVAtlasPacker.h"
#include "blocks/texture/ImageBitmap.h"
#include "blocks/texture/PaintLayer.h"
#include "blocks/texture/TexturePaintingService.h"

namespace fw {

class UVEditorDocument;

// =============================================================================
// BM-UV6.3 — STRATEGIE DI RIPROIEZIONE
// =============================================================================

enum class ReprojectionStrategy : uint8_t {
    Auto = 0,
    SameTopologyUVChange,   // Modifiche alle coordinate UV senza alterazione della topologia
    AtlasRepack,            // Spostamento, rotazione, scaling o cambio pagina atlas
    SeamReconfiguration,    // Modifica delle cuciture e ricomposizione delle isole
    TopologyChanged         // Mesh geometricamente o topologicamente alterata
};

inline const char* GetReprojectionStrategyName(ReprojectionStrategy s) {
    switch (s) {
        case ReprojectionStrategy::Auto:                 return "Auto";
        case ReprojectionStrategy::SameTopologyUVChange: return "SameTopologyUVChange";
        case ReprojectionStrategy::AtlasRepack:          return "AtlasRepack";
        case ReprojectionStrategy::SeamReconfiguration:  return "SeamReconfiguration";
        case ReprojectionStrategy::TopologyChanged:      return "TopologyChanged";
        default:                                         return "Unknown";
    }
}

// =============================================================================
// BM-UV6.6 — MODALITA DI CAMPIONAMENTO & QUALITA
// =============================================================================

enum class ReprojectionSamplingMode : uint8_t {
    Nearest = 0,
    Bilinear = 1,
    Supersampling2x = 2,    // 4 campioni per texel
    Supersampling4x = 3     // 16 campioni per texel
};

enum class ReprojectionStatus : uint8_t {
    Success = 0,
    ZeroWorkNoOp,
    RequiresCorrespondence,  // Richiede mappatura esplicita per mesh con topologia modificata
    InvalidSourceLayout,
    InvalidDestinationLayout,
    MissingSourceTexture,
    DegenerateUVDetected,
    ValidationFailed,
    RollbackExecuted
};

inline const char* GetReprojectionStatusName(ReprojectionStatus s) {
    switch (s) {
        case ReprojectionStatus::Success:                  return "Success";
        case ReprojectionStatus::ZeroWorkNoOp:              return "ZeroWorkNoOp";
        case ReprojectionStatus::RequiresCorrespondence:   return "RequiresCorrespondence";
        case ReprojectionStatus::InvalidSourceLayout:      return "InvalidSourceLayout";
        case ReprojectionStatus::InvalidDestinationLayout: return "InvalidDestinationLayout";
        case ReprojectionStatus::MissingSourceTexture:     return "MissingSourceTexture";
        case ReprojectionStatus::DegenerateUVDetected:     return "DegenerateUVDetected";
        case ReprojectionStatus::ValidationFailed:         return "ValidationFailed";
        case ReprojectionStatus::RollbackExecuted:         return "RollbackExecuted";
        default:                                           return "Unknown";
    }
}

// =============================================================================
// OPZIONI DI CONFIGURAZIONE RIPROIEZIONE (BM-UV6.2 & BM-UV6.6)
// =============================================================================

struct ReprojectionOptions {
    ReprojectionStrategy strategy = ReprojectionStrategy::Auto;
    ReprojectionSamplingMode samplingMode = ReprojectionSamplingMode::Bilinear;
    uint32_t edgeDilationPixels = 2;         // Padding bordi seam per evitare linee scure
    bool preserveLayersIndividually = true;  // Preserva layer separati invece di appiattire
    bool allowLossyResampling = true;        // Consente ricampionamento con interpolazione
    uint32_t overrideWidth = 0;              // 0 = usa risoluzione atlas del layout
    uint32_t overrideHeight = 0;
};

// =============================================================================
// REPORT DIAGNOSTICO RIPROIEZIONE (BM-UV6.2)
// =============================================================================

struct ReprojectionDiagnosticReport {
    ReprojectionStatus status = ReprojectionStatus::Success;
    ReprojectionStrategy strategyUsed = ReprojectionStrategy::Auto;
    uint32_t totalLayersProcessed = 0;
    uint32_t totalTexelsWritten = 0;
    uint32_t totalTexelsDilated = 0;
    uint32_t degenerateTrianglesSkipped = 0;
    uint32_t invertedTrianglesHandled = 0;
    float maxColorDeviation = 0.0f;
    bool zeroWorkHit = false;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;

    void AddError(const std::string& err) {
        errors.push_back(err);
        status = ReprojectionStatus::ValidationFailed;
    }
    void AddWarning(const std::string& warn) {
        warnings.push_back(warn);
    }
};

// =============================================================================
// CORRISPONDENZA TOPOLOGICA ESPLICITA (BM-UV6.3 & BM-UV6.4)
// =============================================================================

struct SurfacePointCorrespondence {
    FaceID sourceFace = INVALID_FACE_ID;
    glm::vec3 barycentricCoords{1.0f, 0.0f, 0.0f}; // Coordinate baricentriche su sourceFace
    float confidence = 1.0f;                       // [0, 1] affidabilita della corrispondenza
};

using SurfaceCorrespondenceProvider = std::function<SurfacePointCorrespondence(
    FaceID destFace, const glm::vec3& destBarycentric, const glm::vec3& destWorldPos)>;

// =============================================================================
// TRANSAZIONE DI MIGRAZIONE ATOMICA (BM-UV6.7)
// =============================================================================

struct ReprojectionTransaction {
    enum class Phase : uint8_t {
        Idle = 0,
        Prepared,
        Validated,
        Reprojected,
        Verified,
        Committed,
        RolledBack
    };

    Phase phase = Phase::Idle;
    std::string transactionId;

    // Snapshot sorgente per rollback garantito
    std::vector<PaintLayer> backupLayers;
    std::unordered_map<uint32_t, ImageBitmap> backupPageBitmaps;

    // Output provvisorio staged
    std::vector<PaintLayer> stagedLayers;
    std::unordered_map<uint32_t, ImageBitmap> stagedPageBitmaps;
    std::unordered_map<uint32_t, std::vector<bool>> coverageMasks;

    ReprojectionDiagnosticReport report;
};

// =============================================================================
// TEXTURE REPROJECTION SERVICE (BM-UV6.2)
// Servizio headless per il ricampionamento e preservazione della pittura su cambi UV
// =============================================================================

class TextureReprojectionService {
public:
    TextureReprojectionService() = default;
    ~TextureReprojectionService() = default;

    // --- Riproiezione Completa su TexturePaintingService ---
    ReprojectionDiagnosticReport ReprojectPaintingService(
        const EditableMesh& sourceMesh,
        const UVLayout& sourceLayout,
        TexturePaintingService& paintingService,
        const EditableMesh& destMesh,
        const UVLayout& destLayout,
        const ReprojectionOptions& options = {},
        SurfaceCorrespondenceProvider correspondenceProvider = nullptr);

    // --- Riproiezione Multi-Pagina Atlas (BM-UV6.5) ---
    ReprojectionDiagnosticReport ReprojectMultiPageBitmaps(
        const EditableMesh& sourceMesh,
        const UVLayout& sourceLayout,
        const std::unordered_map<uint32_t, ImageBitmap>& sourceBitmaps,
        const EditableMesh& destMesh,
        const UVLayout& destLayout,
        std::unordered_map<uint32_t, ImageBitmap>& outDestBitmaps,
        const ReprojectionOptions& options = {},
        SurfaceCorrespondenceProvider correspondenceProvider = nullptr);

    // --- Riproiezione Singola Bitmap ---
    ReprojectionDiagnosticReport ReprojectBitmap(
        const EditableMesh& sourceMesh,
        const UVLayout& sourceLayout,
        const ImageBitmap& sourceBitmap,
        uint32_t sourceAtlasPage,
        const EditableMesh& destMesh,
        const UVLayout& destLayout,
        uint32_t destAtlasPage,
        ImageBitmap& outDestBitmap,
        const ReprojectionOptions& options = {},
        SurfaceCorrespondenceProvider correspondenceProvider = nullptr);

    // --- Integrazione UVEditorDocument (BM-UV6.9) ---
    ReprojectionDiagnosticReport ReprojectDocument(
        UVEditorDocument& document,
        const UVLayout& snapshotLayout,
        TexturePaintingService& paintingService,
        const ReprojectionOptions& options = {});

    // --- Zero-Work Revision Tracking (BM-UV6.8) ---
    uint64_t ComputeReprojectionFingerprint(
        const UVLayout& sourceLayout,
        const UVLayout& destLayout,
        const TexturePaintingService& paintingService,
        const ReprojectionOptions& options) const;

    void InvalidateCache() { m_lastFingerprint = 0; }

    // --- Flusso Transazionale Atomico Esplicito (BM-UV6.7) ---
    ReprojectionTransaction BeginTransaction(const std::string& id);
    bool ValidateTransaction(ReprojectionTransaction& tx,
                             const EditableMesh& sourceMesh, const UVLayout& sourceLayout,
                             const EditableMesh& destMesh, const UVLayout& destLayout,
                             const ReprojectionOptions& options);
    bool ExecuteReprojection(ReprojectionTransaction& tx,
                             const EditableMesh& sourceMesh, const UVLayout& sourceLayout,
                             const std::unordered_map<uint32_t, ImageBitmap>& sourceBitmaps,
                             const EditableMesh& destMesh, const UVLayout& destLayout,
                             const ReprojectionOptions& options,
                             SurfaceCorrespondenceProvider correspondenceProvider = nullptr);
    bool VerifyTransaction(ReprojectionTransaction& tx);
    bool CommitTransaction(ReprojectionTransaction& tx, 
                           std::unordered_map<uint32_t, ImageBitmap>& targetBitmaps);
    bool RollbackTransaction(ReprojectionTransaction& tx);

private:
    // Rasterizzazione e ricampionamento per triangolo
    void RasterizeTriangle(
        const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2,
        const glm::vec2& srcUV0, const glm::vec2& srcUV1, const glm::vec2& srcUV2,
        const ImageBitmap& srcBitmap,
        ImageBitmap& dstBitmap,
        std::vector<bool>& coverageMask,
        uint32_t dstW, uint32_t dstH,
        ReprojectionSamplingMode mode,
        ReprojectionDiagnosticReport& report);

    // Dilatazione bordi isole UV per eliminare seam artifacts
    void DilateEdges(ImageBitmap& bitmap, 
                     std::vector<bool>& coverageMask, 
                     uint32_t width, uint32_t height, 
                     uint32_t iterations);

    // Risoluzione automatica della strategia
    ReprojectionStrategy DeduceStrategy(
        const EditableMesh& sourceMesh,
        const UVLayout& sourceLayout,
        const EditableMesh& destMesh,
        const UVLayout& destLayout,
        const ReprojectionOptions& options) const;

private:
    uint64_t m_lastFingerprint = 0;
};

} // namespace fw
