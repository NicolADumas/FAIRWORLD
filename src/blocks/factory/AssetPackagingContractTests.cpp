#include "pch.h"
#include "AssetPackagingContractTests.h"
#include "AssetPackagingService.h"
#include "FactoryOrchestrator.h"
#include "TextureBakingService.h"
#include "TextureBakingContractTests.h"
#include "PainterStationContractTests.h"
#include "blocks/uv/CompiledTopologyTests.h"
#include "TestRunContext.h"
#include "blocks/BlockRegistry.h"
#include "blocks/MaterialRegistry.h"
#include "blocks/ShapeRegistry.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fw {

std::vector<AssetPackagingTestResult> AssetPackagingContractTests::RunAllTests() {
    std::vector<AssetPackagingTestResult> results;
    results.reserve(10);

    const std::string testRoot = "assets/test_packaging_artifacts";
    std::error_code ec;
    std::filesystem::create_directories(testRoot, ec);
    std::filesystem::create_directories(testRoot + "/textures/blocks", ec);

    // -------------------------------------------------------------------------
    // PKG-01: Package completo valido
    // -------------------------------------------------------------------------
    {
        AssetPackagingTestResult r;
        r.testId = "PKG-01";
        r.testName = "Package completo valido";
        r.inputDesc = "BuildPackage su Draft validato con geometria compilata e metadati";
        r.expectedDesc = "Package IsPublishable() == true, vertexCount > 0, fisica compilata presente";

        ShapeRegistry sReg;
        sReg.Initialize();
        MaterialRegistry mReg;
        mReg.Initialize();
        BlockRegistry bReg;
        bReg.Initialize();

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:pkg_test_01", "PKG Test 01");
        orch.ExecuteStation(ProductionStationID::Shape, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::SurfaceUV, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::Material, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::RawPhysics, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::QualityControl, &sReg, &mReg, &bReg);

        auto pkgOpt = orch.BuildPackage(&sReg, &mReg, &bReg);
        bool hasPkg = pkgOpt.has_value();
        bool pub = hasPkg && pkgOpt->IsPublishable();
        bool geomOk = hasPkg && (pkgOpt->geometry.vertexCount > 0) && (pkgOpt->geometry.triangleCount > 0);
        bool physOk = hasPkg && (pkgOpt->compiledPhysics.mass > 0.0f) && (pkgOpt->compiledPhysics.density > 0.0f);

        r.passed = hasPkg && pub && geomOk && physOk;
        r.observedDesc = "HasPackage: " + std::string(hasPkg ? "true" : "false") +
                         ", IsPublishable: " + std::string(pub ? "true" : "false") +
                         ", Vertices: " + (hasPkg ? std::to_string(pkgOpt->geometry.vertexCount) : "0") +
                         ", Triangles: " + (hasPkg ? std::to_string(pkgOpt->geometry.triangleCount) : "0");
        r.details = r.passed ? "PASS" : "FAIL: Creazione o validazione del package fallita";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // PKG-02: URI texture risolto
    // -------------------------------------------------------------------------
    {
        AssetPackagingTestResult r;
        r.testId = "PKG-02";
        r.testName = "URI texture risolto";
        r.inputDesc = "Draft con pittura bake-ata in PNG e verifica risoluzione fisica su disco";
        r.expectedDesc = "Package possiede albedoMapUri relativo e portabile, file esistente e non vuoto su disco";

        ShapeRegistry sReg;
        sReg.Initialize();
        MaterialRegistry mReg;
        mReg.Initialize();
        BlockRegistry bReg;
        bReg.Initialize();

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:pkg_test_02", "PKG Test 02");
        orch.GetTextureBakingService().SetBaseDirectory(testRoot);
        orch.ExecuteStation(ProductionStationID::Shape, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::SurfaceUV, &sReg, &mReg, &bReg);

        auto* draft = orch.GetCurrentDraft();
        PaintLayer layer;
        layer.name = "AlbedoLayer";
        layer.pixelBuffer = ImageBitmap(32, 32, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.2f, 0.7f, 0.3f, 1.0f));
        draft->paintLayers = { layer };

        orch.ExecuteStation(ProductionStationID::Painter, &sReg, &mReg, &bReg);
        auto bakeRes = orch.BakeCurrentDraftAlbedo(true);

        orch.ExecuteStation(ProductionStationID::Material, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::RawPhysics, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::QualityControl, &sReg, &mReg, &bReg);

        auto pkgOpt = orch.BuildPackage(&sReg, &mReg, &bReg);

        bool bakeOk = bakeRes.success;
        bool hasPkg = pkgOpt.has_value();
        bool uriValid = hasPkg && !pkgOpt->albedoMapUri.empty() && TextureBakingService::ValidatePortableUri(pkgOpt->albedoMapUri);

        std::filesystem::path resolvedPath = std::filesystem::path(testRoot) / (hasPkg ? pkgOpt->albedoMapUri : "");
        bool fileExistsOnDisk = std::filesystem::exists(resolvedPath) && (std::filesystem::file_size(resolvedPath) > 0);

        r.passed = bakeOk && hasPkg && uriValid && fileExistsOnDisk;
        r.observedDesc = "AlbedoURI: '" + (hasPkg ? pkgOpt->albedoMapUri : "") +
                         "', ResolvedFileExists: " + std::string(fileExistsOnDisk ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Risoluzione URI texture nel package fallita";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // PKG-03: Save/Reload round-trip
    // -------------------------------------------------------------------------
    {
        AssetPackagingTestResult r;
        r.testId = "PKG-03";
        r.testName = "Save/Reload round-trip";
        r.inputDesc = "Serializzazione JSON di RawAssetPackage su file e rilettura completa";
        r.expectedDesc = "Tutti i campi (manifest, geometria, fisica, URI texture, fingerprint) coincidono esattamente";

        ShapeRegistry sReg;
        sReg.Initialize();
        MaterialRegistry mReg;
        mReg.Initialize();
        BlockRegistry bReg;
        bReg.Initialize();

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:pkg_roundtrip_03", "PKG Roundtrip 03");
        orch.ExecuteStation(ProductionStationID::Shape, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::SurfaceUV, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::Material, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::RawPhysics, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::QualityControl, &sReg, &mReg, &bReg);

        auto pkgOpt = orch.BuildPackage(&sReg, &mReg, &bReg);
        bool hasPkg = pkgOpt.has_value();

        std::string pkgFile = testRoot + "/pkg_roundtrip_03.json";
        bool saveOk = hasPkg && pkgOpt->SaveToFile(pkgFile);

        RawAssetPackage reloaded;
        bool loadOk = saveOk && reloaded.LoadFromFile(pkgFile);

        bool manifestMatch = loadOk && (reloaded.manifest.assetId == pkgOpt->manifest.assetId) &&
                             (reloaded.manifest.revision == pkgOpt->manifest.revision);
        bool geomMatch = loadOk && (reloaded.geometry.vertexCount == pkgOpt->geometry.vertexCount) &&
                         (reloaded.geometry.triangleCount == pkgOpt->geometry.triangleCount);
        bool physMatch = loadOk && (reloaded.compiledPhysics.mass == pkgOpt->compiledPhysics.mass) &&
                         (reloaded.compiledPhysics.density == pkgOpt->compiledPhysics.density);
        bool fpMatch = loadOk && (reloaded.packageFingerprint == pkgOpt->packageFingerprint);

        r.passed = hasPkg && saveOk && loadOk && manifestMatch && geomMatch && physMatch && fpMatch;
        r.observedDesc = "SaveOk: " + std::string(saveOk ? "true" : "false") +
                         ", LoadOk: " + std::string(loadOk ? "true" : "false") +
                         ", ManifestMatch: " + std::string(manifestMatch ? "true" : "false") +
                         ", GeomMatch: " + std::string(geomMatch ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Round-trip di serializzazione/deserializzazione package fallito";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // PKG-04: Registry binding valido
    // -------------------------------------------------------------------------
    {
        AssetPackagingTestResult r;
        r.testId = "PKG-04";
        r.testName = "Registry binding valido";
        r.inputDesc = "Pubblicazione transazionale su registri Shape, Block e Material";
        r.expectedDesc = "Asset registrato correttamente in runtime: SimBlockDef e PBRMaterialDef validi e coerenti";

        ShapeRegistry sReg;
        sReg.Initialize();
        MaterialRegistry mReg;
        mReg.Initialize();
        BlockRegistry bReg;
        bReg.Initialize();

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:pkg_bound_04", "PKG Bound 04");
        orch.ExecuteStation(ProductionStationID::Shape, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::SurfaceUV, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::Material, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::RawPhysics, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::QualityControl, &sReg, &mReg, &bReg);

        bool pubOk = orch.PublishToRegistries(bReg, mReg, sReg);
        const auto* draft = orch.GetCurrentDraft();

        uint8_t targetVoxelId = draft ? draft->manifest.targetRuntimeVoxelId : 0;
        bool voxelValid = (targetVoxelId != 0) && bReg.IsBlockValid(targetVoxelId);

        bool bDefMatch = false;
        bool mDefMatch = false;
        if (voxelValid) {
            const auto& bDef = bReg.GetBlock(targetVoxelId);
            const auto& mDef = mReg.GetMaterial(targetVoxelId);

            bDefMatch = (bDef.stringId == "fairworld:pkg_bound_04") &&
                        (bDef.displayName == "PKG Bound 04") &&
                        (bDef.physics.collisionEnabled == true);

            mDefMatch = (mDef.target_block_id == targetVoxelId) &&
                        mDef.sharedShape.IsValid();
        }

        r.passed = pubOk && voxelValid && bDefMatch && mDefMatch;
        r.observedDesc = "Published: " + std::string(pubOk ? "true" : "false") +
                         ", VoxelId: " + std::to_string(targetVoxelId) +
                         ", BlockMatch: " + std::string(bDefMatch ? "true" : "false") +
                         ", MaterialMatch: " + std::string(mDefMatch ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Binding ai registri runtime invalido o incompleto";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // PKG-05: Asset senza PaintLayers
    // -------------------------------------------------------------------------
    {
        AssetPackagingTestResult r;
        r.testId = "PKG-05";
        r.testName = "Asset senza PaintLayers";
        r.inputDesc = "Asset puramente procedurale/colore fallback senza alcun layer di pittura";
        r.expectedDesc = "Package pubblicato con successo, albedoMapUri vuoto, proprietà visive PBR preservate";

        ShapeRegistry sReg;
        sReg.Initialize();
        MaterialRegistry mReg;
        mReg.Initialize();
        BlockRegistry bReg;
        bReg.Initialize();

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:pkg_nopaint_05", "PKG No Paint 05");
        auto* draft = orch.GetCurrentDraft();
        draft->paintLayers.clear();
        draft->baseColorFallback = glm::vec3(0.9f, 0.6f, 0.2f);
        draft->roughnessFallback = 0.4f;
        draft->metallicFallback = 0.8f;

        orch.ExecuteStation(ProductionStationID::Shape, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::SurfaceUV, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::Painter, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::Material, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::RawPhysics, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::QualityControl, &sReg, &mReg, &bReg);

        bool pubOk = orch.PublishToRegistries(bReg, mReg, sReg);
        uint8_t targetVoxelId = draft ? draft->manifest.targetRuntimeVoxelId : 0;

        bool matOk = false;
        if (pubOk && targetVoxelId != 0 && bReg.IsBlockValid(targetVoxelId)) {
            const auto& mDef = mReg.GetMaterial(targetVoxelId);
            matOk = mDef.albedoPath.empty() &&
                    (mDef.roughnessFallback == 0.4f) &&
                    (mDef.metallicFallback == 0.8f) &&
                    (mDef.baseColorFallback.x == 0.9f);
        }

        r.passed = pubOk && matOk;
        r.observedDesc = "PublishedWithoutPaint: " + std::string(pubOk ? "true" : "false") +
                         ", EmptyAlbedo: " + std::string(matOk ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Asset senza layer di pittura rifiutato o malformato";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // PKG-06: Asset con texture esterna
    // -------------------------------------------------------------------------
    {
        AssetPackagingTestResult r;
        r.testId = "PKG-06";
        r.testName = "Asset con texture esterna";
        r.inputDesc = "Asset configurato con texture esterna esistente su disco";
        r.expectedDesc = "Package valida l'URI relativo, lo risolve e lo associa correttamente a PBRMaterialDef";

        // Creazione fittizia del file texture esterno valido
        std::string extRelUri = "textures/blocks/external_stone_06_albedo.png";
        std::filesystem::path extPhysPath = std::filesystem::path(testRoot) / extRelUri;
        ImageBitmap extBmp(16, 16, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.4f, 0.4f, 0.4f, 1.0f));
        extBmp.SaveToFile(extPhysPath.string());

        ShapeRegistry sReg;
        sReg.Initialize();
        MaterialRegistry mReg;
        mReg.Initialize();
        BlockRegistry bReg;
        bReg.Initialize();

        FactoryOrchestrator orch;
        orch.GetAssetPackagingService().SetRootAssetDirectory(testRoot);
        orch.OpenDraft("fairworld:pkg_external_06", "PKG External 06");
        auto* draft = orch.GetCurrentDraft();
        draft->albedoPath = extRelUri;

        orch.ExecuteStation(ProductionStationID::Shape, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::SurfaceUV, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::Material, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::RawPhysics, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::QualityControl, &sReg, &mReg, &bReg);

        bool pubOk = orch.PublishToRegistries(bReg, mReg, sReg);
        uint8_t targetVoxelId = draft ? draft->manifest.targetRuntimeVoxelId : 0;

        bool uriMatched = false;
        if (pubOk && targetVoxelId != 0 && bReg.IsBlockValid(targetVoxelId)) {
            const auto& mDef = mReg.GetMaterial(targetVoxelId);
            uriMatched = (mDef.albedoPath == extRelUri);
        }

        r.passed = pubOk && uriMatched;
        r.observedDesc = "PublishedWithExternal: " + std::string(pubOk ? "true" : "false") +
                         ", AlbedoPath: '" + (uriMatched ? extRelUri : "mismatch") + "'";
        r.details = r.passed ? "PASS" : "FAIL: Asset con texture esterna non collegato correttamente";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // PKG-07: Fallimento pubblicazione senza stato parziale (Rollback)
    // -------------------------------------------------------------------------
    {
        AssetPackagingTestResult r;
        r.testId = "PKG-07";
        r.testName = "Fallimento pubblicazione senza stato parziale (Rollback)";
        r.inputDesc = "Tentativo di pubblicazione con Texture URI inesistente su disco per innescare validazione failure";
        r.expectedDesc = "Transazione abortisce; nessun blocco orfano allocato in BlockRegistry, registri intatti";

        ShapeRegistry sReg;
        sReg.Initialize();
        MaterialRegistry mReg;
        mReg.Initialize();
        BlockRegistry bReg;
        bReg.Initialize();

        uint32_t blocksBefore = bReg.GetActiveBlockCount();

        RawAssetPackage badPkg;
        badPkg.manifest.assetId = "fairworld:pkg_bad_07";
        badPkg.manifest.displayName = "Bad Package 07";
        badPkg.manifest.stage = RawAssetStage::Validated;
        badPkg.validationReport.isValid = true;
        badPkg.albedoMapUri = "textures/blocks/non_existent_corrupted_file.png";
        badPkg.geometry.vertexCount = 4;
        badPkg.geometry.vertices.resize(4);

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        auto txRes = service.PublishPackageTransactional(badPkg, bReg, mReg, sReg);

        uint32_t blocksAfter = bReg.GetActiveBlockCount();
        bool blockCountUnchanged = (blocksBefore == blocksAfter);
        bool notPublished = (!txRes.success);
        bool noOrphanId = !bReg.IsStringIdTaken("fairworld:pkg_bad_07");

        r.passed = notPublished && blockCountUnchanged && noOrphanId;
        r.observedDesc = "Success: " + std::string(txRes.success ? "true" : "false") +
                         ", Error: '" + txRes.errorMessage + "'" +
                         ", BlocksCount: " + std::to_string(blocksBefore) + " -> " + std::to_string(blocksAfter);
        r.details = r.passed ? "PASS" : "FAIL: Stato parziale orfano lasciato nei registri dopo fallimento";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // PKG-08: Identità asset e collisioni
    // -------------------------------------------------------------------------
    {
        AssetPackagingTestResult r;
        r.testId = "PKG-08";
        r.testName = "Identità asset e collisioni";
        r.inputDesc = "Tentativo di pubblicare secondo asset avente lo stesso stringId su un target slot differente";
        r.expectedDesc = "Collisione rilevata ed esplicitamente respinta con messaggio diagnostico";

        ShapeRegistry sReg;
        sReg.Initialize();
        MaterialRegistry mReg;
        mReg.Initialize();
        BlockRegistry bReg;
        bReg.Initialize();

        // 1. Registra prima risorsa
        bReg.CreateBlock("fairworld:collision_block", "First Block");
        uint8_t firstId = bReg.GetBlockIdByString("fairworld:collision_block");

        // 2. Prepara secondo package con lo stesso stringId ma targetRuntimeVoxelId forzato a slot diverso
        RawAssetPackage conflictPkg;
        conflictPkg.manifest.assetId = "fairworld:collision_block";
        conflictPkg.manifest.displayName = "Conflicting Block";
        conflictPkg.manifest.targetRuntimeVoxelId = firstId + 5; // Slot intenzionalmente in conflitto
        conflictPkg.manifest.stage = RawAssetStage::Validated;
        conflictPkg.validationReport.isValid = true;
        conflictPkg.geometry.vertexCount = 4;
        conflictPkg.geometry.vertices.resize(4);

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        auto txRes = service.PublishPackageTransactional(conflictPkg, bReg, mReg, sReg);

        bool collisionRejected = (!txRes.success) && (txRes.errorMessage.find("Collisione ID") != std::string::npos);

        r.passed = collisionRejected;
        r.observedDesc = "CollisionRejected: " + std::string(collisionRejected ? "true" : "false") +
                         ", Message: '" + txRes.errorMessage + "'";
        r.details = r.passed ? "PASS" : "FAIL: Collisione stringId non rilevata o permessa";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // PKG-09: Input stabile = nessuna ripubblicazione (Zero-Work)
    // -------------------------------------------------------------------------
    {
        AssetPackagingTestResult r;
        r.testId = "PKG-09";
        r.testName = "Input stabile = nessuna ripubblicazione (Zero-Work)";
        r.inputDesc = "Chiamata ripetuta di PublishPackageTransactional con medesimo packageFingerprint";
        r.expectedDesc = "Seconda pubblicazione rileva Zero-Work hit, nessun nuovo slot o mutazione registri";

        ShapeRegistry sReg;
        sReg.Initialize();
        MaterialRegistry mReg;
        mReg.Initialize();
        BlockRegistry bReg;
        bReg.Initialize();

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:pkg_zerowork_09", "PKG ZeroWork 09");
        orch.ExecuteStation(ProductionStationID::Shape, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::SurfaceUV, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::Material, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::RawPhysics, &sReg, &mReg, &bReg);
        orch.ExecuteStation(ProductionStationID::QualityControl, &sReg, &mReg, &bReg);

        auto pkgOpt = orch.BuildPackage(&sReg, &mReg, &bReg);
        bool hasPkg = pkgOpt.has_value();

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);

        // Prima pubblicazione
        auto res1 = service.PublishPackageTransactional(*pkgOpt, bReg, mReg, sReg);
        uint8_t id1 = res1.targetVoxelId;

        // Seconda pubblicazione a input invariato
        auto res2 = service.PublishPackageTransactional(*pkgOpt, bReg, mReg, sReg);
        uint8_t id2 = res2.targetVoxelId;

        bool zeroWorkDetected = res2.success && (id1 == id2) && res2.wasZeroWorkHit;

        r.passed = hasPkg && res1.success && zeroWorkDetected;
        r.observedDesc = "FirstPub: " + std::to_string(id1) + ", SecondPub: " + std::to_string(id2) +
                         ", ZeroWorkHit: " + (res2.wasZeroWorkHit ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Zero-Work publication cache miss su input identico";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // PKG-10: Nessuna regressione delle suite precedenti
    // -------------------------------------------------------------------------
    {
        AssetPackagingTestResult r;
        r.testId = "PKG-10";
        r.testName = "Nessuna regressione delle suite precedenti";
        r.inputDesc = "Esecuzione delle suite pregresse: CompiledTopology (36), Painter (10), TextureBaking (10)";
        r.expectedDesc = "Tutti i 56 test delle suite precedenti risultano superati al 100%";

        if (TestRunContext::Instance().IsActive()) {
            static const std::vector<std::pair<std::string, size_t>> deps = {
                {"TOP", 36}, {"PNT", 10}, {"TXB", 10}
            };
            auto eval = TestRunContext::Instance().EvaluateRegressionGate(deps);
            size_t topPass = 0, pntPass = 0, txbPass = 0;
            for (const auto& c : eval.checks) {
                if (c.suiteCode == "TOP") topPass = c.passedCount;
                if (c.suiteCode == "PNT") pntPass = c.passedCount;
                if (c.suiteCode == "TXB") txbPass = c.passedCount;
            }
            r.passed = eval.allDependenciesPassed;
            r.observedDesc = "TOP: " + std::to_string(topPass) + "/36, PNT: " + std::to_string(pntPass) + "/10, TXB: " + std::to_string(txbPass) + "/10";
            r.details = r.passed ? "PASS" : "FAIL: " + eval.failureDetails;
        } else {
            auto topRes = CompiledTopologyTests::RunAllTests();
            auto pntRes = PainterStationContractTests::RunAllTests();
            auto txbRes = TextureBakingContractTests::RunAllTests();

            size_t topPass = 0;
            for (const auto& tr : topRes) if (tr.passed) topPass++;

            size_t pntPass = 0;
            for (const auto& pr : pntRes) if (pr.passed) pntPass++;

            size_t txbPass = 0;
            for (const auto& xr : txbRes) if (xr.passed) txbPass++;

            bool allTop = (topPass == topRes.size() && topRes.size() == 36);
            bool allPnt = (pntPass == pntRes.size() && pntRes.size() == 10);
            bool allTxb = (txbPass == txbRes.size() && txbRes.size() == 10);

            r.passed = allTop && allPnt && allTxb;
            r.observedDesc = "TOP: " + std::to_string(topPass) + "/" + std::to_string(topRes.size()) +
                             ", PNT: " + std::to_string(pntPass) + "/" + std::to_string(pntRes.size()) +
                             ", TXB: " + std::to_string(txbPass) + "/" + std::to_string(txbRes.size());
            r.details = r.passed ? "PASS" : "FAIL: Regressione riscontrata nelle suite topologia o texture";
        }
        results.push_back(r);
    }

    return results;
}

} // namespace fw
