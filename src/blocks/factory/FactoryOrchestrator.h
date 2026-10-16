#pragma once

#include <memory>
#include <unordered_map>
#include <optional>
#include <string>
#include <vector>
#include "RawAssetContract.h"
#include "ProductionStationContract.h"
#include "AssetDependencyGraph.h"
#include "TextureBakingService.h"
#include "AssetPackagingService.h"

namespace fw {

class ShapeRegistry;
class MaterialRegistry;
class BlockRegistry;

// =============================================================================
// FACTORY ORCHESTRATOR (BM-FC5)
// Servizio applicativo disaccoppiato da BlockMakerState per la fabbricazione
// e il packaging modulare degli asset grezzi.
// =============================================================================
class FactoryOrchestrator {
public:
    FactoryOrchestrator();
    ~FactoryOrchestrator() = default;

    // --- Lifecycle del Draft ---
    bool OpenDraft(const RawAssetID& id, const std::string& displayName = "New Raw Asset");
    bool LoadDraftFromRegistry(uint8_t blockId, BlockRegistry& bReg, MaterialRegistry& mReg, ShapeRegistry& sReg);
    void CloseDraft();

    bool IsDraftOpen() const { return m_activeDraft.has_value(); }
    RawAssetDraft* GetCurrentDraft() { return m_activeDraft.has_value() ? &(*m_activeDraft) : nullptr; }
    const RawAssetDraft* GetCurrentDraft() const { return m_activeDraft.has_value() ? &(*m_activeDraft) : nullptr; }

    // Persistenza diretta del Draft su file JSON di progetto
    bool SaveDraftToFile(const std::string& filepath) const;
    bool LoadDraftFromFile(const std::string& filepath);

    // --- Sculpt & Modeling Mesh Integration (BM-S9) ---
    void SetDraftSculptMesh(const EditableMesh& mesh);
    const EditableMesh* GetDraftSculptMesh() const;
    EditableMesh* GetDraftSculptMeshMutable();

    // --- Navigazione e Controllo Stazioni ---
    bool SetActiveStation(ProductionStationID id);
    ProductionStationID GetActiveStation() const { return m_activeStation; }

    IProductionStation* GetStation(ProductionStationID id);
    const IProductionStation* GetStation(ProductionStationID id) const;

    // Esecuzione mirata
    bool ExecuteStation(ProductionStationID id, ShapeRegistry* sReg = nullptr, MaterialRegistry* mReg = nullptr, BlockRegistry* bReg = nullptr);
    bool ExecuteActiveStation(ShapeRegistry* sReg = nullptr, MaterialRegistry* mReg = nullptr, BlockRegistry* bReg = nullptr);

    // Esegue la pipeline fino alla stazione specificata risolvendo i nodi dirty in ordine topologico
    bool ExecutePipelineUpTo(ProductionStationID targetStation, ShapeRegistry* sReg = nullptr, MaterialRegistry* mReg = nullptr, BlockRegistry* bReg = nullptr);

    // Notifica di modifica proprietà con fingerprinting per invalidazione selettiva Zero-Work
    void NotifyPropertyModified(DependencyNodeID nodeId, uint64_t newFingerprint);

    // --- Texture Atlas Baking (BM-F2B) ---
    TextureBakingService& GetTextureBakingService() { return m_textureBakingService; }
    const TextureBakingService& GetTextureBakingService() const { return m_textureBakingService; }
    TextureBakeResult BakeCurrentDraftAlbedo(bool forceRewrite = false);

    // --- Quality Control & Packaging ---
    RawAssetValidation RunQualityControlAudit(ShapeRegistry* sReg = nullptr, MaterialRegistry* mReg = nullptr, BlockRegistry* bReg = nullptr);
    std::optional<RawAssetPackage> BuildPackage(ShapeRegistry* sReg = nullptr, MaterialRegistry* mReg = nullptr, BlockRegistry* bReg = nullptr);

    AssetPackagingService& GetAssetPackagingService() { return m_packagingService; }
    const AssetPackagingService& GetAssetPackagingService() const { return m_packagingService; }

    // Pubblica il pacchetto compilato direttamente nei Registri autorevoli di FAIRWORLD
    bool PublishToRegistries(BlockRegistry& bReg, MaterialRegistry& mReg, ShapeRegistry& sReg);
    bool PublishPersistentToRegistries(BlockRegistry& bReg, MaterialRegistry& mReg, ShapeRegistry& sReg);

    // Accesso al grafo di dipendenze
    AssetDependencyGraph& GetDependencyGraph() { return m_dependencyGraph; }
    const AssetDependencyGraph& GetDependencyGraph() const { return m_dependencyGraph; }

    // --- Telemetria e Zero-Work Geometry Compilation (BM-F3) ---
    uint64_t GetGeometryCompileCallCount() const { return m_geometryCompileCallCount; }
    void ResetGeometryCompileCallCount() { m_geometryCompileCallCount = 0; }
    void InvalidateGeometryCache();

    struct CompiledGeometryCache {
        GeometrySource source = GeometrySource::Primitive;
        uint64_t shapeRev = 0;
        uint64_t geomRev = 0;
        uint64_t topoRev = 0;
        uint64_t attrRev = 0;
        uint64_t uvRev = 0;
        size_t surfaceCount = 0;
        uint32_t defaultMatId = 0;
        GeometryCompileResult result;

        void Invalidate() {
            source = GeometrySource::Primitive;
            shapeRev = 0;
            geomRev = 0;
            topoRev = 0;
            attrRev = 0;
            uvRev = 0;
            surfaceCount = 0;
            defaultMatId = 0;
            result = GeometryCompileResult{};
        }
    };

private:
    void InitializeStations();
    uint64_t ComputeStationFingerprint(ProductionStationID id) const;

    std::optional<RawAssetDraft> m_activeDraft;
    ProductionStationID m_activeStation = ProductionStationID::Shape;

    CompiledGeometryCache m_compiledGeometryCache;
    mutable uint64_t m_geometryCompileCallCount = 0;

    AssetDependencyGraph m_dependencyGraph;
    TextureBakingService m_textureBakingService;
    AssetPackagingService m_packagingService;
    std::unordered_map<ProductionStationID, std::unique_ptr<IProductionStation>> m_stations;
};

} // namespace fw
