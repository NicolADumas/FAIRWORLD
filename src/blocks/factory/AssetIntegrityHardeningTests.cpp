#include "pch.h"
#include "AssetIntegrityHardeningTests.h"
#include "FactoryOrchestrator.h"
#include "AssetPackagingService.h"
#include "TextureBakingService.h"
#include "blocks/BlockRegistry.h"
#include "blocks/MaterialRegistry.h"
#include "blocks/ShapeRegistry.h"
#include "blocks/texture/ImageBitmap.h"
#include "Sha256.h"

// Suites per verifica SEC-10
#include "blocks/uv/CompiledTopologyTests.h"
#include "PainterStationContractTests.h"
#include "TextureBakingContractTests.h"
#include "AssetPackagingContractTests.h"
#include "TransactionIntegrityAuditTests.h"
#include "SculptedMeshRuntimeBridgeTests.h"
#include "AssetIntegrityRoundTripTests.h"
#include "TestRunContext.h"

#include <filesystem>
#include <fstream>
#include <chrono>

namespace fw {

std::vector<AssetHardeningTestResult> AssetIntegrityHardeningTests::RunAllTests() {
    std::vector<AssetHardeningTestResult> results;
    results.reserve(10);

    const std::string testRoot = (std::filesystem::temp_directory_path() / "fairworld_sec_gate").string();
    std::error_code ec;
    std::filesystem::create_directories(testRoot, ec);
    std::filesystem::create_directories(testRoot + "/textures/blocks", ec);

    // Helper per creare un RawAssetPackage di test valido
    auto createBasePackage = []() -> RawAssetPackage {
        RawAssetPackage pkg;
        pkg.manifest.assetId = "fairworld:sec_base_block";
        pkg.manifest.displayName = "Base Security Asset";
        pkg.manifest.schemaVersion = 2;
        pkg.manifest.revision = 1;
        pkg.manifest.stage = RawAssetStage::Packaged;
        pkg.geometrySource = GeometrySource::Primitive;
        pkg.shape.type = ShapeType::Cube;
        pkg.shape.id = 1;
        pkg.shape.revision = 1;
        pkg.shape.supportsVisualCompilation = true;
        pkg.shape.supportsCollisionCompilation = true;
        pkg.compiledPhysics.mass = 3.0f;
        pkg.compiledPhysics.density = 2500.0f;
        pkg.compiledPhysics.collisionEnabled = true;
        pkg.baseColorFallback = glm::vec3(0.8f, 0.5f, 0.2f);
        pkg.roughnessFallback = 0.6f;
        pkg.metallicFallback = 0.1f;

        // Genera 24 vertici cubo minimi
        pkg.geometry.vertices.resize(24);
        for (size_t i = 0; i < 24; ++i) {
            auto& v = pkg.geometry.vertices[i];
            v.position = Vec3{ static_cast<float>(i % 2), static_cast<float>((i / 2) % 2), static_cast<float>(i / 4) };
            v.color = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
            v.roughMetal = Vec2{ 0.6f, 0.1f };
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
        pkg.packageFingerprint = 1000003ULL * 24;
        pkg.distributionChecksum = pkg.ComputeDistributionChecksum();
        pkg.validationReport.isValid = true;
        return pkg;
    };

    // -------------------------------------------------------------------------
    // SEC-01: Digest stabile su contenuto identico e vettori standard NIST
    // -------------------------------------------------------------------------
    {
        AssetHardeningTestResult r;
        r.testId = "SEC-01";
        r.testName = "Digest stabile su contenuto identico e vettori standard NIST";
        r.inputDesc = "Vettori NIST (empty, 'abc', multi-chunk) e doppia esecuzione su RawAssetPackage";
        r.expectedDesc = "SHA-256 conforme al 100% ai vettori NIST e digest package deterministico";

        // 1. Vettore NIST 1: Stringa vuota
        Sha256 emptyCtx;
        emptyCtx.Update("");
        std::string nistEmpty = emptyCtx.FinalizeHex();
        bool nistEmptyOk = (nistEmpty == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

        // 2. Vettore NIST 2: "abc"
        Sha256 abcCtx;
        abcCtx.Update("abc");
        std::string nistAbc = abcCtx.FinalizeHex();
        bool nistAbcOk = (nistAbc == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

        // 3. Streaming multi-blocco equivalente
        Sha256 streamCtx;
        streamCtx.Update("a", 1);
        streamCtx.Update("bc", 2);
        bool streamOk = (streamCtx.FinalizeHex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

        // 4. Stabilità su RawAssetPackage
        auto pkg = createBasePackage();
        std::string d1 = pkg.ComputeContentDigestSha256(testRoot);
        std::string d2 = pkg.ComputeContentDigestSha256(testRoot);

        bool validFormat = (d1.size() == 64) && (d2.size() == 64);
        bool match = (d1 == d2);

        r.passed = nistEmptyOk && nistAbcOk && streamOk && validFormat && match;
        r.observedDesc = "NistEmpty: " + std::string(nistEmptyOk ? "PASS" : "FAIL") +
                         ", NistAbc: " + std::string(nistAbcOk ? "PASS" : "FAIL") +
                         ", Stream: " + std::string(streamOk ? "PASS" : "FAIL") +
                         ", PackageMatch: " + std::string(match ? "PASS" : "FAIL");
        r.details = r.passed ? "PASS" : "FAIL: Mancanza di conformita' SHA-256 standard NIST o determinismo package";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // SEC-02: Modifica di un byte rilevata
    // -------------------------------------------------------------------------
    {
        AssetHardeningTestResult r;
        r.testId = "SEC-02";
        r.testName = "Modifica di un byte rilevata";
        r.inputDesc = "Alterazione minima di una coordinata vertice (position.x += 0.001f)";
        r.expectedDesc = "Digest SHA-256 radicalmente differente (effetto valanga crittografico)";

        auto pkgA = createBasePackage();
        std::string dA = pkgA.ComputeContentDigestSha256(testRoot);

        auto pkgB = pkgA;
        pkgB.geometry.vertices[0].position.x += 0.001f;
        std::string dB = pkgB.ComputeContentDigestSha256(testRoot);

        bool diff = (dA != dB) && (dB.size() == 64);

        r.passed = diff;
        r.observedDesc = "dA: " + dA.substr(0, 12) + "..., dB: " + dB.substr(0, 12) + "... Diff: " + (diff ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Alterazione del contenuto geometrico non rilevata da SHA-256";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // SEC-03: Digest indipendente dal percorso assoluto host
    // -------------------------------------------------------------------------
    {
        AssetHardeningTestResult r;
        r.testId = "SEC-03";
        r.testName = "Digest indipendente dal percorso assoluto host";
        r.inputDesc = "Asset con texture identica valutato in due root directory host differenti";
        r.expectedDesc = "Digest identico: nessun percorso host locale trapela nel digest canonico";

        const std::string rootA = testRoot + "/host_dir_alpha";
        const std::string rootB = testRoot + "/host_dir_beta";
        std::filesystem::create_directories(rootA + "/textures/blocks", ec);
        std::filesystem::create_directories(rootB + "/textures/blocks", ec);

        // Scrivi la stessa texture 4x4 in entrambe le directory
        ImageBitmap bmp(4, 4, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.3f, 0.6f, 0.9f, 1.0f));
        auto pngData = bmp.EncodeToPngMemory();

        const std::string relUri = "textures/blocks/portable_tex.png";
        {
            std::ofstream fA(rootA + "/" + relUri, std::ios::binary);
            fA.write(reinterpret_cast<const char*>(pngData.data()), pngData.size());
            std::ofstream fB(rootB + "/" + relUri, std::ios::binary);
            fB.write(reinterpret_cast<const char*>(pngData.data()), pngData.size());
        }

        auto pkgA = createBasePackage();
        pkgA.albedoMapUri = relUri;
        auto pkgB = createBasePackage();
        pkgB.albedoMapUri = relUri;

        std::string digA = pkgA.ComputeContentDigestSha256(rootA);
        std::string digB = pkgB.ComputeContentDigestSha256(rootB);

        bool match = (digA == digB) && (!digA.empty());

        r.passed = match;
        r.observedDesc = "DigA: " + digA.substr(0, 12) + "..., DigB: " + digB.substr(0, 12) + "... Match: " + (match ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: I percorsi host assoluti hanno contaminato il digest canonico";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // SEC-04: Texture mancante
    // -------------------------------------------------------------------------
    {
        AssetHardeningTestResult r;
        r.testId = "SEC-04";
        r.testName = "Texture mancante";
        r.inputDesc = "Package con albedoMapUri verso file inesistente su disco";
        r.expectedDesc = "ValidatePackage fallisce e segnala esplicitamente il file non rintracciabile";

        AssetPackagingService packaging;
        packaging.SetRootAssetDirectory(testRoot);

        auto pkg = createBasePackage();
        pkg.albedoMapUri = "textures/blocks/totally_missing_sec.png";

        auto report = packaging.ValidatePackage(pkg, testRoot);
        bool caught = !report.isValid && !report.errors.empty();

        r.passed = caught;
        r.observedDesc = "IsValid: " + std::string(report.isValid ? "true" : "false") +
                         ", ErrorsCount: " + std::to_string(report.errors.size());
        r.details = r.passed ? "PASS" : "FAIL: File texture inesistente non intercettato da ValidatePackage";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // SEC-05: PNG con firma valida ma payload troncato
    // -------------------------------------------------------------------------
    {
        AssetHardeningTestResult r;
        r.testId = "SEC-05";
        r.testName = "PNG con firma valida ma payload troncato";
        r.inputDesc = "File con magic PNG 8 byte seguiti da payload troncato/illeggibile";
        r.expectedDesc = "ValidateTextureFile rileva payload non decodificabile e rifiuta il package";

        const std::string truncRel = "textures/blocks/truncated_tex.png";
        const std::string truncFull = testRoot + "/" + truncRel;
        {
            std::ofstream f(truncFull, std::ios::binary);
            uint8_t pngMagic[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
            f.write(reinterpret_cast<const char*>(pngMagic), 8);
            // Payload parziale corrotto insufficiente a formare IHDR valido
            const char junk[] = { 0x00, 0x00, 0x00, 0x0D, 'I', 'H', 'D', 'R', 0x01 };
            f.write(junk, sizeof(junk));
        }

        AssetPackagingService packaging;
        packaging.SetRootAssetDirectory(testRoot);

        auto pkg = createBasePackage();
        pkg.albedoMapUri = truncRel;

        auto report = packaging.ValidatePackage(pkg, testRoot);
        bool caught = !report.isValid;

        r.passed = caught;
        r.observedDesc = "IsValid: " + std::string(report.isValid ? "true" : "false") +
                         ", Error: " + (report.errors.empty() ? "None" : report.errors[0]);
        r.details = r.passed ? "PASS" : "FAIL: Payload PNG troncato non rilevato (controllo limitato solo ai magic byte)";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // SEC-06: PNG valido e decodificabile
    // -------------------------------------------------------------------------
    {
        AssetHardeningTestResult r;
        r.testId = "SEC-06";
        r.testName = "PNG valido e decodificabile";
        r.inputDesc = "Texture PNG completa 8x8 RGBA generata conformemente";
        r.expectedDesc = "ValidatePackage supera il controllo, decodifica l'immagine e popola la cache";

        const std::string validRel = "textures/blocks/valid_tex.png";
        const std::string validFull = testRoot + "/" + validRel;
        {
            ImageBitmap bmp(8, 8, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.1f, 0.9f, 0.4f, 1.0f));
            auto pngData = bmp.EncodeToPngMemory();
            std::ofstream f(validFull, std::ios::binary);
            f.write(reinterpret_cast<const char*>(pngData.data()), pngData.size());
        }

        AssetPackagingService packaging;
        packaging.SetRootAssetDirectory(testRoot);

        auto pkg = createBasePackage();
        pkg.albedoMapUri = validRel;

        auto report = packaging.ValidatePackage(pkg, testRoot);
        bool valid = report.isValid;
        bool cachePopulated = (packaging.GetTextureValidationCacheSize() > 0);

        r.passed = valid && cachePopulated;
        r.observedDesc = "IsValid: " + std::string(valid ? "true" : "false") +
                         ", CacheEntries: " + std::to_string(packaging.GetTextureValidationCacheSize());
        r.details = r.passed ? "PASS" : "FAIL: Validazione PNG conforme fallita o cache non popolata";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // SEC-07: Asset con texture alterata esternamente e verifica digest memorizzato
    // -------------------------------------------------------------------------
    {
        AssetHardeningTestResult r;
        r.testId = "SEC-07";
        r.testName = "Asset con texture alterata esternamente e verifica digest memorizzato";
        r.inputDesc = "Package salvato con digest attendibile, seguito da alterazione su disco del file PNG";
        r.expectedDesc = "ValidatePackage e VerifyContentDigest() rilevano il mismatch con il digest memorizzato e rifiutano la pubblicazione";

        const std::string alterRel = "textures/blocks/alter_tex.png";
        const std::string alterFull = testRoot + "/" + alterRel;
        {
            ImageBitmap bmp(8, 8, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.7f, 0.2f, 0.3f, 1.0f));
            auto pngData = bmp.EncodeToPngMemory();
            std::ofstream f(alterFull, std::ios::binary);
            f.write(reinterpret_cast<const char*>(pngData.data()), pngData.size());
        }

        auto pkg = createBasePackage();
        pkg.manifest.assetId = "fairworld:sec_tamper_check";
        pkg.albedoMapUri = alterRel;

        // Fase 1: Calcolo e MEMORIZZAZIONE del digest crittografico attendibile
        std::string digInitial = pkg.ComputeContentDigestSha256(testRoot);
        pkg.contentDigestSha256 = digInitial;

        // Verifica che con digest memorizzato e file intatto la verifica sia valida
        bool initialVerifyOk = pkg.VerifyContentDigest(testRoot);

        // Fase 2: Manomissione esterna su disco (alterazione di un byte nel file PNG)
        {
            std::fstream f(alterFull, std::ios::in | std::ios::out | std::ios::binary);
            f.seekp(16, std::ios::beg);
            char b = 0;
            f.read(&b, 1);
            b ^= 0x5A;
            f.seekp(16, std::ios::beg);
            f.write(&b, 1);
        }

        // Fase 3: VERIFICA del digest memorizzato rispetto al contenuto alterato
        std::string expectedDig, computedDig;
        bool verifyMismatchDetected = !pkg.VerifyContentDigest(testRoot, &expectedDig, &computedDig);

        // Fase 4: ValidatePackage e PublishPackageTransactional devono RIFIUTARE l'asset manomesso
        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        AssetPackagingService packaging;
        packaging.SetRootAssetDirectory(testRoot);

        auto valReport = packaging.ValidatePackage(pkg, testRoot);
        bool valRejected = !valReport.isValid;

        uint32_t activeBlocksBefore = bReg.GetActiveBlockCount();
        auto pubRes = packaging.PublishPackageTransactional(pkg, bReg, mReg, sReg, testRoot);
        uint32_t activeBlocksAfter = bReg.GetActiveBlockCount();

        bool publishRejected = !pubRes.success && (activeBlocksBefore == activeBlocksAfter);

        r.passed = initialVerifyOk && verifyMismatchDetected && valRejected && publishRejected;
        r.observedDesc = "InitialVerify: " + std::string(initialVerifyOk ? "PASS" : "FAIL") +
                         ", MismatchDetected: " + std::string(verifyMismatchDetected ? "PASS" : "FAIL") +
                         ", ValRejected: " + std::string(valRejected ? "PASS" : "FAIL") +
                         ", PublishAborted: " + std::string(publishRejected ? "PASS" : "FAIL");
        r.details = r.passed ? "PASS" : "FAIL: Mancato confronto o rigetto dell'asset con digest memorizzato mismatchato";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // SEC-08: Compatibilità package precedente
    // -------------------------------------------------------------------------
    {
        AssetHardeningTestResult r;
        r.testId = "SEC-08";
        r.testName = "Compatibilità package precedente";
        r.inputDesc = "Caricamento file JSON schemaVersion=1 con solo distributionChecksum legacy e senza contentDigestSha256";
        r.expectedDesc = "LoadFromFile completa senza errori, preserva geometria/fisica e calcola il digest sha256";

        const std::string legacyPath = testRoot + "/legacy_pkg_v1.rawpkg";
        {
            nlohmann::json j;
            j["manifest"] = {
                {"assetId", "fairworld:legacy_stone"},
                {"displayName", "Legacy Stone"},
                {"schemaVersion", 1},
                {"revision", 1},
                {"stage", 2},
                {"targetRuntimeVoxelId", 0}
            };
            j["shape"] = {
                {"id", 1},
                {"revision", 1},
                {"type", 0},
                {"supportsVisualCompilation", true},
                {"supportsCollisionCompilation", true}
            };
            j["transform"] = {
                {"position", {0.0f, 0.0f, 0.0f}},
                {"rotation", {0.0f, 0.0f, 0.0f}},
                {"scale", {1.0f, 1.0f, 1.0f}},
                {"pivot", {0.0f, 0.0f, 0.0f}}
            };
            j["graphics"] = {
                {"baseColor", {0.5f, 0.5f, 0.5f}},
                {"roughness", 0.9f},
                {"metallic", 0.0f},
                {"emissive", 0.0f},
                {"alpha", 1.0f},
                {"behaviors", 0},
                {"albedoMapUri", ""},
                {"normalMapUri", ""},
                {"ormMapUri", ""},
                {"emissiveMapUri", ""},
                {"textureFingerprint", 0}
            };
            j["physics"] = {
                {"mass", 10.0f},
                {"density", 2700.0f},
                {"friction", 0.7f},
                {"restitution", 0.1f},
                {"collisionEnabled", true},
                {"motionType", 0},
                {"massMode", 0}
            };
            j["fingerprints"] = {
                {"packageFingerprint", 555555ULL},
                {"paintFingerprint", 0ULL},
                {"distributionChecksum", 9999999ULL}
                // Nessun contentDigestSha256 (schema precedente)
            };
            j["geometrySource"] = 0;
            j["sculptTopologyJson"] = "";
            j["geometry"] = {
                {"vertexCount", 1},
                {"triangleCount", 0},
                {"bounds", {{"min", {-0.5f, -0.5f, -0.5f}}, {"max", {0.5f, 0.5f, 0.5f}}}},
                {"vertices", {{
                    {"pos", {0.0f, 0.0f, 0.0f}},
                    {"color", {1.0f, 1.0f, 1.0f, 1.0f}},
                    {"roughMetal", {0.9f, 0.0f}},
                    {"materialID", 0},
                    {"normal", {0.0f, 1.0f, 0.0f}},
                    {"ao", 1.0f},
                    {"light", 1.0f},
                    {"emissive", 0.0f}
                }}},
                {"faces", nlohmann::json::array()}
            };

            std::ofstream out(legacyPath);
            out << j.dump(4);
        }

        RawAssetPackage loaded;
        bool loadOk = loaded.LoadFromFile(legacyPath);
        bool attrMatch = (loaded.manifest.assetId == "fairworld:legacy_stone") &&
                         (loaded.compiledPhysics.mass == 10.0f) &&
                         (loaded.distributionChecksum == 9999999ULL) &&
                         (loaded.contentDigestSha256.empty()); // vuoto all'atto del load legacy

        // Calcola digest sha256 per aggiornamento
        std::string computedSha = loaded.ComputeContentDigestSha256(testRoot);
        bool shaComputed = (computedSha.size() == 64);

        r.passed = loadOk && attrMatch && shaComputed;
        r.observedDesc = "LoadOk: " + std::string(loadOk ? "true" : "false") +
                         ", LegacyChecksum: " + std::to_string(loaded.distributionChecksum) +
                         ", UpgradedSha256: " + computedSha.substr(0, 12) + "...";
        r.details = r.passed ? "PASS" : "FAIL: Retrocompatibilità con pacchetti v1 compromessa";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // SEC-09: Zero-Work su contenuto stabile
    // -------------------------------------------------------------------------
    {
        AssetHardeningTestResult r;
        r.testId = "SEC-09";
        r.testName = "Zero-Work su contenuto stabile";
        r.inputDesc = "Doppia validazione e pubblicazione consecutiva con asset e texture invariati";
        r.expectedDesc = "Seconda pubblicazione attiva wasZeroWorkHit e la cache texture evita re-decodifica";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        const std::string zwTexRel = "textures/blocks/zw_tex.png";
        const std::string zwTexFull = testRoot + "/" + zwTexRel;
        {
            ImageBitmap bmp(4, 4, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
            auto pngData = bmp.EncodeToPngMemory();
            std::ofstream f(zwTexFull, std::ios::binary);
            f.write(reinterpret_cast<const char*>(pngData.data()), pngData.size());
        }

        AssetPackagingService packaging;
        packaging.SetRootAssetDirectory(testRoot);

        auto pkg = createBasePackage();
        pkg.manifest.assetId = "fairworld:sec_zw_block";
        pkg.albedoMapUri = zwTexRel;

        // Prima pubblicazione: commit
        auto pub1 = packaging.PublishPackageTransactional(pkg, bReg, mReg, sReg, testRoot);
        size_t cacheCount1 = packaging.GetTextureValidationCacheSize();

        // Seconda pubblicazione: Zero-Work hit
        auto pub2 = packaging.PublishPackageTransactional(pkg, bReg, mReg, sReg, testRoot);
        size_t cacheCount2 = packaging.GetTextureValidationCacheSize();

        bool zwOk = pub1.success && pub2.success && pub2.wasZeroWorkHit && (pub1.targetVoxelId == pub2.targetVoxelId);
        bool cacheStable = (cacheCount1 > 0) && (cacheCount1 == cacheCount2);

        r.passed = zwOk && cacheStable;
        r.observedDesc = "Pub1: " + std::string(pub1.success ? "true" : "false") +
                         ", Pub2ZeroWork: " + std::string(pub2.wasZeroWorkHit ? "true" : "false") +
                         ", CacheStable: " + std::to_string(cacheCount1) + "==" + std::to_string(cacheCount2);
        r.details = r.passed ? "PASS" : "FAIL: Zero-Work o cache di validazione non attivi su input identico";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // SEC-10: Nessuna regressione dei 211 test precedenti
    // -------------------------------------------------------------------------
    {
        AssetHardeningTestResult r;
        r.testId = "SEC-10";
        r.testName = "Nessuna regressione dei 211 test precedenti";
        r.inputDesc = "Esecuzione di TOP(36), PNT(10), TXB(10), PKG(10), TRX(10), SBR(10), INT(10)";
        r.expectedDesc = "Tutti i 96 test delle suite preesistenti superati al 100% (211 baseline totale)";

        if (TestRunContext::Instance().IsActive()) {
            static const std::vector<std::pair<std::string, size_t>> deps = {
                {"TOP", 36}, {"PNT", 10}, {"TXB", 10}, {"PKG", 10}, {"TRX", 10}, {"SBR", 10}, {"INT", 10}
            };
            auto eval = TestRunContext::Instance().EvaluateRegressionGate(deps);
            size_t topPass = 0, pntPass = 0, txbPass = 0, pkgPass = 0, trxPass = 0, sbrPass = 0, intPass = 0;
            for (const auto& c : eval.checks) {
                if (c.suiteCode == "TOP") topPass = c.passedCount;
                if (c.suiteCode == "PNT") pntPass = c.passedCount;
                if (c.suiteCode == "TXB") txbPass = c.passedCount;
                if (c.suiteCode == "PKG") pkgPass = c.passedCount;
                if (c.suiteCode == "TRX") trxPass = c.passedCount;
                if (c.suiteCode == "SBR") sbrPass = c.passedCount;
                if (c.suiteCode == "INT") intPass = c.passedCount;
            }
            r.passed = eval.allDependenciesPassed;
            r.observedDesc = "TOP: " + std::to_string(topPass) + "/36, PNT: " + std::to_string(pntPass) + "/10, TXB: " + std::to_string(txbPass) + "/10, PKG: " + std::to_string(pkgPass) + "/10, TRX: " + std::to_string(trxPass) + "/10, SBR: " + std::to_string(sbrPass) + "/10, INT: " + std::to_string(intPass) + "/10";
            r.details = r.passed ? "PASS" : "FAIL: " + eval.failureDetails;
        } else {
            auto topRes = CompiledTopologyTests::RunAllTests();
            auto pntRes = PainterStationContractTests::RunAllTests();
            auto txbRes = TextureBakingContractTests::RunAllTests();
            auto pkgRes = AssetPackagingContractTests::RunAllTests();
            auto trxRes = TransactionIntegrityAuditTests::RunAllTests();
            auto sbrRes = SculptedMeshRuntimeBridgeTests::RunAllTests();
            auto intRes = AssetIntegrityRoundTripTests::RunAllTests();

            size_t topPass = 0; for (const auto& tr : topRes) if (tr.passed) topPass++;
            size_t pntPass = 0; for (const auto& pr : pntRes) if (pr.passed) pntPass++;
            size_t txbPass = 0; for (const auto& xr : txbRes) if (xr.passed) txbPass++;
            size_t pkgPass = 0; for (const auto& kr : pkgRes) if (kr.passed) pkgPass++;
            size_t trxPass = 0; for (const auto& rr : trxRes) if (rr.passed) trxPass++;
            size_t sbrPass = 0; for (const auto& sr : sbrRes) if (sr.passed) sbrPass++;
            size_t intPass = 0; for (const auto& ir : intRes) if (ir.passed) intPass++;

            bool allPass = (topPass == topRes.size() && topRes.size() == 36) &&
                           (pntPass == pntRes.size() && pntRes.size() == 10) &&
                           (txbPass == txbRes.size() && txbRes.size() == 10) &&
                           (pkgPass == pkgRes.size() && pkgRes.size() == 10) &&
                           (trxPass == trxRes.size() && trxRes.size() == 10) &&
                           (sbrPass == sbrRes.size() && sbrRes.size() == 10) &&
                           (intPass == intRes.size() && intRes.size() == 10);

            r.passed = allPass;
            r.observedDesc = "TOP: " + std::to_string(topPass) + "/" + std::to_string(topRes.size()) +
                             ", PNT: " + std::to_string(pntPass) + "/" + std::to_string(pntRes.size()) +
                             ", TXB: " + std::to_string(txbPass) + "/" + std::to_string(txbRes.size()) +
                             ", PKG: " + std::to_string(pkgPass) + "/" + std::to_string(pkgRes.size()) +
                             ", TRX: " + std::to_string(trxPass) + "/" + std::to_string(trxRes.size()) +
                             ", SBR: " + std::to_string(sbrPass) + "/" + std::to_string(sbrRes.size()) +
                             ", INT: " + std::to_string(intPass) + "/" + std::to_string(intRes.size());
            r.details = r.passed ? "PASS" : "FAIL: Regressione rilevata nelle suite precedenti";
        }
        results.push_back(r);
    }

    return results;
}

} // namespace fw
