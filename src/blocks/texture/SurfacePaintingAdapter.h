#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <limits>
#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "blocks/sculpt/EditableMesh.h"
#include "blocks/uv/UVLayout.h"
#include "blocks/texture/TexturePaintingService.h"
#include "blocks/texture/ImageBitmap.h"
#include "blocks/texture/TextureAsset.h"

namespace fw {

// =============================================================================
// BM-TX4.2 — SURFACE PAINT HIT CONTRACT (OBJECT-SPACE & WORLD-SPACE)
// =============================================================================

struct SurfacePaintHit {
    bool valid = false;
    std::string assetId;
    uint64_t objectId = 0;
    FaceID faceId = INVALID_FACE_ID;
    uint32_t triangleIndex = 0;
    uint32_t surfaceId = 0;
    uint32_t materialId = 0;
    glm::vec3 worldPosition{0.0f};
    glm::vec3 localPosition{0.0f};
    glm::vec3 worldNormal{0.0f, 1.0f, 0.0f};
    glm::vec3 localNormal{0.0f, 1.0f, 0.0f};
    glm::vec3 barycentricCoords{0.0f}; // (w, u, v) dove w = 1 - u - v
    glm::vec2 interpolatedUV{0.0f};
    float distance = std::numeric_limits<float>::infinity();
    uint64_t meshTopologyRevision = 0;
    uint64_t meshAttributeRevision = 0;

    // Helper per verificare se il dato di impatto e obsoleto rispetto alla mesh attuale
    bool IsStale(const EditableMesh& mesh) const {
        return !valid ||
               meshTopologyRevision != mesh.GetTopologyRevision() ||
               meshAttributeRevision != mesh.GetAttributeRevision() ||
               !mesh.IsValid(faceId);
    }
};

// Retrocompatibilita
using SurfaceRaycastHit = SurfacePaintHit;

// =============================================================================
// BM-TX4.4 — UV TO PIXEL MAPPING & ORIENTATION
// =============================================================================

struct UVToPixelConfig {
    uint32_t textureWidth = 512;
    uint32_t textureHeight = 512;
    TextureWrapMode wrapMode = TextureWrapMode::Repeat;
    // Orientamento asse V:
    // false = V=0 in cima (row = V * H, convenzione memoria Y-down)
    // true  = V=0 in basso (row = (1.0 - V) * H, convenzione standard UV OpenGL/Vulkan)
    bool flipV = false;
};

float ApplyWrapModeScalar(float coord, TextureWrapMode mode);
glm::vec2 MapUVToPixelCoords(const glm::vec2& uv, const UVToPixelConfig& cfg);

// =============================================================================
// BM-TX4.5 — PAINT CHANNELS & COLOR SPACES
// =============================================================================

enum class PaintChannel : uint8_t {
    BaseColor = 0,
    Roughness,
    Metallic,
    Normal,
    AmbientOcclusion,
    Emissive,
    Height
};

inline const char* GetPaintChannelName(PaintChannel ch) {
    switch (ch) {
        case PaintChannel::BaseColor:        return "BaseColor";
        case PaintChannel::Roughness:        return "Roughness";
        case PaintChannel::Metallic:         return "Metallic";
        case PaintChannel::Normal:           return "Normal";
        case PaintChannel::AmbientOcclusion: return "AmbientOcclusion";
        case PaintChannel::Emissive:         return "Emissive";
        case PaintChannel::Height:           return "Height";
        default:                             return "Unknown";
    }
}

inline TextureColorSpace GetPaintChannelColorSpace(PaintChannel ch) {
    switch (ch) {
        case PaintChannel::BaseColor:
        case PaintChannel::Emissive:
            return TextureColorSpace::sRGB;
        case PaintChannel::Roughness:
        case PaintChannel::Metallic:
        case PaintChannel::Normal:
        case PaintChannel::AmbientOcclusion:
        case PaintChannel::Height:
        default:
            return TextureColorSpace::Linear;
    }
}

// =============================================================================
// BM-TX4.5 & BM-TX4.6 — TEXTURE TARGET RESOLUTION & COPY-ON-WRITE ISOLATION
// =============================================================================

enum class PaintTargetOwnership : uint8_t {
    SharedMaterial = 0,     // Stesso materiale/texture condiviso da piu superfici
    SharedTexture = 1,      // Texture condivisa esplicitamente
    IsolatedSurface = 2,    // Proprieta esclusiva di una superficie (COW applicato)
    DedicatedAsset = 3      // TextureAsset univoco per l'asset
};

struct ResolvedPaintTarget {
    bool valid = false;
    std::string assetId;
    uint32_t surfaceId = 0;
    uint32_t materialId = 0;
    uint32_t layerId = 0;
    PaintChannel channel = PaintChannel::BaseColor;
    TextureColorSpace colorSpace = TextureColorSpace::sRGB;
    TextureWrapMode wrapMode = TextureWrapMode::Repeat;
    uint32_t width = 512;
    uint32_t height = 512;
    PaintTargetOwnership ownership = PaintTargetOwnership::IsolatedSurface;
    std::string targetIdentifier;
    TexturePaintingService* paintingService = nullptr;
    bool requiresCOW = false;
};

class TextureTargetResolver {
public:
    TextureTargetResolver() = default;
    ~TextureTargetResolver() = default;

