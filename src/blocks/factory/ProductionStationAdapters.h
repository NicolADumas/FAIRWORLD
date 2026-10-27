#pragma once

#include "ProductionStationContract.h"
#include "world/GeometryCompiler.h"
#include "blocks/ShapeRegistry.h"
#include "blocks/MaterialRegistry.h"
#include "blocks/BlockRegistry.h"
#include "blocks/SurfaceDefinition.h"
#include "blocks/texture/ImageBitmap.h"
#include "blocks/texture/PBRTexturePack.h"
#include "blocks/texture/TextureAsset.h"
#include "blocks/texture/TexturePaintingService.h"
#include "blocks/uv/PrimitiveUVMapper.h"
#include "blocks/uv/GeneralUVMapper.h"
#include "blocks/uv/UVAtlasPacker.h"
#include "blocks/uv/TextureReprojectionService.h"
#include "blocks/sculpt/MeshBridgeService.h"
#include <filesystem>
#include <cmath>
#include <cstring>

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
// 2. SCULPT STATION ADAPTER (BM-S9)
// =============================================================================
class SculptStationAdapter : public IProductionStation {
public:
    ProductionStationID GetID() const override { return ProductionStationID::Sculpt; }
    const char* GetName() const override { return "Sculpt Station"; }
    const char* GetDescription() const override { return "Deformazione mesh e scultura volumetrica CPU-authoritative."; }

    std::vector<ProductionStationID> GetPrerequisiteStations() const override {
        return { ProductionStationID::Shape };
    }
    std::vector<StationRequirement> GetRequiredInputs() const override {
        return { {"EditableMesh", false} };
    }
    std::vector<StationArtifact> GetProducedOutputs() const override {
        return { {"SculptedMesh", "EditableMesh"}, {"CompiledPreview", "MeshComponent"} };
    }

    StationStatus GetStatus() const override { return m_status; }
    uint64_t GetLastProcessedRevision() const override { return m_lastRevision; }
    const StationDiagnostic& GetDiagnostic() const override { return m_diagnostic; }

    bool IsImplemented() const override { return true; }
    bool CanExecute(const StationContext& context) const override {
        return context.draft.sculptMesh.has_value() || context.draft.shape.type != ShapeType::None;
    }

