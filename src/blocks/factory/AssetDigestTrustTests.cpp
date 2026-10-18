#include "pch.h"
#include "AssetDigestTrustTests.h"
#include "FactoryOrchestrator.h"
#include "AssetPackagingService.h"
#include "TextureBakingService.h"
#include "blocks/BlockRegistry.h"
#include "blocks/MaterialRegistry.h"
#include "blocks/ShapeRegistry.h"
#include "blocks/texture/ImageBitmap.h"
#include "Sha256.h"

// Suites per verifica regressioni DGV-10
#include "blocks/uv/CompiledTopologyTests.h"
#include "PainterStationContractTests.h"
#include "TextureBakingContractTests.h"
#include "AssetPackagingContractTests.h"
#include "TransactionIntegrityAuditTests.h"
#include "SculptedMeshRuntimeBridgeTests.h"
#include "AssetIntegrityRoundTripTests.h"
#include "AssetIntegrityHardeningTests.h"
#include "TestRunContext.h"

#include <filesystem>
#include <fstream>
#include <chrono>

namespace fw {

std::vector<AssetDigestTestResult> AssetDigestTrustTests::RunAllTests() {
    std::vector<AssetDigestTestResult> results;
    results.reserve(10);

    const std::string testRoot = (std::filesystem::temp_directory_path() / "fairworld_dgv_gate").string();
    std::error_code ec;
    std::filesystem::create_directories(testRoot, ec);
    std::filesystem::create_directories(testRoot + "/textures/blocks", ec);

    // Helper per creare un RawAssetPackage di base valido (Schema V2)
    auto createBasePackage = []() -> RawAssetPackage {
        RawAssetPackage pkg;
        pkg.manifest.assetId = "fairworld:dgv_base_block";
        pkg.manifest.displayName = "Base DGV Asset";
        pkg.manifest.schemaVersion = 2;
        pkg.manifest.revision = 1;
        pkg.manifest.stage = RawAssetStage::Packaged;
        pkg.geometrySource = GeometrySource::Primitive;
        pkg.shape.type = ShapeType::Cube;
        pkg.shape.id = 1;
        pkg.shape.revision = 1;
        pkg.shape.supportsVisualCompilation = true;
        pkg.shape.supportsCollisionCompilation = true;
        pkg.compiledPhysics.mass = 5.0f;
        pkg.compiledPhysics.density = 2400.0f;
        pkg.compiledPhysics.collisionEnabled = true;
        pkg.baseColorFallback = glm::vec3(0.7f, 0.6f, 0.5f);
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
        pkg.packageFingerprint = 1000003ULL * 24;
        pkg.distributionChecksum = pkg.ComputeDistributionChecksum();
        pkg.validationReport.isValid = true;
        return pkg;
    };

    // -------------------------------------------------------------------------
    // DGV-01: V2 valido → Verified
    // -------------------------------------------------------------------------
    {
        AssetDigestTestResult r;
        r.testId = "DGV-01";
        r.testName = "V2 valido -> Verified";
        r.inputDesc = "Package Schema V2 con digest SHA-256 valido e conformità completa";
        r.expectedDesc = "Validazione supera controlli, status Verified, pubblicazione completata in Strict";

        auto pkg = createBasePackage();
        pkg.manifest.assetId = "fairworld:dgv01_valid_v2";
        pkg.contentDigestSha256 = pkg.ComputeContentDigestSha256(testRoot);

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);

        auto valReport = service.ValidatePackage(pkg, testRoot, AssetTrustPolicy::Strict);

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        auto pubRes = service.PublishPackageTransactional(pkg, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        bool valOk = valReport.isValid && (valReport.integrityStatus == AssetIntegrityStatus::Verified);
        bool pubOk = pubRes.success && (pubRes.integrityStatus == AssetIntegrityStatus::Verified) &&
                     (pubRes.errorCode == AssetIntegrityErrorCode::None) && (pubRes.targetVoxelId != 0);
        bool regOk = bReg.IsStringIdTaken("fairworld:dgv01_valid_v2");

        r.passed = valOk && pubOk && regOk;
        r.observedDesc = "ValStatus: " + std::string(AssetIntegrityStatusToString(valReport.integrityStatus)) +
                         ", PubStatus: " + std::string(AssetIntegrityStatusToString(pubRes.integrityStatus)) +
                         ", TargetVoxel: " + std::to_string(pubRes.targetVoxelId);
        r.details = r.passed ? "PASS" : "FAIL: Validazione o pubblicazione fallita per package V2 valido";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // DGV-02: Geometria alterata → Rejected
    // -------------------------------------------------------------------------
    {
        AssetDigestTestResult r;
        r.testId = "DGV-02";
        r.testName = "Geometria alterata -> Rejected";
        r.inputDesc = "Package V2 salvato con digest attendibile, seguito da manomissione di vertice geometrico";
        r.expectedDesc = "ValidatePackage rifiuta con DigestMismatch, PublishPackageTransactional abortisce senza commit";

        auto pkg = createBasePackage();
        pkg.manifest.assetId = "fairworld:dgv02_geom_tamper";
        pkg.contentDigestSha256 = pkg.ComputeContentDigestSha256(testRoot);

        // Manomissione geometria: alterazione coordinate vertice
        pkg.geometry.vertices[0].position.x += 10.0f;

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);

        auto valReport = service.ValidatePackage(pkg, testRoot, AssetTrustPolicy::Strict);

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        auto pubRes = service.PublishPackageTransactional(pkg, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        bool valRejected = (!valReport.isValid) &&
                           (valReport.integrityStatus == AssetIntegrityStatus::Rejected) &&
                           (valReport.primaryErrorCode == AssetIntegrityErrorCode::DigestMismatch);
        bool pubRejected = (!pubRes.success) &&
                           (pubRes.integrityStatus == AssetIntegrityStatus::Rejected) &&
                           (pubRes.errorCode == AssetIntegrityErrorCode::DigestMismatch);
        bool noRegistryMutation = !bReg.IsStringIdTaken("fairworld:dgv02_geom_tamper");

        r.passed = valRejected && pubRejected && noRegistryMutation;
        r.observedDesc = "ValError: " + std::string(AssetIntegrityErrorCodeToString(valReport.primaryErrorCode)) +
                         ", PubError: " + std::string(AssetIntegrityErrorCodeToString(pubRes.errorCode)) +
                         ", RegClean: " + std::string(noRegistryMutation ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Geometria alterata non intercettata da DigestMismatch";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // DGV-03: Texture alterata → Rejected
    // -------------------------------------------------------------------------
    {
        AssetDigestTestResult r;
        r.testId = "DGV-03";
        r.testName = "Texture alterata -> Rejected";
        r.inputDesc = "Package V2 con texture PNG su disco, alterata esternamente dopo il calcolo del digest";
        r.expectedDesc = "DigestMismatch intercettato, status Rejected, pubblicazione respinta";

        const std::string texRel = "textures/blocks/dgv03_albedo.png";
        const std::string texFull = testRoot + "/" + texRel;
        {
            ImageBitmap bmp(16, 16, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.8f, 0.1f, 0.1f, 1.0f));
            auto pngData = bmp.EncodeToPngMemory();
            std::ofstream f(texFull, std::ios::binary);
            f.write(reinterpret_cast<const char*>(pngData.data()), pngData.size());
        }

        auto pkg = createBasePackage();
        pkg.manifest.assetId = "fairworld:dgv03_tex_tamper";
        pkg.albedoMapUri = texRel;
        pkg.contentDigestSha256 = pkg.ComputeContentDigestSha256(testRoot);

        // Manomissione file texture su disco
        {
            std::fstream f(texFull, std::ios::in | std::ios::out | std::ios::binary);
            f.seekp(20, std::ios::beg);
            char b = 0;
            f.read(&b, 1);
            b ^= 0x7F;
            f.seekp(20, std::ios::beg);
            f.write(&b, 1);
        }

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);

        auto valReport = service.ValidatePackage(pkg, testRoot, AssetTrustPolicy::Strict);

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        auto pubRes = service.PublishPackageTransactional(pkg, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        bool valRejected = (!valReport.isValid) && (valReport.integrityStatus == AssetIntegrityStatus::Rejected);
        bool pubRejected = (!pubRes.success) && (pubRes.integrityStatus == AssetIntegrityStatus::Rejected);
        bool noRegistryMutation = !bReg.IsStringIdTaken("fairworld:dgv03_tex_tamper");

        r.passed = valRejected && pubRejected && noRegistryMutation;
        r.observedDesc = "ValError: " + std::string(AssetIntegrityErrorCodeToString(valReport.primaryErrorCode)) +
                         ", PubError: " + std::string(AssetIntegrityErrorCodeToString(pubRes.errorCode)) +
                         ", RegClean: " + std::string(noRegistryMutation ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Texture alterata non rifiutata";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // DGV-04: Digest mancante in STRICT → Rejected
    // -------------------------------------------------------------------------
    {
        AssetDigestTestResult r;
        r.testId = "DGV-04";
        r.testName = "Digest mancante in STRICT -> Rejected";
        r.inputDesc = "Package Schema V2 privo di contentDigestSha256 sotto policy AssetTrustPolicy::Strict";
        r.expectedDesc = "Validazione fallisce con MissingDigest, status Rejected";

        auto pkg = createBasePackage();
        pkg.manifest.assetId = "fairworld:dgv04_missing_digest";
        pkg.manifest.schemaVersion = 2;
        pkg.contentDigestSha256.clear(); // Intenzionalmente omesso

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);

        auto valReport = service.ValidatePackage(pkg, testRoot, AssetTrustPolicy::Strict);

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        auto pubRes = service.PublishPackageTransactional(pkg, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        bool valMissing = (!valReport.isValid) &&
                          (valReport.integrityStatus == AssetIntegrityStatus::Rejected) &&
                          (valReport.primaryErrorCode == AssetIntegrityErrorCode::MissingDigest);
        bool pubMissing = (!pubRes.success) &&
                          (pubRes.integrityStatus == AssetIntegrityStatus::Rejected) &&
                          (pubRes.errorCode == AssetIntegrityErrorCode::MissingDigest);

        r.passed = valMissing && pubMissing;
        r.observedDesc = "ValError: " + std::string(AssetIntegrityErrorCodeToString(valReport.primaryErrorCode)) +
                         ", PubError: " + std::string(AssetIntegrityErrorCodeToString(pubRes.errorCode));
        r.details = r.passed ? "PASS" : "FAIL: Digest assente non intercettato da MissingDigest in Strict";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // DGV-05: Digest malformato → Rejected
    // -------------------------------------------------------------------------
    {
        AssetDigestTestResult r;
        r.testId = "DGV-05";
        r.testName = "Digest malformato -> Rejected";
        r.inputDesc = "Test di stringhe digest non conformi (63 char, caratteri maiuscoli, non esadecimali)";
        r.expectedDesc = "Tutti i digest non-64 hex lowercase respinti con InvalidDigestFormat";

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);

        // 1. Troppo corto (63 char)
        auto pkg1 = createBasePackage();
        pkg1.manifest.assetId = "fairworld:dgv05_short";
        pkg1.contentDigestSha256 = std::string(63, 'a');
        auto val1 = service.ValidatePackage(pkg1, testRoot, AssetTrustPolicy::Strict);

        // 2. Maiuscolo (deve essere rigorosamente lowercase)
        auto pkg2 = createBasePackage();
        pkg2.manifest.assetId = "fairworld:dgv05_upper";
        pkg2.contentDigestSha256 = std::string(64, 'A');
        auto val2 = service.ValidatePackage(pkg2, testRoot, AssetTrustPolicy::Strict);

        // 3. Caratteri non esadecimali
        auto pkg3 = createBasePackage();
        pkg3.manifest.assetId = "fairworld:dgv05_nonhex";
        pkg3.contentDigestSha256 = std::string(63, 'a') + "z";
        auto val3 = service.ValidatePackage(pkg3, testRoot, AssetTrustPolicy::Strict);

        bool ok1 = (!val1.isValid) && (val1.primaryErrorCode == AssetIntegrityErrorCode::InvalidDigestFormat);
        bool ok2 = (!val2.isValid) && (val2.primaryErrorCode == AssetIntegrityErrorCode::InvalidDigestFormat);
        bool ok3 = (!val3.isValid) && (val3.primaryErrorCode == AssetIntegrityErrorCode::InvalidDigestFormat);

        r.passed = ok1 && ok2 && ok3;
        r.observedDesc = "ShortOk: " + std::string(ok1 ? "PASS" : "FAIL") +
                         ", UpperOk: " + std::string(ok2 ? "PASS" : "FAIL") +
                         ", NonHexOk: " + std::string(ok3 ? "PASS" : "FAIL");
        r.details = r.passed ? "PASS" : "FAIL: Formato digest non validato rigorosamente";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // DGV-06: V1 senza digest in AllowLegacy → LegacyUnverified
    // -------------------------------------------------------------------------
    {
        AssetDigestTestResult r;
        r.testId = "DGV-06";
        r.testName = "V1 senza digest in AllowLegacy -> LegacyUnverified";
        r.inputDesc = "Asset Schema V1 senza digest SHA-256 esaminato sotto AllowLegacy e sotto Strict";
        r.expectedDesc = "In AllowLegacy: ammesso come LegacyUnverified. In Strict: respinto come UnsupportedSchema";

        auto pkg = createBasePackage();
        pkg.manifest.assetId = "fairworld:dgv06_legacy";
        pkg.manifest.schemaVersion = 1;
        pkg.contentDigestSha256.clear();

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);

        // Sotto AllowLegacy
        auto valLegacy = service.ValidatePackage(pkg, testRoot, AssetTrustPolicy::AllowLegacy);

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        auto pubLegacy = service.PublishPackageTransactional(pkg, bReg, mReg, sReg, testRoot, AssetTrustPolicy::AllowLegacy);

        // Sotto Strict
        auto valStrict = service.ValidatePackage(pkg, testRoot, AssetTrustPolicy::Strict);

        bool legacyOk = valLegacy.isValid &&
                        (valLegacy.integrityStatus == AssetIntegrityStatus::LegacyUnverified) &&
                        pubLegacy.success &&
                        (pubLegacy.integrityStatus == AssetIntegrityStatus::LegacyUnverified);
        bool strictRejected = (!valStrict.isValid) &&
                              (valStrict.integrityStatus == AssetIntegrityStatus::Rejected) &&
                              (valStrict.primaryErrorCode == AssetIntegrityErrorCode::UnsupportedSchema);

        r.passed = legacyOk && strictRejected;
        r.observedDesc = "LegacyStatus: " + std::string(AssetIntegrityStatusToString(pubLegacy.integrityStatus)) +
                         ", StrictStatus: " + std::string(AssetIntegrityStatusToString(valStrict.integrityStatus)) +
                         ", StrictError: " + std::string(AssetIntegrityErrorCodeToString(valStrict.primaryErrorCode));
        r.details = r.passed ? "PASS" : "FAIL: Comportamento discrepanza V1 tra AllowLegacy e Strict errato";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // DGV-07: Schema futuro sconosciuto → Rejected
    // -------------------------------------------------------------------------
    {
        AssetDigestTestResult r;
        r.testId = "DGV-07";
        r.testName = "Schema futuro sconosciuto -> Rejected";
        r.inputDesc = "Package con schemaVersion non supportata (v3, v99, v0) sotto entrambe le policy";
        r.expectedDesc = "Tutte le versioni non supportate respinte con UnsupportedSchema e status Rejected";

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);

        auto testSchema = [&](uint32_t ver) -> bool {
            auto pkg = createBasePackage();
            pkg.manifest.assetId = "fairworld:dgv07_future_" + std::to_string(ver);
            pkg.manifest.schemaVersion = ver;
            pkg.contentDigestSha256 = std::string(64, '0');

            auto val1 = service.ValidatePackage(pkg, testRoot, AssetTrustPolicy::Strict);
            auto val2 = service.ValidatePackage(pkg, testRoot, AssetTrustPolicy::AllowLegacy);

            return (!val1.isValid) && (val1.primaryErrorCode == AssetIntegrityErrorCode::UnsupportedSchema) &&
                   (!val2.isValid) && (val2.primaryErrorCode == AssetIntegrityErrorCode::UnsupportedSchema);
        };

        bool v3Ok = testSchema(3);
        bool v99Ok = testSchema(99);
        bool v0Ok = testSchema(0);

        r.passed = v3Ok && v99Ok && v0Ok;
        r.observedDesc = "V3: " + std::string(v3Ok ? "PASS" : "FAIL") +
                         ", V99: " + std::string(v99Ok ? "PASS" : "FAIL") +
                         ", V0: " + std::string(v0Ok ? "PASS" : "FAIL");
        r.details = r.passed ? "PASS" : "FAIL: Schema version sconosciute non respinte con UnsupportedSchema";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // DGV-08: Verifica fallita → Zero mutazioni dei registri
    // -------------------------------------------------------------------------
    {
        AssetDigestTestResult r;
        r.testId = "DGV-08";
        r.testName = "Verifica fallita -> Zero mutazioni dei registri";
        r.inputDesc = "Tentativi multipli di pubblicazione con package corrotti/malformati su registri puliti";
        r.expectedDesc = "Nessun blocco registrato, nessuna shape allocata, registri identici all'origine";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        uint32_t bCount0 = bReg.GetActiveBlockCount();
        uint32_t sCount0 = static_cast<uint32_t>(sReg.GetAllShapes().size());

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);

        // Tentativo 1: digest mancante
        auto bad1 = createBasePackage();
        bad1.manifest.assetId = "fairworld:dgv08_bad1";
        bad1.contentDigestSha256.clear();
        service.PublishPackageTransactional(bad1, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        // Tentativo 2: digest errato
        auto bad2 = createBasePackage();
        bad2.manifest.assetId = "fairworld:dgv08_bad2";
        bad2.contentDigestSha256 = std::string(64, 'f');
        service.PublishPackageTransactional(bad2, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        // Tentativo 3: schema non supportato
        auto bad3 = createBasePackage();
        bad3.manifest.assetId = "fairworld:dgv08_bad3";
        bad3.manifest.schemaVersion = 10;
        bad3.contentDigestSha256 = std::string(64, 'a');
        service.PublishPackageTransactional(bad3, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        uint32_t bCount1 = bReg.GetActiveBlockCount();
        uint32_t sCount1 = static_cast<uint32_t>(sReg.GetAllShapes().size());

        bool countsUnchanged = (bCount0 == bCount1) && (sCount0 == sCount1);
        bool noIdsFound = !bReg.IsStringIdTaken("fairworld:dgv08_bad1") &&
                          !bReg.IsStringIdTaken("fairworld:dgv08_bad2") &&
                          !bReg.IsStringIdTaken("fairworld:dgv08_bad3");

        r.passed = countsUnchanged && noIdsFound;
        r.observedDesc = "Blocks: " + std::to_string(bCount0) + " == " + std::to_string(bCount1) +
                         ", Shapes: " + std::to_string(sCount0) + " == " + std::to_string(sCount1) +
                         ", RegistriesIntact: " + std::string(noIdsFound ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Mutazioni orfane riscontrate nei registri a seguito di fallimento verifica";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // DGV-09: Zero-Work non bypassa un cambiamento esterno
    // -------------------------------------------------------------------------
    {
        AssetDigestTestResult r;
        r.testId = "DGV-09";
        r.testName = "Zero-Work non bypassa un cambiamento esterno";
        r.inputDesc = "Pubblicazione con Zero-Work hit seguita da alterazione esterna del file PNG su disco";
        r.expectedDesc = "La seconda pubblicazione su file alterato NON deve essere un Zero-Work hit, ma deve essere rigettata";

        const std::string texRel = "textures/blocks/dgv09_albedo.png";
        const std::string texFull = testRoot + "/" + texRel;
        {
            ImageBitmap bmp(8, 8, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.2f, 0.8f, 0.3f, 1.0f));
            auto pngData = bmp.EncodeToPngMemory();
            std::ofstream f(texFull, std::ios::binary);
            f.write(reinterpret_cast<const char*>(pngData.data()), pngData.size());
        }

        auto pkg = createBasePackage();
        pkg.manifest.assetId = "fairworld:dgv09_zerowork_safety";
        pkg.albedoMapUri = texRel;
        pkg.contentDigestSha256 = pkg.ComputeContentDigestSha256(testRoot);

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        AssetPackagingService service;
        service.SetRootAssetDirectory(testRoot);

        // Prima pubblicazione: commit iniziale
        auto pub1 = service.PublishPackageTransactional(pkg, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        // Seconda pubblicazione a input invariato: deve essere Zero-Work hit
        auto pub2 = service.PublishPackageTransactional(pkg, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);
        bool initialZeroWorkOk = pub1.success && pub2.success && pub2.wasZeroWorkHit;

        // Alterazione esterna della texture su disco
        {
            ImageBitmap alteredBmp(8, 8, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.1f, 0.2f, 0.9f, 1.0f));
            auto alteredPng = alteredBmp.EncodeToPngMemory();
            std::ofstream f(texFull, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(alteredPng.data()), alteredPng.size());
        }

        // Terza pubblicazione con il package avente il vecchio digest memorizzato:
        // DEVE rifiutare e NON bypassare via Zero-Work
        auto pub3 = service.PublishPackageTransactional(pkg, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);
        bool tamperRejected = (!pub3.success) && (!pub3.wasZeroWorkHit) &&
                              (pub3.errorCode == AssetIntegrityErrorCode::DigestMismatch);

        r.passed = initialZeroWorkOk && tamperRejected;
        r.observedDesc = "InitialZeroWork: " + std::string(initialZeroWorkOk ? "PASS" : "FAIL") +
                         ", TamperRejected: " + std::string(tamperRejected ? "PASS" : "FAIL") +
                         ", WasZeroWorkHitOnTamper: " + std::string(pub3.wasZeroWorkHit ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Zero-Work ha bypassato una modifica esterna dei contenuti su disco";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // DGV-10: Regressione delle suite precedenti
    // -------------------------------------------------------------------------
    {
        AssetDigestTestResult r;
        r.testId = "DGV-10";
        r.testName = "Regressione delle suite precedenti";
        r.inputDesc = "Esecuzione completa di TOP(36), PNT(10), TXB(10), PKG(10), TRX(10), SBR(10), INT(10), SEC(10)";
        r.expectedDesc = "Tutti i 106 test contrattuali delle suite storiche superati al 100%";

        if (TestRunContext::Instance().IsActive()) {
            static const std::vector<std::pair<std::string, size_t>> deps = {
                {"TOP", 36}, {"PNT", 10}, {"TXB", 10}, {"PKG", 10}, {"TRX", 10}, {"SBR", 10}, {"INT", 10}, {"SEC", 10}
            };
            auto eval = TestRunContext::Instance().EvaluateRegressionGate(deps);
            size_t topPass = 0, pntPass = 0, txbPass = 0, pkgPass = 0, trxPass = 0, sbrPass = 0, intPass = 0, secPass = 0;
            for (const auto& c : eval.checks) {
                if (c.suiteCode == "TOP") topPass = c.passedCount;
                if (c.suiteCode == "PNT") pntPass = c.passedCount;
                if (c.suiteCode == "TXB") txbPass = c.passedCount;
                if (c.suiteCode == "PKG") pkgPass = c.passedCount;
                if (c.suiteCode == "TRX") trxPass = c.passedCount;
                if (c.suiteCode == "SBR") sbrPass = c.passedCount;
                if (c.suiteCode == "INT") intPass = c.passedCount;
                if (c.suiteCode == "SEC") secPass = c.passedCount;
            }
            r.passed = eval.allDependenciesPassed;
            r.observedDesc = "TOP: " + std::to_string(topPass) + "/36, PNT: " + std::to_string(pntPass) + "/10, TXB: " + std::to_string(txbPass) + "/10, PKG: " + std::to_string(pkgPass) + "/10, TRX: " + std::to_string(trxPass) + "/10, SBR: " + std::to_string(sbrPass) + "/10, INT: " + std::to_string(intPass) + "/10, SEC: " + std::to_string(secPass) + "/10";
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

            size_t topPass = 0; for (const auto& tr : topRes) if (tr.passed) topPass++;
            size_t pntPass = 0; for (const auto& pr : pntRes) if (pr.passed) pntPass++;
            size_t txbPass = 0; for (const auto& xr : txbRes) if (xr.passed) txbPass++;
            size_t pkgPass = 0; for (const auto& kr : pkgRes) if (kr.passed) pkgPass++;
            size_t trxPass = 0; for (const auto& rr : trxRes) if (rr.passed) trxPass++;
            size_t sbrPass = 0; for (const auto& sr : sbrRes) if (sr.passed) sbrPass++;
            size_t intPass = 0; for (const auto& ir : intRes) if (ir.passed) intPass++;
            size_t secPass = 0; for (const auto& sc : secRes) if (sc.passed) secPass++;

            bool allPass = (topPass == topRes.size() && topRes.size() == 36) &&
                           (pntPass == pntRes.size() && pntRes.size() == 10) &&
                           (txbPass == txbRes.size() && txbRes.size() == 10) &&
                           (pkgPass == pkgRes.size() && pkgRes.size() == 10) &&
                           (trxPass == trxRes.size() && trxRes.size() == 10) &&
                           (sbrPass == sbrRes.size() && sbrRes.size() == 10) &&
                           (intPass == intRes.size() && intRes.size() == 10) &&
                           (secPass == secRes.size() && secRes.size() == 10);

            r.passed = allPass;
            r.observedDesc = "TOP: " + std::to_string(topPass) + "/" + std::to_string(topRes.size()) +
                             ", PNT: " + std::to_string(pntPass) + "/" + std::to_string(pntRes.size()) +
                             ", TXB: " + std::to_string(txbPass) + "/" + std::to_string(txbRes.size()) +
                             ", PKG: " + std::to_string(pkgPass) + "/" + std::to_string(pkgRes.size()) +
                             ", TRX: " + std::to_string(trxPass) + "/" + std::to_string(trxRes.size()) +
                             ", SBR: " + std::to_string(sbrPass) + "/" + std::to_string(sbrRes.size()) +
                             ", INT: " + std::to_string(intPass) + "/" + std::to_string(intRes.size()) +
                             ", SEC: " + std::to_string(secPass) + "/" + std::to_string(secRes.size());
            r.details = r.passed ? "PASS" : "FAIL: Rilevata regressione in una delle suite precedenti";
        }
        results.push_back(r);
    }

    return results;
}

} // namespace fw
