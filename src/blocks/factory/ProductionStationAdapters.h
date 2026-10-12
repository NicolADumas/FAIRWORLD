#pragma once

#include "ProductionStationContract.h"
#include "world/GeometryCompiler.h"
#include "blocks/ShapeRegistry.h"
#include "blocks/MaterialRegistry.h"
#include "blocks/BlockRegistry.h"
#include "blocks/SurfaceDefinition.h"
#include <filesystem>
#include <cmath>

namespace fw {

// =============================================================================
// 1. SHAPE STATION ADAPTER (BM-FC6)
// =============================================================================
class ShapeStationAdapter : public IProductionStation {
public:
    ProductionStationID GetID() const override { return ProductionStationID::Shape; }
    const char* GetName() const override { return "Shape Station"; }
    const char* GetDescription() const override { return "Generazione e compilazione della geometria primitiva di base."; }

    std::vector<ProductionStationID> GetPrerequisiteStations() const override { return {}; }
    std::vector<StationRequirement> GetRequiredInputs() const override {
        return { {"ShapeDefinition", true} };
    }
    std::vector<StationArtifact> GetProducedOutputs() const override {
        return { {"MeshGeometry", "MeshComponent"}, {"ShapeBounds", "AABB"} };
    }

    StationStatus GetStatus() const override { return m_status; }
    uint64_t GetLastProcessedRevision() const override { return m_lastRevision; }
    const StationDiagnostic& GetDiagnostic() const override { return m_diagnostic; }

    bool CanExecute(const StationContext& context) const override {
        return context.draft.shape.type != ShapeType::None || context.shapeRegistry != nullptr;
    }

    bool Execute(StationContext& context) override {
        m_diagnostic.Clear();
        m_status = StationStatus::Processing;

        // Se la forma non ha tipo valido, default a Cube
        if (context.draft.shape.type == ShapeType::None) {
            context.draft.shape.type = ShapeType::Cube;
            context.draft.shape.supportsVisualCompilation = true;
        }

        if (context.shapeRegistry) {
            auto valRes = context.shapeRegistry->ValidateParameters(context.draft.shape.type, context.draft.shape.parameters);
            if (!valRes.isValid) {
                m_diagnostic.errors.push_back(std::string("Validazione parametri forma fallita: ") + valRes.errorMessage);
                m_status = StationStatus::Failed;
                return false;
            }
        }

        // Test di compilazione geometrica
        GeometryCompileOptions opts;
        opts.shapeRegistry = context.shapeRegistry;
        auto compileRes = GeometryCompiler::Compile(context.draft.shape, opts);
        if (!compileRes.success) {
            m_diagnostic.errors.push_back(std::string("Compilazione geometrica fallita: ") + compileRes.errorMessage);
            m_status = StationStatus::Failed;
            return false;
        }

        m_lastRevision = context.draft.manifest.revision;
        m_status = StationStatus::Completed;
        context.draft.completedStationsMask |= (1u << static_cast<uint8_t>(ProductionStationID::Shape));
        return true;
    }

    void Invalidate() override {
        m_status = StationStatus::Dirty;
    }

    void Reset() override {
        m_status = StationStatus::Uninitialized;
        m_diagnostic.Clear();
        m_lastRevision = 0;
    }

private:
    StationStatus m_status = StationStatus::Uninitialized;
    StationDiagnostic m_diagnostic;
    uint64_t m_lastRevision = 0;
};

// =============================================================================
// 2. SCULPT STATION ADAPTER (NOT_IMPLEMENTED in Phase 1 -> BM-S)
// =============================================================================
class SculptStationAdapter : public IProductionStation {
public:
    ProductionStationID GetID() const override { return ProductionStationID::Sculpt; }
    const char* GetName() const override { return "Sculpt Station"; }
    const char* GetDescription() const override { return "Deformazione mesh e scultura volumetrica (Pianificata in BM-S)."; }

    std::vector<ProductionStationID> GetPrerequisiteStations() const override {
        return { ProductionStationID::Shape };
    }
    std::vector<StationRequirement> GetRequiredInputs() const override {
        return { {"MeshGeometry", true} };
    }
    std::vector<StationArtifact> GetProducedOutputs() const override {
        return { {"SculptedMesh", "EditableMesh"} };
    }

    StationStatus GetStatus() const override { return StationStatus::NotImplemented; }
    uint64_t GetLastProcessedRevision() const override { return 0; }
    const StationDiagnostic& GetDiagnostic() const override { return m_diagnostic; }

