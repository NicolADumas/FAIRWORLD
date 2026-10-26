#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <limits>
#include <cmath>
#include <algorithm>
#include <memory>
#include <glm/glm.hpp>
#include "blocks/sculpt/EditableMesh.h"
#include "blocks/ShapeRegistry.h"
#include "json.hpp"

namespace fw {

// =============================================================================
// BM-UV1 & BM-UV2: STABLE IDENTIFIERS & ENUMERATIONS
// =============================================================================

using UVLayoutID = uint64_t;
using UVIslandID = uint32_t;
inline constexpr UVIslandID INVALID_UVISLAND_ID = 0xFFFFFFFF;

// =============================================================================
// BM-PAINTER-PERF-02: CACHED UV ISLAND RASTER MASK
// Maschera raster precalcolata per isola UV alla risoluzione del canvas
// =============================================================================

struct UVIslandRasterMask {
    UVIslandID islandId = INVALID_UVISLAND_ID;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t meshTopologyRevision = 0;
    uint64_t meshAttributeRevision = 0;
    uint64_t atlasRevision = 0;
    bool flipV = false;
    std::vector<uint8_t> pixels;

    inline bool IsInside(int32_t px, int32_t py) const {
        if (px < 0 || py < 0 || static_cast<uint32_t>(px) >= width || static_cast<uint32_t>(py) >= height) {
            return false;
        }
        if (pixels.empty()) return false;
        return pixels[static_cast<size_t>(py) * width + static_cast<size_t>(px)] != 0;
    }
};

// Strategie di proiezione e sviluppo bidimensionale
enum class UVMappingStrategy : uint8_t {
    Auto = 0,
    Planar,
    Box,
    Cylindrical,
    Conical,
    Spherical,
    Hemisphere,
    Parametric,
    LSCM,           // Least Squares Conformal Maps (rinviato a BM-UV3)
    ABFPlusPlus,    // Angle-Based Flattening (rinviato a BM-UV3)
    PreserveExisting
};

inline const char* GetUVMappingStrategyName(UVMappingStrategy s) {
    switch (s) {
        case UVMappingStrategy::Auto:             return "Auto";
        case UVMappingStrategy::Planar:           return "Planar";
        case UVMappingStrategy::Box:              return "Box";
        case UVMappingStrategy::Cylindrical:      return "Cylindrical";
        case UVMappingStrategy::Conical:          return "Conical";
        case UVMappingStrategy::Spherical:        return "Spherical";
        case UVMappingStrategy::Hemisphere:       return "Hemisphere";
        case UVMappingStrategy::Parametric:       return "Parametric";
        case UVMappingStrategy::LSCM:             return "LSCM";
        case UVMappingStrategy::ABFPlusPlus:      return "ABFPlusPlus";
        case UVMappingStrategy::PreserveExisting: return "PreserveExisting";
        default:                                  return "Unknown";
    }
}

// Universal Fallback Contract (BM-UV2.9)
enum class PrimitiveMappingStatus : uint8_t {
    SUCCESS = 0,
    PARTIAL_SUCCESS,
    NEEDS_BM_UV3,        // Richiede solver generale LSCM o taglio complesso
    INVALID_TOPOLOGY,    // Mesh non-manifold o vertici orfani
    DEGENERATE_GEOMETRY, // Facce a superficie zero o colineari
    UNSUPPORTED_MAPPING  // Forma non texturizzabile analiticamente
};

inline const char* GetPrimitiveMappingStatusName(PrimitiveMappingStatus s) {
    switch (s) {
        case PrimitiveMappingStatus::SUCCESS:             return "SUCCESS";
        case PrimitiveMappingStatus::PARTIAL_SUCCESS:     return "PARTIAL_SUCCESS";
        case PrimitiveMappingStatus::NEEDS_BM_UV3:        return "NEEDS_BM_UV3";
        case PrimitiveMappingStatus::INVALID_TOPOLOGY:    return "INVALID_TOPOLOGY";
        case PrimitiveMappingStatus::DEGENERATE_GEOMETRY: return "DEGENERATE_GEOMETRY";
        case PrimitiveMappingStatus::UNSUPPORTED_MAPPING: return "UNSUPPORTED_MAPPING";
        default:                                          return "Unknown";
    }
}

// Tipologia di cucitura topologica UV (BM-UV2.8)
enum class SeamType : uint8_t {
    None = 0,
    Manual,          // Seam esplicita creata dall'utente (seamWeight preesistente)
    Automatic,       // Rilevata da soglie angolari o cambi di patch
    ParametricCut,   // Taglio canonico della parametrizzazione (es. generatrice cilindro)
    OpenBoundary     // Bordo geometrico aperto non-manifold
};

// =============================================================================
// METRICHE DI DISTORSIONE (BM-UV1.5 / BM-UV2.10)
// =============================================================================

struct IslandDistortion {
    float areaDistortion = 0.0f;      // Rapporto deviazione area 2D / area 3D normalizzata
    float angleDistortion = 0.0f;     // Deviazione angolare media (in gradi)
    float maxStretch = 1.0f;          // Tensione metrica massima
    uint32_t flippedTriangleCount = 0;// Triangoli con orientamento UV invertito (w < 0)
    bool isAcceptable = true;
};

// Spazi di coordinate UV formalmente distinti (BM-UV4.3)
enum class UVCoordinateSpace : uint8_t {
    LocalIsland = 0,    // Spazio locale normalizzato dell'isola [0, 1] pre-packing
    AtlasNormalized,    // Spazio globale dell'atlas UV [0, 1] post-packing
    PixelCoordinates    // Spazio raster intero in pixel [0, W-1] x [0, H-1]
};

inline const char* GetUVCoordinateSpaceName(UVCoordinateSpace s) {
    switch (s) {
        case UVCoordinateSpace::LocalIsland:      return "LocalIsland";
        case UVCoordinateSpace::AtlasNormalized:  return "AtlasNormalized";
        case UVCoordinateSpace::PixelCoordinates: return "PixelCoordinates";
        default:                                  return "Unknown";
    }
}

// =============================================================================
// CONTRATTO UVISLAND (BM-UV1.4 / BM-UV2.2 / BM-UV4.6)
// Componente connessa bidimensionale di facce orientate nel piano UV
// =============================================================================

struct UVIsland {
    UVIslandID id = 0;
    std::string semanticName = "Island";
    uint32_t semanticSurfaceId = 0; // SurfaceID semantico della forma d'origine

