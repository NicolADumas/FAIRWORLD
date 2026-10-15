#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <glm/glm.hpp>
#include "blocks/sculpt/EditableMesh.h"
#include "blocks/factory/RawAssetContract.h"
#include "world/GeometryCompiler.h"

namespace fw {

class FactoryOrchestrator;
class AssetDependencyGraph;

// =============================================================================
// MESH BRIDGE SERVICE (BM-S9)
// Bridge di connessione autoritativo tra EditableMesh (Sculpt & Modeling)
// e la catena produttiva di Block Maker (GeometryCompiler, Factory, GPU Preview).
// =============================================================================

struct BridgePreviewCache {
    uint64_t lastEvaluatedTopologyRevision = 0;
    uint64_t lastEvaluatedGeometryRevision = 0;
    uint64_t lastEvaluatedAttributeRevision = 0;
    uint32_t lastMaterialId = 0;
    bool isGpuBufferDirty = true;
    MeshComponent cachedMesh;

    void Reset() {
        lastEvaluatedTopologyRevision = 0;
        lastEvaluatedGeometryRevision = 0;
        lastEvaluatedAttributeRevision = 0;
        lastMaterialId = 0;
        isGpuBufferDirty = true;
        cachedMesh = MeshComponent{};
    }
};

struct BridgeTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class MeshBridgeService {
public:
    // --- Conversione e Inizializzazione Shape -> Sculpt ---
    static EditableMesh CreateMeshFromShape(const ShapeDefinition& shapeDef, std::string* outError = nullptr);
    static EditableMesh CreateMeshFromCompiledTopology(const CompiledTopology& topo, std::string* outError = nullptr);

    // --- Sincronizzazione Bidirezionale con RawAssetDraft ---
    static bool SynchronizeDraftToEditableMesh(const RawAssetDraft& draft, EditableMesh& outMesh);
    static bool SynchronizeEditableMeshToDraft(const EditableMesh& mesh, RawAssetDraft& draft, FactoryOrchestrator* orchestrator = nullptr);

    // --- Zero-Work & Preview Compilation ---
    static bool NeedsPreviewRecompile(const EditableMesh& mesh, uint32_t materialId, const BridgePreviewCache& cache);
    static GeometryCompileResult CompileForPreview(const EditableMesh& mesh, const GeometryCompileOptions& options, BridgePreviewCache& cache);

    // --- Authoritative Sculpted Mesh Runtime Bridge (BM-F3) ---
    static GeometryCompileResult CompileSculptedMesh(const EditableMesh& mesh, const GeometryCompileOptions& options, BridgePreviewCache* cache = nullptr);
    static ShapeDefinition CreateShapeDefinitionFromMesh(const EditableMesh& mesh, const GeometryCompileResult& compiled);
    static bool ReconstructMeshFromPackage(const RawAssetPackage& package, EditableMesh& outMesh, std::string* outError = nullptr);

    // --- Validazione e Integration Contract Suite (BRG-1 .. BRG-10) ---
    static std::vector<BridgeTestResult> RunContractTests();
};

} // namespace fw