    // Registra target condiviso per materiale
    void RegisterMaterialTarget(uint32_t materialId, PaintChannel channel, std::shared_ptr<TexturePaintingService> service);

    // Registra target isolato per superficie
    void RegisterSurfaceTarget(uint32_t surfaceId, PaintChannel channel, std::shared_ptr<TexturePaintingService> service);

    // Risoluzione con isolamento Copy-On-Write (COW)
    ResolvedPaintTarget ResolveTarget(const SurfacePaintHit& hit, 
                                      PaintChannel channel = PaintChannel::BaseColor, 
                                      bool requestIsolation = true,
                                      uint32_t defaultWidth = 512,
                                      uint32_t defaultHeight = 512);

    bool HasIsolatedTarget(uint32_t surfaceId, PaintChannel channel) const;
    std::shared_ptr<TexturePaintingService> GetSurfaceTarget(uint32_t surfaceId, PaintChannel channel) const;
    size_t GetIsolatedTargetCount() const { return m_surfaceTargets.size(); }
    void Clear();

private:
    struct TargetKey {
        uint32_t id = 0;
        PaintChannel channel = PaintChannel::BaseColor;
        bool operator==(const TargetKey& o) const { return id == o.id && channel == o.channel; }
    };
    struct TargetKeyHash {
        size_t operator()(const TargetKey& k) const {
            return (static_cast<size_t>(k.id) << 8) ^ static_cast<size_t>(k.channel);
        }
    };

    std::unordered_map<TargetKey, std::shared_ptr<TexturePaintingService>, TargetKeyHash> m_materialTargets;
    std::unordered_map<TargetKey, std::shared_ptr<TexturePaintingService>, TargetKeyHash> m_surfaceTargets;
};

// =============================================================================
// BM-TX4.7 & BM-TX4.9 — BRUSH SPACE MODES & SEAMS
// =============================================================================

enum class BrushSpaceMode : uint8_t {
    UVSpace = 0,       // Raggio espresso direttamente in pixel/texel
    SurfaceSpace = 1   // Raggio espresso in unita metriche 3D (convertito tramite metrica locale del triangolo)
};

enum class SeamMode : uint8_t {
    StopAtSeam = 0,         // Si ferma alla cucitura UV
    ContinueAcrossSeam = 1  // Continua attraverso la cucitura spezzando il segmento per evitare striature
};

// =============================================================================
// BM-TX4.15 — FUTURE GPU INTEGRATION TOKEN (BM-TX5 READY)
// =============================================================================

struct PaintGPUStagingToken {
    std::string textureAssetId;
    std::string paintTargetId;
    PaintDirtyRect dirtyRect;
    TextureColorSpace colorSpace = TextureColorSpace::sRGB;
    uint64_t textureRevision = 0;
    uint32_t uploadGeneration = 0;
    uint64_t lifetimeToken = 0;
    bool readyForUpload = false;
};

// =============================================================================
// BM-TX4.5 — 3D SURFACE BRUSH ADAPTER (HEADLESS)
// =============================================================================

class SurfacePaintingAdapter {
public:
    SurfacePaintingAdapter() = default;
    explicit SurfacePaintingAdapter(TexturePaintingService* paintingService);
    ~SurfacePaintingAdapter() = default;

    void SetPaintingService(TexturePaintingService* service) { m_paintingService = service; }
    TexturePaintingService* GetPaintingService() { return m_paintingService; }
    const TexturePaintingService* GetPaintingService() const { return m_paintingService; }

    void SetTargetResolver(TextureTargetResolver* resolver) { m_targetResolver = resolver; }
    TextureTargetResolver* GetTargetResolver() { return m_targetResolver; }

    // --- Integrazione UVLayout & Cross-Seam Painting (BM-UV5.6 & BM-UV5.7) ---
    void SetUVLayout(const UVLayout* layout) { m_uvLayout = layout; }
    const UVLayout* GetUVLayout() const { return m_uvLayout; }
    void PropagateCrossSeamDab(const EditableMesh& mesh, const SurfacePaintHit& hit, const PaintBrush& brush, float metricRadius);

    // --- Raycast sulla Mesh con gestione Transform / Normal Matrix ---
    static SurfacePaintHit RaycastMesh(const EditableMesh& mesh, 
                                      const glm::vec3& rayOrigin, 
                                      const glm::vec3& rayDirection, 
                                      float maxDistance = 1000.0f,
                                      const glm::mat4& modelMatrix = glm::mat4(1.0f));

