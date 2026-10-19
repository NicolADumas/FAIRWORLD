#include "pch.h"
#include "AssetIntegrityRoundTripTests.h"
#include "FactoryOrchestrator.h"
#include "AssetPackagingService.h"
#include "TextureBakingService.h"
#include "blocks/BlockRegistry.h"
#include "blocks/MaterialRegistry.h"
#include "blocks/ShapeRegistry.h"
#include "blocks/sculpt/MeshBridgeService.h"
#include "blocks/sculpt/EditableMesh.h"
#include "world/GeometryCompiler.h"

// Pre-existing suites for INT-10 regression check
#include "blocks/uv/CompiledTopologyTests.h"
#include "PainterStationContractTests.h"
#include "TextureBakingContractTests.h"
#include "AssetPackagingContractTests.h"
#include "TransactionIntegrityAuditTests.h"
#include "SculptedMeshRuntimeBridgeTests.h"
#include "TestRunContext.h"

#include <filesystem>
#include <fstream>
#include <cmath>

namespace fw {

std::vector<AssetIntegrityTestResult> AssetIntegrityRoundTripTests::RunAllTests() {
    std::vector<AssetIntegrityTestResult> results;
    results.reserve(10);

    const std::string testRoot = (std::filesystem::temp_directory_path() / "fairworld_int_audit").string();
    std::error_code ec;
    std::filesystem::create_directories(testRoot, ec);
    std::filesystem::create_directories(testRoot + "/textures/blocks", ec);

    // -------------------------------------------------------------------------
    // INT-01: Asset primitivo completo
    // -------------------------------------------------------------------------
    {
        AssetIntegrityTestResult r;
        r.testId = "INT-01";
        r.testName = "Asset primitivo completo";
        r.inputDesc = "Pipeline completa per asset parametrico (Cylinder): Draft -> Package -> Save -> Load -> Publish";
        r.expectedDesc = "Package serializzato valido, geometrySource == Primitive, ShapeType == Cylinder nei registri";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:int_cylinder", "Primitive Cylinder");
        auto* draft = orch.GetCurrentDraft();
        draft->shape.type = ShapeType::Cylinder;
        draft->shape.parameters = ShapeParamsCylinder{ 0.5f, 1.5f };
        draft->shape.supportsVisualCompilation = true;
        draft->geometrySource = GeometrySource::Primitive;
        draft->sculptMesh.reset();
        draft->physics.mass = 2.5f;
        draft->baseColorFallback = glm::vec3(0.2f, 0.7f, 0.3f);
        draft->completedStationsMask = 0x1F;

        auto pkgOpt = orch.BuildPackage(&sReg, &mReg, &bReg);
        bool buildOk = pkgOpt.has_value() &&
                       (pkgOpt->geometrySource == GeometrySource::Primitive) &&
                       (pkgOpt->shape.type == ShapeType::Cylinder) &&
                       pkgOpt->sculptTopologyJson.empty() &&
                       (pkgOpt->distributionChecksum != 0);

        const std::string pkgPath = testRoot + "/int_cylinder.rawpkg";
        bool saveOk = buildOk && pkgOpt->SaveToFile(pkgPath);

        RawAssetPackage loadedPkg;
        bool loadOk = saveOk && loadedPkg.LoadFromFile(pkgPath);

        AssetPackagingService packaging;
        packaging.SetRootAssetDirectory(testRoot);
        auto pubRes = packaging.PublishPackageTransactional(loadedPkg, bReg, mReg, sReg);

        bool pubOk = pubRes.success && (pubRes.targetVoxelId != 0);
        bool regOk = false;
        if (pubOk) {
            const auto& bDef = bReg.GetBlock(pubRes.targetVoxelId);
            const auto& mDef = mReg.GetMaterial(pubRes.targetVoxelId);
            const auto* sDef = sReg.GetShapeDef(mDef.sharedShape);
            regOk = (bDef.stringId == "fairworld:int_cylinder") &&
                    (bDef.physics.mass == 2.5f) &&
                    (sDef != nullptr) && (sDef->type == ShapeType::Cylinder);
        }

        r.passed = buildOk && saveOk && loadOk && pubOk && regOk;
        r.observedDesc = "BuildOk: " + std::string(buildOk ? "true" : "false") +
                         ", SaveLoadOk: " + std::string(loadOk ? "true" : "false") +
                         ", Published: " + std::string(pubOk ? "true" : "false") +
                         ", ShapeCylinderInRegistry: " + std::string(regOk ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Pipeline end-to-end per asset primitivo fallita";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // INT-02: Asset scolpito completo
    // -------------------------------------------------------------------------
    {
        AssetIntegrityTestResult r;
        r.testId = "INT-02";
        r.testName = "Asset scolpito completo";
        r.inputDesc = "Pipeline completa per asset scolpito (Cube deformato): Draft -> Compile -> Package -> Save -> Load -> Publish";
        r.expectedDesc = "geometrySource == SculptedMesh, ShapeType == TriangleMesh nei registri, DCEL ricostruibile";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:int_sculpt", "Sculpted Asset");
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        auto vIds = mesh.GetActiveVertexIDs();
        mesh.SetVertexPosition(vIds[0], glm::vec3(0.5f, 2.8f, 0.5f));
        mesh.SetVertexPosition(vIds[1], glm::vec3(-0.5f, 2.2f, 0.5f));
        orch.SetDraftSculptMesh(mesh);

        auto* draft = orch.GetCurrentDraft();
        draft->physics.mass = 4.0f;
        draft->completedStationsMask = 0x1F;

        auto pkgOpt = orch.BuildPackage(&sReg, &mReg, &bReg);
        bool buildOk = pkgOpt.has_value() &&
                       (pkgOpt->geometrySource == GeometrySource::SculptedMesh) &&
                       (pkgOpt->shape.type == ShapeType::TriangleMesh) &&
                       !pkgOpt->sculptTopologyJson.empty() &&
                       (pkgOpt->geometry.bounds.max.y >= 2.8f);

        const std::string pkgPath = testRoot + "/int_sculpt.rawpkg";
        bool saveOk = buildOk && pkgOpt->SaveToFile(pkgPath);

        RawAssetPackage loadedPkg;
        bool loadOk = saveOk && loadedPkg.LoadFromFile(pkgPath);

        EditableMesh reconMesh;
        std::string reconErr;
        bool reconOk = loadOk && MeshBridgeService::ReconstructMeshFromPackage(loadedPkg, reconMesh, &reconErr);

        AssetPackagingService packaging;
        packaging.SetRootAssetDirectory(testRoot);
        auto pubRes = packaging.PublishPackageTransactional(loadedPkg, bReg, mReg, sReg);

        bool pubOk = pubRes.success && (pubRes.targetVoxelId != 0);
        bool shapeOk = false;
        if (pubOk) {
            const auto& mDef = mReg.GetMaterial(pubRes.targetVoxelId);
            const auto* sDef = sReg.GetShapeDef(mDef.sharedShape);
            shapeOk = (sDef != nullptr) && (sDef->type == ShapeType::TriangleMesh);
        }

        r.passed = buildOk && saveOk && loadOk && reconOk && pubOk && shapeOk;
        r.observedDesc = "BuildOk: " + std::string(buildOk ? "true" : "false") +
                         ", ReconstructDCEL: " + std::string(reconOk ? "true" : "false") +
                         ", Published: " + std::string(pubOk ? "true" : "false") +
                         ", ShapeIsTriangleMesh: " + std::string(shapeOk ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Pipeline end-to-end per asset scolpito fallita";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // INT-03: Asset con Albedo baked
    // -------------------------------------------------------------------------
    {
        AssetIntegrityTestResult r;
        r.testId = "INT-03";
        r.testName = "Asset con Albedo baked";
        r.inputDesc = "Asset con Layer pittorico baked su PNG atomico persistente";
        r.expectedDesc = "File PNG valido con magic bytes conformi, URI relativo portabile, validazione 100% PASS";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        FactoryOrchestrator orch;
        orch.GetTextureBakingService().SetBaseDirectory(testRoot);
        orch.GetAssetPackagingService().SetRootAssetDirectory(testRoot);
        orch.OpenDraft("fairworld:int_baked_albedo", "Albedo Asset");

        auto* draft = orch.GetCurrentDraft();
        draft->shape.type = ShapeType::Cube;
        draft->shape.supportsVisualCompilation = true;

        // Configura layer pittorico
        PaintLayer layer;
        layer.name = "BaseAlbedo";
        layer.isVisible = true;
        layer.opacity = 1.0f;
        layer.blendMode = PaintLayerBlendMode::Normal;
        layer.pixelBuffer = ImageBitmap(16, 16, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB);
        for (uint32_t y = 0; y < 16; ++y) {
            for (uint32_t x = 0; x < 16; ++x) {
                layer.pixelBuffer.SetPixel(x, y, glm::vec4(200.0f / 255.0f, 100.0f / 255.0f, 50.0f / 255.0f, 1.0f));
            }
        }
        draft->paintLayers.push_back(layer);
        draft->completedStationsMask = 0x1F;

        bool painterOk = orch.ExecuteStation(ProductionStationID::Painter, &sReg, &mReg, &bReg);
        auto bakeRes = orch.BakeCurrentDraftAlbedo(false);
        bool bakeOk = painterOk && bakeRes.success && (bakeRes.errorCode == BakingErrorCode::None);

        auto pkgOpt = orch.BuildPackage(&sReg, &mReg, &bReg);
        bool buildOk = pkgOpt.has_value() && !pkgOpt->albedoMapUri.empty();

        AssetPackagingService packaging;
        packaging.SetRootAssetDirectory(testRoot);
        auto valReport = packaging.ValidatePackage(*pkgOpt, testRoot);
        bool valOk = valReport.isValid && valReport.errors.empty();

        auto pubRes = packaging.PublishPackageTransactional(*pkgOpt, bReg, mReg, sReg, testRoot);
        bool pubOk = pubRes.success;

        bool matOk = false;
        if (pubOk) {
            const auto& mDef = mReg.GetMaterial(pubRes.targetVoxelId);
            matOk = (mDef.albedoPath == pkgOpt->albedoMapUri);
        }

        r.passed = painterOk && bakeOk && buildOk && valOk && pubOk && matOk;
        r.observedDesc = "PainterOk: " + std::string(painterOk ? "true" : "false") +
                         ", BakeOk: " + std::string(bakeOk ? "true" : "false") +
                         ", RelativeUri: " + (pkgOpt ? pkgOpt->albedoMapUri : "None") +
                         ", ValidatePackage: " + std::string(valOk ? "true" : "false") +
                         ", Published: " + std::string(pubOk ? "true" : "false");
        if (r.passed) {
            r.details = "PASS";
        } else if (!painterOk) {
            r.details = "FAIL: Inizializzazione ed esecuzione Painter Station fallita";
        } else if (!bakeOk) {
            r.details = "FAIL: Integrità artefatto Albedo baked non garantita";
        } else if (!buildOk) {
            r.details = "FAIL: Creazione package con artefatto Albedo fallita";
        } else if (!valOk) {
            std::string errDetail = valReport.errors.empty() ? "" : (" (" + valReport.errors.front() + ")");
            r.details = "FAIL: Validazione package fallita" + errDetail;
        } else {
            r.details = "FAIL: Pubblicazione o binding materiale Albedo fallito";
        }
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // INT-04: Save/Reload con registri ricreati
    // -------------------------------------------------------------------------
    {
        AssetIntegrityTestResult r;
        r.testId = "INT-04";
        r.testName = "Save/Reload con registri ricreati";
        r.inputDesc = "Package salvato su file, chiusura runtime e ripubblicazione in registri creati ex-novo";
        r.expectedDesc = "Package ricaricato si pubblica perfettamente su registri fresh senza puntatori orfani";

        const std::string pkgPath = testRoot + "/int_fresh_test.rawpkg";
        {
            // Sessione A: crea e salva
            ShapeRegistry sA; sA.Initialize();
            BlockRegistry bA; bA.Initialize();
            MaterialRegistry mA; mA.Initialize();

            FactoryOrchestrator orchA;
            orchA.OpenDraft("fairworld:int_fresh_block", "Fresh Reboot Asset");
            auto* draft = orchA.GetCurrentDraft();
            draft->shape.type = ShapeType::Cube;
            draft->physics.mass = 5.5f;
            draft->completedStationsMask = 0x1F;

            auto pkg = orchA.BuildPackage(&sA, &mA, &bA);
            pkg->SaveToFile(pkgPath);
        }

        // Sessione B: Fresh runtime registri
        ShapeRegistry sFresh; sFresh.Initialize();
        BlockRegistry bFresh; bFresh.Initialize();
        MaterialRegistry mFresh; mFresh.Initialize();

        RawAssetPackage loadedPkg;
        bool loadOk = loadedPkg.LoadFromFile(pkgPath);

        AssetPackagingService packaging;
        packaging.SetRootAssetDirectory(testRoot);
        auto pubRes = packaging.PublishPackageTransactional(loadedPkg, bFresh, mFresh, sFresh);

        bool pubOk = pubRes.success && (pubRes.targetVoxelId != 0);
        bool matchOk = false;
        if (pubOk) {
            const auto& b = bFresh.GetBlock(pubRes.targetVoxelId);
            matchOk = (b.stringId == "fairworld:int_fresh_block") && (b.physics.mass == 5.5f);
        }

        r.passed = loadOk && pubOk && matchOk;
        r.observedDesc = "LoadOk: " + std::string(loadOk ? "true" : "false") +
                         ", PublishedFresh: " + std::string(pubOk ? "true" : "false") +
                         ", MatchedAttributes: " + std::string(matchOk ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Ripubblicazione post-ricreazione runtime fallita";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // INT-05: Identità e revisioni persistenti
    // -------------------------------------------------------------------------
    {
        AssetIntegrityTestResult r;
        r.testId = "INT-05";
        r.testName = "Identità e revisioni persistenti";
        r.inputDesc = "Pubblicazione rev 1, modifica parametri fisici e ripubblicazione rev 2";
        r.expectedDesc = "Stesso targetVoxelId riutilizzato senza duplicazione slot, revisione incrementata";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:int_identity_check", "Identity Asset");
        auto* draft = orch.GetCurrentDraft();
        draft->manifest.revision = 1;
        draft->shape.type = ShapeType::Cube;
        draft->physics.mass = 1.0f;
        draft->completedStationsMask = 0x1F;

        // Prima pubblicazione
        bool pub1 = orch.PublishToRegistries(bReg, mReg, sReg);
        uint8_t id1 = bReg.GetBlockIdByString("fairworld:int_identity_check");
        uint32_t bCount1 = bReg.GetActiveBlockCount();

        // Modifica revisione e massa
        draft->manifest.revision = 2;
        draft->physics.mass = 8.0f;
        draft->completedStationsMask = 0x1F;
        orch.InvalidateGeometryCache();

        // Seconda pubblicazione
        bool pub2 = orch.PublishToRegistries(bReg, mReg, sReg);
        uint8_t id2 = bReg.GetBlockIdByString("fairworld:int_identity_check");
        uint32_t bCount2 = bReg.GetActiveBlockCount();

        bool stableId = pub1 && pub2 && (id1 == id2) && (id1 != 0);
        bool noDuplicateSlots = (bCount1 == bCount2);
        bool massUpdated = (bReg.GetBlock(id1).physics.mass == 8.0f);

        r.passed = stableId && noDuplicateSlots && massUpdated;
        r.observedDesc = "Id1: " + std::to_string(id1) + " == Id2: " + std::to_string(id2) +
                         ", BlockCounts: " + std::to_string(bCount1) + " == " + std::to_string(bCount2) +
                         ", MassUpdated: " + std::string(massUpdated ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Perdita di identità dello slot durante l'aggiornamento dell'asset";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // INT-06: Texture mancante o corrotta
    // -------------------------------------------------------------------------
    {
        AssetIntegrityTestResult r;
        r.testId = "INT-06";
        r.testName = "Texture mancante o corrotta";
        r.inputDesc = "Package che referenzia file texture non esistente e file con header PNG corrotto";
        r.expectedDesc = "ValidatePackage rileva l'errore di magic bytes/assenza e abortisce la pubblicazione";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        AssetPackagingService packaging;
        packaging.SetRootAssetDirectory(testRoot);

        // Caso A: Texture mancante
        RawAssetPackage pkgMissing;
        pkgMissing.manifest.assetId = "fairworld:int_missing_tex";
        pkgMissing.manifest.stage = RawAssetStage::Packaged;
        pkgMissing.albedoMapUri = "textures/blocks/missing_tex.png";
        pkgMissing.geometry.vertices.resize(24);
        pkgMissing.geometry.vertexCount = 24;
        auto valMissing = packaging.ValidatePackage(pkgMissing, testRoot);
        bool missingCaught = !valMissing.isValid;

        // Caso B: Texture con header corrotto (non è un PNG)
        const std::string corruptRel = "textures/blocks/corrupt_file.png";
        const std::string corruptFull = testRoot + "/" + corruptRel;
        {
            std::ofstream f(corruptFull, std::ios::binary);
            f << "CORRUPT_NOT_A_VALID_PNG_HEADER_DATA_12345678";
        }
        RawAssetPackage pkgCorrupt;
        pkgCorrupt.manifest.assetId = "fairworld:int_corrupt_tex";
        pkgCorrupt.manifest.stage = RawAssetStage::Packaged;
        pkgCorrupt.albedoMapUri = corruptRel;
        pkgCorrupt.geometry.vertices.resize(24);
        pkgCorrupt.geometry.vertexCount = 24;
        auto valCorrupt = packaging.ValidatePackage(pkgCorrupt, testRoot);
        bool corruptCaught = !valCorrupt.isValid;

        // Verifica che la pubblicazione abortisca senza alterare i registri
        uint32_t bCount0 = bReg.GetActiveBlockCount();
        auto pubRes = packaging.PublishPackageTransactional(pkgCorrupt, bReg, mReg, sReg, testRoot);
        uint32_t bCount1 = bReg.GetActiveBlockCount();
        bool untouched = !pubRes.success && (bCount0 == bCount1);

        r.passed = missingCaught && corruptCaught && untouched;
        r.observedDesc = "MissingCaught: " + std::string(missingCaught ? "true" : "false") +
                         ", CorruptCaught: " + std::string(corruptCaught ? "true" : "false") +
                         ", RegistriesUntouched: " + std::string(untouched ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Texture corrotta o assente non intercettata!";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // INT-07: Invalidazione cache su modifica UV/materiali
    // -------------------------------------------------------------------------
    {
        AssetIntegrityTestResult r;
        r.testId = "INT-07";
        r.testName = "Invalidazione cache su modifica UV/materiali";
        r.inputDesc = "Compilazione package, verifica Zero-Work e successiva modifica di UV/superfici";
        r.expectedDesc = "Cache hit su input identico; ricompilazione immediata al cambio di uvRevision o SurfaceOverride";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        FactoryOrchestrator orch;
        orch.ResetGeometryCompileCallCount();
        orch.OpenDraft("fairworld:int_cache_uv", "Cache Invalidation Asset");
        auto* draft = orch.GetCurrentDraft();
        draft->shape.type = ShapeType::Cube;
        draft->shape.supportsVisualCompilation = true;
        draft->completedStationsMask = 0x1F;

        // Compilazione 1
        auto pkg1 = orch.BuildPackage(&sReg, &mReg, &bReg);
        uint64_t c1 = orch.GetGeometryCompileCallCount();

        // Compilazione 2 (identica -> Zero-Work)
        auto pkg2 = orch.BuildPackage(&sReg, &mReg, &bReg);
        uint64_t c2 = orch.GetGeometryCompileCallCount();
        bool zeroWorkHit = (c1 == 1) && (c2 == 1);

        // Modifica UV / superfici
        draft->uvRevision++;
        SurfaceOverride so;
        so.surfaceId = 2;
        so.useExplicitMaterial = true;
        so.materialId = 7;
        draft->surfaces.push_back(so);

        // Compilazione 3 (deve invalidare e ricompilare)
        auto pkg3 = orch.BuildPackage(&sReg, &mReg, &bReg);
        uint64_t c3 = orch.GetGeometryCompileCallCount();
        bool invalidationOk = (c3 == 2);

        r.passed = zeroWorkHit && invalidationOk;
        r.observedDesc = "C1=" + std::to_string(c1) +
                         ", C2_Unchanged=" + std::to_string(c2) +
                         ", C3_UVModified=" + std::to_string(c3);
        r.details = r.passed ? "PASS" : "FAIL: Mancata invalidazione della cache su modifica UV/superfici";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // INT-08: ShapeHandle condivisi e rollback sicuro
    // -------------------------------------------------------------------------
    {
        AssetIntegrityTestResult r;
        r.testId = "INT-08";
        r.testName = "ShapeHandle condivisi e rollback sicuro";
        r.inputDesc = "Due asset condividono la stessa Shape in ShapeRegistry; Asset B fallisce e fa rollback";
        r.expectedDesc = "Rollback di Asset B NON cancella la forma condivisa referenziata ancora da Asset A";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        AssetPackagingService packaging;
        packaging.SetRootAssetDirectory(testRoot);

        // Pubblica Asset A con successo
        RawAssetPackage pkgA;
        pkgA.manifest.assetId = "fairworld:shared_asset_a";
        pkgA.manifest.stage = RawAssetStage::Packaged;
        pkgA.shape.type = ShapeType::Sphere;
        pkgA.shape.parameters = ShapeParamsSphere{ 1.0f };
        pkgA.shape.supportsVisualCompilation = true;
        pkgA.geometry.vertices.resize(24);
        pkgA.geometry.vertexCount = 24;

        auto resA = packaging.PublishPackageTransactional(pkgA, bReg, mReg, sReg);
        bool pubAOk = resA.success;
        ShapeHandle sharedHandle = mReg.GetMaterial(resA.targetVoxelId).sharedShape;
        bool shapeExistsA = sharedHandle.IsValid() && (sReg.GetShapeDef(sharedHandle) != nullptr);

        // Prepara Asset B che usa la STESSA forma condivisa ma subisce fault injection post-mutazione
        RawAssetPackage pkgB;
        pkgB.manifest.assetId = "fairworld:shared_asset_b";
        pkgB.manifest.stage = RawAssetStage::Packaged;
        pkgB.shape = *sReg.GetShapeDef(sharedHandle); // Stessa forma
        pkgB.geometry.vertices.resize(24);
        pkgB.geometry.vertexCount = 24;

        packaging.SetTestFaultInjection(PublishFaultInjectionPoint::FailAfterMaterialMutation);
        auto resB = packaging.PublishPackageTransactional(pkgB, bReg, mReg, sReg);
        bool bRolledBack = !resB.success && resB.wasRolledBack;

        // Reset fault injection
        packaging.SetTestFaultInjection(PublishFaultInjectionPoint::None);

        // VERIFICA CRUCIALE: la forma in sReg deve esistere ancora!
        const auto* sDefAfter = sReg.GetShapeDef(sharedHandle);
        bool shapeSurvives = (sDefAfter != nullptr) && (mReg.CountShapeUsers(sharedHandle.id) >= 1);

        r.passed = pubAOk && shapeExistsA && bRolledBack && shapeSurvives;
        r.observedDesc = "AssetAPublished: " + std::string(pubAOk ? "true" : "false") +
                         ", AssetBRolledBack: " + std::string(bRolledBack ? "true" : "false") +
                         ", SharedShapeStillIntactInRegistry: " + std::string(shapeSurvives ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Rollback ha orfanizzato la forma condivisa di altri asset!";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // INT-09: Input invariato = Zero-Work
    // -------------------------------------------------------------------------
    {
        AssetIntegrityTestResult r;
        r.testId = "INT-09";
        r.testName = "Input invariato = Zero-Work";
        r.inputDesc = "Due chiamate a PublishPackageTransactional con lo stesso RawAssetPackage";
        r.expectedDesc = "Seconda chiamata rileva Zero-Work hit senza allocare nuovi blocchi o forme";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        AssetPackagingService packaging;
        packaging.SetRootAssetDirectory(testRoot);

        RawAssetPackage pkg;
        pkg.manifest.assetId = "fairworld:int_zero_work";
        pkg.manifest.stage = RawAssetStage::Packaged;
        pkg.shape.type = ShapeType::Cube;
        pkg.shape.supportsVisualCompilation = true;
        pkg.geometry.vertices.resize(24);
        pkg.geometry.vertexCount = 24;
        pkg.packageFingerprint = 987654321ULL;

        auto res1 = packaging.PublishPackageTransactional(pkg, bReg, mReg, sReg);
        uint32_t bCount1 = bReg.GetActiveBlockCount();

        auto res2 = packaging.PublishPackageTransactional(pkg, bReg, mReg, sReg);
        uint32_t bCount2 = bReg.GetActiveBlockCount();

        bool ok = res1.success && res2.success && res2.wasZeroWorkHit &&
                  (res1.targetVoxelId == res2.targetVoxelId) && (bCount1 == bCount2);

        r.passed = ok;
        r.observedDesc = "Pass1Ok: " + std::string(res1.success ? "true" : "false") +
                         ", Pass2ZeroWorkHit: " + std::string(res2.wasZeroWorkHit ? "true" : "false") +
                         ", BlockCountStable: " + std::to_string(bCount1) + "==" + std::to_string(bCount2);
        r.details = r.passed ? "PASS" : "FAIL: Zero-Work bypass mancato per input identico";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // INT-10: Nessuna regressione dei 201 test precedenti
    // -------------------------------------------------------------------------
    {
        AssetIntegrityTestResult r;
        r.testId = "INT-10";
        r.testName = "Nessuna regressione dei 201 test precedenti";
        r.inputDesc = "Esecuzione di TOP(36), PNT(10), TXB(10), PKG(10), TRX(10), SBR(10)";
        r.expectedDesc = "Tutti gli 86 test delle suite sopra menzionate risultano PASS al 100%";

        if (TestRunContext::Instance().IsActive()) {
            static const std::vector<std::pair<std::string, size_t>> deps = {
                {"TOP", 36}, {"PNT", 10}, {"TXB", 10}, {"PKG", 10}, {"TRX", 10}, {"SBR", 10}
            };
            auto eval = TestRunContext::Instance().EvaluateRegressionGate(deps);
            size_t topPass = 0, pntPass = 0, txbPass = 0, pkgPass = 0, trxPass = 0, sbrPass = 0;
            for (const auto& c : eval.checks) {
                if (c.suiteCode == "TOP") topPass = c.passedCount;
                if (c.suiteCode == "PNT") pntPass = c.passedCount;
                if (c.suiteCode == "TXB") txbPass = c.passedCount;
                if (c.suiteCode == "PKG") pkgPass = c.passedCount;
                if (c.suiteCode == "TRX") trxPass = c.passedCount;
                if (c.suiteCode == "SBR") sbrPass = c.passedCount;
            }
            r.passed = eval.allDependenciesPassed;
            r.observedDesc = "TOP: " + std::to_string(topPass) + "/36, PNT: " + std::to_string(pntPass) + "/10, TXB: " + std::to_string(txbPass) + "/10, PKG: " + std::to_string(pkgPass) + "/10, TRX: " + std::to_string(trxPass) + "/10, SBR: " + std::to_string(sbrPass) + "/10";
            r.details = r.passed ? "PASS" : "FAIL: " + eval.failureDetails;
        } else {
            auto topRes = CompiledTopologyTests::RunAllTests();
            auto pntRes = PainterStationContractTests::RunAllTests();
            auto txbRes = TextureBakingContractTests::RunAllTests();
            auto pkgRes = AssetPackagingContractTests::RunAllTests();
            auto trxRes = TransactionIntegrityAuditTests::RunAllTests();
            auto sbrRes = SculptedMeshRuntimeBridgeTests::RunAllTests();

            size_t topPass = 0; for (const auto& tr : topRes) if (tr.passed) topPass++;
            size_t pntPass = 0; for (const auto& pr : pntRes) if (pr.passed) pntPass++;
            size_t txbPass = 0; for (const auto& xr : txbRes) if (xr.passed) txbPass++;
            size_t pkgPass = 0; for (const auto& kr : pkgRes) if (kr.passed) pkgPass++;
            size_t trxPass = 0; for (const auto& rr : trxRes) if (rr.passed) trxPass++;
            size_t sbrPass = 0; for (const auto& sr : sbrRes) if (sr.passed) sbrPass++;

            bool allPass = (topPass == topRes.size() && topRes.size() == 36) &&
                           (pntPass == pntRes.size() && pntRes.size() == 10) &&
                           (txbPass == txbRes.size() && txbRes.size() == 10) &&
                           (pkgPass == pkgRes.size() && pkgRes.size() == 10) &&
                           (trxPass == trxRes.size() && trxRes.size() == 10) &&
                           (sbrPass == sbrRes.size() && sbrRes.size() == 10);

            r.passed = allPass;
            r.observedDesc = "TOP: " + std::to_string(topPass) + "/" + std::to_string(topRes.size()) +
                             ", PNT: " + std::to_string(pntPass) + "/" + std::to_string(pntRes.size()) +
                             ", TXB: " + std::to_string(txbPass) + "/" + std::to_string(txbRes.size()) +
                             ", PKG: " + std::to_string(pkgPass) + "/" + std::to_string(pkgRes.size()) +
                             ", TRX: " + std::to_string(trxPass) + "/" + std::to_string(trxRes.size()) +
                             ", SBR: " + std::to_string(sbrPass) + "/" + std::to_string(sbrRes.size());
            r.details = r.passed ? "PASS" : "FAIL: Regressione rilevata nelle suite precedenti";
        }
        results.push_back(r);
    }

    return results;
}

} // namespace fw
