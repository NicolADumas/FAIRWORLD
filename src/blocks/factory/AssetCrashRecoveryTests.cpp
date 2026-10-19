#include "pch.h"
#include "AssetCrashRecoveryTests.h"
#include "FactoryOrchestrator.h"
#include "AssetPackagingService.h"
#include "TextureBakingService.h"
#include "blocks/BlockRegistry.h"
#include "blocks/MaterialRegistry.h"
#include "blocks/ShapeRegistry.h"
#include "blocks/texture/ImageBitmap.h"
#include "Sha256.h"

// Suites per verifica regressioni CRH-10
#include "blocks/uv/CompiledTopologyTests.h"
#include "PainterStationContractTests.h"
#include "TextureBakingContractTests.h"
#include "AssetPackagingContractTests.h"
#include "TransactionIntegrityAuditTests.h"
#include "SculptedMeshRuntimeBridgeTests.h"
#include "AssetIntegrityRoundTripTests.h"
#include "AssetIntegrityHardeningTests.h"
#include "AssetDigestTrustTests.h"
#include "TestRunContext.h"

#include <filesystem>
#include <fstream>
#include <chrono>

namespace fw {

std::vector<AssetCrashRecoveryTestResult> AssetCrashRecoveryTests::RunAllTests() {
    std::vector<AssetCrashRecoveryTestResult> results;
    results.reserve(10);

    const std::string testRoot = (std::filesystem::temp_directory_path() / "fairworld_crh_gate").string();
    std::error_code ec;
    std::filesystem::remove_all(testRoot, ec);
    std::filesystem::create_directories(testRoot, ec);
    std::filesystem::create_directories(testRoot + "/textures/blocks", ec);

    // Helper per creare un RawAssetPackage Schema V2 valido
    auto createBasePackage = [](const std::string& assetId = "fairworld:crh_base_block", uint64_t rev = 1) -> RawAssetPackage {
        RawAssetPackage pkg;
        pkg.manifest.assetId = assetId;
        pkg.manifest.displayName = "CRH Test Block";
        pkg.manifest.schemaVersion = 2;
        pkg.manifest.revision = rev;
        pkg.manifest.stage = RawAssetStage::Packaged;
        pkg.geometrySource = GeometrySource::Primitive;
        pkg.shape.type = ShapeType::Cube;
        pkg.shape.id = 1;
        pkg.shape.revision = 1;
        pkg.shape.supportsVisualCompilation = true;
        pkg.shape.supportsCollisionCompilation = true;
        pkg.compiledPhysics.mass = 4.0f;
        pkg.compiledPhysics.density = 2200.0f;
        pkg.compiledPhysics.collisionEnabled = true;
        pkg.baseColorFallback = glm::vec3(0.6f, 0.4f, 0.3f);
        pkg.roughnessFallback = 0.5f;
        pkg.metallicFallback = 0.0f;

        // Genera 24 vertici cubo minimi
        pkg.geometry.vertices.resize(24);
        for (size_t i = 0; i < 24; ++i) {
            auto& v = pkg.geometry.vertices[i];
            v.position = Vec3{ static_cast<float>(i % 2), static_cast<float>((i / 2) % 2), static_cast<float>(i / 4) };
            v.color = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
            v.roughMetal = Vec2{ 0.5f, 0.0f };
            v.materialID = 1;
            v.normal = Vec3{ 0.0f, 1.0f, 0.0f };
            v.ao = 1.0f;
            v.light = 1.0f;
            v.emissive = 0.0f;
        }
        pkg.geometry.vertexCount = 24;

        // 12 facce triangolari
        for (int i = 0; i < 12; ++i) {
            Face f;
            f.indices = { (i * 2) % 24, (i * 2 + 1) % 24, (i * 2 + 2) % 24 };
            f.surfaceId = static_cast<uint32_t>(i % 6);
            f.matIndex = 0;
            pkg.geometry.faces.push_back(f);
        }
        pkg.geometry.triangleCount = 12;
        pkg.packageFingerprint = 1000003ULL * 24 + rev;
        pkg.distributionChecksum = pkg.ComputeDistributionChecksum();
        pkg.validationReport.isValid = true;
        return pkg;
    };

    // -------------------------------------------------------------------------
    // CRH-01: Pubblicazione normale (Staging -> Commit -> Sync)
    // -------------------------------------------------------------------------
    {
        AssetCrashRecoveryTestResult r;
        r.testId = "CRH-01";
        r.testName = "Pubblicazione normale";
        r.inputDesc = "Pubblicazione persistente con staging isolato, validazione, commit durevole e sync registri";
        r.expectedDesc = "Transazione completata con successo, active.rawpkg esistente, registri sincronizzati";

        auto pkg = createBasePackage("fairworld:crh01_normal", 1);
        pkg.contentDigestSha256 = pkg.ComputeContentDigestSha256(testRoot);

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);

        auto pubRes = service.PublishPackagePersistent(pkg, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        std::string activePath = service.GetActivePublishedPackagePath("fairworld:crh01_normal", testRoot);
        bool fileExists = std::filesystem::exists(activePath, ec);
        bool regOk = bReg.IsStringIdTaken("fairworld:crh01_normal") && (pubRes.targetVoxelId != 0);

        r.passed = pubRes.success && fileExists && regOk;
        r.observedDesc = "Success: " + std::string(pubRes.success ? "true" : "false") +
                         ", FileCommitted: " + std::string(fileExists ? "true" : "false") +
                         ", VoxelId: " + std::to_string(pubRes.targetVoxelId);
        r.details = r.passed ? "PASS" : "FAIL: Pubblicazione persistente normale fallita";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // CRH-02: Errore durante staging
    // -------------------------------------------------------------------------
    {
        AssetCrashRecoveryTestResult r;
        r.testId = "CRH-02";
        r.testName = "Errore durante staging";
        r.inputDesc = "Iniezione guasto FailDuringStaging su PublishPackagePersistent";
        r.expectedDesc = "Transazione abortisce, staging pulito, nessun file pubblicato in published/, zero registri mutati";

        auto pkg = createBasePackage("fairworld:crh02_staging_fail", 1);
        pkg.contentDigestSha256 = pkg.ComputeContentDigestSha256(testRoot);

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        uint32_t bCount0 = bReg.GetActiveBlockCount();

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTransactionFaultInjection(TransactionFaultPoint::FailDuringStaging);

        auto pubRes = service.PublishPackagePersistent(pkg, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        std::string activePath = service.GetActivePublishedPackagePath("fairworld:crh02_staging_fail", testRoot);
        bool fileNotPublished = !std::filesystem::exists(activePath, ec);
        bool regUntouched = (bReg.GetActiveBlockCount() == bCount0) && !bReg.IsStringIdTaken("fairworld:crh02_staging_fail");

        r.passed = (!pubRes.success) && fileNotPublished && regUntouched;
        r.observedDesc = "Success: " + std::string(pubRes.success ? "true" : "false") +
                         ", PublishedAbsent: " + std::string(fileNotPublished ? "true" : "false") +
                         ", RegClean: " + std::string(regUntouched ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Guasto staging non isolato o residui lasciati nei registri/filesystem";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // CRH-03: Errore prima del commit
    // -------------------------------------------------------------------------
    {
        AssetCrashRecoveryTestResult r;
        r.testId = "CRH-03";
        r.testName = "Errore prima del commit";
        r.inputDesc = "Iniezione guasto FailBeforeCommit: file scritti in staging ma crash prima del commit durevole";
        r.expectedDesc = "Pubblicazione fallisce, active.rawpkg non creato, recovery elimina staging uncommitted";

        auto pkg = createBasePackage("fairworld:crh03_precommit_fail", 1);
        pkg.contentDigestSha256 = pkg.ComputeContentDigestSha256(testRoot);

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTransactionFaultInjection(TransactionFaultPoint::FailBeforeCommit);

        auto pubRes = service.PublishPackagePersistent(pkg, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        std::string activePath = service.GetActivePublishedPackagePath("fairworld:crh03_precommit_fail", testRoot);
        bool notCommitted = !std::filesystem::exists(activePath, ec);

        // Esegue recovery per scartare lo staging rimasto incompiuto
        service.SetTransactionFaultInjection(TransactionFaultPoint::None);
        auto recReport = service.RunCrashRecovery(&bReg, &mReg, &sReg, testRoot, AssetTrustPolicy::Strict);
        bool recoveryDiscarded = (recReport.incompleteTransactionsDiscarded >= 1);
        bool notInRegistry = !bReg.IsStringIdTaken("fairworld:crh03_precommit_fail");

        r.passed = (!pubRes.success) && notCommitted && recoveryDiscarded && notInRegistry;
        r.observedDesc = "Success: " + std::string(pubRes.success ? "true" : "false") +
                         ", NotCommitted: " + std::string(notCommitted ? "true" : "false") +
                         ", StagingDiscarded: " + std::string(recoveryDiscarded ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Errore prima del commit non gestito con scarto durevole della transazione";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // CRH-04: Errore immediatamente dopo commit
    // -------------------------------------------------------------------------
    {
        AssetCrashRecoveryTestResult r;
        r.testId = "CRH-04";
        r.testName = "Errore immediatamente dopo commit";
        r.inputDesc = "Iniezione FailImmediatelyAfterCommit: commit durevole riuscito, ma crash prima del sync registri";
        r.expectedDesc = "Commit su disco presente; recovery post-crash rileva il package e sincronizza i registri";

        auto pkg = createBasePackage("fairworld:crh04_after_commit", 1);
        pkg.contentDigestSha256 = pkg.ComputeContentDigestSha256(testRoot);

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTransactionFaultInjection(TransactionFaultPoint::FailImmediatelyAfterCommit);

        auto pubRes = service.PublishPackagePersistent(pkg, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        std::string activePath = service.GetActivePublishedPackagePath("fairworld:crh04_after_commit", testRoot);
        bool fileCommitted = std::filesystem::exists(activePath, ec);

        // Simula crash / riavvio pulendo completamente i registri in memoria
        sReg.Initialize();
        bReg.Initialize();
        mReg.Initialize();
        bool regEmptyBeforeRec = !bReg.IsStringIdTaken("fairworld:crh04_after_commit");

        // Crash Recovery su startup con registri puliti
        service.SetTransactionFaultInjection(TransactionFaultPoint::None);
        auto recReport = service.RunCrashRecovery(&bReg, &mReg, &sReg, testRoot, AssetTrustPolicy::Strict);

        bool recoveredInReg = bReg.IsStringIdTaken("fairworld:crh04_after_commit");

        r.passed = (!pubRes.success) && fileCommitted && regEmptyBeforeRec && recoveredInReg;
        r.observedDesc = "DiskCommitted: " + std::string(fileCommitted ? "true" : "false") +
                         ", RegPreRecEmpty: " + std::string(regEmptyBeforeRec ? "true" : "false") +
                         ", RegPostRecRecovered: " + std::string(recoveredInReg ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Package committed non recuperato dopo crash post-commit";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // CRH-05: Recovery di transazione incompleta
    // -------------------------------------------------------------------------
    {
        AssetCrashRecoveryTestResult r;
        r.testId = "CRH-05";
        r.testName = "Recovery di transazione incompleta";
        r.inputDesc = "Presenza di cartella orfana in .staging/ con journal non-committed; esecuzione RunCrashRecovery";
        r.expectedDesc = "Cartella orfana scartata ed eliminata dal disco, nessun asset fantasma esposto nei registri";

        const std::string orphanStaging = testRoot + "/.staging/tx_orphan_incomplete_test";
        std::filesystem::create_directories(orphanStaging, ec);
        {
            AssetTransactionJournal tj;
            tj.transactionId = "tx_orphan_incomplete_test";
            tj.assetId = "fairworld:crh05_phantom";
            tj.revision = 1;
            tj.state = AssetTransactionState::FilesStaged; // Non-committed!
            tj.SaveToFile(orphanStaging + "/tx_journal.json");
            std::ofstream dummy(orphanStaging + "/dummy_payload.bin");
            dummy << "incomplete data";
        }

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);

        auto recReport = service.RunCrashRecovery(&bReg, &mReg, &sReg, testRoot, AssetTrustPolicy::Strict);

        bool orphanDirDeleted = !std::filesystem::exists(orphanStaging, ec);
        bool phantomNotInReg = !bReg.IsStringIdTaken("fairworld:crh05_phantom");

        r.passed = (recReport.incompleteTransactionsDiscarded >= 1) && orphanDirDeleted && phantomNotInReg;
        r.observedDesc = "DiscardedCount: " + std::to_string(recReport.incompleteTransactionsDiscarded) +
                         ", DirDeleted: " + std::string(orphanDirDeleted ? "true" : "false") +
                         ", RegClean: " + std::string(phantomNotInReg ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Transazione incompleta orfana non eliminata dal recovery";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // CRH-06: Recovery idempotente
    // -------------------------------------------------------------------------
    {
        AssetCrashRecoveryTestResult r;
        r.testId = "CRH-06";
        r.testName = "Recovery idempotente";
        r.inputDesc = "Due esecuzioni consecutive di RunCrashRecovery sul medesimo stato del filesystem";
        r.expectedDesc = "La seconda esecuzione riporta 0 scarti e produce esattamente il medesimo stato pulito";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);

        auto rep1 = service.RunCrashRecovery(&bReg, &mReg, &sReg, testRoot, AssetTrustPolicy::Strict);
        uint32_t bCount1 = bReg.GetActiveBlockCount();

        auto rep2 = service.RunCrashRecovery(&bReg, &mReg, &sReg, testRoot, AssetTrustPolicy::Strict);
        uint32_t bCount2 = bReg.GetActiveBlockCount();

        bool rep2Clean = (rep2.incompleteTransactionsDiscarded == 0) && (bCount1 == bCount2);

        r.passed = rep1.success && rep2.success && rep2Clean;
        r.observedDesc = "Rep1Success: " + std::string(rep1.success ? "true" : "false") +
                         ", Rep2Discarded: " + std::to_string(rep2.incompleteTransactionsDiscarded) +
                         ", BlockCountsEqual: " + std::string(bCount1 == bCount2 ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Recovery non idempotente su esecuzioni consecutive";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // CRH-07: Texture condivisa tra asset
    // -------------------------------------------------------------------------
    {
        AssetCrashRecoveryTestResult r;
        r.testId = "CRH-07";
        r.testName = "Texture condivisa tra asset";
        r.inputDesc = "Asset A pubblicato con texture T; Asset B fallisce la pubblicazione usando T; recovery eseguito";
        r.expectedDesc = "La texture T rimane integra su disco, Asset A continua a essere valido e referenziato";

        const std::string sharedTexRel = "textures/blocks/crh07_shared.png";
        const std::string sharedTexFull = testRoot + "/" + sharedTexRel;
        {
            ImageBitmap bmp(8, 8, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.3f, 0.7f, 0.8f, 1.0f));
            auto pngData = bmp.EncodeToPngMemory();
            std::ofstream f(sharedTexFull, std::ios::binary);
            f.write(reinterpret_cast<const char*>(pngData.data()), pngData.size());
        }

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);

        // 1. Pubblicazione Asset A con successo
        auto pkgA = createBasePackage("fairworld:crh07_asset_a", 1);
        pkgA.albedoMapUri = sharedTexRel;
        pkgA.contentDigestSha256 = pkgA.ComputeContentDigestSha256(testRoot);
        auto pubA = service.PublishPackagePersistent(pkgA, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        // 2. Pubblicazione Asset B con errore/crash simulato
        auto pkgB = createBasePackage("fairworld:crh07_asset_b", 1);
        pkgB.albedoMapUri = sharedTexRel;
        pkgB.contentDigestSha256 = pkgB.ComputeContentDigestSha256(testRoot);

        service.SetTransactionFaultInjection(TransactionFaultPoint::FailBeforeCommit);
        auto pubB = service.PublishPackagePersistent(pkgB, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);
        service.SetTransactionFaultInjection(TransactionFaultPoint::None);

        // 3. Esecuzione Crash Recovery
        service.RunCrashRecovery(&bReg, &mReg, &sReg, testRoot, AssetTrustPolicy::Strict);

        bool texStillExists = std::filesystem::exists(sharedTexFull, ec);
        bool assetAValid = bReg.IsStringIdTaken("fairworld:crh07_asset_a");
        bool assetBRejected = !bReg.IsStringIdTaken("fairworld:crh07_asset_b");

        r.passed = pubA.success && (!pubB.success) && texStillExists && assetAValid && assetBRejected;
        r.observedDesc = "PubA: " + std::string(pubA.success ? "PASS" : "FAIL") +
                         ", PubB: " + std::string(pubB.success ? "FAIL (expected)" : "PASS") +
                         ", TexIntact: " + std::string(texStillExists ? "true" : "false") +
                         ", AssetAValid: " + std::string(assetAValid ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Texture condivisa rimossa o compromessa da fallimento di asset terzo";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // CRH-08: Revisione precedente preservata
    // -------------------------------------------------------------------------
    {
        AssetCrashRecoveryTestResult r;
        r.testId = "CRH-08";
        r.testName = "Revisione precedente preservata";
        r.inputDesc = "Revisione 1 pubblicata; tentativo di pubblicare revisione 2 fallisce; verifica stato attivo";
        r.expectedDesc = "Revisione 1 rimane intatta, attiva nei registri e su disco, revisione 2 non sovrascrive";

        const std::string crh08Root = testRoot + "/crh08";
        std::filesystem::remove_all(crh08Root, ec);
        std::filesystem::create_directories(crh08Root, ec);

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        AssetPackagingService service;
        service.SetRootAssetDirectory(crh08Root);

        // 1. Pubblica rev 1
        auto pkgRev1 = createBasePackage("fairworld:crh08_versioned", 1);
        pkgRev1.compiledPhysics.mass = 10.0f;
        pkgRev1.contentDigestSha256 = pkgRev1.ComputeContentDigestSha256(crh08Root);
        auto pub1 = service.PublishPackagePersistent(pkgRev1, bReg, mReg, sReg, crh08Root, AssetTrustPolicy::Strict);

        // 2. Tenta di pubblicare rev 2 con guasto prima del commit
        auto pkgRev2 = createBasePackage("fairworld:crh08_versioned", 2);
        pkgRev2.compiledPhysics.mass = 999.0f;
        pkgRev2.contentDigestSha256 = pkgRev2.ComputeContentDigestSha256(crh08Root);

        service.SetTransactionFaultInjection(TransactionFaultPoint::FailBeforeCommit);
        auto pub2 = service.PublishPackagePersistent(pkgRev2, bReg, mReg, sReg, crh08Root, AssetTrustPolicy::Strict);
        service.SetTransactionFaultInjection(TransactionFaultPoint::None);

        // 3. Esegui recovery e verifica lo stato attivo
        service.RunCrashRecovery(&bReg, &mReg, &sReg, crh08Root, AssetTrustPolicy::Strict);

        std::string activePath = service.GetActivePublishedPackagePath("fairworld:crh08_versioned", crh08Root);
        RawAssetPackage loadedActive;
        bool loadOk = loadedActive.LoadFromFile(activePath);

        bool isStillRev1 = loadOk && (loadedActive.manifest.revision == 1) && (loadedActive.compiledPhysics.mass == 10.0f);
        uint8_t blkId = bReg.GetBlockIdByString("fairworld:crh08_versioned");
        bool regIsRev1 = (blkId != 0) && (bReg.GetBlock(blkId).physics.mass == 10.0f);

        r.passed = pub1.success && (!pub2.success) && isStillRev1 && regIsRev1;
        r.observedDesc = "Pub1: " + std::string(pub1.success ? "PASS" : "FAIL") +
                         ", Pub2Fail: " + std::string(!pub2.success ? "PASS" : "FAIL") +
                         ", ActiveIsRev1: " + std::string(isStillRev1 ? "true" : "false") +
                         ", RegIsRev1: " + std::string(regIsRev1 ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Revisione precedente sovrascritta o corrotta da tentativo rev fallito";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // CRH-09: Zero-Work su package committed
    // -------------------------------------------------------------------------
    {
        AssetCrashRecoveryTestResult r;
        r.testId = "CRH-09";
        r.testName = "Zero-Work su package committed";
        r.inputDesc = "Pubblicazione persistente iniziale seguita da seconda chiamata a input identico";
        r.expectedDesc = "Seconda chiamata rileva Zero-Work hit, nessun nuovo staging, zero scritture ridondanti";

        auto pkg = createBasePackage("fairworld:crh09_zerowork", 1);
        pkg.contentDigestSha256 = pkg.ComputeContentDigestSha256(testRoot);

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);

        // Prima pubblicazione: commit su disco
        auto pub1 = service.PublishPackagePersistent(pkg, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        // Seconda pubblicazione su input identico
        auto pub2 = service.PublishPackagePersistent(pkg, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        bool zwOk = pub1.success && pub2.success && pub2.wasZeroWorkHit && (pub1.targetVoxelId == pub2.targetVoxelId);

        r.passed = zwOk;
        r.observedDesc = "Pub1: " + std::string(pub1.success ? "true" : "false") +
                         ", Pub2ZeroWorkHit: " + std::string(pub2.wasZeroWorkHit ? "true" : "false") +
                         ", TargetVoxelId: " + std::to_string(pub1.targetVoxelId);
        r.details = r.passed ? "PASS" : "FAIL: Zero-Work non rilevato su package già committed identico";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // CRH-10: Regressione delle suite precedenti
    // -------------------------------------------------------------------------
    {
        AssetCrashRecoveryTestResult r;
        r.testId = "CRH-10";
        r.testName = "Regressione delle suite precedenti";
        r.inputDesc = "Esecuzione completa di TOP(36), PNT(10), TXB(10), PKG(10), TRX(10), SBR(10), INT(10), SEC(10), DGV(10)";
        r.expectedDesc = "Tutti i 116 test contrattuali delle 9 suite precedenti risultano superati al 100%";

        if (TestRunContext::Instance().IsActive()) {
            static const std::vector<std::pair<std::string, size_t>> deps = {
                {"TOP", 36}, {"PNT", 10}, {"TXB", 10}, {"PKG", 10}, {"TRX", 10}, {"SBR", 10}, {"INT", 10}, {"SEC", 10}, {"DGV", 10}
            };
            auto eval = TestRunContext::Instance().EvaluateRegressionGate(deps);
            size_t topPass = 0, pntPass = 0, txbPass = 0, pkgPass = 0, trxPass = 0, sbrPass = 0, intPass = 0, secPass = 0, dgvPass = 0;
            for (const auto& c : eval.checks) {
                if (c.suiteCode == "TOP") topPass = c.passedCount;
                if (c.suiteCode == "PNT") pntPass = c.passedCount;
                if (c.suiteCode == "TXB") txbPass = c.passedCount;
                if (c.suiteCode == "PKG") pkgPass = c.passedCount;
                if (c.suiteCode == "TRX") trxPass = c.passedCount;
                if (c.suiteCode == "SBR") sbrPass = c.passedCount;
                if (c.suiteCode == "INT") intPass = c.passedCount;
                if (c.suiteCode == "SEC") secPass = c.passedCount;
                if (c.suiteCode == "DGV") dgvPass = c.passedCount;
            }
            r.passed = eval.allDependenciesPassed;
            r.observedDesc = "TOP: " + std::to_string(topPass) + "/36, PNT: " + std::to_string(pntPass) + "/10, TXB: " + std::to_string(txbPass) + "/10, PKG: " + std::to_string(pkgPass) + "/10, TRX: " + std::to_string(trxPass) + "/10, SBR: " + std::to_string(sbrPass) + "/10, INT: " + std::to_string(intPass) + "/10, SEC: " + std::to_string(secPass) + "/10, DGV: " + std::to_string(dgvPass) + "/10";
            r.details = r.passed ? "PASS" : "FAIL: " + eval.failureDetails;
        } else {
            auto topRes = CompiledTopologyTests::RunAllTests();
            auto pntRes = PainterStationContractTests::RunAllTests();
            auto txbRes = TextureBakingContractTests::RunAllTests();
            auto pkgRes = AssetPackagingContractTests::RunAllTests();
            auto trxRes = TransactionIntegrityAuditTests::RunAllTests();
            auto sbrRes = SculptedMeshRuntimeBridgeTests::RunAllTests();
            auto intRes = AssetIntegrityRoundTripTests::RunAllTests();
            auto secRes = AssetIntegrityHardeningTests::RunAllTests();
            auto dgvRes = AssetDigestTrustTests::RunAllTests();

            size_t topPass = 0; for (const auto& tr : topRes) if (tr.passed) topPass++;
            size_t pntPass = 0; for (const auto& pr : pntRes) if (pr.passed) pntPass++;
            size_t txbPass = 0; for (const auto& xr : txbRes) if (xr.passed) txbPass++;
            size_t pkgPass = 0; for (const auto& kr : pkgRes) if (kr.passed) pkgPass++;
            size_t trxPass = 0; for (const auto& rr : trxRes) if (rr.passed) trxPass++;
            size_t sbrPass = 0; for (const auto& sr : sbrRes) if (sr.passed) sbrPass++;
            size_t intPass = 0; for (const auto& ir : intRes) if (ir.passed) intPass++;
            size_t secPass = 0; for (const auto& sc : secRes) if (sc.passed) secPass++;
            size_t dgvPass = 0; for (const auto& dg : dgvRes) if (dg.passed) dgvPass++;

            bool allPass = (topPass == topRes.size() && topRes.size() == 36) &&
                           (pntPass == pntRes.size() && pntRes.size() == 10) &&
                           (txbPass == txbRes.size() && txbRes.size() == 10) &&
                           (pkgPass == pkgRes.size() && pkgRes.size() == 10) &&
                           (trxPass == trxRes.size() && trxRes.size() == 10) &&
                           (sbrPass == sbrRes.size() && sbrRes.size() == 10) &&
                           (intPass == intRes.size() && intRes.size() == 10) &&
                           (secPass == secRes.size() && secRes.size() == 10) &&
                           (dgvPass == dgvRes.size() && dgvRes.size() == 10);

            r.passed = allPass;
            r.observedDesc = "TOP: " + std::to_string(topPass) + "/" + std::to_string(topRes.size()) +
                             ", PNT: " + std::to_string(pntPass) + "/" + std::to_string(pntRes.size()) +
                             ", TXB: " + std::to_string(txbPass) + "/" + std::to_string(txbRes.size()) +
                             ", PKG: " + std::to_string(pkgPass) + "/" + std::to_string(pkgRes.size()) +
                             ", TRX: " + std::to_string(trxPass) + "/" + std::to_string(trxRes.size()) +
                             ", SBR: " + std::to_string(sbrPass) + "/" + std::to_string(sbrRes.size()) +
                             ", INT: " + std::to_string(intPass) + "/" + std::to_string(intRes.size()) +
                             ", SEC: " + std::to_string(secPass) + "/" + std::to_string(secRes.size()) +
                             ", DGV: " + std::to_string(dgvPass) + "/" + std::to_string(dgvRes.size());
            r.details = r.passed ? "PASS" : "FAIL: Regressione rilevata nelle suite precedenti";
        }
        results.push_back(r);
    }

    return results;
}

} // namespace fw
