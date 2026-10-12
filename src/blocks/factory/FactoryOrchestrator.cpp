#include "pch.h"
#include "FactoryOrchestrator.h"
#include "ProductionStationAdapters.h"
#include "blocks/BlockRegistry.h"
#include "blocks/MaterialRegistry.h"
#include "blocks/ShapeRegistry.h"
#include "world/GeometryCompiler.h"
#include <fstream>
#include <iostream>

namespace fw {

FactoryOrchestrator::FactoryOrchestrator() {
    InitializeStations();
}

void FactoryOrchestrator::InitializeStations() {
    m_stations.clear();
    m_stations[ProductionStationID::Shape] = std::make_unique<ShapeStationAdapter>();
    m_stations[ProductionStationID::Sculpt] = std::make_unique<SculptStationAdapter>();
    m_stations[ProductionStationID::SurfaceUV] = std::make_unique<SurfaceUVStationAdapter>();
    m_stations[ProductionStationID::Texture] = std::make_unique<TextureStationAdapter>();
    m_stations[ProductionStationID::Painter] = std::make_unique<PainterStationAdapter>();
    m_stations[ProductionStationID::Material] = std::make_unique<MaterialStationAdapter>();
    m_stations[ProductionStationID::RawPhysics] = std::make_unique<RawPhysicsStationAdapter>();
    m_stations[ProductionStationID::QualityControl] = std::make_unique<QualityControlStationAdapter>();
    m_stations[ProductionStationID::Packaging] = std::make_unique<PackagingStationAdapter>();
}

bool FactoryOrchestrator::OpenDraft(const RawAssetID& id, const std::string& displayName) {
    RawAssetDraft draft;
    draft.manifest.assetId = id;
    draft.manifest.displayName = displayName;
    draft.manifest.revision = 1;
    draft.manifest.stage = RawAssetStage::Draft;

    uint64_t nowSec = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
    draft.manifest.creationTimestamp = nowSec;
    draft.manifest.lastModifiedTimestamp = nowSec;

    // Configura forma iniziale a cubo di default
    draft.shape.type = ShapeType::Cube;
    draft.shape.supportsVisualCompilation = true;

    m_activeDraft = draft;
    m_activeStation = ProductionStationID::Shape;

    // Resetta lo stato delle stazioni
    for (auto& pair : m_stations) {
        pair.second->Reset();
    }
    m_dependencyGraph.InitializeStandardGraph();
    return true;
}

bool FactoryOrchestrator::LoadDraftFromRegistry(uint8_t blockId, BlockRegistry& bReg, MaterialRegistry& mReg, ShapeRegistry& sReg) {
    if (!bReg.IsBlockValid(blockId)) return false;

    const auto& bDef = bReg.GetBlock(blockId);
    const auto& mDef = mReg.GetMaterial(blockId);
    const auto* sDef = sReg.GetShapeDef(mDef.sharedShape);

    RawAssetDraft draft;
    draft.manifest.assetId = bDef.stringId;
    draft.manifest.displayName = bDef.displayName;
    draft.manifest.targetRuntimeVoxelId = blockId;
    draft.manifest.revision = mDef.revision > 0 ? mDef.revision : 1;
    draft.manifest.stage = RawAssetStage::Draft;

    // Geometria
    if (sDef) {
        draft.shape = *sDef;
    } else {
        draft.shape.type = ShapeType::Cube;
        draft.shape.supportsVisualCompilation = true;
    }

    // Trasformazioni
    draft.localPosition = bDef.localPosition;
    draft.localRotation = bDef.localRotation;
    draft.localScale = bDef.localScale;
    draft.pivotOffset = bDef.pivotOffset;

    // Superfici & UV
    draft.baseUVTransform = mDef.baseUVTransform;
    draft.baseProjection = mDef.baseProjection;
    draft.surfaces = mDef.surfaces;

    // Grafica & PBR
    draft.baseColorFallback = mDef.baseColorFallback;
    draft.roughnessFallback = mDef.roughnessFallback;
    draft.metallicFallback = mDef.metallicFallback;
    draft.emissiveStrength = mDef.emissiveStrength;
    draft.alphaFallback = mDef.alphaFallback;
    draft.albedoPath = mDef.albedoPath;
    draft.normalPath = mDef.normalPath;
    draft.ormPath = mDef.ormPath;
    draft.behaviors = mDef.behaviors;

    // Fisica
    draft.physics = bDef.physics;
    draft.thermalResistance = bDef.thermal_resistance;
    draft.thermalCapacity = bDef.thermal_capacity;
    draft.lightEmission = bDef.lightEmissionLevel;

    draft.isDirty = false;
    draft.completedStationsMask = 0;

    m_activeDraft = draft;
    m_activeStation = ProductionStationID::Shape;

    // Valuta le stazioni iniziali per allineare lo stato
    ExecuteStation(ProductionStationID::Shape, &sReg, &mReg, &bReg);
    ExecuteStation(ProductionStationID::SurfaceUV, &sReg, &mReg, &bReg);
    ExecuteStation(ProductionStationID::Texture, &sReg, &mReg, &bReg);
    ExecuteStation(ProductionStationID::Material, &sReg, &mReg, &bReg);
    ExecuteStation(ProductionStationID::RawPhysics, &sReg, &mReg, &bReg);
    return true;
}

void FactoryOrchestrator::CloseDraft() {
    m_activeDraft.reset();
    for (auto& pair : m_stations) {
        pair.second->Reset();
    }
}

bool FactoryOrchestrator::SaveDraftToFile(const std::string& filepath) const {
    if (!m_activeDraft.has_value()) return false;
    nlohmann::json j;
    to_json(j, *m_activeDraft);
    std::ofstream file(filepath);
    if (!file.is_open()) return false;
    file << j.dump(4);
    return true;
}

bool FactoryOrchestrator::LoadDraftFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;
    nlohmann::json j;
    try {
        file >> j;
    } catch (...) {
        return false;
    }
    RawAssetDraft draft;
    from_json(j, draft);
    m_activeDraft = draft;
    m_activeStation = ProductionStationID::Shape;
    return true;
}

