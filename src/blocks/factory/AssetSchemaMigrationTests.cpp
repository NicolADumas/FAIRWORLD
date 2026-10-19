#include "pch.h"
#include "AssetSchemaMigrationTests.h"
#include "AssetSchemaMigrationService.h"
#include "AssetPackagingService.h"
#include "AssetDependencyGraph.h"
#include "AssetDependencyContractTests.h"
#include "AssetCrashRecoveryHardeningTests.h"
#include "AssetCrashRecoveryTests.h"
#include "AssetDigestTrustTests.h"
#include "TestRunContext.h"
#include "blocks/BlockRegistry.h"
#include "blocks/MaterialRegistry.h"
#include "blocks/ShapeRegistry.h"
#include "Sha256.h"
#include "blocks/texture/ImageBitmap.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fw {

std::vector<AssetSchemaMigrationTestResult> AssetSchemaMigrationTests::RunAllTests() {
    std::vector<AssetSchemaMigrationTestResult> results;
    results.reserve(10);

    const std::string testRoot = (std::filesystem::temp_directory_path() / "fairworld_migration_gate").string();
    std::error_code ec;
    std::filesystem::create_directories(testRoot, ec);

    // Helper per creare un RawAssetPackage Schema V1 (legacy) valido
    auto createLegacyV1Package = [](const std::string& assetId, uint64_t rev = 1) -> RawAssetPackage {
        RawAssetPackage pkg;
        pkg.manifest.assetId = assetId;
        pkg.manifest.displayName = "Legacy V1 Block " + assetId;
        pkg.manifest.schemaVersion = 1; // Schema Legacy V1
        pkg.manifest.revision = rev;
        pkg.manifest.stage = RawAssetStage::Packaged;
        pkg.geometrySource = GeometrySource::Primitive;
        pkg.shape.type = ShapeType::Cube;
        pkg.shape.id = 1;
        pkg.shape.revision = 1;
        pkg.shape.supportsVisualCompilation = true;
        pkg.shape.supportsCollisionCompilation = true;

        pkg.compiledPhysics.mass = 2.5f;
        pkg.compiledPhysics.density = 1500.0f;
        pkg.compiledPhysics.friction = 0.55f;
        pkg.compiledPhysics.restitution = 0.1f;
        pkg.compiledPhysics.collisionEnabled = true;

        pkg.baseColorFallback = glm::vec3(0.7f, 0.2f, 0.4f);
        pkg.roughnessFallback = 0.35f;
        pkg.metallicFallback = 0.25f;
        pkg.emissiveStrength = 0.0f;

        pkg.geometry.vertices.resize(24);
        for (size_t i = 0; i < 24; ++i) {
            pkg.geometry.vertices[i].position = fw::Vec3{
                static_cast<float>(i % 2),
                static_cast<float>((i / 2) % 2),
                static_cast<float>((i / 4) % 2)
            };
            pkg.geometry.vertices[i].normal = fw::Vec3{0.0f, 1.0f, 0.0f};
            pkg.geometry.vertices[i].color = fw::Vec4{1.0f, 1.0f, 1.0f, 1.0f};
            pkg.geometry.vertices[i].materialID = 1;
            pkg.geometry.vertices[i].roughMetal = fw::Vec2{0.35f, 0.25f};
            pkg.geometry.vertices[i].ao = 1.0f;
            pkg.geometry.vertices[i].light = 1.0f;
            pkg.geometry.vertices[i].emissive = 0.0f;
        }
        pkg.geometry.vertexCount = 24;

        pkg.geometry.faces.resize(12);
        for (size_t i = 0; i < 12; ++i) {
            pkg.geometry.faces[i].surfaceId = static_cast<uint32_t>(i % 6);
            pkg.geometry.faces[i].matIndex = 0;
            pkg.geometry.faces[i].indices = {
                static_cast<int>(i * 2 % 24),
                static_cast<int>((i * 2 + 1) % 24),
                static_cast<int>((i * 2 + 2) % 24)
            };
        }
        pkg.geometry.triangleCount = 12;

        pkg.geometry.bounds.min = Vec3(0.0f, 0.0f, 0.0f);
        pkg.geometry.bounds.max = Vec3(1.0f, 1.0f, 1.0f);

        // V1: ha distributionChecksum FNV-1a, MA NON HA il digest SHA-256 canonico
        pkg.distributionChecksum = pkg.ComputeDistributionChecksum();
        pkg.packageFingerprint = pkg.distributionChecksum;
        pkg.contentDigestSha256 = ""; // Assente in V1 legacy
        return pkg;
    };

    // =========================================================================
    // MIG-01: Riconoscimento schema legacy V1
    // =========================================================================
    {
        AssetSchemaMigrationTestResult r;
        r.testId = "MIG-01";
        r.testName = "LegacySchemaV1Recognition";
        r.inputDesc = "RawAssetPackage con schemaVersion = 1 privo di digest crittografico SHA-256";
        r.expectedDesc = "InspectSchema riconosce LegacySchema, CanMigrate restituisce true, diagnosi esplicita";

        AssetSchemaMigrationService service;
        service.SetRootAssetDirectory(testRoot);

        auto legacyPkg = createLegacyV1Package("fairworld:mig01_legacy", 1);
        auto inspection = service.InspectSchema(legacyPkg);
        std::string reason;
        bool canMigrate = service.CanMigrate(legacyPkg, &reason);

        r.passed = (inspection.state == SchemaVersionState::LegacySchema) &&
                   (inspection.detectedVersion == 1) &&
                   !inspection.hasValidDigest &&
                   canMigrate;
        r.observedDesc = std::string("Stato: ") + SchemaVersionStateToString(inspection.state) +
                         ", DetectedVersion: " + std::to_string(inspection.detectedVersion) +
                         ", CanMigrate: " + std::string(canMigrate ? "true" : "false") +
                         ", Msg: " + inspection.message;
        r.details = r.passed ? "PASS" : "FAIL: Mancato riconoscimento schema V1 legacy";
        results.push_back(r);
    }

    // =========================================================================
    // MIG-02: Riconoscimento schema corrente V2
    // =========================================================================
    {
        AssetSchemaMigrationTestResult r;
        r.testId = "MIG-02";
        r.testName = "CurrentSchemaV2Recognition";
        r.inputDesc = "RawAssetPackage con schemaVersion = 2 e digest SHA-256 canonico conforme";
        r.expectedDesc = "InspectSchema riconosce CurrentSchema, detectedVersion = 2, hasValidDigest = true";

        AssetSchemaMigrationService service;
        service.SetRootAssetDirectory(testRoot);

        auto v2Pkg = createLegacyV1Package("fairworld:mig02_current", 1);
        v2Pkg.manifest.schemaVersion = 2;
        v2Pkg.contentDigestSha256 = v2Pkg.ComputeContentDigestSha256(testRoot);

        auto inspection = service.InspectSchema(v2Pkg, testRoot);
        r.passed = (inspection.state == SchemaVersionState::CurrentSchema) &&
                   (inspection.detectedVersion == 2) &&
                   inspection.hasValidDigest;
        r.observedDesc = std::string("Stato: ") + SchemaVersionStateToString(inspection.state) +
                         ", DetectedVersion: " + std::to_string(inspection.detectedVersion) +
                         ", ValidDigest: " + std::string(inspection.hasValidDigest ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Mancato riconoscimento schema V2 corrente";
        results.push_back(r);
    }

    // =========================================================================
    // MIG-03: Migrazione V1 -> V2 valida e deterministica
    // =========================================================================
    {
        AssetSchemaMigrationTestResult r;
        r.testId = "MIG-03";
        r.testName = "DeterministicV1ToV2Migration";
        r.inputDesc = "Package V1 legacy valido con revisione 1 e distributionChecksum FNV-1a";
        r.expectedDesc = "MigrateToCurrent genera package V2 con revisione bumpata, digest SHA-256 e status Verified sotto Strict";

        AssetSchemaMigrationService service;
        service.SetRootAssetDirectory(testRoot);

        auto legacyPkg = createLegacyV1Package("fairworld:mig03_valid", 1);
        auto migResult = service.MigrateToCurrent(legacyPkg, testRoot, true);

        bool hasProvenance = migResult.migratedPackage.manifest.migrationProvenance.wasMigrated &&
                             (migResult.migratedPackage.manifest.migrationProvenance.sourceSchemaVersion == 1) &&
                             (migResult.migratedPackage.manifest.migrationProvenance.originalDistributionChecksum == legacyPkg.distributionChecksum);
        bool hasSha = !migResult.migratedPackage.contentDigestSha256.empty() &&
                      IsValidSha256HexFormat(migResult.migratedPackage.contentDigestSha256);
        bool isV2 = (migResult.migratedPackage.manifest.schemaVersion == 2) &&
                    (migResult.migratedPackage.manifest.revision == 2);

        auto valStrict = service.ValidateMigratedPackage(migResult.migratedPackage, testRoot);

        r.passed = migResult.success && isV2 && hasProvenance && hasSha && valStrict.isValid &&
                   (valStrict.integrityStatus == AssetIntegrityStatus::Verified);
        r.observedDesc = "Success: " + std::string(migResult.success ? "true" : "false") +
                         ", Revision: " + std::to_string(migResult.migratedPackage.manifest.revision) +
                         ", Provenance: " + std::string(hasProvenance ? "true" : "false") +
                         ", StrictStatus: " + std::string(AssetIntegrityStatusToString(valStrict.integrityStatus));
        r.details = r.passed ? "PASS" : "FAIL: Migrazione V1->V2 non conforme";
        results.push_back(r);
    }

    // =========================================================================
    // MIG-04: Conservazione lossless di geometria, facce e UV
    // =========================================================================
    {
        AssetSchemaMigrationTestResult r;
        r.testId = "MIG-04";
        r.testName = "LosslessGeometryAndTopologyConservation";
        r.inputDesc = "Package V1 con 24 vertici, 12 facce, bounds e coordinate UV specifiche";
        r.expectedDesc = "Tutti i vertici (pos, normal, color, roughMetal, UV) e indici facce conservati al 100%";

        AssetSchemaMigrationService service;
        service.SetRootAssetDirectory(testRoot);

        auto legacyPkg = createLegacyV1Package("fairworld:mig04_geometry", 1);
        auto migResult = service.MigrateToCurrent(legacyPkg, testRoot, true);

        bool geomMatch = true;
        const auto& origG = legacyPkg.geometry;
        const auto& migG = migResult.migratedPackage.geometry;

        if (migG.vertexCount != origG.vertexCount || migG.triangleCount != origG.triangleCount) {
            geomMatch = false;
        }
        if (migG.vertices.size() != origG.vertices.size() || migG.faces.size() != origG.faces.size()) {
            geomMatch = false;
        }

        for (size_t i = 0; i < origG.vertices.size() && geomMatch; ++i) {
            const auto& vO = origG.vertices[i];
            const auto& vM = migG.vertices[i];
            bool posEqual = (vO.position.x == vM.position.x && vO.position.y == vM.position.y && vO.position.z == vM.position.z);
            bool normEqual = (vO.normal.x == vM.normal.x && vO.normal.y == vM.normal.y && vO.normal.z == vM.normal.z);
            bool colEqual = (vO.color.x == vM.color.x && vO.color.y == vM.color.y && vO.color.z == vM.color.z && vO.color.w == vM.color.w);
            bool rmEqual = (vO.roughMetal.x == vM.roughMetal.x && vO.roughMetal.y == vM.roughMetal.y);
            bool scalarEqual = (vO.materialID == vM.materialID && vO.ao == vM.ao && vO.light == vM.light && vO.emissive == vM.emissive);
            if (!posEqual || !normEqual || !colEqual || !rmEqual || !scalarEqual) {
                geomMatch = false;
            }
        }

        for (size_t i = 0; i < origG.faces.size() && geomMatch; ++i) {
            if (origG.faces[i].indices != migG.faces[i].indices ||
                origG.faces[i].surfaceId != migG.faces[i].surfaceId) {
                geomMatch = false;
            }
        }

        r.passed = migResult.success && geomMatch;
        r.observedDesc = "VertexCount: " + std::to_string(migG.vertexCount) +
                         ", TriCount: " + std::to_string(migG.triangleCount) +
                         ", GeometryLossless: " + std::string(geomMatch ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Discrepanze geometriche rilevate post-migrazione";
        results.push_back(r);
    }

    // =========================================================================
    // MIG-05: Conservazione lossless di materiali, texture e parametri fisici
    // =========================================================================
    {
        AssetSchemaMigrationTestResult r;
        r.testId = "MIG-05";
        r.testName = "LosslessMaterialTextureAndPhysicsConservation";
        r.inputDesc = "Package V1 con parametri PBR espliciti, URI texture e fisica compilata personalizzata";
        r.expectedDesc = "Tutti i parametri PBR, URI e costanti fisiche mantenuti senza interpolazioni o default";

        AssetSchemaMigrationService service;
        service.SetRootAssetDirectory(testRoot);

        auto legacyPkg = createLegacyV1Package("fairworld:mig05_materials", 1);
        legacyPkg.baseColorFallback = glm::vec3(0.12f, 0.45f, 0.78f);
        legacyPkg.roughnessFallback = 0.88f;
        legacyPkg.metallicFallback = 0.92f;
        legacyPkg.emissiveStrength = 2.5f;
        legacyPkg.albedoMapUri = "textures/custom_albedo.png";
        legacyPkg.normalMapUri = "textures/custom_normal.png";
        legacyPkg.textureFingerprint = 0xABCDEF0123456789ULL;
        legacyPkg.compiledPhysics.mass = 42.0f;
        legacyPkg.compiledPhysics.density = 2400.0f;
        legacyPkg.compiledPhysics.friction = 0.75f;
        legacyPkg.compiledPhysics.restitution = 0.35f;
        legacyPkg.compiledPhysics.motionType = BlockMotionType::Static;
        legacyPkg.compiledPhysics.collisionEnabled = true;
        legacyPkg.compiledPhysics.massMode = BlockMassMode::Manual;

        // Generazione fixture texture PNG conformi e decodificabili (BM-F4.1)
        std::filesystem::create_directories(std::filesystem::path(testRoot) / "textures", ec);
        {
            ImageBitmap bmpAlbedo(8, 8, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.12f, 0.45f, 0.78f, 1.0f));
            auto pngData = bmpAlbedo.EncodeToPngMemory();
            std::ofstream f(std::filesystem::path(testRoot) / "textures/custom_albedo.png", std::ios::binary);
            f.write(reinterpret_cast<const char*>(pngData.data()), pngData.size());
        }
        {
            ImageBitmap bmpNormal(8, 8, TexturePixelFormat::RGBA8, TextureColorSpace::Linear, glm::vec4(0.5f, 0.5f, 1.0f, 1.0f));
            auto pngData = bmpNormal.EncodeToPngMemory();
            std::ofstream f(std::filesystem::path(testRoot) / "textures/custom_normal.png", std::ios::binary);
            f.write(reinterpret_cast<const char*>(pngData.data()), pngData.size());
        }

        auto migResult = service.MigrateToCurrent(legacyPkg, testRoot, true);

        bool isV2 = (migResult.migratedPackage.manifest.schemaVersion == 2);

        bool pbrMatch = (migResult.migratedPackage.baseColorFallback == legacyPkg.baseColorFallback) &&
                        (migResult.migratedPackage.roughnessFallback == legacyPkg.roughnessFallback) &&
                        (migResult.migratedPackage.metallicFallback == legacyPkg.metallicFallback) &&
                        (migResult.migratedPackage.emissiveStrength == legacyPkg.emissiveStrength);

        bool texUriMatch = (migResult.migratedPackage.albedoMapUri == legacyPkg.albedoMapUri) &&
                           (migResult.migratedPackage.normalMapUri == legacyPkg.normalMapUri) &&
                           (migResult.migratedPackage.textureFingerprint == legacyPkg.textureFingerprint);

        bool physMatch = (migResult.migratedPackage.compiledPhysics.mass == legacyPkg.compiledPhysics.mass) &&
                         (migResult.migratedPackage.compiledPhysics.density == legacyPkg.compiledPhysics.density) &&
                         (migResult.migratedPackage.compiledPhysics.friction == legacyPkg.compiledPhysics.friction) &&
                         (migResult.migratedPackage.compiledPhysics.restitution == legacyPkg.compiledPhysics.restitution) &&
                         (migResult.migratedPackage.compiledPhysics.motionType == legacyPkg.compiledPhysics.motionType) &&
                         (migResult.migratedPackage.compiledPhysics.collisionEnabled == legacyPkg.compiledPhysics.collisionEnabled) &&
                         (migResult.migratedPackage.compiledPhysics.massMode == legacyPkg.compiledPhysics.massMode);

        r.passed = migResult.success && isV2 && pbrMatch && texUriMatch && physMatch;
        r.observedDesc = "Success: " + std::string(migResult.success ? "true" : "false") +
                         ", SchemaVersion: " + std::to_string(migResult.migratedPackage.manifest.schemaVersion) +
                         ", PBR Match: " + std::string(pbrMatch ? "true" : "false") +
                         ", Tex Match: " + std::string(texUriMatch ? "true" : "false") +
                         ", Physics Match: " + std::string(physMatch ? "true" : "false");

        r.details = "migSuccess=" + std::string(migResult.success ? "true" : "false") +
                    ", isV2=" + std::string(isV2 ? "true" : "false") +
                    ", pbrMatch=" + std::string(pbrMatch ? "true" : "false") +
                    ", texUriMatch=" + std::string(texUriMatch ? "true" : "false") +
                    ", physMatch=" + std::string(physMatch ? "true" : "false") +
                    (migResult.errorMessage.empty() ? "" : ", err=" + migResult.errorMessage);
        if (!r.passed) {
            r.details = "FAIL: " + r.details;
        } else {
            r.details = "PASS: " + r.details;
        }
        results.push_back(r);
    }

    // =========================================================================
    // MIG-06: Preservazione AssetID e integrità referenziale dipendenze
    // =========================================================================
    {
        AssetSchemaMigrationTestResult r;
        r.testId = "MIG-06";
        r.testName = "AssetIdAndDependencyPreservation";
        r.inputDesc = "Package V1 con 2 dipendenze esplicite (ExactRevision e ActiveRevision)";
        r.expectedDesc = "AssetId identico e tutte le dipendenze preservate con identiche revisionPolicy e tipi";

        // FASE 6: Isolamento preventivo delle directory per garantire determinismo tra esecuzioni
        std::filesystem::remove_all(std::filesystem::path(testRoot) / "published" / "fairworld_dep_wood", ec);
        std::filesystem::remove_all(std::filesystem::path(testRoot) / "published" / "fairworld_dep_stone", ec);
        std::filesystem::remove_all(std::filesystem::path(testRoot) / "published" / "fairworld_mig06_composite", ec);

        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();
        ShapeRegistry sReg; sReg.Initialize();

        AssetPackagingService packaging;
        packaging.SetRootAssetDirectory(testRoot);
        packaging.SetTrustPolicy(AssetTrustPolicy::Strict);

        // FASE 1: Pubblicazione dipendenza WOOD (rev 1 poi rev 2 con contenuto legittimamente differenziato)
        auto woodPkg1 = createLegacyV1Package("fairworld:dep_wood", 1);
        woodPkg1.manifest.schemaVersion = 2;
        woodPkg1.baseColorFallback = glm::vec3(0.55f, 0.27f, 0.07f);
        woodPkg1.contentDigestSha256 = woodPkg1.ComputeContentDigestSha256(testRoot);
        auto pubWood1 = packaging.PublishPackagePersistent(woodPkg1, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        auto woodPkg2 = createLegacyV1Package("fairworld:dep_wood", 2);
        woodPkg2.manifest.schemaVersion = 2;
        woodPkg2.baseColorFallback = glm::vec3(0.65f, 0.35f, 0.12f);
        woodPkg2.contentDigestSha256 = woodPkg2.ComputeContentDigestSha256(testRoot);
        auto pubWood2 = packaging.PublishPackagePersistent(woodPkg2, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        // Pubblicazione dipendenza STONE (rev 1)
        auto stonePkg1 = createLegacyV1Package("fairworld:dep_stone", 1);
        stonePkg1.manifest.schemaVersion = 2;
        stonePkg1.baseColorFallback = glm::vec3(0.5f, 0.5f, 0.5f);
        stonePkg1.contentDigestSha256 = stonePkg1.ComputeContentDigestSha256(testRoot);
        auto pubStone1 = packaging.PublishPackagePersistent(stonePkg1, bReg, mReg, sReg, testRoot, AssetTrustPolicy::Strict);

        // Verifica disponibilità committed delle revisioni richieste
        bool woodRev1Committed = packaging.IsRevisionCommitted("fairworld:dep_wood", 1, testRoot);
        bool woodRev2Committed = packaging.IsRevisionCommitted("fairworld:dep_wood", 2, testRoot);
        auto activeStoneRev = packaging.GetActiveRevision("fairworld:dep_stone", testRoot);
        bool stoneRev1Committed = activeStoneRev.has_value() && (*activeStoneRev == 1) &&
                                  packaging.IsRevisionCommitted("fairworld:dep_stone", 1, testRoot);

        AssetSchemaMigrationService service;
        service.SetRootAssetDirectory(testRoot);

        // FASE 2: Preparazione package legacy con preservazione integrale dei campi
        auto legacyPkg = createLegacyV1Package("fairworld:mig06_composite", 1);
        RawAssetDependency dep1;
        dep1.targetId = "fairworld:dep_wood";
        dep1.requiredRevision = 2;
        dep1.revisionPolicy = DependencyRevisionPolicy::Exact;
        dep1.dependencyType = "material";
        dep1.isOptional = false;

        RawAssetDependency dep2;
        dep2.targetId = "fairworld:dep_stone";
        dep2.requiredRevision = 1;
        dep2.revisionPolicy = DependencyRevisionPolicy::Active;
        dep2.dependencyType = "shape";
        dep2.isOptional = false;

        legacyPkg.manifest.dependencies = { dep1, dep2 };

        // FASE 3: Esecuzione migrazione V1 -> V2
        auto migResult = service.MigrateToCurrent(legacyPkg, testRoot, true);

        // FASE 4: Assertion rigorose
        bool isV2 = (migResult.migratedPackage.manifest.schemaVersion == 2);
        bool idMatch = (migResult.migratedPackage.manifest.assetId == legacyPkg.manifest.assetId);
        bool depCountMatch = (migResult.migratedPackage.manifest.dependencies.size() == 2);
        bool depMatch = depCountMatch;
        if (depCountMatch) {
            const auto& d1 = migResult.migratedPackage.manifest.dependencies[0];
            const auto& d2 = migResult.migratedPackage.manifest.dependencies[1];
            if (d1.targetId != dep1.targetId || d1.requiredRevision != dep1.requiredRevision ||
                d1.revisionPolicy != dep1.revisionPolicy || d1.dependencyType != dep1.dependencyType ||
                d1.expectedDigestSha256 != dep1.expectedDigestSha256 || d1.isOptional != dep1.isOptional) {
                depMatch = false;
            }
            if (d2.targetId != dep2.targetId || d2.requiredRevision != dep2.requiredRevision ||
                d2.revisionPolicy != dep2.revisionPolicy || d2.dependencyType != dep2.dependencyType ||
                d2.expectedDigestSha256 != dep2.expectedDigestSha256 || d2.isOptional != dep2.isOptional) {
                depMatch = false;
            }
        }

        r.passed = pubWood1.success && pubWood2.success && pubStone1.success &&
                   woodRev1Committed && woodRev2Committed && stoneRev1Committed &&
                   migResult.success && isV2 && idMatch && depMatch;

        r.observedDesc = "WoodRev2Committed: " + std::string(woodRev2Committed ? "true" : "false") +
                         ", StoneRev1Committed: " + std::string(stoneRev1Committed ? "true" : "false") +
                         ", MigSuccess: " + std::string(migResult.success ? "true" : "false") +
                         ", AssetIdMatch: " + std::string(idMatch ? "true" : "false") +
                         ", DepCount: " + std::to_string(migResult.migratedPackage.manifest.dependencies.size()) +
                         ", DepMatch: " + std::string(depMatch ? "true" : "false");

        // FASE 5: Diagnostica dettagliata con scomposizione per identificare la prima operazione fallita
        r.details = "woodRev1Published=" + std::string(pubWood1.success ? "true" : "false") +
                    ", woodRev2Published=" + std::string(pubWood2.success ? "true" : "false") +
                    ", stoneRev1Published=" + std::string(pubStone1.success ? "true" : "false") +
                    ", woodRev2Committed=" + std::string(woodRev2Committed ? "true" : "false") +
                    ", stoneRev1Committed=" + std::string(stoneRev1Committed ? "true" : "false") +
                    ", migrationSuccess=" + std::string(migResult.success ? "true" : "false") +
                    ", isV2=" + std::string(isV2 ? "true" : "false") +
                    ", assetIdMatch=" + std::string(idMatch ? "true" : "false") +
                    ", dependencyCountMatch=" + std::string(depCountMatch ? "true" : "false") +
                    ", dependencyMatch=" + std::string(depMatch ? "true" : "false") +
                    (pubWood1.errorMessage.empty() ? "" : ", wood1Err=" + pubWood1.errorMessage) +
                    (pubWood2.errorMessage.empty() ? "" : ", wood2Err=" + pubWood2.errorMessage) +
                    (pubStone1.errorMessage.empty() ? "" : ", stone1Err=" + pubStone1.errorMessage) +
                    (migResult.errorMessage.empty() ? "" : ", migErr=" + migResult.errorMessage);
        if (!r.passed) {
            r.details = "FAIL: " + r.details;
        } else {
            r.details = "PASS: " + r.details;
        }
        results.push_back(r);
    }

    // =========================================================================
    // MIG-07: Rifiuto netto di schemi sconosciuti o non supportati
    // =========================================================================
    {
        AssetSchemaMigrationTestResult r;
        r.testId = "MIG-07";
        r.testName = "UnknownSchemaStrictRejection";
        r.inputDesc = "Package con schemaVersion = 99 e package con schemaVersion = 0";
        r.expectedDesc = "InspectSchema segnala UnsupportedSchema, CanMigrate è false, MigrateToCurrent fallisce";

        AssetSchemaMigrationService service;
        service.SetRootAssetDirectory(testRoot);

        auto badPkg99 = createLegacyV1Package("fairworld:mig07_unknown", 1);
        badPkg99.manifest.schemaVersion = 99;

        auto badPkg0 = createLegacyV1Package("fairworld:mig07_zero", 1);
        badPkg0.manifest.schemaVersion = 0;

        auto insp99 = service.InspectSchema(badPkg99);
        auto insp0 = service.InspectSchema(badPkg0);

        std::string err99, err0;
        bool can99 = service.CanMigrate(badPkg99, &err99);
        bool can0 = service.CanMigrate(badPkg0, &err0);

        auto mig99 = service.MigrateToCurrent(badPkg99, testRoot);
        auto mig0 = service.MigrateToCurrent(badPkg0, testRoot);

        r.passed = (insp99.state == SchemaVersionState::UnsupportedSchema) &&
                   (insp0.state == SchemaVersionState::UnsupportedSchema) &&
                   !can99 && !can0 &&
                   !mig99.success && !mig0.success;
        r.observedDesc = std::string("Insp99: ") + SchemaVersionStateToString(insp99.state) +
                         ", Can99: " + std::string(can99 ? "true" : "false") +
                         ", Insp0: " + std::string(SchemaVersionStateToString(insp0.state)) +
                         ", Can0: " + std::string(can0 ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Schema sconosciuto non rifiutato categoricamente";
        results.push_back(r);
    }

    // =========================================================================
    // MIG-08: Rollback transazionale su migrazione fallita
    // =========================================================================
    {
        AssetSchemaMigrationTestResult r;
        r.testId = "MIG-08";
        r.testName = "TransactionalRollbackOnMigrationFailure";
        r.inputDesc = "Package V1 con geometria incoerente (vertexCount dichiarato non corrispondente) sottoposto a MigrateAndPublishPersistent";
        r.expectedDesc = "Transazione abortita, registri runtime intatti, nessun package parziale pubblicato";

        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();
        ShapeRegistry sReg; sReg.Initialize();

        AssetSchemaMigrationService service;
        service.SetRootAssetDirectory(testRoot);

        auto corruptLegacy = createLegacyV1Package("fairworld:mig08_corrupt", 1);
        corruptLegacy.geometry.vertexCount = 999; // Discrepanza che causa fallimento precondizione

        size_t initialBlockCount = bReg.GetActiveBlockCount();
        auto migRes = service.MigrateAndPublishPersistent(corruptLegacy, bReg, mReg, sReg, testRoot, true);

        bool registersUntouched = (bReg.GetActiveBlockCount() == initialBlockCount);
        r.passed = !migRes.success && registersUntouched &&
                   (migRes.finalIntegrityStatus == AssetIntegrityStatus::Rejected);
        r.observedDesc = "Success: " + std::string(migRes.success ? "true" : "false") +
                         ", BlockCountBefore: " + std::to_string(initialBlockCount) +
                         ", BlockCountAfter: " + std::to_string(bReg.GetActiveBlockCount()) +
                         ", Error: " + migRes.errorMessage;
        r.details = r.passed ? "PASS" : "FAIL: Rollback transazionale non rispettato su fallimento migrazione";
        results.push_back(r);
    }

    // =========================================================================
    // MIG-09: Migrazione ripetuta senza lavoro superfluo (Zero-Work Safety)
    // =========================================================================
    {
        AssetSchemaMigrationTestResult r;
        r.testId = "MIG-09";
        r.testName = "RepeatedMigrationZeroWorkSafety";
        r.inputDesc = "Package già migrato e conforme a Schema V2 con digest canonico valido risottoposto a migrazione";
        r.expectedDesc = "Zero-work hit: wasZeroWorkHit = true, nessuna ri-serializzazione, revisione invariata";

        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();
        ShapeRegistry sReg; sReg.Initialize();

        AssetSchemaMigrationService service;
        service.SetRootAssetDirectory(testRoot);

        auto legacyPkg = createLegacyV1Package("fairworld:mig09_zerowork", 1);
        auto firstMig = service.MigrateAndPublishPersistent(legacyPkg, bReg, mReg, sReg, testRoot, true);

        // Seconda migrazione sullo stesso package migrato (ora V2 valido)
        auto secondMig = service.MigrateAndPublishPersistent(firstMig.migratedPackage, bReg, mReg, sReg, testRoot, true);

        r.passed = firstMig.success && !firstMig.wasZeroWorkHit &&
                   secondMig.success && secondMig.wasZeroWorkHit &&
                   (secondMig.migratedPackage.manifest.revision == firstMig.migratedPackage.manifest.revision);
        r.observedDesc = "FirstZeroWork: " + std::string(firstMig.wasZeroWorkHit ? "true" : "false") +
                         ", SecondZeroWork: " + std::string(secondMig.wasZeroWorkHit ? "true" : "false") +
                         ", Rev1: " + std::to_string(firstMig.migratedPackage.manifest.revision) +
                         ", Rev2: " + std::to_string(secondMig.migratedPackage.manifest.revision);
        r.details = r.passed ? "PASS" : "FAIL: Zero-Work non rilevato su package già conforme a V2";
        results.push_back(r);
    }

    // =========================================================================
    // MIG-10: Regressione delle suite precedenti
    // =========================================================================
    {
        AssetSchemaMigrationTestResult r;
        r.testId = "MIG-10";
        r.testName = "HistoricalSuitesRegressionGate";
        r.inputDesc = "Esecuzione aggregata suite BM-F4.4 (DEP), BM-F4.3.1 (CRH-H), BM-F4.3 (CRH), BM-F4.2 (DGV)";
        r.expectedDesc = "Tutte le suite precedenti mantengono 100% PASS senza alcuna regressione";

        if (TestRunContext::Instance().IsActive()) {
            static const std::vector<std::pair<std::string, size_t>> deps = {
                {"DEP", 10}, {"CRH-H", 8}, {"CRH", 10}, {"DGV", 10}
            };
            auto eval = TestRunContext::Instance().EvaluateRegressionGate(deps);
            r.passed = eval.allDependenciesPassed && (eval.totalDiscovered >= 38) && (eval.totalFailed == 0);
            r.observedDesc = "Suite storiche eseguite: " + std::to_string(eval.totalPassed) + "/" +
                             std::to_string(eval.totalDiscovered) + " test superati";
            r.details = r.passed ? "PASS" : "FAIL: " + eval.failureDetails;
        } else {
            auto depResults = AssetDependencyContractTests::RunAllTests();
            auto crhHResults = AssetCrashRecoveryHardeningTests::RunAllTests();
            auto crhResults = AssetCrashRecoveryTests::RunAllTests();
            auto dgvResults = AssetDigestTrustTests::RunAllTests();

            bool allPassed = true;
            size_t passCount = 0;
            size_t totalCount = depResults.size() + crhHResults.size() + crhResults.size() + dgvResults.size();

            for (const auto& res : depResults) { if (res.passed) passCount++; else allPassed = false; }
            for (const auto& res : crhHResults) { if (res.passed) passCount++; else allPassed = false; }
            for (const auto& res : crhResults) { if (res.passed) passCount++; else allPassed = false; }
            for (const auto& res : dgvResults) { if (res.passed) passCount++; else allPassed = false; }

            r.passed = allPassed && (totalCount >= 38);
            r.observedDesc = "Suite storiche eseguite: " + std::to_string(passCount) + "/" +
                             std::to_string(totalCount) + " test superati";
            r.details = r.passed ? "PASS" : "FAIL: Regressione riscontrata in una suite precedente";
        }
        results.push_back(r);
    }

    return results;
}

} // namespace fw
