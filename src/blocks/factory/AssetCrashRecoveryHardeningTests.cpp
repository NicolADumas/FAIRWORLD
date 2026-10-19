#include "pch.h"
#include "AssetCrashRecoveryHardeningTests.h"
#include "FactoryOrchestrator.h"
#include "AssetPackagingService.h"
#include "AssetFilesystemAdapter.h"
#include "blocks/BlockRegistry.h"
#include "blocks/MaterialRegistry.h"
#include "blocks/ShapeRegistry.h"
#include "Sha256.h"

#include <filesystem>
#include <fstream>
#include <chrono>

namespace fw {

std::vector<CrashRecoveryHardeningTestResult> AssetCrashRecoveryHardeningTests::RunAllTests() {
    std::vector<CrashRecoveryHardeningTestResult> results;
    results.reserve(8);

    const std::filesystem::path tempBase = std::filesystem::temp_directory_path();
    const std::filesystem::path testRootPath = tempBase / "fairworld_crh_hardening_gate";
    const std::string testRoot = testRootPath.string();

    std::error_code ec;
    // FASE 1: Protezione e verifica sicurezza percorso fixture
    bool safeToClean = !testRoot.empty() &&
                       (testRootPath != tempBase) &&
                       (testRootPath.filename() == "fairworld_crh_hardening_gate");

    if (std::filesystem::exists(testRootPath, ec)) {
        if (std::filesystem::is_symlink(testRootPath, ec)) {
            safeToClean = false;
        }
    }

    if (!safeToClean) {
        CrashRecoveryHardeningTestResult abortRes;
        abortRes.testId = "CRH-H00";
        abortRes.testName = "SuiteFixtureIsolationSafetyCheck";
        abortRes.inputDesc = testRoot;
        abortRes.expectedDesc = "Directory fixture sicura in temp_directory_path: fairworld_crh_hardening_gate";
        abortRes.observedDesc = "Verifica sicurezza fallita: percorso non sicuro o symlink rilevato";
        abortRes.passed = false;
        abortRes.details = "ABORT: Impossibile procedere con la pulizia della fixture. Percorso rifiutato: " + testRoot;
        results.push_back(abortRes);
        return results;
    }

    std::filesystem::remove_all(testRootPath, ec);
    ec.clear();
    std::filesystem::create_directories(testRootPath, ec);
    if (ec) {
        CrashRecoveryHardeningTestResult abortRes;
        abortRes.testId = "CRH-H00";
        abortRes.testName = "SuiteFixtureIsolationSafetyCheck";
        abortRes.inputDesc = testRoot;
        abortRes.expectedDesc = "Creazione directory fixture riuscita";
        abortRes.observedDesc = "Errore filesystem: " + ec.message();
        abortRes.passed = false;
        abortRes.details = "ABORT: Errore filesystem creazione directory fixture: " + ec.message();
        results.push_back(abortRes);
        return results;
    }

    // Helper per creare un RawAssetPackage Schema V2 valido
    auto createBasePackage = [](const std::string& assetId = "fairworld:crh_h_base_block", uint64_t rev = 1) -> RawAssetPackage {
        RawAssetPackage pkg;
        pkg.manifest.assetId = assetId;
        pkg.manifest.displayName = "CRH Hardening Block";
        pkg.manifest.schemaVersion = 2;
        pkg.manifest.revision = rev;
        pkg.manifest.stage = RawAssetStage::Packaged;
        pkg.geometrySource = GeometrySource::Primitive;
        pkg.shape.type = ShapeType::Cube;
        pkg.shape.id = 1;
        pkg.shape.revision = 1;
        pkg.shape.supportsVisualCompilation = true;
        pkg.shape.supportsCollisionCompilation = true;
        pkg.compiledPhysics.mass = 2.0f;
        pkg.compiledPhysics.density = 1500.0f;
        pkg.compiledPhysics.collisionEnabled = true;
        pkg.baseColorFallback = glm::vec3(0.5f, 0.7f, 0.2f);
        pkg.roughnessFallback = 0.5f;
        pkg.metallicFallback = 0.0f;

        pkg.geometry.vertices.resize(24);
        for (size_t i = 0; i < 24; ++i) {
            pkg.geometry.vertices[i].position = fw::Vec3{static_cast<float>(i % 2), static_cast<float>((i / 2) % 2), static_cast<float>((i / 4) % 2)};
            pkg.geometry.vertices[i].normal = fw::Vec3{0.0f, 1.0f, 0.0f};
            pkg.geometry.vertices[i].color = fw::Vec4{1.0f, 1.0f, 1.0f, 1.0f};
            pkg.geometry.vertices[i].materialID = 1;
            pkg.geometry.vertices[i].roughMetal = fw::Vec2{0.5f, 0.0f};
            pkg.geometry.vertices[i].ao = 1.0f;
            pkg.geometry.vertices[i].light = 1.0f;
            pkg.geometry.vertices[i].emissive = 0.0f;
        }
        pkg.geometry.vertexCount = 24;

        pkg.geometry.faces.resize(12);
        for (size_t i = 0; i < 12; ++i) {
            pkg.geometry.faces[i].surfaceId = static_cast<uint32_t>(i % 6);
            pkg.geometry.faces[i].matIndex = 0;
            pkg.geometry.faces[i].indices = { static_cast<int>(i * 2 % 24), static_cast<int>((i * 2 + 1) % 24), static_cast<int>((i * 2 + 2) % 24) };
        }
        pkg.geometry.triangleCount = 12;

        pkg.contentDigestSha256 = pkg.ComputeContentDigestSha256();
        pkg.packageFingerprint = pkg.ComputeDistributionChecksum();
        return pkg;
    };

    // =========================================================================
    // CRH-H01: Crash tra commit.journal e active pointer
    // =========================================================================
    {
        CrashRecoveryHardeningTestResult r;
        r.testId = "CRH-H01";
        r.testName = "CrashBetweenJournalAndActivePointer";
        r.inputDesc = "Fault injection FailBetweenCommitAndActivePointer";
        r.expectedDesc = "Revisione committed presente, active pointer mancante, recovery riconcilia l'attivo";

        // FASE 2: Isolamento preventivo dell'asset: garantisce assenza di active pointer precedente e revisioni residue
        std::filesystem::path pubAssetDir = std::filesystem::path(testRoot) / "published" / "fairworld_crh_h01_block";
        if (std::filesystem::exists(pubAssetDir, ec)) {
            std::filesystem::remove_all(pubAssetDir, ec);
        }

        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();
        ShapeRegistry sReg; sReg.Initialize();
        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTrustPolicy(AssetTrustPolicy::Strict);
        service.SetTransactionFaultInjection(TransactionFaultPoint::FailBetweenCommitAndActivePointer);

        const uint64_t expectedRevision = 1;
        auto pkg = createBasePackage("fairworld:crh_h01_block", expectedRevision);
        auto pubRes = service.PublishPackagePersistent(pkg, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        bool publishReportedCrash = !pubRes.success;
        bool isCommitted = service.IsRevisionCommitted("fairworld:crh_h01_block", expectedRevision, testRoot);
        auto activeBefore = service.GetActiveRevision("fairworld:crh_h01_block", testRoot);

        // Ora eseguiamo il recovery
        service.SetTransactionFaultInjection(TransactionFaultPoint::None);
        auto report = service.RunCrashRecovery(&bReg, &mReg, &sReg, testRoot, AssetTrustPolicy::Strict);
        auto activeAfter = service.GetActiveRevision("fairworld:crh_h01_block", testRoot);

        r.passed = publishReportedCrash && isCommitted && !activeBefore.has_value() &&
                   report.reconciledActivePointers > 0 && activeAfter.has_value() && *activeAfter == expectedRevision;
        r.observedDesc = "Crash rilevato: " + std::string(publishReportedCrash ? "SI" : "NO") +
                         ", Committed: " + (isCommitted ? "SI" : "NO") +
                         ", Active pre-recovery: " + (activeBefore.has_value() ? std::to_string(*activeBefore) : "NULL") +
                         ", Active post-recovery: " + (activeAfter.has_value() ? std::to_string(*activeAfter) : "NULL") +
                         ", Riconciliazioni: " + std::to_string(report.reconciledActivePointers);

        // FASE 3: Diagnostica dettagliata con scomposizione completa degli stati
        r.details = "publishReportedCrash=" + std::string(publishReportedCrash ? "true" : "false") +
                    ", isCommitted=" + (isCommitted ? "true" : "false") +
                    ", activeBefore=" + (activeBefore.has_value() ? std::to_string(*activeBefore) : "nullopt") +
                    ", reconciledActivePointers=" + std::to_string(report.reconciledActivePointers) +
                    ", activeAfter=" + (activeAfter.has_value() ? std::to_string(*activeAfter) : "nullopt") +
                    ", expectedRevision=" + std::to_string(expectedRevision);
        if (!r.passed) {
            r.details = "FAIL: " + r.details;
        } else {
            r.details = "PASS: " + r.details;
        }
        results.push_back(r);
    }

    // =========================================================================
    // CRH-H02: Journal committed con active pointer mancante
    // =========================================================================
    {
        CrashRecoveryHardeningTestResult r;
        r.testId = "CRH-H02";
        r.testName = "JournalCommittedWithMissingActivePointer";
        r.inputDesc = "Rimozione manuale di active.rawpkg con rev_1 committed intatta";
        r.expectedDesc = "Recovery identifica rev_1 committed e ricostruisce active pointer";

        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();
        ShapeRegistry sReg; sReg.Initialize();
        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTrustPolicy(AssetTrustPolicy::Strict);

        auto pkg = createBasePackage("fairworld:crh_h02_block", 1);
        service.PublishPackagePersistent(pkg, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        // Rimuoviamo active.rawpkg e active_journal.json
        std::filesystem::path pubAssetDir = std::filesystem::path(testRoot) / "published" / "fairworld_crh_h02_block";
        std::filesystem::remove(pubAssetDir / "active.rawpkg", ec);
        std::filesystem::remove(pubAssetDir / "active_journal.json", ec);

        auto activePre = service.GetActiveRevision("fairworld:crh_h02_block", testRoot);
        auto report = service.RunCrashRecovery(&bReg, &mReg, &sReg, testRoot, AssetTrustPolicy::Strict);
        auto activePost = service.GetActiveRevision("fairworld:crh_h02_block", testRoot);

        r.passed = !activePre.has_value() && report.reconciledActivePointers > 0 &&
                   activePost.has_value() && *activePost == 1;
        r.observedDesc = "Active pre-recovery: " + (activePre.has_value() ? std::to_string(*activePre) : "ASSENTE") +
                         ", Riconciliato: " + (activePost.has_value() ? "rev_" + std::to_string(*activePost) : "FALLITO");
        r.details = r.passed ? ("PASS: " + r.observedDesc) : ("FAIL: " + r.observedDesc + ", reconciled=" + std::to_string(report.reconciledActivePointers));
        results.push_back(r);
    }

    // =========================================================================
    // CRH-H03: Active pointer verso revisione non committed
    // =========================================================================
    {
        CrashRecoveryHardeningTestResult r;
        r.testId = "CRH-H03";
        r.testName = "ActivePointerToUncommittedRevision";
        r.inputDesc = "active.rawpkg manomesso a rev_999 senza journal corrispondente";
        r.expectedDesc = "Divergenza rilevata: active non committed scartato, fallback a rev_1 committed";

        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();
        ShapeRegistry sReg; sReg.Initialize();
        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTrustPolicy(AssetTrustPolicy::Strict);

        auto pkg = createBasePackage("fairworld:crh_h03_block", 1);
        service.PublishPackagePersistent(pkg, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        // Manomettiamo active.rawpkg scrivendo revisione 999 senza commit.journal in rev_999
        auto forgedPkg = createBasePackage("fairworld:crh_h03_block", 999);
        std::filesystem::path pubAssetDir = std::filesystem::path(testRoot) / "published" / "fairworld_crh_h03_block";
        forgedPkg.SaveToFile((pubAssetDir / "active.rawpkg").string());

        // GetActiveRevision deve rifiutare di fidarsi di active.rawpkg non convalidato da commit.journal
        auto activeForged = service.GetActiveRevision("fairworld:crh_h03_block", testRoot);

        auto report = service.RunCrashRecovery(&bReg, &mReg, &sReg, testRoot, AssetTrustPolicy::Strict);
        auto activePost = service.GetActiveRevision("fairworld:crh_h03_block", testRoot);

        r.passed = !activeForged.has_value() && report.uncommittedActivePointersDiscarded > 0 &&
                   activePost.has_value() && *activePost == 1;
        r.observedDesc = "Falsa revisione accettata: " + std::string(activeForged.has_value() ? "SI (ERRORE)" : "NO (RIFIUTATA)") +
                         ", Puntatori non committed scartati: " + std::to_string(report.uncommittedActivePointersDiscarded) +
                         ", Revisione ripristinata: " + (activePost.has_value() ? "rev_" + std::to_string(*activePost) : "NESSUNA");
        r.details = r.passed ? ("PASS: " + r.observedDesc) : ("FAIL: " + r.observedDesc);
        results.push_back(r);
    }

    // =========================================================================
    // CRH-H04: Rename / Replace fallito
    // =========================================================================
    {
        CrashRecoveryHardeningTestResult r;
        r.testId = "CRH-H04";
        r.testName = "AtomicReplaceFailureHandled";
        r.inputDesc = "AtomicReplace con sorgente inesistente o target locked";
        r.expectedDesc = "Ritorna false con messaggio diagnostico, nessun crash, file target intatto";

        std::filesystem::path testFile = std::filesystem::path(testRoot) / "replace_target.txt";
        std::filesystem::path srcNonExistent = std::filesystem::path(testRoot) / "non_existent_src.tmp";

        {
            std::ofstream out(testFile);
            out << "TARGET_ORIGINALE_INTATTO";
        }

        std::string err;
        bool ok = AssetFilesystemAdapter::AtomicReplace(srcNonExistent, testFile, &err);

        // Verifichiamo che il target originale sia ancora integro
        std::string content;
        {
            std::ifstream in(testFile);
            in >> content;
        }

        r.passed = (!ok) && (!err.empty()) && (content == "TARGET_ORIGINALE_INTATTO");
        r.observedDesc = "AtomicReplace fallito in modo sicuro: " + std::string(!ok ? "SI" : "NO") +
                         ", Errore riportato: '" + err + "'" +
                         ", Contenuto preservato: " + content;
        r.details = r.passed ? ("PASS: " + r.observedDesc) : ("FAIL: " + r.observedDesc);
        results.push_back(r);
    }

    // =========================================================================
    // CRH-H05: Staging con symlink o directory junction verso directory esterna
    // =========================================================================
    {
        CrashRecoveryHardeningTestResult r;
        r.testId = "CRH-H05";
        r.testName = "StagingSymlinkSafeRecovery";
        r.inputDesc = "Symlink o file orfano in staging puntante a directory esterna";
        r.expectedDesc = "SafeRemoveAll non attraversa ne' cancella contenuti esterni";

        std::filesystem::path externalDir = std::filesystem::temp_directory_path() / "fairworld_external_canary_crh";
        std::filesystem::create_directories(externalDir, ec);
        std::filesystem::path canaryFile = externalDir / "canary.txt";
        {
            std::ofstream out(canaryFile);
            out << "CANARINO_NON_TOCCARE";
        }

        std::filesystem::path stagingTest = std::filesystem::path(testRoot) / ".staging" / "tx_test_h05_junction";
        std::filesystem::create_directories(stagingTest, ec);

        // Creiamo un symlink o directory di test in staging
        std::filesystem::path linkPath = stagingTest / "external_link";
        std::filesystem::create_directory_symlink(externalDir, linkPath, ec);

        std::string remErr;
        bool remOk = AssetFilesystemAdapter::SafeRemoveAll(stagingTest, testRoot, &remErr);
        bool canaryAlive = std::filesystem::exists(canaryFile, ec);

        r.passed = remOk && canaryAlive && !std::filesystem::exists(stagingTest, ec);
        r.observedDesc = "SafeRemoveAll eseguito: " + std::string(remOk ? "SI" : "NO") +
                         ", File canarino esterno preservato: " + (canaryAlive ? "SI" : "NO (CANCELLATO!)") +
                         ", Staging rimosso: " + (!std::filesystem::exists(stagingTest, ec) ? "SI" : "NO");
        r.details = r.passed ? ("PASS: " + r.observedDesc) : ("FAIL: " + r.observedDesc + ", err=" + remErr);
        std::filesystem::remove_all(externalDir, ec);
        results.push_back(r);
    }

    // =========================================================================
    // CRH-H06: Recovery ripetuto (Idempotenza)
    // =========================================================================
    {
        CrashRecoveryHardeningTestResult r;
        r.testId = "CRH-H06";
        r.testName = "RepeatedRecoveryIdempotence";
        r.inputDesc = "Tre esecuzioni consecutive di RunCrashRecovery sullo stesso stato";
        r.expectedDesc = "Le esecuzioni successive producono 0 transazioni scartate e 0 mutazioni";

        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();
        ShapeRegistry sReg; sReg.Initialize();
        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTrustPolicy(AssetTrustPolicy::Strict);

        auto rep1 = service.RunCrashRecovery(&bReg, &mReg, &sReg, testRoot, AssetTrustPolicy::Strict);
        auto rep2 = service.RunCrashRecovery(&bReg, &mReg, &sReg, testRoot, AssetTrustPolicy::Strict);
        auto rep3 = service.RunCrashRecovery(&bReg, &mReg, &sReg, testRoot, AssetTrustPolicy::Strict);

        bool rep2Zero = (rep2.incompleteTransactionsDiscarded == 0) && (rep2.reconciledActivePointers == 0);
        bool rep3Zero = (rep3.incompleteTransactionsDiscarded == 0) && (rep3.reconciledActivePointers == 0);

        r.passed = rep1.success && rep2.success && rep3.success && rep2Zero && rep3Zero &&
                   (rep2.committedPackagesRecovered == rep3.committedPackagesRecovered);
        r.observedDesc = "Pass 1: " + std::to_string(rep1.committedPackagesRecovered) + " rec, " + std::to_string(rep1.incompleteTransactionsDiscarded) + " scartati | " +
                         "Pass 2: " + std::to_string(rep2.committedPackagesRecovered) + " rec, " + std::to_string(rep2.incompleteTransactionsDiscarded) + " scartati | " +
                         "Pass 3: " + std::to_string(rep3.committedPackagesRecovered) + " rec, " + std::to_string(rep3.incompleteTransactionsDiscarded) + " scartati";
        r.details = r.passed ? ("PASS: " + r.observedDesc) : ("FAIL: " + r.observedDesc);
        results.push_back(r);
    }

    // =========================================================================
    // CRH-H07: Registry reconstruction idempotente
    // =========================================================================
    {
        CrashRecoveryHardeningTestResult r;
        r.testId = "CRH-H07";
        r.testName = "RegistryReconstructionIdempotence";
        r.inputDesc = "ReconstructRegistriesFromCommitted eseguito due volte consecutive";
        r.expectedDesc = "Stesso numero esatto di blocchi registrati, nessun blocco o shape duplicato";

        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();
        ShapeRegistry sReg; sReg.Initialize();
        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTrustPolicy(AssetTrustPolicy::Strict);

        size_t count1 = service.ReconstructRegistriesFromCommitted(bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);
        size_t blocks1 = bReg.GetActiveBlockCount();

        size_t count2 = service.ReconstructRegistriesFromCommitted(bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);
        size_t blocks2 = bReg.GetActiveBlockCount();

        r.passed = (count1 > 0) && (count1 == count2) && (blocks1 == blocks2);
        r.observedDesc = "Pass 1: " + std::to_string(count1) + " pacchetti (" + std::to_string(blocks1) + " blocchi) | " +
                         "Pass 2: " + std::to_string(count2) + " pacchetti (" + std::to_string(blocks2) + " blocchi)";
        r.details = r.passed ? ("PASS: " + r.observedDesc) : ("FAIL: " + r.observedDesc);
        results.push_back(r);
    }

    // =========================================================================
    // CRH-H08: Errore di ricostruzione senza registri parziali
    // =========================================================================
    {
        CrashRecoveryHardeningTestResult r;
        r.testId = "CRH-H08";
        r.testName = "ReconstructionErrorNoPartialState";
        r.inputDesc = "Un asset valido e un asset corrotto con digest SHA-256 falsificato";
        r.expectedDesc = "Asset valido caricato, asset corrotto rifiutato senza lasciare shape o block orfani";

        const std::string testSubRoot = testRoot + "/h08_sub";
        std::filesystem::create_directories(testSubRoot, ec);

        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();
        ShapeRegistry sReg; sReg.Initialize();
        AssetPackagingService service;
        service.SetRootAssetDirectory(testSubRoot);
        service.SetTrustPolicy(AssetTrustPolicy::Strict);

        // 1. Pubblica un asset valido
        auto goodPkg = createBasePackage("fairworld:crh_h08_good", 1);
        service.PublishPackagePersistent(goodPkg, bReg, mReg, sReg, testSubRoot, AssetTrustPolicy::Strict);

        // 2. Crea a mano una directory published con asset manomesso (digest corrotto)
        std::filesystem::path corruptDir = std::filesystem::path(testSubRoot) / "published" / "fairworld_crh_h08_corrupt" / "rev_1";
        std::filesystem::create_directories(corruptDir, ec);

        auto corruptPkg = createBasePackage("fairworld:crh_h08_corrupt", 1);
        corruptPkg.contentDigestSha256 = "0000000000000000000000000000000000000000000000000000000000000000";
        corruptPkg.SaveToFile((corruptDir / "package.rawpkg").string());

        AssetTransactionJournal tj;
        tj.transactionId = "tx_corrupt_test_01";
        tj.assetId = "fairworld:crh_h08_corrupt";
        tj.revision = 1;
        tj.state = AssetTransactionState::Committed;
        tj.contentDigestSha256 = "0000000000000000000000000000000000000000000000000000000000000000";
        tj.SaveToFile((corruptDir / "commit.journal").string());

        // Ora eseguiamo la ricostruzione autoritativa con policy Strict
        size_t reconstructed = service.ReconstructRegistriesFromCommitted(bReg, mReg, sReg, testSubRoot, AssetTrustPolicy::Strict);

        bool goodBlockRegistered = (bReg.GetBlockIdByString("fairworld:crh_h08_good") != 0);
        bool corruptBlockRegistered = (bReg.GetBlockIdByString("fairworld:crh_h08_corrupt") != 0);

        r.passed = (reconstructed == 1) && goodBlockRegistered && (!corruptBlockRegistered);
        r.observedDesc = "Pacchetti ricostruiti: " + std::to_string(reconstructed) +
                         ", Asset valido presente: " + std::string(goodBlockRegistered ? "SI" : "NO") +
                         ", Asset corrotto escluso: " + std::string(!corruptBlockRegistered ? "SI" : "NO");
        r.details = r.passed ? ("PASS: " + r.observedDesc) : ("FAIL: " + r.observedDesc);
        results.push_back(r);
    }

    return results;
}

} // namespace fw