    bool IsImplemented() const override { return false; }
    bool CanExecute(const StationContext&) const override { return false; }
    bool Execute(StationContext&) override {
        m_diagnostic.Clear();
        m_diagnostic.warnings.push_back("Sculpt Station e' pianificata per la fase successiva (BM-S).");
        return false;
    }
    void Invalidate() override {}
    void Reset() override {}

private:
    mutable StationDiagnostic m_diagnostic;
};

// =============================================================================
// 3. SURFACE & UV STATION ADAPTER (BM-FC6)
// =============================================================================
class SurfaceUVStationAdapter : public IProductionStation {
public:
    ProductionStationID GetID() const override { return ProductionStationID::SurfaceUV; }
    const char* GetName() const override { return "Surface & UV Station"; }
    const char* GetDescription() const override { return "Sezionamento delle superfici semantiche e coordinate di mappatura UV."; }

    std::vector<ProductionStationID> GetPrerequisiteStations() const override {
        return { ProductionStationID::Shape };
    }
    std::vector<StationRequirement> GetRequiredInputs() const override {
        return { {"ShapeDefinition", true} };
    }
    std::vector<StationArtifact> GetProducedOutputs() const override {
        return { {"SurfaceOverrides", "std::vector<SurfaceOverride>"}, {"UVTransforms", "UVTransform"} };
    }

    StationStatus GetStatus() const override { return m_status; }
    uint64_t GetLastProcessedRevision() const override { return m_lastRevision; }
    const StationDiagnostic& GetDiagnostic() const override { return m_diagnostic; }

    bool CanExecute(const StationContext& context) const override {
        return (context.draft.completedStationsMask & (1u << static_cast<uint8_t>(ProductionStationID::Shape))) != 0;
    }

    bool Execute(StationContext& context) override {
        m_diagnostic.Clear();
        m_status = StationStatus::Processing;

        uint32_t expectedSurfaces = GetSemanticSurfaceCount(context.draft.shape.type);
        if (expectedSurfaces == 0) {
            m_diagnostic.warnings.push_back("Nessuna superficie semantica registrata per questa forma (fallback a 1).");
        }

        // Verifica che le superfici sovrascritte non abbiano indici fuori intervallo
        for (const auto& so : context.draft.surfaces) {
            if (so.surfaceId >= expectedSurfaces && expectedSurfaces > 0) {
                m_diagnostic.warnings.push_back("SurfaceOverride #" + std::to_string(so.surfaceId) + 
                    " eccede il numero di superfici semantiche della forma (" + std::to_string(expectedSurfaces) + ").");
            }
        }

        m_lastRevision = context.draft.manifest.revision;
        m_status = StationStatus::Completed;
        context.draft.completedStationsMask |= (1u << static_cast<uint8_t>(ProductionStationID::SurfaceUV));
        return true;
    }

    void Invalidate() override { m_status = StationStatus::Dirty; }
    void Reset() override { m_status = StationStatus::Uninitialized; m_diagnostic.Clear(); m_lastRevision = 0; }

private:
    StationStatus m_status = StationStatus::Uninitialized;
    StationDiagnostic m_diagnostic;
    uint64_t m_lastRevision = 0;
};

// =============================================================================
// 4. TEXTURE STATION ADAPTER (BM-FC6)
// =============================================================================
class TextureStationAdapter : public IProductionStation {
public:
    ProductionStationID GetID() const override { return ProductionStationID::Texture; }
    const char* GetName() const override { return "Texture Station"; }
    const char* GetDescription() const override { return "Verifica presenza e binding dei file texture (Albedo, Normal, ORM)."; }

    std::vector<ProductionStationID> GetPrerequisiteStations() const override { return {}; }
    std::vector<StationRequirement> GetRequiredInputs() const override { return {}; }
    std::vector<StationArtifact> GetProducedOutputs() const override {
        return { {"TextureFileBindings", "FilePaths"} };
    }

    StationStatus GetStatus() const override { return m_status; }
    uint64_t GetLastProcessedRevision() const override { return m_lastRevision; }
    const StationDiagnostic& GetDiagnostic() const override { return m_diagnostic; }

    bool CanExecute(const StationContext&) const override { return true; }

    bool Execute(StationContext& context) override {
        m_diagnostic.Clear();
        m_status = StationStatus::Processing;

        // Verifica esistenza opzionale dei percorsi texture su disco
        auto checkPath = [&](const std::string& path, const char* name) {
            if (!path.empty() && !std::filesystem::exists(path) && !std::filesystem::exists(std::string("../") + path)) {
                m_diagnostic.warnings.push_back(std::string("File texture ") + name + " non trovato su disco: " + path);
            }
        };

        checkPath(context.draft.albedoPath, "Albedo");
        checkPath(context.draft.normalPath, "Normal");
        checkPath(context.draft.ormPath, "ORM");

        m_lastRevision = context.draft.manifest.revision;
        m_status = StationStatus::Completed;
        context.draft.completedStationsMask |= (1u << static_cast<uint8_t>(ProductionStationID::Texture));
        return true;
    }

