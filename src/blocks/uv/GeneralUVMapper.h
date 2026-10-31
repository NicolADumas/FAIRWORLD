#pragma once

#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <unordered_map>
#include <glm/glm.hpp>
#include "blocks/sculpt/EditableMesh.h"
#include "blocks/sculpt/MeshRepairService.h"
#include "blocks/ShapeRegistry.h"
#include "blocks/uv/UVLayout.h"
#include "blocks/uv/PrimitiveUVMapper.h"
#include "blocks/uv/SeamGraph.h"
#include "blocks/uv/UVIslandExtractor.h"
#include "blocks/uv/LSCMParameterizer.h"

namespace fw {

// =============================================================================
// MODALITA DI DISPATCHING UNWRAPPING (BM-UV3.9)
// =============================================================================

enum class UVUnwrapDispatchMode : uint8_t {
    Auto = 0,
    AnalyticMapping,
    GeneralUnwrapping,
    PreserveExisting,
    RequiresRepair,
    Unsupported
};

inline const char* GetUVUnwrapDispatchModeName(UVUnwrapDispatchMode m) {
    switch (m) {
        case UVUnwrapDispatchMode::Auto:              return "Auto";
        case UVUnwrapDispatchMode::AnalyticMapping:   return "AnalyticMapping";
        case UVUnwrapDispatchMode::GeneralUnwrapping: return "GeneralUnwrapping";
        case UVUnwrapDispatchMode::PreserveExisting:  return "PreserveExisting";
        case UVUnwrapDispatchMode::RequiresRepair:    return "RequiresRepair";
        case UVUnwrapDispatchMode::Unsupported:       return "Unsupported";
        default:                                      return "Unknown";
    }
}

// =============================================================================
// CLASSIFICAZIONE DI SUPPORTO TOPOLOGICO (BM-UV3.6)
// =============================================================================

enum class TopologySupportClassification : uint8_t {
    Supported = 0,
    SupportedWithDiagnostic,
    RequiresRepair,
    Unsupported
};

inline const char* GetTopologySupportClassificationName(TopologySupportClassification c) {
    switch (c) {
        case TopologySupportClassification::Supported:               return "Supported";
        case TopologySupportClassification::SupportedWithDiagnostic: return "SupportedWithDiagnostic";
        case TopologySupportClassification::RequiresRepair:          return "RequiresRepair";
        case TopologySupportClassification::Unsupported:             return "Unsupported";
        default:                                                     return "Unknown";
    }
}

// =============================================================================
// OPZIONI GENERALI DI UNWRAPPING (BM-UV3)
// =============================================================================

struct GeneralUVOptions {
    UVUnwrapDispatchMode dispatchMode = UVUnwrapDispatchMode::Auto;
    SeamOptions seamOptions;
    LSCMOptions lscmOptions;
    bool enableZeroWorkCache = true;
    bool autoRepairDegeneracies = false; // Se true, esegue riparazione non invasiva su facce degeneri
    uint32_t atlasTargetWidth = 1024;
    uint32_t atlasTargetHeight = 1024;
    float paddingPixels = 4.0f;
};

// =============================================================================
// ESITO GENERALE DI UNWRAPPING (BM-UV3)
// =============================================================================

struct GeneralUVResult {
    bool success = false;
    UVUnwrapDispatchMode modeUsed = UVUnwrapDispatchMode::GeneralUnwrapping;
    TopologySupportClassification topologyClassification = TopologySupportClassification::Supported;
    UVLayout layout;
    UVValidationReport validation;
    bool zeroWorkCacheHit = false;
    std::string diagnosticMessage;
};

// =============================================================================
// CACHE ZERO-WORK PER UNWRAPPING UV (BM-UV3.8)
// =============================================================================

struct UVZeroWorkCache {
    const void* lastEvaluatedMeshAddress = nullptr;
    uint64_t lastEvaluatedTopologyRevision = 0;
    uint64_t lastEvaluatedGeometryRevision = 0;
    uint64_t lastEvaluatedAttributeRevision = 0;
    uint64_t seamConfigFingerprint = 0;
    UVLayout cachedLayout;
    bool isValid = false;
};

// =============================================================================
// GENERAL UV MAPPER (BM-UV3)
// Servizio autoritativo headless per l'unwrapping di qualsiasi EditableMesh
// =============================================================================

class GeneralUVMapper {
public:
    GeneralUVMapper() = default;
    ~GeneralUVMapper() = default;

    // --- Master Entry Point per EditableMesh generica ---
    GeneralUVResult UnwrapMesh(EditableMesh& mesh, 
                               const GeneralUVOptions& options = {});

    // --- Master Dispatcher Fabbrica (con ShapeType e opzioni) ---
    GeneralUVResult UnwrapWithAutoDispatch(EditableMesh& mesh, 
                                           ShapeType originalType, 
                                           const ShapeParameters& params, 
                                           bool isMeshSculptedOrModified, 
                                           const GeneralUVOptions& options = {});

    // --- Analisi di Robustezza Topologica ---
    static TopologySupportClassification ClassifyTopology(const EditableMesh& mesh, 
                                                          std::vector<std::string>& outIssues);

    // --- Reset Cache Zero-Work ---
    void InvalidateCache() { m_cache.isValid = false; }

private:
    uint64_t ComputeSeamConfigFingerprint(const GeneralUVOptions& options) const;

private:
    UVZeroWorkCache m_cache;
    PrimitiveUVMapper m_primitiveMapper;
};

} // namespace fw
