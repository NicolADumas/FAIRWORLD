#include "pch.h"
#include "FactoryOrchestrator.h"
#include "ProductionStationAdapters.h"
#include "blocks/BlockRegistry.h"
#include "blocks/MaterialRegistry.h"
#include "blocks/ShapeRegistry.h"
#include "blocks/sculpt/MeshBridgeService.h"
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
    draft.manifest.schemaVersion = 2; // BM-F4.2
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

void FactoryOrchestrator::FlushActiveDraft() {
    if (m_activeDraft.has_value()) {
        uint8_t bId = static_cast<uint8_t>(m_activeDraft->manifest.targetRuntimeVoxelId);
        m_draftMap[bId] = *m_activeDraft;
    }
}

bool FactoryOrchestrator::HasDraftForBlock(uint8_t blockId) const {
    return m_draftMap.find(blockId) != m_draftMap.end();
}

const RawAssetDraft* FactoryOrchestrator::GetDraftForBlock(uint8_t blockId) const {
    auto it = m_draftMap.find(blockId);
    return (it != m_draftMap.end()) ? &it->second : nullptr;
}

bool FactoryOrchestrator::LoadDraftFromRegistry(uint8_t blockId, BlockRegistry& bReg, MaterialRegistry& mReg, ShapeRegistry& sReg) {
    if (!bReg.IsBlockValid(blockId)) return false;

    // BM-P04: Salva il draft corrente prima di sostituirlo
    FlushActiveDraft();

    // Se esiste gia' un draft modificato in questa sessione Factory, ripristinalo direttamente
    auto it = m_draftMap.find(blockId);
    if (it != m_draftMap.end()) {
        m_activeDraft = it->second;
        m_activeStation = ProductionStationID::Shape;
        return true;
    }

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
        if (sDef->type == ShapeType::TriangleMesh) {
            draft.geometrySource = GeometrySource::SculptedMesh;
            draft.sculptMesh = MeshBridgeService::CreateMeshFromShape(draft.shape);
        } else {
            draft.geometrySource = GeometrySource::Primitive;
        }
    } else {
        draft.shape.type = ShapeType::Cube;
        draft.shape.supportsVisualCompilation = true;
        draft.geometrySource = GeometrySource::Primitive;
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
    draft.scientificMaterialId = bDef.scientificMaterialId;
    draft.scientificOverrides = bDef.scientificOverrides;
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
    m_draftMap[blockId] = draft;
    m_activeStation = ProductionStationID::Shape;

    // Valuta le stazioni iniziali per allineare lo stato
    ExecuteStation(ProductionStationID::Shape, &sReg, &mReg, &bReg);
    ExecuteStation(ProductionStationID::SurfaceUV, &sReg, &mReg, &bReg);
    ExecuteStation(ProductionStationID::Texture, &sReg, &mReg, &bReg);
    ExecuteStation(ProductionStationID::Painter, &sReg, &mReg, &bReg);
    ExecuteStation(ProductionStationID::Material, &sReg, &mReg, &bReg);
    ExecuteStation(ProductionStationID::RawPhysics, &sReg, &mReg, &bReg);

    if (m_activeDraft.has_value()) {
        m_draftMap[blockId] = *m_activeDraft;
    }
    return true;
}

