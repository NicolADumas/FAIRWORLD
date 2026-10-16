#include "AssetSchemaMigrationService.h"
#include <fstream>
#include <iostream>

namespace fw {

AssetSchemaMigrationService::AssetSchemaMigrationService(AssetPackagingService* packagingService)
    : m_externalPackagingService(packagingService)
{
    if (!m_externalPackagingService) {
        m_ownedPackagingService = std::make_unique<AssetPackagingService>();
    }
}

AssetPackagingService& AssetSchemaMigrationService::GetPackagingService() {
    if (m_externalPackagingService) {
        if (!m_rootAssetDirectory.empty()) {
            m_externalPackagingService->SetRootAssetDirectory(m_rootAssetDirectory);
        }
        return *m_externalPackagingService;
    }
    if (!m_rootAssetDirectory.empty()) {
        m_ownedPackagingService->SetRootAssetDirectory(m_rootAssetDirectory);
    }
    return *m_ownedPackagingService;
}

const AssetPackagingService& AssetSchemaMigrationService::GetPackagingService() const {
    if (m_externalPackagingService) {
        return *m_externalPackagingService;
    }
    return *m_ownedPackagingService;
}

SchemaInspectionResult AssetSchemaMigrationService::InspectSchema(
    const RawAssetPackage& package,
    const std::string& rootDir
) const {
    SchemaInspectionResult result;
    result.detectedVersion = package.manifest.schemaVersion;
    result.digestSha256 = package.contentDigestSha256;
    result.hasMigrationProvenance = package.manifest.migrationProvenance.wasMigrated;

    if (package.manifest.schemaVersion == 0 || package.manifest.schemaVersion > CURRENT_RAW_ASSET_SCHEMA_VERSION) {
        result.state = SchemaVersionState::UnsupportedSchema;
        result.hasValidDigest = false;
        result.message = "Versione schema non supportata: " + std::to_string(package.manifest.schemaVersion) +
                         " (supportate solo V1 legacy e V2 corrente)";
        return result;
    }

    if (package.manifest.schemaVersion == LEGACY_RAW_ASSET_SCHEMA_VERSION) {
        result.state = SchemaVersionState::LegacySchema;
        result.hasValidDigest = !package.contentDigestSha256.empty() && IsValidSha256HexFormat(package.contentDigestSha256);
        result.message = "Schema legacy V1 rilevato. Migrazione a V2 richiesta.";
        return result;
    }

    // Schema V2 corrente
    result.hasValidDigest = !package.contentDigestSha256.empty() && IsValidSha256HexFormat(package.contentDigestSha256);
    if (!result.hasValidDigest) {
        result.state = SchemaVersionState::MigrationFailed;
        result.message = "Schema V2 dichiarato ma privo di digest crittografico SHA-256 valido.";
        return result;
    }

    const std::string effectiveRoot = rootDir.empty() ? m_rootAssetDirectory : rootDir;
    std::string computedDigest = package.ComputeContentDigestSha256(effectiveRoot);
    if (computedDigest != package.contentDigestSha256) {
        result.state = SchemaVersionState::MigrationFailed;
        result.message = "Schema V2 con digest SHA-256 non corrispondente ai contenuti (atteso " +
                         package.contentDigestSha256.substr(0, 16) + "..., calcolato " +
                         computedDigest.substr(0, 16) + "...).";
        return result;
    }

    result.state = SchemaVersionState::CurrentSchema;
    result.message = "Schema corrente V2 valido, integro e verificato.";
    return result;
}

SchemaInspectionResult AssetSchemaMigrationService::InspectSchemaFile(
    const std::string& packageFilePath,
    const std::string& rootDir
) const {
    SchemaInspectionResult result;
    std::error_code ec;
    if (!std::filesystem::exists(packageFilePath, ec)) {
        result.state = SchemaVersionState::UnsupportedSchema;
        result.message = "File pacchetto inesistente: " + packageFilePath;
        return result;
    }

    RawAssetPackage pkg;
    if (!pkg.LoadFromFile(packageFilePath)) {
        result.state = SchemaVersionState::UnsupportedSchema;
        result.message = "Impossibile deserializzare JSON da file: " + packageFilePath;
        return result;
    }

    return InspectSchema(pkg, rootDir);
}

bool AssetSchemaMigrationService::CanMigrate(
    const RawAssetPackage& package,
    std::string* outReason
) const {
    auto inspection = InspectSchema(package);
    if (inspection.state == SchemaVersionState::UnsupportedSchema) {
        if (outReason) *outReason = inspection.message;
        return false;
    }

    if (package.manifest.assetId.empty()) {
        if (outReason) *outReason = "Manifest assetId vuoto: identita' asset assente.";
        return false;
    }

    std::string idErr;
    if (!BlockRegistry::ValidateStringId(package.manifest.assetId, &idErr)) {
        if (outReason) *outReason = "Identificatore asset non conforme: " + idErr;
        return false;
    }

    if (package.geometry.vertices.empty()) {
        if (outReason) *outReason = "Geometria compilata assente: nessun vertice.";
        return false;
    }

    if (package.geometry.vertexCount != package.geometry.vertices.size()) {
        if (outReason) *outReason = "Discrepanza conteggio vertici (" +
                                    std::to_string(package.geometry.vertexCount) + " vs " +
                                    std::to_string(package.geometry.vertices.size()) + ").";
        return false;
    }

    if (package.compiledPhysics.mass <= 0.0f && package.compiledPhysics.massMode == BlockMassMode::Manual) {
        if (outReason) *outReason = "Massa fisica non valida (deve essere > 0 per modalita Manual).";
        return false;
    }

    if (package.compiledPhysics.density <= 0.0f) {
        if (outReason) *outReason = "Densita' fisica non valida (deve essere > 0).";
        return false;
    }

    return true;
}

bool AssetSchemaMigrationService::CanMigrateFile(
    const std::string& packageFilePath,
    std::string* outReason
) const {
    RawAssetPackage pkg;
    if (!pkg.LoadFromFile(packageFilePath)) {
        if (outReason) *outReason = "Impossibile aprire o decodificare il file specificato: " + packageFilePath;
        return false;
    }
    return CanMigrate(pkg, outReason);
}

MigrationPlan AssetSchemaMigrationService::BuildMigrationPlan(
    const RawAssetPackage& package,
    bool allocateNewRevision
) const {
    MigrationPlan plan;
    plan.sourceVersion = package.manifest.schemaVersion;
    plan.targetVersion = CURRENT_RAW_ASSET_SCHEMA_VERSION;
    plan.assetId = package.manifest.assetId;
    plan.sourceRevision = package.manifest.revision;
    plan.targetRevision = allocateNewRevision ? (package.manifest.revision + 1) : package.manifest.revision;

    std::string reason;
    if (!CanMigrate(package, &reason)) {
        plan.isFeasible = false;
        plan.failureReason = reason;
        return plan;
    }

    plan.isFeasible = true;

    if (package.manifest.schemaVersion == CURRENT_RAW_ASSET_SCHEMA_VERSION) {
        MigrationPlanStep step;
        step.stepName = "Zero-Work Check";
        step.description = "Package gia' nello schema corrente V2 con digest canonico valido; nessuna migrazione richiesta.";
        step.isCompleted = true;
        plan.steps.push_back(step);
        return plan;
    }

    plan.steps.push_back({"1. Verifica invarianti legacy", "Caricamento e validazione precondizioni sotto policy AllowLegacy", false});
    plan.steps.push_back({"2. Allocazione revisione", "Impostazione revisione target " + std::to_string(plan.targetRevision), false});
    plan.steps.push_back({"3. Migrazione lossless metadati", "Aggiornamento schemaVersion a 2 e conservazione identita' asset", false});
    plan.steps.push_back({"4. Conservazione 100% geometria e UV", "Preservazione integrale di pos, normal, color, roughMetal, ao, light, emissive e bounds", false});
    plan.steps.push_back({"5. Conservazione materiali, texture e fisica", "Preservazione PBR, fallback, percorsi texture e parametri fisici", false});
    plan.steps.push_back({"6. Conservazione 1:1 dipendenze", "Preservazione di tutte le dipendenze dichiarate e relative policy", false});
    plan.steps.push_back({"7. Registrazione provenance", "Marcatura wasMigrated=true, sourceVersion=1 e checksum FNV-1a originale", false});
    plan.steps.push_back({"8. Calcolo digest canonico SHA-256", "Generazione digest SHA-256 V2 canonico deterministico", false});
    plan.steps.push_back({"9. Validazione Strict V2", "Verifica finale formale sotto AssetTrustPolicy::Strict", false});

    return plan;
}

MigrationResult AssetSchemaMigrationService::MigrateToCurrent(
    const RawAssetPackage& legacyPackage,
    const std::string& rootDir,
    bool allocateNewRevision
) const {
    const std::string effectiveRoot = rootDir.empty() ? m_rootAssetDirectory : rootDir;
    auto inspection = InspectSchema(legacyPackage, effectiveRoot);

    // FASE 8: Zero-Work check
    if (inspection.state == SchemaVersionState::CurrentSchema) {
        MigrationResult zeroRes;
        zeroRes.success = true;
        zeroRes.wasZeroWorkHit = true;
        zeroRes.migratedPackage = legacyPackage;
        zeroRes.finalIntegrityStatus = AssetIntegrityStatus::Verified;
        zeroRes.stepsExecuted.push_back("Zero-work: package gia conforme a schema V2 con digest canonico verificato.");
        return zeroRes;
    }

    if (inspection.state == SchemaVersionState::UnsupportedSchema) {
        MigrationResult failRes;
        failRes.success = false;
        failRes.wasZeroWorkHit = false;
        failRes.errorMessage = "Impossibile eseguire migrazione: " + inspection.message;
        failRes.finalIntegrityStatus = AssetIntegrityStatus::Rejected;
        return failRes;
    }

    // FASE 4 / 5: Pre-validazione invarianti con policy AllowLegacy
    auto preVal = GetPackagingService().ValidatePackage(legacyPackage, effectiveRoot, AssetTrustPolicy::AllowLegacy);
    if (!preVal.isValid) {
        MigrationResult failRes;
        failRes.success = false;
        failRes.wasZeroWorkHit = false;
        failRes.errorMessage = "Pre-validazione legacy fallita: " +
                               (preVal.errors.empty() ? "Invarianti corrotte" : preVal.errors.front());
        failRes.finalIntegrityStatus = AssetIntegrityStatus::Rejected;
        return failRes;
    }

    // Costruzione lossless del package V2
    RawAssetPackage migrated = legacyPackage;

    // 1. Metadati e versione schema
    migrated.manifest.schemaVersion = CURRENT_RAW_ASSET_SCHEMA_VERSION;
    if (allocateNewRevision) {
        migrated.manifest.revision = legacyPackage.manifest.revision + 1;
    }
    migrated.manifest.lastModifiedTimestamp = GetCurrentTimestampSec();

    // 2. Registrazione provenienza (FASE 5)
    migrated.manifest.migrationProvenance.wasMigrated = true;
    migrated.manifest.migrationProvenance.sourceSchemaVersion = legacyPackage.manifest.schemaVersion;
    migrated.manifest.migrationProvenance.migrationTimestamp = GetCurrentTimestampSec();
    migrated.manifest.migrationProvenance.originalDistributionChecksum = legacyPackage.distributionChecksum;
    migrated.manifest.migrationProvenance.migrationNotes =
        "Lossless migration from Schema V1 (LegacyUnverified) to Schema V2 (Verified) under BM-F4.5 contract.";

    // 3. Garanzia stadio minimo
    if (migrated.manifest.stage < RawAssetStage::Packaged) {
        migrated.manifest.stage = RawAssetStage::Packaged;
    }

    // 4. Calcolo nuovo digest canonico SHA-256 (FASE 5)
    migrated.contentDigestSha256 = migrated.ComputeContentDigestSha256(effectiveRoot);

    // 5. Validazione post-migrazione con policy Strict (FASE 5)
    auto postVal = ValidateMigratedPackage(migrated, effectiveRoot);
    if (!postVal.isValid || postVal.integrityStatus != AssetIntegrityStatus::Verified) {
        MigrationResult failRes;
        failRes.success = false;
        failRes.wasZeroWorkHit = false;
        failRes.errorMessage = "Validazione Strict post-migrazione V2 fallita: " +
                               (postVal.errors.empty() ? "Errore di integrita' o dipendenze" : postVal.errors.front());
        failRes.finalIntegrityStatus = postVal.integrityStatus;
        return failRes;
    }

    MigrationResult successRes;
    successRes.success = true;
    successRes.wasZeroWorkHit = false;
    successRes.migratedPackage = std::move(migrated);
    successRes.finalIntegrityStatus = AssetIntegrityStatus::Verified;
    successRes.stepsExecuted = {
        "1. Precondizioni V1 verificate con policy AllowLegacy",
        "2. Copia lossless integrale di geometria, facce, UV, materiali, texture, fisica e dipendenze",
        "3. Aggiornamento schemaVersion a 2 e bump revisione a " + std::to_string(successRes.migratedPackage.manifest.revision),
        "4. Registrazione metadati di provenienza migrazione con timestamp e checksum originale",
        "5. Generazione digest crittografico canonico SHA-256 V2: " + successRes.migratedPackage.contentDigestSha256.substr(0, 16) + "...",
        "6. Validazione formale Strict superata con status Verified"
    };
    return successRes;
}

RawAssetValidation AssetSchemaMigrationService::ValidateMigratedPackage(
    const RawAssetPackage& migratedPackage,
    const std::string& rootDir
) const {
    const std::string effectiveRoot = rootDir.empty() ? m_rootAssetDirectory : rootDir;
    return GetPackagingService().ValidatePackage(migratedPackage, effectiveRoot, AssetTrustPolicy::Strict);
}

MigrationResult AssetSchemaMigrationService::MigrateAndPublishPersistent(
    const RawAssetPackage& legacyPackage,
    BlockRegistry& bReg,
    MaterialRegistry& mReg,
    ShapeRegistry& sReg,
    const std::string& rootDir,
    bool allocateNewRevision
) {
    const std::string effectiveRoot = rootDir.empty() ? m_rootAssetDirectory : rootDir;

    // FASE 8: Zero-Work preliminare
    auto inspection = InspectSchema(legacyPackage, effectiveRoot);
    if (inspection.state == SchemaVersionState::CurrentSchema) {
        MigrationResult zeroRes;
        zeroRes.success = true;
        zeroRes.wasZeroWorkHit = true;
        zeroRes.migratedPackage = legacyPackage;
        zeroRes.finalIntegrityStatus = AssetIntegrityStatus::Verified;
        zeroRes.stepsExecuted.push_back("Zero-work: package gia' persistito e conforme a schema V2; nessuna riscrittura.");
        return zeroRes;
    }

    // Migrazione in-memory con verifiche lossless e crittografiche
    auto migResult = MigrateToCurrent(legacyPackage, effectiveRoot, allocateNewRevision);
    if (!migResult.success) {
        return migResult;
    }

    // FASE 7: Pubblicazione atomica persistente tramite staging isolato e crash recovery journal
    auto pubResult = GetPackagingService().PublishPackagePersistent(
        migResult.migratedPackage,
        bReg,
        mReg,
        sReg,
        effectiveRoot,
        AssetTrustPolicy::Strict
    );

    if (!pubResult.success) {
        MigrationResult failRes;
        failRes.success = false;
        failRes.wasZeroWorkHit = false;
        failRes.errorMessage = "Transazione di persistenza fallita: " + pubResult.errorMessage;
        failRes.finalIntegrityStatus = AssetIntegrityStatus::Rejected;
        return failRes;
    }

    migResult.stepsExecuted.push_back("7. Transazione persistente committed con successo (ID tx: " + pubResult.transactionId + ")");
    return migResult;
}

MigrationResult AssetSchemaMigrationService::MigrateFilePersistent(
    const std::string& sourcePackagePath,
    BlockRegistry& bReg,
    MaterialRegistry& mReg,
    ShapeRegistry& sReg,
    const std::string& rootDir,
    bool allocateNewRevision
) {
    RawAssetPackage legacyPkg;
    if (!legacyPkg.LoadFromFile(sourcePackagePath)) {
        MigrationResult failRes;
        failRes.success = false;
        failRes.wasZeroWorkHit = false;
        failRes.errorMessage = "Impossibile aprire o decodificare package da file: " + sourcePackagePath;
        failRes.finalIntegrityStatus = AssetIntegrityStatus::Rejected;
        return failRes;
    }

    return MigrateAndPublishPersistent(legacyPkg, bReg, mReg, sReg, rootDir, allocateNewRevision);
}

} // namespace fw