    void Invalidate() override { m_status = StationStatus::Dirty; }
    void Reset() override { m_status = StationStatus::Uninitialized; m_diagnostic.Clear(); m_lastRevision = 0; }

private:
    StationStatus m_status = StationStatus::Uninitialized;
    StationDiagnostic m_diagnostic;
    uint64_t m_lastRevision = 0;
};

// =============================================================================
// 5. PAINTER STATION ADAPTER (NOT_IMPLEMENTED in Phase 1)
// =============================================================================
class PainterStationAdapter : public IProductionStation {
public:
    ProductionStationID GetID() const override { return ProductionStationID::Painter; }
    const char* GetName() const override { return "Painter Station"; }
    const char* GetDescription() const override { return "Pittura texture e mascheratura vertex color (Pianificata)."; }

    std::vector<ProductionStationID> GetPrerequisiteStations() const override {
        return { ProductionStationID::SurfaceUV, ProductionStationID::Texture };
    }
    std::vector<StationRequirement> GetRequiredInputs() const override { return {}; }
    std::vector<StationArtifact> GetProducedOutputs() const override { return {}; }

    StationStatus GetStatus() const override { return StationStatus::NotImplemented; }
    uint64_t GetLastProcessedRevision() const override { return 0; }
    const StationDiagnostic& GetDiagnostic() const override { return m_diagnostic; }

    bool IsImplemented() const override { return false; }
    bool CanExecute(const StationContext&) const override { return false; }
    bool Execute(StationContext&) override { return false; }
    void Invalidate() override {}
    void Reset() override {}

private:
    mutable StationDiagnostic m_diagnostic;
};

// =============================================================================
// 6. MATERIAL STATION ADAPTER (BM-FC6)
// =============================================================================
class MaterialStationAdapter : public IProductionStation {
public:
    ProductionStationID GetID() const override { return ProductionStationID::Material; }
    const char* GetName() const override { return "Material Station"; }
    const char* GetDescription() const override { return "Configurazione parametri PBR e shader properties."; }

    std::vector<ProductionStationID> GetPrerequisiteStations() const override {
        return { ProductionStationID::SurfaceUV };
    }
    std::vector<StationRequirement> GetRequiredInputs() const override {
        return { {"PBRProperties", true} };
    }
    std::vector<StationArtifact> GetProducedOutputs() const override {
        return { {"PBRMaterialDef", "PBRMaterialDef"} };
    }

    StationStatus GetStatus() const override { return m_status; }
    uint64_t GetLastProcessedRevision() const override { return m_lastRevision; }
    const StationDiagnostic& GetDiagnostic() const override { return m_diagnostic; }

    bool CanExecute(const StationContext&) const override { return true; }

    bool Execute(StationContext& context) override {
        m_diagnostic.Clear();
        m_status = StationStatus::Processing;

        // Validazione range fisici [0, 1]
        if (context.draft.roughnessFallback < 0.0f || context.draft.roughnessFallback > 1.0f) {
            m_diagnostic.warnings.push_back("Roughness fuori dal range [0, 1], clamp applicato.");
            context.draft.roughnessFallback = std::clamp(context.draft.roughnessFallback, 0.0f, 1.0f);
        }
        if (context.draft.metallicFallback < 0.0f || context.draft.metallicFallback > 1.0f) {
            m_diagnostic.warnings.push_back("Metallic fuori dal range [0, 1], clamp applicato.");
            context.draft.metallicFallback = std::clamp(context.draft.metallicFallback, 0.0f, 1.0f);
        }
        if (context.draft.emissiveStrength < 0.0f) {
            m_diagnostic.errors.push_back("Emissive strength non puo' essere negativa.");
            m_status = StationStatus::Failed;
            return false;
        }

        m_lastRevision = context.draft.manifest.revision;
        m_status = StationStatus::Completed;
        context.draft.completedStationsMask |= (1u << static_cast<uint8_t>(ProductionStationID::Material));
        return true;
    }

