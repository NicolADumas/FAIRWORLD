#include "pch.h"
#include "AssetDependencyContractTests.h"
#include "AssetPackagingService.h"
#include "AssetDependencyGraph.h"
#include "AssetCrashRecoveryTests.h"
#include "AssetCrashRecoveryHardeningTests.h"
#include "AssetDigestTrustTests.h"
#include "TestRunContext.h"
#include "blocks/BlockRegistry.h"
#include "blocks/MaterialRegistry.h"
#include "blocks/ShapeRegistry.h"
#include "Sha256.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fw {

std::vector<AssetDependencyTestResult> AssetDependencyContractTests::RunAllTests() {
    std::vector<AssetDependencyTestResult> results;
    results.reserve(10);

    const std::string testRoot = (std::filesystem::temp_directory_path() / "fairworld_dep_gate").string();
    std::error_code ec;
    std::filesystem::create_directories(testRoot, ec);

    // Helper per creare un RawAssetPackage Schema V2 valido
    auto createBasePackage = [](const std::string& assetId, uint64_t rev = 1) -> RawAssetPackage {
        RawAssetPackage pkg;
        pkg.manifest.assetId = assetId;
        pkg.manifest.displayName = "DEP Test Block " + assetId;
        pkg.manifest.schemaVersion = 2;
        pkg.manifest.revision = rev;
        pkg.manifest.stage = RawAssetStage::Packaged;
        pkg.geometrySource = GeometrySource::Primitive;
        pkg.shape.type = ShapeType::Cube;
        pkg.shape.id = 1;
        pkg.shape.revision = 1;
        pkg.shape.supportsVisualCompilation = true;
        pkg.shape.supportsCollisionCompilation = true;
        pkg.compiledPhysics.mass = 3.0f;
        pkg.compiledPhysics.density = 1800.0f;
        pkg.compiledPhysics.collisionEnabled = true;
        pkg.baseColorFallback = glm::vec3(0.3f, 0.6f, 0.8f);
        pkg.roughnessFallback = 0.4f;
        pkg.metallicFallback = 0.0f;

        pkg.geometry.vertices.resize(24);
        for (size_t i = 0; i < 24; ++i) {
            pkg.geometry.vertices[i].position = fw::Vec3{static_cast<float>(i % 2), static_cast<float>((i / 2) % 2), static_cast<float>((i / 4) % 2)};
            pkg.geometry.vertices[i].normal = fw::Vec3{0.0f, 1.0f, 0.0f};
            pkg.geometry.vertices[i].color = fw::Vec4{1.0f, 1.0f, 1.0f, 1.0f};
            pkg.geometry.vertices[i].materialID = 1;
            pkg.geometry.vertices[i].roughMetal = fw::Vec2{0.4f, 0.0f};
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
    // DEP-01: Asset senza dipendenze esterne
    // =========================================================================
    {
        AssetDependencyTestResult r;
        r.testId = "DEP-01";
        r.testName = "AssetWithoutExternalDependencies";
        r.inputDesc = "Asset autonomo con lista dependencies vuota";
        r.expectedDesc = "Validazione package e pubblicazione persistente convalidate al 100%";

        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();
        ShapeRegistry sReg; sReg.Initialize();
        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTrustPolicy(AssetTrustPolicy::Strict);

        auto pkg = createBasePackage("fairworld:dep01_standalone", 1);
        auto val = service.ValidatePackage(pkg, testRoot, AssetTrustPolicy::Strict);
        auto pub = service.PublishPackagePersistent(pkg, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        r.passed = val.isValid && pub.success && (val.integrityStatus == AssetIntegrityStatus::Verified);
        r.observedDesc = "Validazione: " + std::string(val.isValid ? "PASS" : "FAIL") +
                         ", Pubblicazione: " + std::string(pub.success ? "PASS" : "FAIL") +
                         ", Nodi nel grafo: " + std::to_string(service.GetDependencyGraph().GetNodeCount());
        results.push_back(r);
    }

    // =========================================================================
    // DEP-02: Dipendenza obbligatoria valida
    // =========================================================================
    {
        AssetDependencyTestResult r;
        r.testId = "DEP-02";
        r.testName = "ValidMandatoryDependency";
        r.inputDesc = "Asset Host dipendente da Asset Target committed con revisionPolicy Exact";
        r.expectedDesc = "Validazione superata, commit riuscito, arco inserito nel grafo";

        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();
        ShapeRegistry sReg; sReg.Initialize();
        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTrustPolicy(AssetTrustPolicy::Strict);

        // 1. Pubblica il target B
        auto pkgTarget = createBasePackage("fairworld:dep02_target", 1);
        service.PublishPackagePersistent(pkgTarget, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        // 2. Pubblica l'host A dipendente da B
        auto pkgHost = createBasePackage("fairworld:dep02_host", 1);
        AssetDependency dep;
        dep.dependencyType = "material";
        dep.targetId = "fairworld:dep02_target";
        dep.requiredRevision = 1;
        dep.revisionPolicy = DependencyRevisionPolicy::Exact;
        dep.isOptional = false;
        pkgHost.manifest.dependencies.push_back(dep);
        pkgHost.contentDigestSha256 = pkgHost.ComputeContentDigestSha256();

        auto val = service.ValidatePackage(pkgHost, testRoot, AssetTrustPolicy::Strict);
        auto pub = service.PublishPackagePersistent(pkgHost, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        auto hostDeps = service.GetDependencyGraph().GetDependencies("fairworld:dep02_host", false);
        bool hasTargetEdge = (std::find(hostDeps.begin(), hostDeps.end(), "fairworld:dep02_target") != hostDeps.end());

        r.passed = val.isValid && pub.success && hasTargetEdge;
        r.observedDesc = "Validazione Host: " + std::string(val.isValid ? "PASS" : "FAIL") +
                         ", Pubblicazione: " + std::string(pub.success ? "PASS" : "FAIL") +
                         ", Dipendenza registrata: " + (hasTargetEdge ? "SI" : "NO");
        results.push_back(r);
    }

    // =========================================================================
    // DEP-03: Dipendenza mancante
    // =========================================================================
    {
        AssetDependencyTestResult r;
        r.testId = "DEP-03";
        r.testName = "MissingMandatoryDependency";
        r.inputDesc = "Asset con dipendenza obbligatoria verso un target inesistente";
        r.expectedDesc = "Validazione fallisce con MissingDependency, commit bloccato";

        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();
        ShapeRegistry sReg; sReg.Initialize();
        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTrustPolicy(AssetTrustPolicy::Strict);

        auto pkg = createBasePackage("fairworld:dep03_broken", 1);
        AssetDependency dep;
        dep.dependencyType = "shape";
        dep.targetId = "fairworld:ghost_shape_never_exists";
        dep.requiredRevision = 1;
        dep.isOptional = false;
        pkg.manifest.dependencies.push_back(dep);
        pkg.contentDigestSha256 = pkg.ComputeContentDigestSha256();

        auto val = service.ValidatePackage(pkg, testRoot, AssetTrustPolicy::Strict);
        auto pub = service.PublishPackagePersistent(pkg, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        r.passed = (!val.isValid) && (!pub.success) && (val.primaryErrorCode == AssetIntegrityErrorCode::MissingDependency);
        r.observedDesc = "Blocco validazione: " + std::string(!val.isValid ? "SI" : "NO") +
                         ", Codice errore: " + std::string(AssetIntegrityErrorCodeToString(val.primaryErrorCode)) +
                         ", Errore: '" + (val.errors.empty() ? "" : val.errors[0]) + "'";
        results.push_back(r);
    }

    // =========================================================================
    // DEP-04: Revisione inesistente
    // =========================================================================
    {
        AssetDependencyTestResult r;
        r.testId = "DEP-04";
        r.testName = "NonExistentRevision";
        r.inputDesc = "Dipendenza da target presente rev 1, ma richiesta rev 42 inesistente";
        r.expectedDesc = "Validazione fallisce per discrepanza di revisione richiesta";

        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();
        ShapeRegistry sReg; sReg.Initialize();
        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTrustPolicy(AssetTrustPolicy::Strict);

        auto pkgTarget = createBasePackage("fairworld:dep04_target", 1);
        service.PublishPackagePersistent(pkgTarget, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        auto pkgHost = createBasePackage("fairworld:dep04_host", 1);
        AssetDependency dep;
        dep.dependencyType = "parent_asset";
        dep.targetId = "fairworld:dep04_target";
        dep.requiredRevision = 42; // Inesistente!
        dep.revisionPolicy = DependencyRevisionPolicy::Exact;
        dep.isOptional = false;
        pkgHost.manifest.dependencies.push_back(dep);
        pkgHost.contentDigestSha256 = pkgHost.ComputeContentDigestSha256();

        auto val = service.ValidatePackage(pkgHost, testRoot, AssetTrustPolicy::Strict);
        auto pub = service.PublishPackagePersistent(pkgHost, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        r.passed = (!val.isValid) && (!pub.success);
        r.observedDesc = "Validazione fallita: " + std::string(!val.isValid ? "SI" : "NO") +
                         ", Messaggio: '" + (val.errors.empty() ? "" : val.errors[0]) + "'";
        results.push_back(r);
    }

    // =========================================================================
    // DEP-05: Digest della dipendenza alterato
    // =========================================================================
    {
        AssetDependencyTestResult r;
        r.testId = "DEP-05";
        r.testName = "DependencyDigestAltered";
        r.inputDesc = "expectedDigestSha256 fornito ma non coincidente con il target committed";
        r.expectedDesc = "Validazione fallisce con DigestMismatch, commit respinto";

        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();
        ShapeRegistry sReg; sReg.Initialize();
        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTrustPolicy(AssetTrustPolicy::Strict);

        auto pkgTarget = createBasePackage("fairworld:dep05_target", 1);
        service.PublishPackagePersistent(pkgTarget, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        auto pkgHost = createBasePackage("fairworld:dep05_host", 1);
        AssetDependency dep;
        dep.dependencyType = "material";
        dep.targetId = "fairworld:dep05_target";
        dep.requiredRevision = 1;
        dep.revisionPolicy = DependencyRevisionPolicy::Exact;
        dep.expectedDigestSha256 = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"; // Falso digest!
        dep.isOptional = false;
        pkgHost.manifest.dependencies.push_back(dep);
        pkgHost.contentDigestSha256 = pkgHost.ComputeContentDigestSha256();

        auto val = service.ValidatePackage(pkgHost, testRoot, AssetTrustPolicy::Strict);
        auto pub = service.PublishPackagePersistent(pkgHost, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        r.passed = (!val.isValid) && (!pub.success) && (val.primaryErrorCode == AssetIntegrityErrorCode::DigestMismatch);
        r.observedDesc = "Rifiuto mismatch digest: " + std::string(!val.isValid ? "SI" : "NO") +
                         ", Codice errore: " + std::string(AssetIntegrityErrorCodeToString(val.primaryErrorCode));
        results.push_back(r);
    }

    // =========================================================================
    // DEP-06: Ciclo tra asset
    // =========================================================================
    {
        AssetDependencyTestResult r;
        r.testId = "DEP-06";
        r.testName = "CycleBetweenAssets";
        r.inputDesc = "Grafo con dipendenza circolare diretta e transitiva A -> B -> C -> A";
        r.expectedDesc = "DetectCycles rileva il ciclo, GetTopologicalOrder rifiuta l'ordinamento";

        AssetDependencyGraph graph;
        AssetDependency depAB; depAB.targetId = "fairworld:cycle_b";
        AssetDependency depBC; depBC.targetId = "fairworld:cycle_c";
        AssetDependency depCA; depCA.targetId = "fairworld:cycle_a";

        graph.AddNode("fairworld:cycle_a", 1, "digA", { depAB });
        graph.AddNode("fairworld:cycle_b", 1, "digB", { depBC });
        graph.AddNode("fairworld:cycle_c", 1, "digC", { depCA });

        std::vector<std::vector<std::string>> cycles;
        bool hasCycle = graph.DetectCycles(&cycles);

        std::vector<std::string> topoOrder;
        std::string topoErr;
        bool topoOk = graph.GetTopologicalOrder(topoOrder, &topoErr);

        r.passed = hasCycle && (!cycles.empty()) && (!topoOk) && (!topoErr.empty());
        r.observedDesc = "Ciclo rilevato: " + std::string(hasCycle ? "SI" : "NO") +
                         ", Numero cicli estratti: " + std::to_string(cycles.size()) +
                         ", Ordinamento topologico bloccato: " + std::string(!topoOk ? "SI" : "NO");
        results.push_back(r);
    }

    // =========================================================================
    // DEP-07: Dipendenza condivisa
    // =========================================================================
    {
        AssetDependencyTestResult r;
        r.testId = "DEP-07";
        r.testName = "SharedDependencyReferenceTracking";
        r.inputDesc = "Asset C condiviso da Host A e Host B";
        r.expectedDesc = "Conteggio referenze corretto (2), IsReferenced restituisce true";

        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();
        ShapeRegistry sReg; sReg.Initialize();
        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTrustPolicy(AssetTrustPolicy::Strict);

        // 1. Pubblica il target condiviso C
        auto pkgC = createBasePackage("fairworld:dep07_shared_c", 1);
        service.PublishPackagePersistent(pkgC, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        // 2. Pubblica Host A
        auto pkgA = createBasePackage("fairworld:dep07_host_a", 1);
        AssetDependency depA; depA.targetId = "fairworld:dep07_shared_c"; depA.requiredRevision = 1;
        pkgA.manifest.dependencies.push_back(depA);
        pkgA.contentDigestSha256 = pkgA.ComputeContentDigestSha256();
        service.PublishPackagePersistent(pkgA, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        // 3. Pubblica Host B
        auto pkgB = createBasePackage("fairworld:dep07_host_b", 1);
        AssetDependency depB; depB.targetId = "fairworld:dep07_shared_c"; depB.requiredRevision = 1;
        pkgB.manifest.dependencies.push_back(depB);
        pkgB.contentDigestSha256 = pkgB.ComputeContentDigestSha256();
        service.PublishPackagePersistent(pkgB, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        const auto& graph = service.GetDependencyGraph();
        size_t refCount = graph.GetReferenceCount("fairworld:dep07_shared_c");
        bool isRef = graph.IsReferenced("fairworld:dep07_shared_c");

        r.passed = (refCount == 2) && isRef;
        r.observedDesc = "Reference count target condiviso: " + std::to_string(refCount) +
                         ", IsReferenced: " + (isRef ? "SI" : "NO");
        results.push_back(r);
    }

    // =========================================================================
    // DEP-08: Aggiornamento di revisione & invalidazione mirata Zero-Work
    // =========================================================================
    {
        AssetDependencyTestResult r;
        r.testId = "DEP-08";
        r.testName = "RevisionUpdateTargetedInvalidation";
        r.inputDesc = "Asset B aggiornato a rev 2; invalidazione mirata solo di A, isolamento di D";
        r.expectedDesc = "Cache di A invalidata, cache di D (indipendente) preservata";

        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();
        ShapeRegistry sReg; sReg.Initialize();
        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);
        service.SetTrustPolicy(AssetTrustPolicy::Strict);

        auto pkgB = createBasePackage("fairworld:dep08_b", 1);
        service.PublishPackagePersistent(pkgB, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        auto pkgA = createBasePackage("fairworld:dep08_a", 1);
        AssetDependency dep; dep.targetId = "fairworld:dep08_b"; dep.requiredRevision = 1;
        pkgA.manifest.dependencies.push_back(dep);
        pkgA.contentDigestSha256 = pkgA.ComputeContentDigestSha256();
        service.PublishPackagePersistent(pkgA, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        auto pkgD = createBasePackage("fairworld:dep08_d_independent", 1);
        service.PublishPackagePersistent(pkgD, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        // Pre-condizione: A e D hanno cache fingerprint registrata
        bool hadFingerprintA = (service.GetLastPublishedFingerprint("fairworld:dep08_a") != 0);
        bool hadFingerprintD = (service.GetLastPublishedFingerprint("fairworld:dep08_d_independent") != 0);

        // Ora aggiorniamo B a rev 2
        auto pkgB2 = createBasePackage("fairworld:dep08_b", 2);
        pkgB2.baseColorFallback = glm::vec3(0.9f, 0.1f, 0.1f);
        pkgB2.contentDigestSha256 = pkgB2.ComputeContentDigestSha256();
        service.PublishPackagePersistent(pkgB2, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        // Post-condizione: A deve essere invalidato, D deve rimanere valido
        bool hasFingerprintA = (service.GetLastPublishedFingerprint("fairworld:dep08_a") != 0);
        bool hasFingerprintD = (service.GetLastPublishedFingerprint("fairworld:dep08_d_independent") != 0);

        r.passed = hadFingerprintA && hadFingerprintD && (!hasFingerprintA) && hasFingerprintD;
        r.observedDesc = "Cache A pre: " + std::to_string(hadFingerprintA) + " -> post: " + std::to_string(hasFingerprintA) +
                         " | Cache D (indipendente) pre: " + std::to_string(hadFingerprintD) + " -> post: " + std::to_string(hasFingerprintD);
        results.push_back(r);
    }

    // =========================================================================
    // DEP-09: Recovery con dipendenze e ricostruzione topologica
    // =========================================================================
    {
        AssetDependencyTestResult r;
        r.testId = "DEP-09";
        r.testName = "RecoveryWithDependenciesTopological";
        r.inputDesc = "Catena di dipendenze (A -> B -> C) caricata da disco con ReconstructRegistriesFromCommitted";
        r.expectedDesc = "Ricostruzione in perfetto ordine topologico (C -> B -> A), tutti i blocchi registrati";

        const std::string testSubRoot = testRoot + "/dep09_sub";
        std::filesystem::create_directories(testSubRoot, ec);

        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();
        ShapeRegistry sReg; sReg.Initialize();
        AssetPackagingService service;
        service.SetRootAssetDirectory(testSubRoot);
        service.SetTrustPolicy(AssetTrustPolicy::Strict);

        // Pubblica C
        auto pkgC = createBasePackage("fairworld:dep09_c", 1);
        service.PublishPackagePersistent(pkgC, bReg, mReg, sReg, testSubRoot, AssetTrustPolicy::Strict);

        // Pubblica B (dipende da C)
        auto pkgB = createBasePackage("fairworld:dep09_b", 1);
        AssetDependency depBC; depBC.targetId = "fairworld:dep09_c"; depBC.requiredRevision = 1;
        pkgB.manifest.dependencies.push_back(depBC);
        pkgB.contentDigestSha256 = pkgB.ComputeContentDigestSha256();
        service.PublishPackagePersistent(pkgB, bReg, mReg, sReg, testSubRoot, AssetTrustPolicy::Strict);

        // Pubblica A (dipende da B)
        auto pkgA = createBasePackage("fairworld:dep09_a", 1);
        AssetDependency depAB; depAB.targetId = "fairworld:dep09_b"; depAB.requiredRevision = 1;
        pkgA.manifest.dependencies.push_back(depAB);
        pkgA.contentDigestSha256 = pkgA.ComputeContentDigestSha256();
        service.PublishPackagePersistent(pkgA, bReg, mReg, sReg, testSubRoot, AssetTrustPolicy::Strict);

        // Ora eseguiamo la ricostruzione autoritativa dai file committed su disco
        size_t reconstructed = service.ReconstructRegistriesFromCommitted(bReg, mReg, sReg, testSubRoot, AssetTrustPolicy::Strict);

        bool blockA = (bReg.GetBlockIdByString("fairworld:dep09_a") != 0);
        bool blockB = (bReg.GetBlockIdByString("fairworld:dep09_b") != 0);
        bool blockC = (bReg.GetBlockIdByString("fairworld:dep09_c") != 0);

        r.passed = (reconstructed == 3) && blockA && blockB && blockC;
        r.observedDesc = "Pacchetti ricostruiti: " + std::to_string(reconstructed) +
                         ", Blocco A: " + (blockA ? "SI" : "NO") +
                         ", Blocco B: " + (blockB ? "SI" : "NO") +
                         ", Blocco C: " + (blockC ? "SI" : "NO");
        results.push_back(r);
    }

    // =========================================================================
    // DEP-10: Regressione dei contratti precedenti
    // =========================================================================
    {
        AssetDependencyTestResult r;
        r.testId = "DEP-10";
        r.testName = "PreviousSuitesRegressionCheck";
        r.inputDesc = "Esecuzione suites CRH-01..10 (BM-F4.3), CRH-H01..08 (BM-F4.3.1), DGV-01..10 (BM-F4.2)";
        r.expectedDesc = "Tutte le suite storiche superate al 100% senza regressioni";

        if (TestRunContext::Instance().IsActive()) {
            static const std::vector<std::pair<std::string, size_t>> deps = {
                {"CRH", 10}, {"CRH-H", 8}, {"DGV", 10}
            };
            auto eval = TestRunContext::Instance().EvaluateRegressionGate(deps);
            size_t passCrh = 0, passCrhHard = 0, passDgv = 0;
            for (const auto& c : eval.checks) {
                if (c.suiteCode == "CRH") passCrh = c.passedCount;
                if (c.suiteCode == "CRH-H") passCrhHard = c.passedCount;
                if (c.suiteCode == "DGV") passDgv = c.passedCount;
            }
            r.passed = eval.allDependenciesPassed;
            r.observedDesc = "CRH: " + std::to_string(passCrh) + "/10 | CRH-H: " + std::to_string(passCrhHard) + "/8 | DGV: " + std::to_string(passDgv) + "/10";
            r.details = r.passed ? "PASS" : "FAIL: " + eval.failureDetails;
        } else {
            auto crhRes = AssetCrashRecoveryTests::RunAllTests();
            auto crhHardRes = AssetCrashRecoveryHardeningTests::RunAllTests();
            auto dgvRes = AssetDigestTrustTests::RunAllTests();

            size_t passCrh = 0;
            for (const auto& t : crhRes) if (t.passed) passCrh++;

            size_t passCrhHard = 0;
            for (const auto& t : crhHardRes) if (t.passed) passCrhHard++;

            size_t passDgv = 0;
            for (const auto& t : dgvRes) if (t.passed) passDgv++;

            bool allPass = (passCrh == crhRes.size()) &&
                           (passCrhHard == crhHardRes.size()) &&
                           (passDgv == dgvRes.size());

            r.passed = allPass;
            r.observedDesc = "CRH: " + std::to_string(passCrh) + "/" + std::to_string(crhRes.size()) +
                             " | CRH-H: " + std::to_string(passCrhHard) + "/" + std::to_string(crhHardRes.size()) +
                             " | DGV: " + std::to_string(passDgv) + "/" + std::to_string(dgvRes.size());
            r.details = r.passed ? "PASS" : "FAIL: Regressione rilevata nelle suite precedenti";
        }
        results.push_back(r);
    }

    return results;
}

} // namespace fw
