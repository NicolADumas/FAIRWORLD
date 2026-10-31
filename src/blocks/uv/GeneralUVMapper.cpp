#include "pch.h"
#include "GeneralUVMapper.h"
#include <cmath>

namespace fw {

TopologySupportClassification GeneralUVMapper::ClassifyTopology(const EditableMesh& mesh, 
                                                                std::vector<std::string>& outIssues) {
    outIssues.clear();

    if (mesh.GetFaceCount() == 0 || mesh.GetVertexCount() == 0) {
        outIssues.push_back("Mesh priva di elementi (0 vertici o 0 facce).");
        return TopologySupportClassification::Unsupported;
    }

    auto degenReport = MeshRepairService::DetectDegeneracies(mesh);

    if (!degenReport.pinchedNonManifoldVertices.empty()) {
        outIssues.push_back("Rilevati vertici non-manifold a clessidra (pinched vertices: " + 
            std::to_string(degenReport.pinchedNonManifoldVertices.size()) + "). Richiede MeshRepairService.");
        return TopologySupportClassification::RequiresRepair;
    }

    if (!degenReport.zeroAreaFaces.empty() || !degenReport.duplicateVertexFaces.empty()) {
        outIssues.push_back("Rilevate facce degeneri (zero-area o vertici duplicati: " + 
            std::to_string(degenReport.zeroAreaFaces.size() + degenReport.duplicateVertexFaces.size()) + ").");
        return TopologySupportClassification::SupportedWithDiagnostic;
    }

    return TopologySupportClassification::Supported;
}

uint64_t GeneralUVMapper::ComputeSeamConfigFingerprint(const GeneralUVOptions& options) const {
    uint64_t h = 14695981039346656037ULL;
    auto hashVal = [&](uint64_t v) {
        h ^= v;
        h *= 1099511628211ULL;
    };

    hashVal(static_cast<uint64_t>(options.dispatchMode));
    hashVal(static_cast<uint64_t>(options.seamOptions.dihedralAngleThresholdDegrees * 100.0f));
    hashVal(options.seamOptions.seamOnSharpEdges ? 1 : 0);
    hashVal(options.seamOptions.seamOnSurfaceBoundary ? 1 : 0);
    hashVal(options.seamOptions.seamOnMaterialBoundary ? 1 : 0);
    hashVal(options.seamOptions.autoCutClosedComponents ? 1 : 0);
    hashVal(static_cast<uint64_t>(options.lscmOptions.tolerance * 1000000.0f));
    hashVal(options.lscmOptions.fallbackToPlanarOnFailure ? 1 : 0);
    return h;
}

GeneralUVResult GeneralUVMapper::UnwrapMesh(EditableMesh& mesh, const GeneralUVOptions& options) {
    GeneralUVResult result;
    result.modeUsed = UVUnwrapDispatchMode::GeneralUnwrapping;

    // 1. Controllo Zero-Work Revision Driven (BM-UV3.8)
    uint64_t currentFp = ComputeSeamConfigFingerprint(options);
    if (options.enableZeroWorkCache && m_cache.isValid) {
        if (m_cache.lastEvaluatedMeshAddress == &mesh &&
            m_cache.lastEvaluatedTopologyRevision == mesh.GetTopologyRevision() &&
            m_cache.lastEvaluatedGeometryRevision == mesh.GetGeometryRevision() &&
            m_cache.lastEvaluatedAttributeRevision == mesh.GetAttributeRevision() &&
            m_cache.seamConfigFingerprint == currentFp) {
            result.layout = m_cache.cachedLayout;
            result.zeroWorkCacheHit = true;
            result.success = true;
            result.diagnosticMessage = "Zero-Work cache hit: mesh e configurazione inalterate.";
            return result;
        }
    }

    // 2. Classificazione della robustezza topologica (BM-UV3.6)
    std::vector<std::string> issues;
    result.topologyClassification = ClassifyTopology(mesh, issues);
    if (result.topologyClassification == TopologySupportClassification::Unsupported) {
        result.success = false;
        result.diagnosticMessage = "Topologia non supportata: " + (issues.empty() ? "" : issues[0]);
        return result;
    }

    if (result.topologyClassification == TopologySupportClassification::RequiresRepair && options.autoRepairDegeneracies) {
        MeshRepairService::RemoveDegenerateFaces(mesh);
    }

    // 3. Costruzione del SeamGraph topologico autoritativo (BM-UV3.2)
    SeamGraph seamGraph;
    if (!seamGraph.BuildFromMesh(mesh, options.seamOptions)) {
        result.success = false;
        result.diagnosticMessage = "Costruzione del SeamGraph fallita.";
        return result;
    }
    seamGraph.ApplySeamsToMesh(mesh);

    // 4. Estrazione delle UVIsland tramite attraversamento topologico (BM-UV3.3)
    IslandExtractionReport extractReport;
    UVLayout layout;
    layout.assetId = "MeshAsset_GeneralUnwrap";
    layout.islands = UVIslandExtractor::ExtractIslands(mesh, seamGraph, layout.faceToIslandMap, extractReport);

    if (layout.islands.empty()) {
        result.success = false;
        result.diagnosticMessage = "Nessuna isola UV estratta dalla topologia.";
        return result;
    }

    // 5. Parametrizzazione conforme per ciascuna UVIsland (LSCM / Planar Fallback) (BM-UV3.4)
    for (auto& island : layout.islands) {
        auto lscmRes = LSCMParameterizer::ParameterizeIsland(mesh, island, options.lscmOptions);
        island.distortion = lscmRes.distortion;
    }

    // 6. Aggiornamento delle revisioni e validazione finale (BM-UV3.7 / BM-UV3.8)
    mesh.IncrementAttributeRevision();
    layout.meshTopologyRevision = mesh.GetTopologyRevision();
    layout.meshAttributeRevision = mesh.GetAttributeRevision();
    layout.uvRevision = m_cache.cachedLayout.uvRevision + 1;
    layout.strategyUsed = UVMappingStrategy::LSCM;
    layout.isValid = true;

    PrimitiveUVMapper::ValidateGeneratedUV(mesh, layout, result.validation);

    // 7. Aggiornamento della cache Zero-Work
    m_cache.lastEvaluatedMeshAddress = &mesh;
    m_cache.lastEvaluatedTopologyRevision = mesh.GetTopologyRevision();
    m_cache.lastEvaluatedGeometryRevision = mesh.GetGeometryRevision();
    m_cache.lastEvaluatedAttributeRevision = mesh.GetAttributeRevision();
    m_cache.seamConfigFingerprint = currentFp;
    m_cache.cachedLayout = layout;
    m_cache.isValid = true;

    result.layout = layout;
    result.success = result.validation.isValid;
    result.diagnosticMessage = result.success ? "Unwrapping generale LSCM completato con successo." 
                                             : "Unwrapping completato con avvisi o distorsioni.";
    return result;
}

GeneralUVResult GeneralUVMapper::UnwrapWithAutoDispatch(EditableMesh& mesh, 
                                                       ShapeType originalType, 
                                                       const ShapeParameters& params, 
                                                       bool isMeshSculptedOrModified, 
                                                       const GeneralUVOptions& options) {
    if (options.dispatchMode == UVUnwrapDispatchMode::PreserveExisting) {
        GeneralUVResult res;
        res.modeUsed = UVUnwrapDispatchMode::PreserveExisting;
        res.layout.id = 1;
        res.layout.strategyUsed = UVMappingStrategy::Auto;
        res.layout.meshTopologyRevision = mesh.GetTopologyRevision();
        res.layout.meshAttributeRevision = mesh.GetAttributeRevision();
        res.layout.uvRevision = 1;
        res.layout.atlasRevision = 1;

        UVIsland island;
        island.id = 0;
        island.semanticName = "PreservedUVIsland";
        for (FaceID fId : mesh.GetActiveFaceIDs()) {
            island.faces.push_back(fId);
            res.layout.faceToIslandMap[fId] = 0;
        }
        res.layout.islands.push_back(island);
        res.layout.isValid = true;
        res.success = true;
        res.diagnosticMessage = "Coordinate UV esistenti preservate integralmente.";
        return res;
    }

    // Dispatching intelligente (BM-UV3.9)
    if (options.dispatchMode == UVUnwrapDispatchMode::AnalyticMapping || 
        (options.dispatchMode == UVUnwrapDispatchMode::Auto && !isMeshSculptedOrModified && 
         originalType != ShapeType::None && 
         originalType != ShapeType::Heightfield && 
         originalType != ShapeType::SignedDistanceField && 
         originalType != ShapeType::Compound &&
         originalType != ShapeType::TriangleMesh)) {

        PrimitiveUVOptions primOpt;
        primOpt.normalizeTexelDensity = true;
        primOpt.markSeamEdges = true;
        primOpt.preventSeamWrapStretch = true;

        auto primRes = m_primitiveMapper.AnalyzeAndMap(mesh, originalType, params, primOpt);
        
        GeneralUVResult res;
        res.modeUsed = UVUnwrapDispatchMode::AnalyticMapping;
        res.success = primRes.success;
        res.layout = primRes.layout;
        res.validation = primRes.validation;
        res.diagnosticMessage = primRes.diagnosticMessage;
        return res;
    }

    // Per mesh scolpite, estruse, tagliate o arbitrarie -> Solver generale LSCM
    return UnwrapMesh(mesh, options);
}

} // namespace fw