    void Invalidate() override { m_status = StationStatus::Dirty; }
    void Reset() override { m_status = StationStatus::Uninitialized; m_diagnostic.Clear(); m_lastRevision = 0; }

private:
    StationStatus m_status = StationStatus::Uninitialized;
    StationDiagnostic m_diagnostic;
    uint64_t m_lastRevision = 0;
};

// =============================================================================
// 7. RAW PHYSICS STATION ADAPTER (BM-FC6)
// =============================================================================
class RawPhysicsStationAdapter : public IProductionStation {
public:
    ProductionStationID GetID() const override { return ProductionStationID::RawPhysics; }
    const char* GetName() const override { return "Raw Physics Station"; }
    const char* GetDescription() const override { return "Proprietà fisiche grezze per-asset (Massa, Attrito, Restituzione)."; }

    std::vector<ProductionStationID> GetPrerequisiteStations() const override {
        return { ProductionStationID::Shape };
    }
    std::vector<StationRequirement> GetRequiredInputs() const override {
        return { {"BlockPhysicsDef", true} };
    }
    std::vector<StationArtifact> GetProducedOutputs() const override {
        return { {"CompiledPhysicsDef", "BlockPhysicsDef"} };
    }

    StationStatus GetStatus() const override { return m_status; }
    uint64_t GetLastProcessedRevision() const override { return m_lastRevision; }
    const StationDiagnostic& GetDiagnostic() const override { return m_diagnostic; }

    bool CanExecute(const StationContext&) const override { return true; }

    bool Execute(StationContext& context) override {
        m_diagnostic.Clear();
        m_status = StationStatus::Processing;

        if (context.draft.physics.mass <= 0.0f && context.draft.physics.massMode == BlockMassMode::Manual) {
            m_diagnostic.errors.push_back("La massa deve essere strettamente positiva (> 0 kg).");
            m_status = StationStatus::Failed;
            return false;
        }
        if (context.draft.physics.density <= 0.0f) {
            m_diagnostic.errors.push_back("La densita deve essere strettamente positiva (> 0 kg/m^3).");
            m_status = StationStatus::Failed;
            return false;
        }
        if (context.draft.physics.friction < 0.0f || context.draft.physics.friction > 1.0f) {
            m_diagnostic.warnings.push_back("Coefficiente di attrito fuori scala [0, 1]. Clamp applicato.");
            context.draft.physics.friction = std::clamp(context.draft.physics.friction, 0.0f, 1.0f);
        }
        if (context.draft.physics.restitution < 0.0f || context.draft.physics.restitution > 1.0f) {
            m_diagnostic.warnings.push_back("Coefficiente di restituzione fuori scala [0, 1]. Clamp applicato.");
            context.draft.physics.restitution = std::clamp(context.draft.physics.restitution, 0.0f, 1.0f);
        }

        m_lastRevision = context.draft.manifest.revision;
        m_status = StationStatus::Completed;
        context.draft.completedStationsMask |= (1u << static_cast<uint8_t>(ProductionStationID::RawPhysics));
        return true;
    }

    void Invalidate() override { m_status = StationStatus::Dirty; }
    void Reset() override { m_status = StationStatus::Uninitialized; m_diagnostic.Clear(); m_lastRevision = 0; }

private:
    StationStatus m_status = StationStatus::Uninitialized;
    StationDiagnostic m_diagnostic;
    uint64_t m_lastRevision = 0;
};

// =============================================================================
// 8. QUALITY CONTROL (QC) STATION ADAPTER (BM-FC6)
// =============================================================================
class QualityControlStationAdapter : public IProductionStation {
public:
    ProductionStationID GetID() const override { return ProductionStationID::QualityControl; }
    const char* GetName() const override { return "Quality Control Station"; }
    const char* GetDescription() const override { return "Audit di conformità complessivo, assenza degenerazioni e vincoli."; }

    std::vector<ProductionStationID> GetPrerequisiteStations() const override {
        return {
            ProductionStationID::Shape,
            ProductionStationID::SurfaceUV,
            ProductionStationID::Material,
            ProductionStationID::RawPhysics
        };
    }
    std::vector<StationRequirement> GetRequiredInputs() const override {
        return { {"ValidatedOutputs", true} };
    }
    std::vector<StationArtifact> GetProducedOutputs() const override {
        return { {"RawAssetValidation", "ValidationReport"} };
    }

    StationStatus GetStatus() const override { return m_status; }
    uint64_t GetLastProcessedRevision() const override { return m_lastRevision; }
    const StationDiagnostic& GetDiagnostic() const override { return m_diagnostic; }

    bool CanExecute(const StationContext& context) const override {
        uint32_t mask = context.draft.completedStationsMask;
        bool shapeOk = (mask & (1u << static_cast<uint8_t>(ProductionStationID::Shape))) != 0;
        bool surfOk = (mask & (1u << static_cast<uint8_t>(ProductionStationID::SurfaceUV))) != 0;
        bool matOk = (mask & (1u << static_cast<uint8_t>(ProductionStationID::Material))) != 0;
        bool physOk = (mask & (1u << static_cast<uint8_t>(ProductionStationID::RawPhysics))) != 0;
        return shapeOk && surfOk && matOk && physOk;
    }