    bool Execute(StationContext& context) override {
        m_diagnostic.Clear();
        m_status = StationStatus::Processing;

        // Se il draft non possiede ancora una EditableMesh, inizializzala a partire dalla forma del draft
        if (!context.draft.sculptMesh.has_value()) {
            if (context.draft.shape.type != ShapeType::None) {
                context.draft.sculptMesh = MeshBridgeService::CreateMeshFromShape(context.draft.shape);
            } else {
                context.draft.sculptMesh = EditableMesh::CreateCube(1.0f);
            }
        }

        // 1. Validazione topologica della mesh
        auto valReport = context.draft.sculptMesh->ValidateTopology();
        if (!valReport.isValid) {
            for (const auto& err : valReport.errors) {
                m_diagnostic.errors.push_back("Errore topologico Sculpt: " + err);
            }
            m_status = StationStatus::Failed;
            return false;
        }

        for (const auto& warn : valReport.warnings) {
            m_diagnostic.warnings.push_back("Warning topologico Sculpt: " + warn);
        }

        // 2. Compilazione di test verso GeometryCompiler
        GeometryCompileOptions opts;
        opts.surfaceOverrides = &context.draft.surfaces;
        opts.defaultUVTransform = &context.draft.baseUVTransform;
        opts.defaultProjection = context.draft.baseProjection;

        auto compRes = GeometryCompiler::CompileEditableMesh(*context.draft.sculptMesh, opts);
        if (!compRes.success) {
            m_diagnostic.errors.push_back("Compilazione geometrica Sculpt fallita: " + compRes.errorMessage);
            m_status = StationStatus::Failed;
            return false;
        }

        m_lastRevision = context.draft.manifest.revision;
        m_status = StationStatus::Completed;
        context.draft.completedStationsMask |= (1u << static_cast<uint8_t>(ProductionStationID::Sculpt));
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
// 3. SURFACE & UV STATION ADAPTER (BM-FC6)
// =============================================================================
class SurfaceUVStationAdapter : public IProductionStation {
public:
    ProductionStationID GetID() const override { return ProductionStationID::SurfaceUV; }
    const char* GetName() const override { return "Surface & UV Station"; }
    const char* GetDescription() const override { return "Sezionamento delle superfici semantiche e coordinate di mappatura UV (BM-UV2)."; }

    std::vector<ProductionStationID> GetPrerequisiteStations() const override {
        return { ProductionStationID::Shape };
    }
    std::vector<StationRequirement> GetRequiredInputs() const override {
        return { {"ShapeDefinition", true} };
    }
    std::vector<StationArtifact> GetProducedOutputs() const override {
        return { 
            {"SurfaceOverrides", "std::vector<SurfaceOverride>"}, 
            {"UVTransforms", "UVTransform"},
            {"UVLayout", "UVLayout"}
        };
    }

    StationStatus GetStatus() const override { return m_status; }
    uint64_t GetLastProcessedRevision() const override { return m_lastRevision; }
    const StationDiagnostic& GetDiagnostic() const override { return m_diagnostic; }
    bool IsImplemented() const override { return true; }

    bool CanExecute(const StationContext& context) const override {
        return (context.draft.completedStationsMask & (1u << static_cast<uint8_t>(ProductionStationID::Shape))) != 0;
    }

    bool Execute(StationContext& context) override {
        // 1. Calcolo del fingerprint d'ingresso per invariante Zero-Work (BM-UV2.11 / BM-F3)
        uint64_t currentFp = 14695981039346656037ULL;
        auto hashU64 = [&](uint64_t v) {
            currentFp ^= v;
            currentFp *= 1099511628211ULL;
        };
        auto hashFloat = [&](float f) {
            uint32_t u = 0;
            std::memcpy(&u, &f, sizeof(float));
            hashU64(u);
        };

        auto hashUVTransform = [&](const UVTransform& uvT) {
            hashFloat(uvT.tiling.x);
            hashFloat(uvT.tiling.y);
            hashFloat(uvT.offset.x);
            hashFloat(uvT.offset.y);
            hashFloat(uvT.rotationDegrees);
            hashU64((uvT.mirrorU ? 1ULL : 0ULL) | (uvT.mirrorV ? 2ULL : 0ULL));
        };

        bool isSculptedInput = (context.draft.geometrySource == GeometrySource::SculptedMesh) && context.draft.sculptMesh.has_value();
        hashU64(isSculptedInput ? 1ULL : 0ULL);

        if (isSculptedInput) {
            hashU64(context.draft.sculptMesh->GetTopologyRevision());
            hashU64(context.draft.sculptMesh->GetGeometryRevision());
        } else {
            hashU64(static_cast<uint64_t>(context.draft.shape.type));
            hashU64(context.draft.shape.revision);
        }

        hashU64(static_cast<uint64_t>(context.draft.baseProjection));
        hashUVTransform(context.draft.baseUVTransform);

        hashU64(static_cast<uint64_t>(context.draft.surfaces.size()));
        for (const auto& so : context.draft.surfaces) {
            hashU64(so.surfaceId);
            hashU64(static_cast<uint64_t>(so.projection));
            hashUVTransform(so.uvTransform);
        }

        // BM-UV2 / BM-F3: Zero-Work - se la stazione e' gia' Completed, l'input e' invariato e il layout e' valido, non rieseguire
        if (m_status == StationStatus::Completed && currentFp == m_lastFingerprint && m_lastFingerprint != 0 &&
            context.draft.uvLayout.has_value() && context.draft.uvLayout->isValid) {
            context.draft.completedStationsMask |= (1u << static_cast<uint8_t>(ProductionStationID::SurfaceUV));
            return true;
        }

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

        // BM-UV8: Conservazione preventiva di snapshot se l'asset ha pittura pregressa
        bool hasExistingPaint = !context.draft.paintLayers.empty() || 
                               (context.draft.textures.has_value() && context.draft.textures->IsValid());
        bool hasPreviousUV = context.draft.uvLayout.has_value() && context.draft.uvLayout->isValid;
        
        UVLayout previousLayout;
        EditableMesh previousMesh;
        if (hasExistingPaint && hasPreviousUV) {
            previousLayout = *context.draft.uvLayout;
            if (context.draft.sculptMesh.has_value()) {
                previousMesh = *context.draft.sculptMesh;
            } else {
                previousMesh = MeshBridgeService::CreateMeshFromShape(context.draft.shape);
            }
        }

        // BM-UV2 & BM-UV3: Generazione autoritativa del layout UV su EditableMesh
        EditableMesh workMesh;
        bool isSculpted = (context.draft.geometrySource == GeometrySource::SculptedMesh) && context.draft.sculptMesh.has_value();
        if (isSculpted) {
            workMesh = *context.draft.sculptMesh;
        } else {
            workMesh = MeshBridgeService::CreateMeshFromShape(context.draft.shape);
        }

        // Risoluzione target coerente con i layer correnti o il layout precedente (previene mutazione 512->1024)
        uint32_t targetResW = 512;
        uint32_t targetResH = 512;
        if (!context.draft.paintLayers.empty() && context.draft.paintLayers[0].pixelBuffer.IsValid()) {
            targetResW = context.draft.paintLayers[0].pixelBuffer.GetWidth();
            targetResH = context.draft.paintLayers[0].pixelBuffer.GetHeight();
        } else if (hasPreviousUV && previousLayout.atlasTargetWidth > 0 && previousLayout.atlasTargetHeight > 0) {
            targetResW = previousLayout.atlasTargetWidth;
            targetResH = previousLayout.atlasTargetHeight;
        }

        GeneralUVOptions opt;
        opt.seamOptions.seamOnSharpEdges = true;
        opt.seamOptions.autoCutClosedComponents = true;

        auto mapRes = m_generalUVMapper.UnwrapWithAutoDispatch(workMesh, 
                                                               context.draft.shape.type, 
                                                               context.draft.shape.parameters, 
                                                               isSculpted, 
                                                               opt);

        mapRes.layout.atlasTargetWidth = targetResW;
        mapRes.layout.atlasTargetHeight = targetResH;

        // BM-UV4: Packing deterministico delle isole UV in Texture Atlas (solo per unwrapping generale non analitico)
        if (mapRes.modeUsed == UVUnwrapDispatchMode::GeneralUnwrapping) {
            AtlasPackOptions packOpt;
            packOpt.targetWidth = targetResW;
            packOpt.targetHeight = targetResH;
            packOpt.paddingPixels = 4.0f;
            packOpt.allowRotation = false; // Preserva orientamento per consistenza visuale
            packOpt.allowMultiAtlas = true;
            auto packRes = m_atlasPacker.PackLayout(workMesh, mapRes.layout, packOpt);

            if (!packRes.success) {
                for (const auto& err : packRes.errors) {
                    m_diagnostic.warnings.push_back("AtlasPacking error: " + err);
                }
            }
        }

        // BM-UV8: Rilevamento modifiche effettive UV e invocazione transazionale di TextureReprojectionService
        if (hasExistingPaint && hasPreviousUV) {
            bool uvActuallyChanged = (previousLayout.islands.size() != mapRes.layout.islands.size()) ||
                                     (previousLayout.strategyUsed != mapRes.layout.strategyUsed) ||
                                     (previousLayout.atlasTargetWidth != mapRes.layout.atlasTargetWidth) ||
                                     (previousLayout.atlasTargetHeight != mapRes.layout.atlasTargetHeight);

            if (!uvActuallyChanged) {
                for (size_t i = 0; i < previousLayout.islands.size(); ++i) {
                    if (glm::distance(previousLayout.islands[i].atlasTransform, mapRes.layout.islands[i].atlasTransform) > 1e-4f) {
                        uvActuallyChanged = true;
                        break;
                    }
                }
            }

            if (uvActuallyChanged) {
                // Distinzione tra modifica UV pura e alterazione topologica della mesh
                bool topologyChanged = (previousMesh.GetTopologyRevision() != workMesh.GetTopologyRevision()) ||
                                       (previousMesh.GetFaceCount() != workMesh.GetFaceCount());

                if (topologyChanged) {
                    m_diagnostic.warnings.push_back("TextureReprojection: Rilevata modifica topologica senza corrispondenza esplicita (REQUIRES_CORRESPONDENCE). Migrazione pittura sospesa in sicurezza per prevenire corruzioni.");
                } else if (!context.draft.paintLayers.empty()) {
                    TexturePaintingService tempPainter;
                    tempPainter.InitializeCanvas(targetResW, targetResH);
                    tempPainter.GetLayers() = context.draft.paintLayers;

                    ReprojectionOptions reprojOpt;
                    reprojOpt.strategy = ReprojectionStrategy::Auto;
                    reprojOpt.preserveLayersIndividually = true;
                    reprojOpt.overrideWidth = targetResW;
                    reprojOpt.overrideHeight = targetResH;

                    auto reprojReport = m_reprojectionService.ReprojectPaintingService(
                        previousMesh, previousLayout, tempPainter,
                        workMesh, mapRes.layout, reprojOpt);

                    if (reprojReport.status == ReprojectionStatus::Success || reprojReport.status == ReprojectionStatus::ZeroWorkNoOp) {
                        context.draft.paintLayers = std::move(tempPainter.GetLayers());
                        m_diagnostic.warnings.push_back("TextureReprojection: Pittura migrata con successo sul nuovo layout UV (" + 
                            std::to_string(reprojReport.totalLayersProcessed) + " layer riproiettati).");
                    } else {
                        m_diagnostic.warnings.push_back("TextureReprojection: Fallimento transazione (" + 
                            std::string(GetReprojectionStatusName(reprojReport.status)) + "). Ripristinato layout precedente.");
                    }
                }
            } else {
                // Se il layout non è mutato, preserva le revisioni e le coordinate esistenti per non invalidare la pittura
                mapRes.layout.uvRevision = previousLayout.uvRevision;
                mapRes.layout.atlasRevision = previousLayout.atlasRevision;
            }
        }

        context.draft.uvLayout = mapRes.layout;
        context.draft.uvRevision = mapRes.layout.uvRevision;

        // BM-UV-PAINTER MG03: Conserva sempre la mesh autoritativa un-wrapped nel draft
        // in modo che BlockMaker, Painter e Viewport 3D campionino la stessa topologia UV coerente.
        context.draft.sculptMesh = workMesh;

        if (mapRes.topologyClassification == TopologySupportClassification::RequiresRepair) {
            m_diagnostic.warnings.push_back("Mesh topologicamente problematica: richiede riparazione manifold.");
        }

        for (const auto& w : mapRes.validation.warnings) {
            m_diagnostic.warnings.push_back(w);
        }

        m_lastRevision = context.draft.manifest.revision;
        m_lastFingerprint = currentFp;
        m_status = StationStatus::Completed;
        context.draft.completedStationsMask |= (1u << static_cast<uint8_t>(ProductionStationID::SurfaceUV));
        return true;
    }

    void Invalidate() override { 
        m_status = StationStatus::Dirty; 
        m_lastFingerprint = 0;
    }

    void Reset() override { 
        m_status = StationStatus::Uninitialized; 
        m_diagnostic.Clear(); 
        m_lastRevision = 0; 
        m_lastFingerprint = 0;
    }

private:
    StationStatus m_status = StationStatus::Uninitialized;
    StationDiagnostic m_diagnostic;
    uint64_t m_lastRevision = 0;
    uint64_t m_lastFingerprint = 0;
    PrimitiveUVMapper m_uvMapper;
    GeneralUVMapper m_generalUVMapper;
    UVAtlasPacker m_atlasPacker;
    TextureReprojectionService m_reprojectionService;
};

// =============================================================================
// 4. TEXTURE STATION ADAPTER (BM-FC6)
// =============================================================================
// 4. TEXTURE STATION ADAPTER (BM-TX2)
// =============================================================================
class TextureStationAdapter : public IProductionStation {
public:
    ProductionStationID GetID() const override { return ProductionStationID::Texture; }
    const char* GetName() const override { return "Texture Station"; }
    const char* GetDescription() const override { return "Importazione, normalizzazione e binding delle mappe texture PBR (BM-TX2)."; }

    std::vector<ProductionStationID> GetPrerequisiteStations() const override { return {}; }
    std::vector<StationRequirement> GetRequiredInputs() const override { return {}; }
    std::vector<StationArtifact> GetProducedOutputs() const override {
        return { 
            {"PBRTexturePack", "PBRTexturePack"},
            {"TextureFileBindings", "FilePaths"} 
        };
    }

    StationStatus GetStatus() const override { return m_status; }
    uint64_t GetLastProcessedRevision() const override { return m_lastRevision; }
    const StationDiagnostic& GetDiagnostic() const override { return m_diagnostic; }

    bool IsImplemented() const override { return true; }
    bool CanExecute(const StationContext&) const override { return true; }

    bool Execute(StationContext& context) override {
        m_diagnostic.Clear();
        m_status = StationStatus::Processing;

        // 1. Calcolo del fingerprint d'ingresso per invariante Zero-Work
        uint64_t currentFp = 14695981039346656037ULL;
        auto addStr = [&](const std::string& s) {
            for (char c : s) { currentFp ^= static_cast<uint8_t>(c); currentFp *= 1099511628211ULL; }
        };
        addStr(context.draft.albedoPath);
        addStr(context.draft.normalPath);
        addStr(context.draft.ormPath);
        addStr(context.draft.emissivePath);
        for (const auto& s : context.draft.surfaces) {
            if (s.textures.HasTextures()) {
                addStr(s.textures.albedoMapPath);
                addStr(s.textures.normalMapPath);
                addStr(s.textures.ormMapPath);
            }
        }
        currentFp ^= context.draft.textureRevision;
        currentFp *= 1099511628211ULL;

        // BM-TX2.9: Zero-Work - se input stabile e stato Completed, non fare I/O o ricalcoli
        if (m_status == StationStatus::Completed && currentFp == m_lastFingerprint && m_lastFingerprint != 0) {
            return true;
        }

        // 2. Inizializza il PBRTexturePack se non presente
        if (!context.draft.textures.has_value()) {
            context.draft.textures = PBRTexturePack();
        }

        auto& pack = *context.draft.textures;
        pack.fallbackBaseColor = glm::vec4(context.draft.baseColorFallback, context.draft.alphaFallback);
        pack.fallbackRoughness = context.draft.roughnessFallback;
        pack.fallbackMetallic = context.draft.metallicFallback;
        pack.fallbackEmissive = glm::vec3(context.draft.emissiveStrength);

        auto resolvePath = [](const std::string& p) -> std::string {
            if (p.empty()) return "";
            if (std::filesystem::exists(p)) return p;
            if (std::filesystem::exists("assets/" + p)) return "assets/" + p;
            if (std::filesystem::exists("../assets/" + p)) return "../assets/" + p;
            if (std::filesystem::exists("../" + p)) return "../" + p;
            return "";
        };

        // 3. Importazione e decodifica mappe con color space appropriato
        auto loadMap = [&](const std::string& path, TextureColorSpace cs, auto setter, const char* name) {
            if (!path.empty()) {
                std::string resPath = resolvePath(path);
                if (!resPath.empty()) {
                    ImageBitmap img;
                    if (img.LoadFromFile(resPath, cs)) {
                        (pack.*setter)(img);
                    } else {
                        m_diagnostic.warnings.push_back(std::string("Decodifica fallita per mappa ") + name + ": " + path);
                    }
                } else {
                    m_diagnostic.warnings.push_back(std::string("File texture ") + name + " non trovato su disco: " + path);
                }
            }
        };

        loadMap(context.draft.albedoPath, TextureColorSpace::sRGB, &PBRTexturePack::SetBaseColorMap, "Albedo");
        loadMap(context.draft.normalPath, TextureColorSpace::Linear, &PBRTexturePack::SetNormalMap, "Normal");
        loadMap(context.draft.ormPath, TextureColorSpace::Linear, &PBRTexturePack::SetORMMap, "ORM");
        loadMap(context.draft.emissivePath, TextureColorSpace::sRGB, &PBRTexturePack::SetEmissiveMap, "Emissive");

        // 4. Auto-packing ORM se non fornito esplicitamente
        if (!pack.HasORM()) {
            pack.GenerateORM(512, 512);
        }

        // 5. Validazione finale
        auto valReport = pack.Validate();
        for (const auto& err : valReport.errors) {
            m_diagnostic.errors.push_back(err);
        }
        for (const auto& warn : valReport.warnings) {
            m_diagnostic.warnings.push_back(warn);
        }

        if (m_diagnostic.HasErrors()) {
            m_status = StationStatus::Failed;
            return false;
        }

        m_lastRevision = context.draft.manifest.revision;
        m_lastFingerprint = currentFp;
        m_status = StationStatus::Completed;
        context.draft.completedStationsMask |= (1u << static_cast<uint8_t>(ProductionStationID::Texture));
        return true;
    }

    void Invalidate() override { 
        m_status = StationStatus::Dirty; 
        m_lastFingerprint = 0;
    }

    void Reset() override { 
        m_status = StationStatus::Uninitialized; 
        m_diagnostic.Clear(); 
        m_lastRevision = 0; 
        m_lastFingerprint = 0;
    }

private:
    StationStatus m_status = StationStatus::Uninitialized;
    StationDiagnostic m_diagnostic;
    uint64_t m_lastRevision = 0;
    uint64_t m_lastFingerprint = 0;
};

// =============================================================================
// 5. PAINTER STATION ADAPTER (BM-F2A)
// =============================================================================
class PainterStationAdapter : public IProductionStation {
public:
    ProductionStationID GetID() const override { return ProductionStationID::Painter; }
    const char* GetName() const override { return "Painter Station"; }
    const char* GetDescription() const override { return "Pittura texture multi-layer, compositing CPU e sincronizzazione pittura (BM-F2A)."; }

    std::vector<ProductionStationID> GetPrerequisiteStations() const override {
        return { ProductionStationID::SurfaceUV };
    }
    std::vector<StationRequirement> GetRequiredInputs() const override {
        return { {"UVLayout", false}, {"PaintLayers", false} };
    }
    std::vector<StationArtifact> GetProducedOutputs() const override {
        return { 
            {"PaintLayers", "std::vector<PaintLayer>"},
            {"CompositeAlbedo", "ImageBitmap"}
        };
    }

    StationStatus GetStatus() const override { return m_status; }
    uint64_t GetLastProcessedRevision() const override { return m_lastRevision; }
    const StationDiagnostic& GetDiagnostic() const override { return m_diagnostic; }

    bool IsImplemented() const override { return true; }

    bool CanExecute(const StationContext& context) const override {
        // La stazione può essere eseguita se la SurfaceUV station è stata completata
        return (context.draft.completedStationsMask & (1u << static_cast<uint8_t>(ProductionStationID::SurfaceUV))) != 0;
    }

    bool Execute(StationContext& context) override {
        m_diagnostic.Clear();
        m_status = StationStatus::Processing;

        // 1. Calcolo del fingerprint d'ingresso per invariante Zero-Work
        uint64_t currentFp = 14695981039346656037ULL;
        currentFp ^= context.draft.paintRevision;
        currentFp *= 1099511628211ULL;
        currentFp ^= context.draft.uvRevision;
        currentFp *= 1099511628211ULL;
        currentFp ^= static_cast<uint64_t>(context.draft.paintLayers.size());
        currentFp *= 1099511628211ULL;

        for (const auto& layer : context.draft.paintLayers) {
            currentFp ^= layer.layerId;
            currentFp *= 1099511628211ULL;
            currentFp ^= static_cast<uint64_t>(layer.blendMode);
            currentFp *= 1099511628211ULL;
            currentFp ^= layer.pixelBuffer.GetWidth();
            currentFp *= 1099511628211ULL;
            currentFp ^= layer.pixelBuffer.GetHeight();
            currentFp *= 1099511628211ULL;
            // Hashing del contenuto reale dei pixel per rilevare ogni variazione pittorica
            currentFp ^= layer.pixelBuffer.ComputeContentHash();
            currentFp *= 1099511628211ULL;
        }

        // BM-F2A: Zero-Work - se input stabile e stato Completed, nessun ricalcolo
        if (m_status == StationStatus::Completed && currentFp == m_lastFingerprint && m_lastFingerprint != 0) {
            return true;
        }

        // 2. Se non sono presenti layer nel Draft, la stazione completa con successo senza alterare o fabbricare layer vuoti
        if (context.draft.paintLayers.empty()) {
            m_compositeAlbedo.Clear();
            m_lastRevision = context.draft.manifest.revision;
            m_lastFingerprint = currentFp;
            m_status = StationStatus::Completed;
            context.draft.completedStationsMask |= (1u << static_cast<uint8_t>(ProductionStationID::Painter));
            return true;
        }

        // 3. Verifica presenza UVLayout valido quando sono presenti layer di pittura
        if (!context.draft.uvLayout.has_value() || !context.draft.uvLayout->isValid) {
            m_diagnostic.warnings.push_back("UVLayout non disponibile o non valido nel draft: pittura preservata senza compositing 3D.");
        }

        // 4. Validazione di integrità dei layer di pittura esistenti
        uint32_t expectedW = context.draft.paintLayers.front().pixelBuffer.GetWidth();
        uint32_t expectedH = context.draft.paintLayers.front().pixelBuffer.GetHeight();

        for (size_t i = 0; i < context.draft.paintLayers.size(); ++i) {
            const auto& layer = context.draft.paintLayers[i];
            if (!layer.pixelBuffer.IsValid()) {
                m_diagnostic.errors.push_back("PaintLayer #" + std::to_string(i) + " ('" + layer.name + "'): buffer pixel non valido o vuoto.");
                m_status = StationStatus::Failed;
                return false;
            }
            // Controllo rigoroso dimensioni: impedire compositing con layer eterogenei che corromperebbero l'output
            if (layer.pixelBuffer.GetWidth() != expectedW || layer.pixelBuffer.GetHeight() != expectedH) {
                m_diagnostic.errors.push_back("PaintLayer #" + std::to_string(i) + " ('" + layer.name + "'): dimensioni non uniformi con gli altri layer (" +
                    std::to_string(layer.pixelBuffer.GetWidth()) + "x" + std::to_string(layer.pixelBuffer.GetHeight()) + " vs " +
                    std::to_string(expectedW) + "x" + std::to_string(expectedH) + "). Compositing bloccato per prevenire output corrotto.");
                m_status = StationStatus::Failed;
                return false;
            }
            if (layer.opacity < 0.0f || layer.opacity > 1.0f) {
                m_diagnostic.warnings.push_back("PaintLayer #" + std::to_string(i) + " ('" + layer.name + "'): opacita fuori scala [0, 1]. Clamp applicato.");
            }
        }

        // 5. Compositing non distruttivo attraverso il backend TexturePaintingService
        TexturePaintingService painter;
        painter.InitializeCanvas(expectedW > 0 ? expectedW : 512, expectedH > 0 ? expectedH : 512);
        painter.GetLayers() = context.draft.paintLayers;

        ImageBitmap compositeResult;
        bool compositeOk = painter.CompositeLayers(compositeResult);
        if (!compositeOk) {
            m_diagnostic.errors.push_back("Errore durante il compositing dei layer di pittura.");
            m_status = StationStatus::Failed;
            return false;
        }

        // Memorizzazione del risultato compositato per il successivo consumo da parte del packaging / baking
        m_compositeAlbedo = std::move(compositeResult);

        m_lastRevision = context.draft.manifest.revision;
        m_lastFingerprint = currentFp;
        m_status = StationStatus::Completed;
        context.draft.completedStationsMask |= (1u << static_cast<uint8_t>(ProductionStationID::Painter));
        return true;
    }

    void Invalidate() override { 
        m_status = StationStatus::Dirty; 
        m_lastFingerprint = 0;
        m_compositeAlbedo.Clear();
    }

    void Reset() override { 
        m_status = StationStatus::Uninitialized; 
        m_diagnostic.Clear(); 
        m_lastRevision = 0; 
        m_lastFingerprint = 0;
        m_compositeAlbedo.Clear();
    }

    // Accesso all'artefatto compositato per Packaging e Baking (BM-F2B)
    const ImageBitmap& GetCompositeAlbedo() const { return m_compositeAlbedo; }
    bool HasCompositeAlbedo() const { return m_compositeAlbedo.IsValid(); }

private:
    StationStatus m_status = StationStatus::Uninitialized;
    StationDiagnostic m_diagnostic;
    uint64_t m_lastRevision = 0;
    uint64_t m_lastFingerprint = 0;
    ImageBitmap m_compositeAlbedo;
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

        // 3. Verifica di conformità geometrica della mesh Sculpt (se presente)
        if (context.draft.sculptMesh.has_value()) {
            const auto& sMesh = *context.draft.sculptMesh;
            auto sReport = sMesh.ValidateTopology();
            if (!sReport.isValid) {
                for (const auto& err : sReport.errors) {
                    m_diagnostic.errors.push_back("QC Sculpt Topology: " + err);
                }
            }
            for (const auto& w : sReport.warnings) {
                m_diagnostic.warnings.push_back("QC Sculpt Warning: " + w);
            }
            // Controllo coordinate degenerate (NaN / Inf)
            for (auto vId : sMesh.GetActiveVertexIDs()) {
                const auto* v = sMesh.GetVertex(vId);
                if (v && (std::isnan(v->position.x) || std::isnan(v->position.y) || std::isnan(v->position.z) ||
                          std::isinf(v->position.x) || std::isinf(v->position.y) || std::isinf(v->position.z))) {
                    m_diagnostic.errors.push_back("QC Sculpt Vertice Degenere: coordinate NaN o Inf rilevate.");
                    break;
                }
            }
        }

        // 4. Verifica di integrità del pacchetto Texture PBR (se presente)
        if (context.draft.textures.has_value()) {
            auto texReport = context.draft.textures->Validate();
            if (!texReport.isValid) {
                for (const auto& err : texReport.errors) {
                    m_diagnostic.errors.push_back("QC Texture PBR: " + err);
                }
            }
            for (const auto& w : texReport.warnings) {
                m_diagnostic.warnings.push_back("QC Texture Warning: " + w);
            }
        }

        // 5. Verifica dei Layer di Pittura (BM-TX3, se presenti)
        if (!context.draft.paintLayers.empty()) {
            for (size_t i = 0; i < context.draft.paintLayers.size(); ++i) {
                const auto& l = context.draft.paintLayers[i];
                if (l.opacity < 0.0f || l.opacity > 1.0f) {
                    m_diagnostic.warnings.push_back("QC PaintLayer #" + std::to_string(i) + " (" + l.name + "): opacita fuori scala [0, 1].");
                }
                if (!l.pixelBuffer.IsValid()) {
                    m_diagnostic.errors.push_back("QC PaintLayer #" + std::to_string(i) + " (" + l.name + "): buffer pixel non valido.");
                }
            }
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

        GeometryCompileResult compRes;
        if (context.draft.geometrySource == GeometrySource::SculptedMesh) {
            if (!context.draft.sculptMesh.has_value() || context.draft.sculptMesh->IsEmpty() || context.draft.sculptMesh->GetFaceCount() == 0) {
                m_diagnostic.errors.push_back("Geometria Sculpt non valida o assente nel draft.");
                m_status = StationStatus::Failed;
                return false;
            }
            compRes = GeometryCompiler::CompileEditableMesh(*context.draft.sculptMesh, opts);
        } else {
            compRes = GeometryCompiler::Compile(context.draft.shape, opts);
        }

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