bool FactoryOrchestrator::SetActiveStation(ProductionStationID id) {
    m_activeStation = id;
    return true;
}

IProductionStation* FactoryOrchestrator::GetStation(ProductionStationID id) {
    auto it = m_stations.find(id);
    return (it != m_stations.end()) ? it->second.get() : nullptr;
}

const IProductionStation* FactoryOrchestrator::GetStation(ProductionStationID id) const {
    auto it = m_stations.find(id);
    return (it != m_stations.end()) ? it->second.get() : nullptr;
}

bool FactoryOrchestrator::ExecuteStation(ProductionStationID id, ShapeRegistry* sReg, MaterialRegistry* mReg, BlockRegistry* bReg) {
    if (!m_activeDraft.has_value()) return false;

    IProductionStation* station = GetStation(id);
    if (!station || !station->IsImplemented()) return false;

    StationContext ctx{ *m_activeDraft, sReg, mReg, bReg, nullptr };
    if (!station->CanExecute(ctx)) return false;

    bool ok = station->Execute(ctx);
    if (ok) {
        // Allinea il grafo di dipendenze
        DependencyNodeID matchedNode = DependencyNodeID::ShapeGeometry;
        switch (id) {
            case ProductionStationID::Shape: matchedNode = DependencyNodeID::ShapeGeometry; break;
            case ProductionStationID::SurfaceUV: matchedNode = DependencyNodeID::SurfaceLayout; break;
            case ProductionStationID::Texture: matchedNode = DependencyNodeID::TextureResources; break;
            case ProductionStationID::Material: matchedNode = DependencyNodeID::MaterialParameters; break;
            case ProductionStationID::RawPhysics: matchedNode = DependencyNodeID::RawPhysicsProxy; break;
            case ProductionStationID::QualityControl: matchedNode = DependencyNodeID::QualityControlAudit; break;
            case ProductionStationID::Packaging: matchedNode = DependencyNodeID::PackageArtifact; break;
            default: break;
        }
        m_dependencyGraph.MarkNodeClean(matchedNode, ComputeStationFingerprint(id));
    }
    return ok;
}

bool FactoryOrchestrator::ExecuteActiveStation(ShapeRegistry* sReg, MaterialRegistry* mReg, BlockRegistry* bReg) {
    return ExecuteStation(m_activeStation, sReg, mReg, bReg);
}