    std::vector<FaceID> faces;
    std::vector<HalfEdgeID> boundaryHalfEdges;
    std::vector<EdgeID> seamEdges;

    // Coordinate locali normalizzate per l'isola (prima del packing)
    glm::vec4 localUVBounds{0.0f, 0.0f, 1.0f, 1.0f}; // [uMin, vMin, uMax, vMax]

    // Trasformazione nello spazio atlas finale [scaleU, scaleV, offsetU, offsetV]
    // Identita (1, 1, 0, 0) prima di BM-UV4 Atlas Packing per prevenire doppie trasformazioni
    glm::vec4 atlasTransform{1.0f, 1.0f, 0.0f, 0.0f};
    float rotationDegrees = 0.0f; // 0, 90, 180, 270

    uint32_t atlasPage = 0; // AtlasPageID per supporto Multi-Atlas (BM-UV4.6)

    float surfaceArea3D = 0.0f;
    float area2D = 0.0f;
    float texelDensity = 0.0f; // pixel per unita 3D

    IslandDistortion distortion;

    // Cache maschera raster per isola UV (BM-PAINTER-PERF-02)
    mutable std::shared_ptr<const UVIslandRasterMask> cachedRasterMask;
};

// =============================================================================
// CONTRATTO UVLAYOUT (BM-UV1.4 / BM-UV2.2)
// Rappresentazione autoritativa globale della parametrizzazione dell'asset
// =============================================================================

struct UVLayout {
    UVLayoutID id = 0;
    std::string assetId;

    // Revision Tracking per Zero-Work (BM-UV2.11)
    uint64_t meshTopologyRevision = 0;
    uint64_t meshAttributeRevision = 0;
    uint64_t uvRevision = 1;
    uint64_t atlasRevision = 1;

    UVMappingStrategy strategyUsed = UVMappingStrategy::Auto;
    PrimitiveMappingStatus status = PrimitiveMappingStatus::SUCCESS;

    std::vector<UVIsland> islands;
    std::unordered_map<FaceID, UVIslandID> faceToIslandMap;

    // Parametri target atlas
    uint32_t atlasTargetWidth = 1024;
    uint32_t atlasTargetHeight = 1024;
    uint32_t totalAtlasPages = 1;     // Pagine totali generate dall'AtlasPacker (BM-UV4.6)
    float paddingPixels = 4.0f;
    float globalTexelDensity = 0.0f;
    float atlasOccupancyRatio = 0.0f; // Percentuale texel utilizzati (0.0 .. 1.0)