    // --- Configurazione Metrica di Superficie, Angoli e Seams ---
    void SetBrushSpaceMode(BrushSpaceMode mode) { m_brushSpaceMode = mode; }
    BrushSpaceMode GetBrushSpaceMode() const { return m_brushSpaceMode; }

    void SetSeamMode(SeamMode mode) { m_seamMode = mode; }
    SeamMode GetSeamMode() const { return m_seamMode; }

    void SetIsolateBySurfaceId(bool isolate) { m_isolateBySurfaceId = isolate; }
    bool GetIsolateBySurfaceId() const { return m_isolateBySurfaceId; }

    void SetMaxNormalDeviationDegrees(float deg) { 
        m_maxNormalDeviationDegrees = deg; 
        m_minNormalDot = std::cos(glm::radians(deg));
    }
    float GetMaxNormalDeviationDegrees() const { return m_maxNormalDeviationDegrees; }

    void SetSeamDiscontinuityThreshold(float threshold) { m_seamDiscontinuityThreshold = threshold; }
    float GetSeamDiscontinuityThreshold() const { return m_seamDiscontinuityThreshold; }

    void SetUVConfig(const UVToPixelConfig& cfg) { m_uvConfig = cfg; }
    const UVToPixelConfig& GetUVConfig() const { return m_uvConfig; }

    void SetActiveChannel(PaintChannel channel) { m_activeChannel = channel; }
    PaintChannel GetActiveChannel() const { return m_activeChannel; }

    // --- Calcolo Metrica di Superficie (Jacobiano / Derivate Tangenti) ---
    static float ComputeSurfaceMetricPixels(const EditableMesh& mesh, 
                                            const SurfacePaintHit& hit, 
                                            float metricRadius, 
                                            const UVToPixelConfig& cfg);

    // --- Interazione Pittura 3D ---
    bool BeginStroke3D(const EditableMesh& mesh, 
                       const glm::vec3& rayOrigin, 
                       const glm::vec3& rayDirection, 
                       const PaintBrush& brush, 
                       uint32_t targetLayerId = 0, 
                       bool targetMask = false,
                       const glm::mat4& modelMatrix = glm::mat4(1.0f));

    bool BeginStroke3D(const EditableMesh& mesh, 
                       const SurfacePaintHit& hit, 
                       const PaintBrush& brush, 
                       uint32_t targetLayerId = 0, 
                       bool targetMask = false);

    bool UpdateStroke3D(const EditableMesh& mesh, 
                        const glm::vec3& rayOrigin, 
                        const glm::vec3& rayDirection, 
                        const PaintBrush& brush,
                        const glm::mat4& modelMatrix = glm::mat4(1.0f));

    bool UpdateStroke3D(const EditableMesh& mesh, 
                        const SurfacePaintHit& hit, 
                        const PaintBrush& brush);

    PaintDirtyRect EndStroke3D();
    bool CancelStroke3D();

    // Staging GPU token per BM-TX5
    PaintGPUStagingToken GenerateGPUStagingToken(const std::string& assetId) const;

    bool IsStroking() const { return m_isStroking; }
    const SurfacePaintHit& GetLastHit() const { return m_lastHit; }
    uint32_t GetActiveSurfaceId() const { return m_activeSurfaceId; }
    uint32_t GetActiveMaterialId() const { return m_activeMaterialId; }
    bool IsRaycastLost() const { return m_raycastLost; }

    void SetLockToActiveFace(bool lock) { m_lockToActiveFace = lock; }
    bool IsLockToActiveFace() const { return m_lockToActiveFace; }
    void SetLockedFaceId(FaceID fid) { m_lockedFaceId = fid; }
    FaceID GetLockedFaceId() const { return m_lockedFaceId; }

private:
    TexturePaintingService* m_paintingService = nullptr;
    TextureTargetResolver*  m_targetResolver = nullptr;
    const UVLayout*         m_uvLayout = nullptr;
    UVToPixelConfig m_uvConfig;

    BrushSpaceMode m_brushSpaceMode = BrushSpaceMode::UVSpace;
    SeamMode m_seamMode = SeamMode::ContinueAcrossSeam;
    bool m_isolateBySurfaceId = true;
    float m_maxNormalDeviationDegrees = 65.0f;
    float m_minNormalDot = 0.4226f; // cos(65 deg)
    float m_seamDiscontinuityThreshold = 0.35f;
    PaintChannel m_activeChannel = PaintChannel::BaseColor;

    bool m_isStroking = false;
    bool m_raycastLost = false;
    SurfacePaintHit m_lastHit;
    glm::vec3 m_strokeOriginNormal{0.0f, 1.0f, 0.0f};
    glm::vec2 m_prevPixelCoords{0.0f};
    uint32_t m_activeSurfaceId = 0;
    uint32_t m_activeMaterialId = 0;
    PaintDirtyRect m_accumulatedDirtyRect;
    uint32_t m_activeLayerId = 0;
    bool m_activeTargetMask = false;
    bool m_lockToActiveFace = true;
    FaceID m_lockedFaceId;
};

} // namespace fw