bool FactoryOrchestrator::ExecutePipelineUpTo(ProductionStationID targetStation, ShapeRegistry* sReg, MaterialRegistry* mReg, BlockRegistry* bReg) {
    if (!m_activeDraft.has_value()) return false;

    // Ordine di valutazione delle stazioni
    std::vector<ProductionStationID> sequence = {
        ProductionStationID::Shape,
        ProductionStationID::SurfaceUV,
        ProductionStationID::Texture,
        ProductionStationID::Material,
        ProductionStationID::RawPhysics,
        ProductionStationID::QualityControl,
        ProductionStationID::Packaging
    };

    for (auto stID : sequence) {
        if (!ExecuteStation(stID, sReg, mReg, bReg)) {
            // Se fallisce una stazione obbligatoria prima o coincidente con il target, stop
            if (stID == targetStation) return false;
        }
        if (stID == targetStation) break;
    }
    return true;
}

void FactoryOrchestrator::NotifyPropertyModified(DependencyNodeID nodeId, uint64_t newFingerprint) {
    if (!m_activeDraft.has_value()) return;

    bool invalidated = m_dependencyGraph.InvalidateNode(nodeId, newFingerprint);
    if (invalidated) {
        m_activeDraft->MarkDirty();
    }
}

RawAssetValidation FactoryOrchestrator::RunQualityControlAudit(ShapeRegistry* sReg, MaterialRegistry* mReg, BlockRegistry* bReg) {
    RawAssetValidation report;
    report.isValid = false;

    if (!m_activeDraft.has_value()) {
        report.AddError("Nessun RawAssetDraft attivo da validare.");
        return report;
    }

    // Assicurati che le stazioni a monte siano eseguite
    ExecuteStation(ProductionStationID::Shape, sReg, mReg, bReg);
    ExecuteStation(ProductionStationID::SurfaceUV, sReg, mReg, bReg);
    ExecuteStation(ProductionStationID::Material, sReg, mReg, bReg);
    ExecuteStation(ProductionStationID::RawPhysics, sReg, mReg, bReg);

    bool qcOk = ExecuteStation(ProductionStationID::QualityControl, sReg, mReg, bReg);
    const auto* qcStation = GetStation(ProductionStationID::QualityControl);
    if (qcStation) {
        for (const auto& err : qcStation->GetDiagnostic().errors) {
            report.AddError(err);
        }
        for (const auto& warn : qcStation->GetDiagnostic().warnings) {
            report.AddWarning(warn);
        }
    }
    report.isValid = qcOk && report.errors.empty();
    return report;
}

std::optional<RawAssetPackage> FactoryOrchestrator::BuildPackage(ShapeRegistry* sReg, MaterialRegistry* mReg, BlockRegistry* bReg) {
    if (!m_activeDraft.has_value()) return std::nullopt;

    auto valReport = RunQualityControlAudit(sReg, mReg, bReg);
    if (!valReport.isValid) {
        return std::nullopt;
    }

    bool packOk = ExecuteStation(ProductionStationID::Packaging, sReg, mReg, bReg);
    if (!packOk) return std::nullopt;

    RawAssetPackage package;
    package.manifest = m_activeDraft->manifest;
    package.manifest.stage = RawAssetStage::Packaged;
    package.validationReport = valReport;

    // Compila geometria finale
    GeometryCompileOptions opts;
    opts.shapeRegistry = sReg;
    opts.surfaceOverrides = &m_activeDraft->surfaces;
    opts.defaultUVTransform = &m_activeDraft->baseUVTransform;
    opts.defaultProjection = m_activeDraft->baseProjection;

    auto compRes = GeometryCompiler::Compile(m_activeDraft->shape, opts);
    if (!compRes.success) {
        return std::nullopt;
    }

    package.geometry.vertices = compRes.mesh.vertices;
    package.geometry.faces = compRes.mesh.faces;
    package.geometry.bounds = compRes.mesh.bounds();
    package.geometry.vertexCount = static_cast<uint32_t>(compRes.mesh.vertices.size());
    package.geometry.triangleCount = static_cast<uint32_t>(compRes.mesh.faces.size());

    // Proprietà fisiche congelate
    package.compiledPhysics = m_activeDraft->physics;

    // Trasformazioni congelate
    package.packagePosition = m_activeDraft->localPosition;
    package.packageRotation = m_activeDraft->localRotation;
    package.packageScale = m_activeDraft->localScale;
    package.packagePivot = m_activeDraft->pivotOffset;

    // Texture URIs
    package.albedoMapUri = m_activeDraft->albedoPath;
    package.normalMapUri = m_activeDraft->normalPath;
    package.ormMapUri = m_activeDraft->ormPath;

    // Fingerprint sintetico deterministico
    package.packageFingerprint = m_activeDraft->manifest.revision * 1000003ULL + 
                                 static_cast<uint64_t>(package.geometry.vertexCount);

    return package;
}