    bool isValid = false;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;

    void Clear() {
        id = 0;
        assetId.clear();
        meshTopologyRevision = 0;
        meshAttributeRevision = 0;
        uvRevision = 1;
        atlasRevision = 1;
        strategyUsed = UVMappingStrategy::Auto;
        status = PrimitiveMappingStatus::SUCCESS;
        islands.clear();
        faceToIslandMap.clear();
        isValid = false;
        warnings.clear();
        errors.clear();
    }
};

// Generatore e lookup autoritativo cache maschera raster per isola UV (BM-PAINTER-PERF-02)
std::shared_ptr<const UVIslandRasterMask> GetOrCreateIslandRasterMask(
    const EditableMesh& mesh,
    const UVLayout& layout,
    UVIslandID islandId,
    uint32_t width,
    uint32_t height,
    bool flipV = false
);

// =============================================================================
// REPORT DIAGNOSTICO VALIDAZIONE UV (BM-UV2.10)
// =============================================================================

struct UVValidationReport {
    bool isValid = true;
    uint32_t totalCornersChecked = 0;
    uint32_t nanInfCount = 0;
    uint32_t degenerateUVTriangles = 0;
    uint32_t invertedUVTriangles = 0;
    uint32_t unmappedFaceCount = 0;

    float minU = 0.0f, maxU = 1.0f;
    float minV = 0.0f, maxV = 1.0f;
    float averageAreaDistortion = 0.0f;
    float maxAngleDistortionDegrees = 0.0f;

    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    void AddError(const std::string& err) {
        errors.push_back(err);
        isValid = false;
    }
    void AddWarning(const std::string& warn) {
        warnings.push_back(warn);
    }
    void Reset() {
        isValid = true;
        totalCornersChecked = 0;
        nanInfCount = 0;
        degenerateUVTriangles = 0;
        invertedUVTriangles = 0;
        unmappedFaceCount = 0;
        minU = minV = 0.0f;
        maxU = maxV = 1.0f;
        averageAreaDistortion = 0.0f;
        maxAngleDistortionDegrees = 0.0f;
        errors.clear();
        warnings.clear();
    }
};

// =============================================================================
// SERIALIZZAZIONE JSON VERSIONATA (BM-UV2.2)
// =============================================================================

inline void to_json(nlohmann::json& j, const IslandDistortion& d) {
    j = nlohmann::json{
        {"areaDistortion", d.areaDistortion},
        {"angleDistortion", d.angleDistortion},
        {"maxStretch", d.maxStretch},
        {"flippedTriangleCount", d.flippedTriangleCount},
        {"isAcceptable", d.isAcceptable}
    };
}

inline void from_json(const nlohmann::json& j, IslandDistortion& d) {
    d.areaDistortion = j.value("areaDistortion", 0.0f);
    d.angleDistortion = j.value("angleDistortion", 0.0f);
    d.maxStretch = j.value("maxStretch", 1.0f);
    d.flippedTriangleCount = j.value("flippedTriangleCount", (uint32_t)0);
    d.isAcceptable = j.value("isAcceptable", true);
}

inline void to_json(nlohmann::json& j, const UVIsland& island) {
    std::vector<uint64_t> packedFaces;
    packedFaces.reserve(island.faces.size());
    for (const auto& f : island.faces) packedFaces.push_back(f.ToPacked());

    j = nlohmann::json{
        {"id", island.id},
        {"semanticName", island.semanticName},
        {"semanticSurfaceId", island.semanticSurfaceId},
        {"faces", packedFaces},
        {"localUVBounds", {island.localUVBounds.x, island.localUVBounds.y, island.localUVBounds.z, island.localUVBounds.w}},
        {"atlasTransform", {island.atlasTransform.x, island.atlasTransform.y, island.atlasTransform.z, island.atlasTransform.w}},
        {"rotationDegrees", island.rotationDegrees},
        {"atlasPage", island.atlasPage},
        {"surfaceArea3D", island.surfaceArea3D},
        {"area2D", island.area2D},
        {"texelDensity", island.texelDensity},
        {"distortion", island.distortion}
    };
}

inline void from_json(const nlohmann::json& j, UVIsland& island) {
    island.id = j.value("id", (UVIslandID)0);
    island.semanticName = j.value("semanticName", "Island");
    island.semanticSurfaceId = j.value("semanticSurfaceId", (uint32_t)0);

    island.faces.clear();
    if (j.contains("faces") && j["faces"].is_array()) {
        for (const auto& item : j["faces"]) {
            island.faces.push_back(FaceID::FromPacked(item.get<uint64_t>()));
        }
    }

    if (j.contains("localUVBounds") && j["localUVBounds"].is_array() && j["localUVBounds"].size() >= 4) {
        island.localUVBounds = glm::vec4(
            j["localUVBounds"][0].get<float>(), j["localUVBounds"][1].get<float>(),
            j["localUVBounds"][2].get<float>(), j["localUVBounds"][3].get<float>());
    }

    if (j.contains("atlasTransform") && j["atlasTransform"].is_array() && j["atlasTransform"].size() >= 4) {
        island.atlasTransform = glm::vec4(
            j["atlasTransform"][0].get<float>(), j["atlasTransform"][1].get<float>(),
            j["atlasTransform"][2].get<float>(), j["atlasTransform"][3].get<float>());
    }

    island.rotationDegrees = j.value("rotationDegrees", 0.0f);
    island.atlasPage = j.value("atlasPage", (uint32_t)0);
    island.surfaceArea3D = j.value("surfaceArea3D", 0.0f);
    island.area2D = j.value("area2D", 0.0f);
    island.texelDensity = j.value("texelDensity", 0.0f);
    if (j.contains("distortion")) j["distortion"].get_to(island.distortion);
}

inline void to_json(nlohmann::json& j, const UVLayout& layout) {
    j = nlohmann::json{
        {"id", layout.id},
        {"assetId", layout.assetId},
        {"meshTopologyRevision", layout.meshTopologyRevision},
        {"meshAttributeRevision", layout.meshAttributeRevision},
        {"uvRevision", layout.uvRevision},
        {"atlasRevision", layout.atlasRevision},
        {"strategyUsed", static_cast<uint8_t>(layout.strategyUsed)},
        {"status", static_cast<uint8_t>(layout.status)},
        {"islands", layout.islands},
        {"atlasTargetWidth", layout.atlasTargetWidth},
        {"atlasTargetHeight", layout.atlasTargetHeight},
        {"totalAtlasPages", layout.totalAtlasPages},
        {"paddingPixels", layout.paddingPixels},
        {"globalTexelDensity", layout.globalTexelDensity},
        {"atlasOccupancyRatio", layout.atlasOccupancyRatio},
        {"isValid", layout.isValid},
        {"warnings", layout.warnings},
        {"errors", layout.errors}
    };
}

inline void from_json(const nlohmann::json& j, UVLayout& layout) {
    layout.id = j.value("id", (UVLayoutID)0);
    layout.assetId = j.value("assetId", "");
    layout.meshTopologyRevision = j.value("meshTopologyRevision", (uint64_t)0);
    layout.meshAttributeRevision = j.value("meshAttributeRevision", (uint64_t)0);
    layout.uvRevision = j.value("uvRevision", (uint64_t)1);
    layout.atlasRevision = j.value("atlasRevision", (uint64_t)1);
    layout.strategyUsed = static_cast<UVMappingStrategy>(j.value("strategyUsed", (uint8_t)0));
    layout.status = static_cast<PrimitiveMappingStatus>(j.value("status", (uint8_t)0));
    if (j.contains("islands") && j["islands"].is_array()) {
        layout.islands = j["islands"].get<std::vector<UVIsland>>();
    }
    layout.faceToIslandMap.clear();
    for (const auto& isl : layout.islands) {
        for (const auto& fId : isl.faces) {
            layout.faceToIslandMap[fId] = isl.id;
        }
    }
    layout.atlasTargetWidth = j.value("atlasTargetWidth", (uint32_t)1024);
    layout.atlasTargetHeight = j.value("atlasTargetHeight", (uint32_t)1024);
    layout.totalAtlasPages = j.value("totalAtlasPages", (uint32_t)1);
    layout.paddingPixels = j.value("paddingPixels", 4.0f);
    layout.globalTexelDensity = j.value("globalTexelDensity", 0.0f);
    layout.atlasOccupancyRatio = j.value("atlasOccupancyRatio", 0.0f);
    layout.isValid = j.value("isValid", false);
    if (j.contains("warnings")) layout.warnings = j["warnings"].get<std::vector<std::string>>();
    if (j.contains("errors")) layout.errors = j["errors"].get<std::vector<std::string>>();
}

} // namespace fw