    bool Execute(StationContext& context) override {
        m_diagnostic.Clear();
        m_status = StationStatus::Processing;

        // 1. Verifica identificatore StringID
        std::string errStr;
        if (!BlockRegistry::ValidateStringId(context.draft.manifest.assetId, &errStr)) {
            m_diagnostic.errors.push_back("String ID non valido: " + errStr);
        }

        // 2. Verifica scala non degenere
        if (std::abs(context.draft.localScale.x) < 1e-4f ||
            std::abs(context.draft.localScale.y) < 1e-4f ||
            std::abs(context.draft.localScale.z) < 1e-4f) {
            m_diagnostic.errors.push_back("Scale degenere: le componenti devono avere |scale| >= 0.0001.");
        }

        if (m_diagnostic.HasErrors()) {
            m_status = StationStatus::Failed;
            context.draft.manifest.stage = RawAssetStage::Draft;
            return false;
        }

        m_lastRevision = context.draft.manifest.revision;
        m_status = StationStatus::Completed;
        context.draft.manifest.stage = RawAssetStage::Validated;
        context.draft.completedStationsMask |= (1u << static_cast<uint8_t>(ProductionStationID::QualityControl));
        return true;
    }

    void Invalidate() override { m_status = StationStatus::Dirty; }
    void Reset() override { m_status = StationStatus::Uninitialized; m_diagnostic.Clear(); m_lastRevision = 0; }

private:
    StationStatus m_status = StationStatus::Uninitialized;
    StationDiagnostic m_diagnostic;
    uint64_t m_lastRevision = 0;
};

// =============================================================================
// 9. PACKAGING STATION ADAPTER (BM-FC6)
// =============================================================================
class PackagingStationAdapter : public IProductionStation {
public:
    ProductionStationID GetID() const override { return ProductionStationID::Packaging; }
    const char* GetName() const override { return "Packaging Station"; }
    const char* GetDescription() const override { return "Congelamento e compilazione transazionale del pacchetto grezzo."; }

    std::vector<ProductionStationID> GetPrerequisiteStations() const override {
        return { ProductionStationID::QualityControl };
    }
    std::vector<StationRequirement> GetRequiredInputs() const override {
        return { {"ValidatedDraft", true} };
    }
    std::vector<StationArtifact> GetProducedOutputs() const override {
        return { {"RawAssetPackage", "PackageArtifact"} };
    }

    StationStatus GetStatus() const override { return m_status; }
    uint64_t GetLastProcessedRevision() const override { return m_lastRevision; }
    const StationDiagnostic& GetDiagnostic() const override { return m_diagnostic; }

    bool CanExecute(const StationContext& context) const override {
        return context.draft.manifest.stage >= RawAssetStage::Validated;
    }

    bool Execute(StationContext& context) override {
        m_diagnostic.Clear();
        m_status = StationStatus::Processing;

        if (context.draft.manifest.stage < RawAssetStage::Validated) {
            m_diagnostic.errors.push_back("Impossibile impacchettare: l'asset deve prima superare il Quality Control.");
            m_status = StationStatus::Failed;
            return false;
        }

        // Compila la geometria finale per il pacchetto
        GeometryCompileOptions opts;
        opts.shapeRegistry = context.shapeRegistry;
        opts.surfaceOverrides = &context.draft.surfaces;
        opts.defaultUVTransform = &context.draft.baseUVTransform;
        opts.defaultProjection = context.draft.baseProjection;

        auto compRes = GeometryCompiler::Compile(context.draft.shape, opts);
        if (!compRes.success) {
            m_diagnostic.errors.push_back("Errore compilazione geometria durante il packaging: " + compRes.errorMessage);
            m_status = StationStatus::Failed;
            return false;
        }

        m_lastRevision = context.draft.manifest.revision;
        m_status = StationStatus::Completed;
        context.draft.manifest.stage = RawAssetStage::Packaged;
        context.draft.completedStationsMask |= (1u << static_cast<uint8_t>(ProductionStationID::Packaging));
        return true;
    }

    void Invalidate() override { m_status = StationStatus::Dirty; }
    void Reset() override { m_status = StationStatus::Uninitialized; m_diagnostic.Clear(); m_lastRevision = 0; }

private:
    StationStatus m_status = StationStatus::Uninitialized;
    StationDiagnostic m_diagnostic;
    uint64_t m_lastRevision = 0;
};

} // namespace fw