bool FactoryOrchestrator::PublishToRegistries(BlockRegistry& bReg, MaterialRegistry& mReg, ShapeRegistry& sReg) {
    if (!m_activeDraft.has_value()) return false;

    auto pkgOpt = BuildPackage(&sReg, &mReg, &bReg);
    if (!pkgOpt.has_value() || !pkgOpt->IsPublishable()) {
        return false;
    }

    const auto& pkg = *pkgOpt;
    uint8_t targetVoxelId = pkg.manifest.targetRuntimeVoxelId;

    // Se l'asset non ha ancora uno slot assegnato, allochiamone uno
    if (targetVoxelId == 0 || !bReg.IsBlockValid(targetVoxelId)) {
        auto createRes = bReg.CreateBlock(pkg.manifest.assetId, pkg.manifest.displayName);
        if (!createRes.success) {
            return false;
        }
        targetVoxelId = createRes.allocatedId;
        m_activeDraft->manifest.targetRuntimeVoxelId = targetVoxelId;
    }

    // 1. Registrazione Forma in ShapeRegistry
    ShapeHandle assignedShape = sReg.RegisterShape(m_activeDraft->shape);

    // 2. Sincronizzazione SimBlockDef in BlockRegistry
    SimBlockDef& bDef = bReg.GetBlockMutable(targetVoxelId);
    bDef.stringId = pkg.manifest.assetId;
    bDef.displayName = pkg.manifest.displayName;
    bDef.physics = pkg.compiledPhysics;
    bDef.localPosition = pkg.packagePosition;
    bDef.localRotation = pkg.packageRotation;
    bDef.localScale = pkg.packageScale;
    bDef.pivotOffset = pkg.packagePivot;
    bDef.thermal_resistance = m_activeDraft->thermalResistance;
    bDef.thermal_capacity = m_activeDraft->thermalCapacity;
    bDef.lightEmissionLevel = m_activeDraft->lightEmission;
    bDef.SyncLegacyPhysicsFields();

    // 3. Sincronizzazione PBRMaterialDef in MaterialRegistry
    PBRMaterialDef& matDef = mReg.GetMaterialMutable(targetVoxelId);
    matDef.target_block_id = targetVoxelId;
    matDef.sharedShape = assignedShape;
    matDef.baseColorFallback = m_activeDraft->baseColorFallback;
    matDef.roughnessFallback = m_activeDraft->roughnessFallback;
    matDef.metallicFallback = m_activeDraft->metallicFallback;
    matDef.emissiveStrength = m_activeDraft->emissiveStrength;
    matDef.alphaFallback = m_activeDraft->alphaFallback;
    matDef.albedoPath = pkg.albedoMapUri;
    matDef.normalPath = pkg.normalMapUri;
    matDef.ormPath = pkg.ormMapUri;
    matDef.behaviors = m_activeDraft->behaviors;
    matDef.baseUVTransform = m_activeDraft->baseUVTransform;
    matDef.baseProjection = m_activeDraft->baseProjection;
    matDef.surfaces = m_activeDraft->surfaces;
    matDef.revision++;

    m_activeDraft->manifest.stage = RawAssetStage::Published;
    m_activeDraft->isDirty = false;
    return true;
}

uint64_t FactoryOrchestrator::ComputeStationFingerprint(ProductionStationID id) const {
    if (!m_activeDraft.has_value()) return 0;
    // Calcola un hash leggero basato sui parametri correnti della stazione
    uint64_t rev = m_activeDraft->manifest.revision;
    return (rev * 2654435761ULL) ^ (static_cast<uint64_t>(id) << 32);
}

} // namespace fw