void FactoryOrchestrator::CloseDraft() {
    FlushActiveDraft();
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

void FactoryOrchestrator::SetDraftSculptMesh(const EditableMesh& mesh) {
    if (m_activeDraft.has_value()) {
        m_activeDraft->sculptMesh = mesh;
        m_activeDraft->geometrySource = GeometrySource::SculptedMesh;
        m_activeDraft->MarkDirty();
        uint64_t fp = mesh.GetTopologyRevision() * 100003ULL + mesh.GetGeometryRevision();
        m_dependencyGraph.InvalidateNode(DependencyNodeID::SculptGeometry, fp);
    }
}

void FactoryOrchestrator::InvalidateGeometryCache() {
    m_compiledGeometryCache.Invalidate();
}

const EditableMesh* FactoryOrchestrator::GetDraftSculptMesh() const {
    if (m_activeDraft.has_value() && m_activeDraft->sculptMesh.has_value()) {
        return &(*m_activeDraft->sculptMesh);
    }
    return nullptr;
}

EditableMesh* FactoryOrchestrator::GetDraftSculptMeshMutable() {
    if (m_activeDraft.has_value() && m_activeDraft->sculptMesh.has_value()) {
        return &(*m_activeDraft->sculptMesh);
    }
    return nullptr;
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
            case ProductionStationID::Sculpt: matchedNode = DependencyNodeID::SculptGeometry; break;
            case ProductionStationID::SurfaceUV: matchedNode = DependencyNodeID::SurfaceLayout; break;
            case ProductionStationID::Texture: matchedNode = DependencyNodeID::TextureResources; break;
            case ProductionStationID::Painter: matchedNode = DependencyNodeID::PaintLayers; break;
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
        ProductionStationID::Sculpt,
        ProductionStationID::SurfaceUV,
        ProductionStationID::Texture,
        ProductionStationID::Painter,
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
    ExecuteStation(ProductionStationID::Texture, sReg, mReg, bReg);
    ExecuteStation(ProductionStationID::Painter, sReg, mReg, bReg);
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

TextureBakeResult FactoryOrchestrator::BakeCurrentDraftAlbedo(bool forceRewrite) {
    if (!m_activeDraft.has_value()) {
        TextureBakeResult res;
        res.success = false;
        res.errorCode = BakingErrorCode::InvalidSourceImage;
        res.errorMessage = "Nessun draft aperto.";
        return res;
    }

    auto* painterStation = dynamic_cast<PainterStationAdapter*>(GetStation(ProductionStationID::Painter));
    if (!painterStation) {
        TextureBakeResult res;
        res.success = false;
        res.errorCode = BakingErrorCode::InvalidSourceImage;
        res.errorMessage = "Stazione Painter non trovata.";
        return res;
    }

    return m_textureBakingService.BakeDraftAlbedo(*m_activeDraft, *painterStation, forceRewrite);
}

std::optional<RawAssetPackage> FactoryOrchestrator::BuildPackage(ShapeRegistry* sReg, MaterialRegistry* mReg, BlockRegistry* bReg) {
    if (!m_activeDraft.has_value()) return std::nullopt;

    auto valReport = RunQualityControlAudit(sReg, mReg, bReg);
    if (!valReport.isValid) {
        return std::nullopt;
    }

    // BM-F2B: Se Painter e' completata e il Draft ha layers ma albedoPath e' vuoto, effettua il bake automatico
    if ((m_activeDraft->completedStationsMask & (1u << static_cast<uint8_t>(ProductionStationID::Painter))) &&
        !m_activeDraft->paintLayers.empty() && m_activeDraft->albedoPath.empty()) {
        BakeCurrentDraftAlbedo(false);
    }

    bool packOk = ExecuteStation(ProductionStationID::Packaging, sReg, mReg, bReg);
    if (!packOk) return std::nullopt;

    RawAssetPackage package;
    package.manifest = m_activeDraft->manifest;
    package.manifest.schemaVersion = 2; // BM-F4.2
    package.manifest.stage = RawAssetStage::Packaged;
    package.validationReport = valReport;

    // Compila geometria finale
    GeometryCompileOptions opts;
    opts.shapeRegistry = sReg;
    opts.surfaceOverrides = &m_activeDraft->surfaces;
    opts.defaultUVTransform = &m_activeDraft->baseUVTransform;
    opts.defaultProjection = m_activeDraft->baseProjection;

    GeometryCompileResult compRes;
    bool isSculpt = (m_activeDraft->geometrySource == GeometrySource::SculptedMesh || m_activeDraft->sculptMesh.has_value());
    if (isSculpt) {
        if (!m_activeDraft->sculptMesh.has_value() || m_activeDraft->sculptMesh->IsEmpty() || m_activeDraft->sculptMesh->GetFaceCount() == 0) {
            // Fallimento esplicito: NESSUN fallback silenzioso a primitiva
            return std::nullopt;
        }

        bool canReuse = (m_compiledGeometryCache.source == GeometrySource::SculptedMesh &&
                         m_compiledGeometryCache.geomRev == m_activeDraft->sculptMesh->GetGeometryRevision() &&
                         m_compiledGeometryCache.topoRev == m_activeDraft->sculptMesh->GetTopologyRevision() &&
                         m_compiledGeometryCache.attrRev == m_activeDraft->sculptMesh->GetAttributeRevision() &&
                         m_compiledGeometryCache.uvRev == m_activeDraft->uvRevision &&
                         m_compiledGeometryCache.surfaceCount == m_activeDraft->surfaces.size() &&
                         m_compiledGeometryCache.defaultMatId == opts.defaultMaterialId &&
                         m_compiledGeometryCache.result.success);
        if (canReuse) {
            compRes = m_compiledGeometryCache.result;
        } else {
            m_geometryCompileCallCount++;
            compRes = MeshBridgeService::CompileSculptedMesh(*m_activeDraft->sculptMesh, opts, nullptr);
            if (!compRes.success) {
                return std::nullopt;
            }
            m_compiledGeometryCache.source = GeometrySource::SculptedMesh;
            m_compiledGeometryCache.geomRev = m_activeDraft->sculptMesh->GetGeometryRevision();
            m_compiledGeometryCache.topoRev = m_activeDraft->sculptMesh->GetTopologyRevision();
            m_compiledGeometryCache.attrRev = m_activeDraft->sculptMesh->GetAttributeRevision();
            m_compiledGeometryCache.uvRev = m_activeDraft->uvRevision;
            m_compiledGeometryCache.surfaceCount = m_activeDraft->surfaces.size();
            m_compiledGeometryCache.defaultMatId = opts.defaultMaterialId;
            m_compiledGeometryCache.result = compRes;
        }

        package.geometrySource = GeometrySource::SculptedMesh;
        package.sculptTopologyJson = m_activeDraft->sculptMesh->ToJson().dump();
        package.shape = MeshBridgeService::CreateShapeDefinitionFromMesh(*m_activeDraft->sculptMesh, compRes);
    } else {
        bool canReuse = (m_compiledGeometryCache.source == GeometrySource::Primitive &&
                         m_compiledGeometryCache.shapeRev == m_activeDraft->shape.revision &&
                         m_compiledGeometryCache.uvRev == m_activeDraft->uvRevision &&
                         m_compiledGeometryCache.surfaceCount == m_activeDraft->surfaces.size() &&
                         m_compiledGeometryCache.defaultMatId == opts.defaultMaterialId &&
                         m_compiledGeometryCache.result.success);
        if (canReuse) {
            compRes = m_compiledGeometryCache.result;
        } else {
            m_geometryCompileCallCount++;
            compRes = GeometryCompiler::Compile(m_activeDraft->shape, opts);
            if (!compRes.success) {
                return std::nullopt;
            }
            m_compiledGeometryCache.source = GeometrySource::Primitive;
            m_compiledGeometryCache.shapeRev = m_activeDraft->shape.revision;
            m_compiledGeometryCache.uvRev = m_activeDraft->uvRevision;
            m_compiledGeometryCache.surfaceCount = m_activeDraft->surfaces.size();
            m_compiledGeometryCache.defaultMatId = opts.defaultMaterialId;
            m_compiledGeometryCache.result = compRes;
        }

        package.geometrySource = GeometrySource::Primitive;
        package.sculptTopologyJson = "";
        package.shape = m_activeDraft->shape;
    }

    package.geometry.vertices = compRes.mesh.vertices;
    package.geometry.faces = compRes.mesh.faces;
    package.geometry.bounds = compRes.mesh.bounds();
    package.geometry.vertexCount = static_cast<uint32_t>(compRes.mesh.vertices.size());
    package.geometry.triangleCount = static_cast<uint32_t>(compRes.mesh.faces.size());

    // Proprietà fisiche congelate
    package.compiledPhysics = m_activeDraft->physics;

    // Parametri visivi compilati (BM-F2C)
    package.scientificMaterialId = m_activeDraft->scientificMaterialId;
    package.scientificOverrides = m_activeDraft->scientificOverrides;
    package.baseColorFallback = m_activeDraft->baseColorFallback;
    package.roughnessFallback = m_activeDraft->roughnessFallback;
    package.metallicFallback = m_activeDraft->metallicFallback;
    package.emissiveStrength = m_activeDraft->emissiveStrength;
    package.alphaFallback = m_activeDraft->alphaFallback;
    package.behaviors = m_activeDraft->behaviors;

    // Trasformazioni congelate
    package.packagePosition = m_activeDraft->localPosition;
    package.packageRotation = m_activeDraft->localRotation;
    package.packageScale = m_activeDraft->localScale;
    package.packagePivot = m_activeDraft->pivotOffset;

    // Texture URIs & Fingerprints (BM-F2B)
    package.albedoMapUri = m_activeDraft->albedoPath;
    package.normalMapUri = m_activeDraft->normalPath;
    package.ormMapUri = m_activeDraft->ormPath;

    auto* painterStation = dynamic_cast<const PainterStationAdapter*>(GetStation(ProductionStationID::Painter));
    if (painterStation && painterStation->HasCompositeAlbedo()) {
        package.paintFingerprint = painterStation->GetCompositeAlbedo().ComputeContentHash();
        package.textureFingerprint = package.paintFingerprint;
    }

    // Fingerprint sintetico deterministico
    package.packageFingerprint = m_activeDraft->manifest.revision * 1000003ULL + 
                                 static_cast<uint64_t>(package.geometry.vertexCount);
    if (isSculpt && m_activeDraft->sculptMesh.has_value()) {
        package.packageFingerprint = package.packageFingerprint * 1000003ULL +
                                     m_activeDraft->sculptMesh->GetGeometryRevision() * 1009ULL +
                                     m_activeDraft->sculptMesh->GetTopologyRevision();
    }

    // Checksum di distribuzione e digest crittografico SHA-256 (BM-F4 / BM-F4.1)
    package.distributionChecksum = package.ComputeDistributionChecksum();
    package.contentDigestSha256 = package.ComputeContentDigestSha256(m_packagingService.GetRootAssetDirectory());

    return package;
}

bool FactoryOrchestrator::PublishToRegistries(BlockRegistry& bReg, MaterialRegistry& mReg, ShapeRegistry& sReg) {
    if (!m_activeDraft.has_value()) return false;

    auto pkgOpt = BuildPackage(&sReg, &mReg, &bReg);
    if (!pkgOpt.has_value()) {
        return false;
    }

    auto publishRes = m_packagingService.PublishPackageTransactional(*pkgOpt, bReg, mReg, sReg);
    if (!publishRes.success) {
        return false;
    }

    m_activeDraft->manifest.stage = RawAssetStage::Published;
    m_activeDraft->manifest.targetRuntimeVoxelId = publishRes.targetVoxelId;
    m_activeDraft->isDirty = false;
    return true;
}

bool FactoryOrchestrator::PublishPersistentToRegistries(BlockRegistry& bReg, MaterialRegistry& mReg, ShapeRegistry& sReg) {
    if (!m_activeDraft.has_value()) return false;

    auto pkgOpt = BuildPackage(&sReg, &mReg, &bReg);
    if (!pkgOpt.has_value()) {
        return false;
    }

    auto publishRes = m_packagingService.PublishPackagePersistent(*pkgOpt, bReg, mReg, sReg);
    if (!publishRes.success) {
        return false;
    }

    m_activeDraft->manifest.stage = RawAssetStage::Published;
    m_activeDraft->manifest.targetRuntimeVoxelId = publishRes.targetVoxelId;
    m_activeDraft->isDirty = false;
    return true;
}

uint64_t FactoryOrchestrator::ComputeStationFingerprint(ProductionStationID id) const {
    if (!m_activeDraft.has_value()) return 0;
    // Calcola un hash leggero basato sui parametri correnti della stazione
    uint64_t rev = m_activeDraft->manifest.revision;
    if (id == ProductionStationID::Sculpt && m_activeDraft->sculptMesh.has_value()) {
        rev = rev * 1000003ULL + m_activeDraft->sculptMesh->GetTopologyRevision() * 1009ULL + m_activeDraft->sculptMesh->GetGeometryRevision();
    } else if (id == ProductionStationID::Painter) {
        rev = rev * 1000003ULL + m_activeDraft->paintRevision * 1009ULL + static_cast<uint64_t>(m_activeDraft->paintLayers.size());
    }
    return (rev * 2654435761ULL) ^ (static_cast<uint64_t>(id) << 32);
}

} // namespace fw
