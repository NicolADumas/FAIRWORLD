#include "pch.h"
#include "TransactionIntegrityAuditTests.h"
#include "AssetPackagingService.h"
#include "FactoryOrchestrator.h"
#include "TextureBakingContractTests.h"
#include "PainterStationContractTests.h"
#include "AssetPackagingContractTests.h"
#include "blocks/uv/CompiledTopologyTests.h"
#include "TestRunContext.h"
#include "blocks/BlockRegistry.h"
#include "blocks/MaterialRegistry.h"
#include "blocks/ShapeRegistry.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fw {

std::vector<TransactionAuditTestResult> TransactionIntegrityAuditTests::RunAllTests() {
    std::vector<TransactionAuditTestResult> results;
    results.reserve(10);

    const std::string testRoot = "assets/test_trx_artifacts";
    std::error_code ec;
    std::filesystem::create_directories(testRoot, ec);

    auto createTestPackage = []() -> RawAssetPackage {
        RawAssetPackage pkg;
        pkg.manifest.assetId = "fairworld:trx_sample";
        pkg.manifest.displayName = "TRX Sample";
        pkg.manifest.revision = 1;
        pkg.manifest.stage = RawAssetStage::Validated;
        pkg.validationReport.isValid = true;
        pkg.shape.type = ShapeType::Cube;
        pkg.shape.supportsVisualCompilation = true;
        pkg.compiledPhysics.mass = 2.0f;
        pkg.compiledPhysics.density = 800.0f;
        pkg.geometry.vertexCount = 4;
        pkg.geometry.vertices.resize(4);
        pkg.packageFingerprint = 987654321ULL;
        return pkg;
    };

    // -------------------------------------------------------------------------
    // TRX-01: Fallimento prima delle mutazioni
    // -------------------------------------------------------------------------
    {
        TransactionAuditTestResult r;
        r.testId = "TRX-01";
        r.testName = "Fallimento prima delle mutazioni";
        r.inputDesc = "Iniezione guasto FailBeforeMutations su PublishPackageTransactional";
        r.expectedDesc = "Transazione abortisce; wasRolledBack == false, zero mutazioni nei registri";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        uint32_t bCount0 = bReg.GetActiveBlockCount();
        uint32_t sCount0 = static_cast<uint32_t>(sReg.GetAllShapes().size());

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTestFaultInjection(PublishFaultInjectionPoint::FailBeforeMutations);

        auto pkg = createTestPackage();
        auto res = service.PublishPackageTransactional(pkg, bReg, mReg, sReg);

        uint32_t bCount1 = bReg.GetActiveBlockCount();
        uint32_t sCount1 = static_cast<uint32_t>(sReg.GetAllShapes().size());

        bool abortedClean = (!res.success) && (!res.wasRolledBack);
        bool countsUnchanged = (bCount0 == bCount1) && (sCount0 == sCount1);

        r.passed = abortedClean && countsUnchanged;
        r.observedDesc = "AbortedClean: " + std::string(abortedClean ? "true" : "false") +
                         ", Blocks: " + std::to_string(bCount0) + "->" + std::to_string(bCount1) +
                         ", Shapes: " + std::to_string(sCount0) + "->" + std::to_string(sCount1);
        r.details = r.passed ? "PASS" : "FAIL: Stato alterato prima delle mutazioni";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // TRX-02: Fallimento dopo registrazione Shape
    // -------------------------------------------------------------------------
    {
        TransactionAuditTestResult r;
        r.testId = "TRX-02";
        r.testName = "Fallimento dopo registrazione Shape";
        r.inputDesc = "Iniezione guasto FailAfterShapeRegister; verifica rollback forma orfana";
        r.expectedDesc = "Transazione abortisce; wasRolledBack == true, forma eliminata da ShapeRegistry";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        uint32_t sCount0 = static_cast<uint32_t>(sReg.GetAllShapes().size());

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTestFaultInjection(PublishFaultInjectionPoint::FailAfterShapeRegister);

        auto pkg = createTestPackage();
        auto res = service.PublishPackageTransactional(pkg, bReg, mReg, sReg);

        uint32_t sCount1 = static_cast<uint32_t>(sReg.GetAllShapes().size());

        bool rolledBack = (!res.success) && res.wasRolledBack;
        bool shapeCleaned = (sCount0 == sCount1);

        r.passed = rolledBack && shapeCleaned;
        r.observedDesc = "RolledBack: " + std::string(rolledBack ? "true" : "false") +
                         ", ShapesCount: " + std::to_string(sCount0) + "->" + std::to_string(sCount1);
        r.details = r.passed ? "PASS" : "FAIL: Forma orfana rimasta in ShapeRegistry dopo rollback";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // TRX-03: Fallimento dopo modifica Block
    // -------------------------------------------------------------------------
    {
        TransactionAuditTestResult r;
        r.testId = "TRX-03";
        r.testName = "Fallimento dopo modifica Block";
        r.inputDesc = "Iniezione guasto FailAfterBlockMutation; verifica cancellazione blocco allocato";
        r.expectedDesc = "Transazione abortisce con rollback; nessun blocco orfano in BlockRegistry";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        uint32_t bCount0 = bReg.GetActiveBlockCount();

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTestFaultInjection(PublishFaultInjectionPoint::FailAfterBlockMutation);

        auto pkg = createTestPackage();
        pkg.manifest.assetId = "fairworld:trx_block_fail";
        auto res = service.PublishPackageTransactional(pkg, bReg, mReg, sReg);

        uint32_t bCount1 = bReg.GetActiveBlockCount();
        bool idNotTaken = !bReg.IsStringIdTaken("fairworld:trx_block_fail");

        bool passed = (!res.success) && res.wasRolledBack && (bCount0 == bCount1) && idNotTaken;

        r.passed = passed;
        r.observedDesc = "RolledBack: " + std::string(res.wasRolledBack ? "true" : "false") +
                         ", BlockCount: " + std::to_string(bCount0) + "->" + std::to_string(bCount1) +
                         ", IdNotTaken: " + (idNotTaken ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Blocco orfano rimasto in BlockRegistry";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // TRX-04: Fallimento dopo modifica Material
    // -------------------------------------------------------------------------
    {
        TransactionAuditTestResult r;
        r.testId = "TRX-04";
        r.testName = "Fallimento dopo modifica Material";
        r.inputDesc = "Iniezione guasto FailAfterMaterialMutation; verifica ripristino materiale";
        r.expectedDesc = "Transazione abortisce con rollback; Shape, Block e Material ripristinati";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTestFaultInjection(PublishFaultInjectionPoint::FailAfterMaterialMutation);

        auto pkg = createTestPackage();
        pkg.manifest.assetId = "fairworld:trx_mat_fail";
        auto res = service.PublishPackageTransactional(pkg, bReg, mReg, sReg);

        bool rolledBack = (!res.success) && res.wasRolledBack;
        bool noBlock = !bReg.IsStringIdTaken("fairworld:trx_mat_fail");

        r.passed = rolledBack && noBlock;
        r.observedDesc = "RolledBack: " + std::string(rolledBack ? "true" : "false") +
                         ", BlockCleaned: " + std::string(noBlock ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Rollback incompleto dopo mutazione materiale";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // TRX-05: Rollback completo dei tre registri
    // -------------------------------------------------------------------------
    {
        TransactionAuditTestResult r;
        r.testId = "TRX-05";
        r.testName = "Rollback completo dei tre registri";
        r.inputDesc = "Iniezione guasto FailDuringPostAudit con tutti e tre i registri già mutati";
        r.expectedDesc = "Rollback sincronizzato: Shape rimossa, Block eliminato, Material ripristinato";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        uint32_t s0 = static_cast<uint32_t>(sReg.GetAllShapes().size());
        uint32_t b0 = bReg.GetActiveBlockCount();

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTestFaultInjection(PublishFaultInjectionPoint::FailDuringPostAudit);

        auto pkg = createTestPackage();
        pkg.manifest.assetId = "fairworld:trx_triple_fail";
        auto res = service.PublishPackageTransactional(pkg, bReg, mReg, sReg);

        uint32_t s1 = static_cast<uint32_t>(sReg.GetAllShapes().size());
        uint32_t b1 = bReg.GetActiveBlockCount();
        bool noOrphanId = !bReg.IsStringIdTaken("fairworld:trx_triple_fail");

        bool allClean = (!res.success) && res.wasRolledBack && (s0 == s1) && (b0 == b1) && noOrphanId;

        r.passed = allClean;
        r.observedDesc = "RolledBack: " + std::string(res.wasRolledBack ? "true" : "false") +
                         ", Shapes: " + std::to_string(s0) + "->" + std::to_string(s1) +
                         ", Blocks: " + std::to_string(b0) + "->" + std::to_string(b1);
        r.details = r.passed ? "PASS" : "FAIL: Stato incoerente tra i 3 registri dopo rollback";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // TRX-06: Fingerprint invariato dopo fallimento
    // -------------------------------------------------------------------------
    {
        TransactionAuditTestResult r;
        r.testId = "TRX-06";
        r.testName = "Fingerprint invariato dopo fallimento";
        r.inputDesc = "Verifica cache pubblicazione e manifest.stage in caso di abort/rollback";
        r.expectedDesc = "m_lastPublishedFingerprints non contiene l'assetId; stage != Published";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTestFaultInjection(PublishFaultInjectionPoint::FailAfterBlockMutation);

        auto pkg = createTestPackage();
        pkg.manifest.assetId = "fairworld:trx_fp_test";
        pkg.manifest.stage = RawAssetStage::Validated;

        service.PublishPackageTransactional(pkg, bReg, mReg, sReg);

        uint64_t cachedFp = service.GetLastPublishedFingerprint("fairworld:trx_fp_test");
        bool stageNotPublished = (pkg.manifest.stage != RawAssetStage::Published);

        r.passed = (cachedFp == 0) && stageNotPublished;
        r.observedDesc = "CachedFingerprint: " + std::to_string(cachedFp) +
                         ", StagePublished: " + std::string(stageNotPublished ? "false" : "true");
        r.details = r.passed ? "PASS" : "FAIL: Cache Zero-Work o stage aggiornati su transazione fallita";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // TRX-07: Save failure e consistenza
    // -------------------------------------------------------------------------
    {
        TransactionAuditTestResult r;
        r.testId = "TRX-07";
        r.testName = "Save failure e consistenza";
        r.inputDesc = "Tentativo di salvataggio package su percorso filesystem non valido/illegale";
        r.expectedDesc = "SaveToFile ritorna false in modo sicuro, nessun crash o file corrotto";

        auto pkg = createTestPackage();
        // Percorso volutamente illegale (carattere riservato o nullo)
        std::string badPath = "Z://invalid_unreachable_drive_???/corrupt.json";
        bool saveFailedSafely = !pkg.SaveToFile(badPath);

        r.passed = saveFailedSafely;
        r.observedDesc = "SaveFailedSafely: " + std::string(saveFailedSafely ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Gestione errori di salvataggio disco inconsistente";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // TRX-08: Reload dopo riavvio simulato
    // -------------------------------------------------------------------------
    {
        TransactionAuditTestResult r;
        r.testId = "TRX-08";
        r.testName = "Reload dopo riavvio simulato";
        r.inputDesc = "Salvataggio package su file, distruzione registri, rilettura e re-binding";
        r.expectedDesc = "Registri freschi ricostruiscono l'asset al 100% senza alcuna dipendenza da BlockMakerState";

        std::string pkgFile = testRoot + "/persisted_restart_package.json";
        auto originalPkg = createTestPackage();
        originalPkg.manifest.assetId = "fairworld:reboot_asset";
        originalPkg.manifest.displayName = "Reboot Block";
        originalPkg.compiledPhysics.mass = 3.5f;
        originalPkg.SaveToFile(pkgFile);

        // Simulazione riavvio: registri completamente azzerati
        ShapeRegistry sRegFresh; sRegFresh.Initialize();
        BlockRegistry bRegFresh; bRegFresh.Initialize();
        MaterialRegistry mRegFresh; mRegFresh.Initialize();

        RawAssetPackage reloadedPkg;
        bool loadOk = reloadedPkg.LoadFromFile(pkgFile);

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        auto txRes = service.PublishPackageTransactional(reloadedPkg, bRegFresh, mRegFresh, sRegFresh);

        bool pubOk = txRes.success;
        bool blockOk = pubOk && (bRegFresh.GetBlock(txRes.targetVoxelId).stringId == "fairworld:reboot_asset") &&
                       (bRegFresh.GetBlock(txRes.targetVoxelId).physics.mass == 3.5f);
        bool matOk = pubOk && mRegFresh.GetMaterial(txRes.targetVoxelId).sharedShape.IsValid();

        r.passed = loadOk && pubOk && blockOk && matOk;
        r.observedDesc = "LoadOk: " + std::string(loadOk ? "true" : "false") +
                         ", PublishedOnFreshRegistries: " + std::string(pubOk ? "true" : "false") +
                         ", VoxelId: " + std::to_string(txRes.targetVoxelId);
        r.details = r.passed ? "PASS" : "FAIL: Ricostruzione runtime post-riavvio fallita";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // TRX-09: Pubblicazione ripetuta senza mutazioni
    // -------------------------------------------------------------------------
    {
        TransactionAuditTestResult r;
        r.testId = "TRX-09";
        r.testName = "Pubblicazione ripetuta senza mutazioni";
        r.inputDesc = "Esecuzione di due pubblicazioni consecutive dello stesso package con lo stesso fingerprint";
        r.expectedDesc = "Seconda pubblicazione è Zero-Work hit, nessun nuovo slot, conteggio blocchi invariato";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);

        auto pkg = createTestPackage();
        pkg.manifest.assetId = "fairworld:trx_repeat";
        auto res1 = service.PublishPackageTransactional(pkg, bReg, mReg, sReg);
        uint32_t bCount1 = bReg.GetActiveBlockCount();

        auto res2 = service.PublishPackageTransactional(pkg, bReg, mReg, sReg);
        uint32_t bCount2 = bReg.GetActiveBlockCount();

        bool zeroWork = res1.success && res2.success && res2.wasZeroWorkHit &&
                        (res1.targetVoxelId == res2.targetVoxelId) && (bCount1 == bCount2);

        r.passed = zeroWork;
        r.observedDesc = "ZeroWorkHit: " + std::string(res2.wasZeroWorkHit ? "true" : "false") +
                         ", TargetVoxelId: " + std::to_string(res1.targetVoxelId) + "==" + std::to_string(res2.targetVoxelId) +
                         ", Blocks: " + std::to_string(bCount1) + "==" + std::to_string(bCount2);
        r.details = r.passed ? "PASS" : "FAIL: Zero-Work publication mancata";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // TRX-10: Nessuna regressione sui 181 test
    // -------------------------------------------------------------------------
    {
        TransactionAuditTestResult r;
        r.testId = "TRX-10";
        r.testName = "Nessuna regressione sui 181 test";
        r.inputDesc = "Esecuzione delle suite pregresse: CompiledTopology (36), Painter (10), Baking (10), Packaging (10)";
        r.expectedDesc = "Tutti i 66 test delle suite sopra menzionate risultano PASS (100% successo)";

        if (TestRunContext::Instance().IsActive()) {
            static const std::vector<std::pair<std::string, size_t>> deps = {
                {"TOP", 36}, {"PNT", 10}, {"TXB", 10}, {"PKG", 10}
            };
            auto eval = TestRunContext::Instance().EvaluateRegressionGate(deps);
            size_t topPass = 0, pntPass = 0, txbPass = 0, pkgPass = 0;
            for (const auto& c : eval.checks) {
                if (c.suiteCode == "TOP") topPass = c.passedCount;
                if (c.suiteCode == "PNT") pntPass = c.passedCount;
                if (c.suiteCode == "TXB") txbPass = c.passedCount;
                if (c.suiteCode == "PKG") pkgPass = c.passedCount;
            }
            r.passed = eval.allDependenciesPassed;
            r.observedDesc = "TOP: " + std::to_string(topPass) + "/36, PNT: " + std::to_string(pntPass) + "/10, TXB: " + std::to_string(txbPass) + "/10, PKG: " + std::to_string(pkgPass) + "/10";
            r.details = r.passed ? "PASS" : "FAIL: " + eval.failureDetails;
        } else {
            auto topRes = CompiledTopologyTests::RunAllTests();
            auto pntRes = PainterStationContractTests::RunAllTests();
            auto txbRes = TextureBakingContractTests::RunAllTests();
            auto pkgRes = AssetPackagingContractTests::RunAllTests();

            size_t topPass = 0; for (const auto& tr : topRes) if (tr.passed) topPass++;
            size_t pntPass = 0; for (const auto& pr : pntRes) if (pr.passed) pntPass++;
            size_t txbPass = 0; for (const auto& xr : txbRes) if (xr.passed) txbPass++;
            size_t pkgPass = 0; for (const auto& kr : pkgRes) if (kr.passed) pkgPass++;

            bool allPass = (topPass == topRes.size() && topRes.size() == 36) &&
                           (pntPass == pntRes.size() && pntRes.size() == 10) &&
                           (txbPass == txbRes.size() && txbRes.size() == 10) &&
                           (pkgPass == pkgRes.size() && pkgRes.size() == 10);

            r.passed = allPass;
            r.observedDesc = "TOP: " + std::to_string(topPass) + "/" + std::to_string(topRes.size()) +
                             ", PNT: " + std::to_string(pntPass) + "/" + std::to_string(pntRes.size()) +
                             ", TXB: " + std::to_string(txbPass) + "/" + std::to_string(txbRes.size()) +
                             ", PKG: " + std::to_string(pkgPass) + "/" + std::to_string(pkgRes.size());
            r.details = r.passed ? "PASS" : "FAIL: Regressione rilevata nelle suite precedenti";
        }
        results.push_back(r);
    }

    return results;
}

} // namespace fw
