#include "pch.h"
#include "AssetPackagingService.h"
#include "TextureBakingService.h"
#include "stb_image.h"
#include <filesystem>
#include <fstream>

namespace fw {

AssetPackagingService::AssetPackagingService() = default;

bool AssetPackagingService::ValidateTextureFile(const std::string& fullPath, const std::string& mapName, std::string* outError, AssetIntegrityErrorCode* outCode) const {
    if (outCode) *outCode = AssetIntegrityErrorCode::None;
    std::error_code ec;
    if (!std::filesystem::exists(fullPath, ec) || std::filesystem::file_size(fullPath, ec) == 0) {
        if (outError) *outError = "File texture " + mapName + " non rintracciabile o vuoto su disco: " + fullPath;
        if (outCode) *outCode = AssetIntegrityErrorCode::MissingDependency;
        return false;
    }

    auto lastWrite = std::filesystem::last_write_time(fullPath, ec);
    auto fileSize = std::filesystem::file_size(fullPath, ec);

    // Verifica cache di validazione (Zero-Work decoding bypass)
    auto it = m_textureValidationCache.find(fullPath);
    if (it != m_textureValidationCache.end() &&
        it->second.lastWriteTime == lastWrite &&
        it->second.fileSize == fileSize) {
        if (!it->second.isValid) {
            if (outError) *outError = it->second.errorMessage;
            if (outCode) *outCode = AssetIntegrityErrorCode::TextureDecodeFailure;
        }
        return it->second.isValid;
    }

    // Lettura completa del file binario
    std::ifstream file(fullPath, std::ios::binary);
    if (!file.is_open()) {
        std::string err = "Impossibile aprire il file texture " + mapName + ": " + fullPath;
        m_textureValidationCache[fullPath] = { lastWrite, fileSize, false, err, 0, 0 };
        if (outError) *outError = err;
        if (outCode) *outCode = AssetIntegrityErrorCode::FileReadFailure;
        return false;
    }

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    // 1. Verifica signature magic PNG (8 byte)
    if (buffer.size() < 8 ||
        buffer[0] != 0x89 || buffer[1] != 'P' ||
        buffer[2] != 'N'  || buffer[3] != 'G' ||
        buffer[4] != 0x0D || buffer[5] != 0x0A ||
        buffer[6] != 0x1A || buffer[7] != 0x0A) {
        std::string err = "File texture " + mapName + " corrotto o formato non valido (magic PNG non riconosciuto): " + fullPath;
        m_textureValidationCache[fullPath] = { lastWrite, fileSize, false, err, 0, 0 };
        if (outError) *outError = err;
        if (outCode) *outCode = AssetIntegrityErrorCode::TextureDecodeFailure;
        return false;
    }

    // 2. Verifica payload PNG decodificabile e non troncato (BM-F4.1)
    int w = 0, h = 0, channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(buffer.data(), static_cast<int>(buffer.size()), &w, &h, &channels, 4);
    if (!decoded) {
        std::string err = "File texture " + mapName + " corrotto o troncato (impossibile decodificare il payload PNG): " + fullPath;
        m_textureValidationCache[fullPath] = { lastWrite, fileSize, false, err, 0, 0 };
        if (outError) *outError = err;
        if (outCode) *outCode = AssetIntegrityErrorCode::TextureDecodeFailure;
        return false;
    }

    stbi_image_free(decoded);

    // Salva esito positivo in cache
    m_textureValidationCache[fullPath] = {
        lastWrite,
        fileSize,
        true,
        "",
        static_cast<uint32_t>(w),
        static_cast<uint32_t>(h)
    };
    return true;
}

RawAssetValidation AssetPackagingService::ValidatePackage(
    const RawAssetPackage& package,
    const std::string& rootDir,
    std::optional<AssetTrustPolicy> policy,
    const std::string& dependencyRootDir
) const {
    RawAssetValidation report;
    report.isValid = true;
    report.integrityStatus = AssetIntegrityStatus::Rejected;
    report.primaryErrorCode = AssetIntegrityErrorCode::None;

    const std::string effectiveRoot = rootDir.empty() ? m_rootAssetDirectory : rootDir;
    const std::string effectiveDepRoot = dependencyRootDir.empty() ? effectiveRoot : dependencyRootDir;
    const AssetTrustPolicy effectivePolicy = policy.value_or(m_defaultTrustPolicy);

    // 1. Validazione Metadati e Identità
    if (package.manifest.assetId.empty()) {
        report.AddError("Manifest assetId vuoto.");
    } else {
        std::string idErr;
        if (!BlockRegistry::ValidateStringId(package.manifest.assetId, &idErr)) {
            report.AddError("Identificatore asset non valido: " + idErr);
        }
    }

    // 2. Validazione Schema Version & Trust Policy (BM-F4.2)
    if (package.manifest.schemaVersion == 0 || package.manifest.schemaVersion > 2) {
        report.AddError("Versione schema non supportata: " + std::to_string(package.manifest.schemaVersion) +
                        " (supportate solo V1 legacy e V2)", AssetIntegrityErrorCode::UnsupportedSchema);
    } else if (package.manifest.schemaVersion == 1) {
        if (effectivePolicy == AssetTrustPolicy::Strict) {
            report.AddError("Asset schema legacy V1 rifiutato sotto policy di fiducia Strict.", AssetIntegrityErrorCode::UnsupportedSchema);
        } else {
            if (package.contentDigestSha256.empty()) {
                report.AddWarning("Asset schema legacy V1 privo di digest crittografico ammesso in modalita' AllowLegacy (LegacyUnverified).");
            }
        }
    } else if (package.manifest.schemaVersion == 2) {
        if (package.contentDigestSha256.empty()) {
            report.AddError("Digest SHA-256 mancante per asset schema V2 (obbligatorio).", AssetIntegrityErrorCode::MissingDigest);
        }
    }

    // 3. Validazione Geometria Compilata
    if (package.geometry.vertices.empty()) {
        report.AddError("Geometria compilata assente: nessun vertice nel pacchetto.");
    } else if (package.geometry.vertexCount != package.geometry.vertices.size()) {
        report.AddError("Discrepanza conteggio vertici: atteso " + std::to_string(package.geometry.vertexCount) +
                        ", presenti " + std::to_string(package.geometry.vertices.size()));
    }
    if (package.geometrySource == GeometrySource::SculptedMesh && package.shape.type != ShapeType::TriangleMesh) {
        report.AddError("Disallineamento sorgente geometrica: dichiarato SculptedMesh ma ShapeDefinition non e' TriangleMesh.");
    }

    // 4. Validazione Parametri Fisici
    if (package.compiledPhysics.mass <= 0.0f && package.compiledPhysics.massMode == BlockMassMode::Manual) {
        report.AddError("Massa fisica deve essere strettamente positiva (> 0 kg).");
    }
    if (package.compiledPhysics.density <= 0.0f) {
        report.AddError("Densita fisica deve essere strettamente positiva (> 0 kg/m^3).");
    }
    if (package.compiledPhysics.friction < 0.0f || package.compiledPhysics.friction > 1.0f) {
        report.AddWarning("Coefficiente di attrito fuori scala [0, 1]. Clamp applicato.");
    }

    // 5. Validazione Mappe Texture Referenziate con Decodifica e Cache (BM-F4.1 / BM-F4.2)
    auto validateTextureMap = [&](const std::string& uri, const std::string& mapName) {
        if (uri.empty()) return;
        std::string uriErr;
        if (!TextureBakingService::ValidatePortableUri(uri, &uriErr)) {
            report.AddError("URI " + mapName + " non valido: " + uriErr, AssetIntegrityErrorCode::MissingDependency);
            return;
        }
        std::filesystem::path fullPath = std::filesystem::path(effectiveRoot) / uri;
        std::string texErr;
        AssetIntegrityErrorCode code = AssetIntegrityErrorCode::None;
        if (!ValidateTextureFile(fullPath.string(), mapName, &texErr, &code)) {
            report.AddError(texErr, code);
        }
    };

    validateTextureMap(package.albedoMapUri, "Albedo");
    validateTextureMap(package.normalMapUri, "Normal map");
    validateTextureMap(package.ormMapUri, "ORM map");
    validateTextureMap(package.emissiveMapUri, "Emissive map");

    // 6. Stadio del ciclo di vita
    if (package.manifest.stage < RawAssetStage::Validated) {
        report.AddError("Stadio manifest insufficiente: l'asset deve essere almeno Validated o Packaged.");
    }

    // 7. Verifica Integrità Crittografica SHA-256 (BM-F4.2)
    if (!package.contentDigestSha256.empty()) {
        if (!IsValidSha256HexFormat(package.contentDigestSha256)) {
            report.AddError("Formato digest SHA-256 non valido (attesi esattamente 64 caratteri esadecimali minuscoli): " +
                            package.contentDigestSha256, AssetIntegrityErrorCode::InvalidDigestFormat);
        } else {
            std::string computedDigest = package.ComputeContentDigestSha256(effectiveRoot);
            if (package.contentDigestSha256 != computedDigest) {
                report.AddError("Violazione di integrita' crittografica (contentDigestSha256 mismatch): atteso " +
                                package.contentDigestSha256.substr(0, 16) +
                                "..., calcolato " + computedDigest.substr(0, 16) + "... Asset o texture alterati.",
                                AssetIntegrityErrorCode::DigestMismatch);
            }
        }
    }

    // 8. Validazione Dipendenze Dichiarate & Integrita' Referenziale (BM-F4.4)
    auto depReport = AssetDependencyGraph::ValidatePackageDependencies(package, effectiveDepRoot, *this, effectivePolicy);
    if (!depReport.isValid) {
        for (const auto& depErr : depReport.errors) {
            report.AddError(depErr, depReport.primaryErrorCode);
        }
    }

    // Determinazione dello status di integrità complessivo
    if (!report.isValid) {
        report.integrityStatus = AssetIntegrityStatus::Rejected;
    } else {
        if (package.manifest.schemaVersion == 1 && package.contentDigestSha256.empty()) {
            report.integrityStatus = AssetIntegrityStatus::LegacyUnverified;
            if (report.primaryErrorCode == AssetIntegrityErrorCode::None) {
                report.primaryErrorCode = AssetIntegrityErrorCode::LegacyUnverified;
            }
        } else {
            report.integrityStatus = AssetIntegrityStatus::Verified;
            report.primaryErrorCode = AssetIntegrityErrorCode::None;
        }
    }

    return report;
}

AssetPublishTransactionResult AssetPackagingService::PublishPackageTransactional(
    RawAssetPackage& package,
    BlockRegistry& bReg,
    MaterialRegistry& mReg,
    ShapeRegistry& sReg,
    const std::string& rootDir,
    std::optional<AssetTrustPolicy> policy
) {
    AssetPublishTransactionResult result;
    const std::string effectiveRoot = rootDir.empty() ? m_rootAssetDirectory : rootDir;
    const AssetTrustPolicy effectivePolicy = policy.value_or(m_defaultTrustPolicy);

    // FASE 1: PRE-VALIDAZIONE RIGOROSA & VERIFICA INTEGRITA CRITTOGRAFICA (VERIFY BEFORE COMMIT)
    auto valReport = ValidatePackage(package, effectiveRoot, effectivePolicy);
    if (!valReport.isValid) {
        result.success = false;
        result.errorMessage = valReport.errors.empty() ? "Validazione package fallita." : valReport.errors[0];
        result.integrityStatus = valReport.integrityStatus;
        result.errorCode = valReport.primaryErrorCode;
        result.wasRolledBack = false;
        return result;
    }

    // FASE 2: VERIFICA COLLISIONI STRING ID E SLOT OWNERSHIP (BM-F4)
    uint8_t existingId = bReg.GetBlockIdByString(package.manifest.assetId);
    uint8_t targetVoxelId = package.manifest.targetRuntimeVoxelId;

    if (existingId != 0 && targetVoxelId != 0 && existingId != targetVoxelId) {
        result.success = false;
        result.errorMessage = "Collisione ID: l'asset '" + package.manifest.assetId +
                              "' e' gia' registrato allo slot ID " + std::to_string(existingId) +
                              ", diverso dal target richiesto " + std::to_string(targetVoxelId);
        result.integrityStatus = AssetIntegrityStatus::Rejected;
        result.errorCode = AssetIntegrityErrorCode::None;
        result.wasRolledBack = false;
        return result;
    }

    // Invariante di identità e ownership: uno slot runtime già assegnato non può essere sovrascritto da un asset diverso
    if (targetVoxelId != 0 && bReg.IsBlockValid(targetVoxelId)) {
        const std::string& occupantId = bReg.GetBlock(targetVoxelId).stringId;
        if (!occupantId.empty() && occupantId != package.manifest.assetId) {
            result.success = false;
            result.errorMessage = "Collisione ID: lo slot runtime ID " + std::to_string(targetVoxelId) +
                                  " e' gia' assegnato all'asset '" + occupantId +
                                  "', impossibile assegnarlo a '" + package.manifest.assetId + "'";
            result.integrityStatus = AssetIntegrityStatus::Rejected;
            result.errorCode = AssetIntegrityErrorCode::None;
            result.wasRolledBack = false;
            return result;
        }
    }

    // FASE 3: VERIFICA ZERO-WORK SICURA (BM-F4.2 ZERO-WORK SAFETY)
    // Richiede identità di packageFingerprint, digest SHA-256 e contesto di fiducia
    auto recIt = m_lastPublishedRecords.find(package.manifest.assetId);
    if (recIt != m_lastPublishedRecords.end() &&
        recIt->second.packageFingerprint == package.packageFingerprint &&
        recIt->second.integrityStatus == valReport.integrityStatus &&
        recIt->second.trustPolicy == effectivePolicy &&
        package.packageFingerprint != 0) {
        
        bool digestMatches = (recIt->second.contentDigestSha256 == package.contentDigestSha256);
        if (!digestMatches && package.manifest.stage != RawAssetStage::Published) {
            RawAssetPackage simulated = package;
            simulated.manifest.stage = RawAssetStage::Published;
            simulated.manifest.targetRuntimeVoxelId = recIt->second.targetVoxelId;
            if (simulated.ComputeContentDigestSha256(effectiveRoot) == recIt->second.contentDigestSha256) {
                digestMatches = true;
            }
        }

        if (digestMatches) {
            if (existingId != 0 && bReg.IsBlockValid(existingId) && bReg.GetBlock(existingId).stringId == package.manifest.assetId) {
                result.success = true;
                result.targetVoxelId = existingId;
                result.assignedShape = mReg.GetMaterial(existingId).sharedShape;
                result.integrityStatus = valReport.integrityStatus;
                result.errorCode = AssetIntegrityErrorCode::None;
                result.wasZeroWorkHit = true;
                result.wasRolledBack = false;
                return result;
            }
        }
    }

    // FASE 3b: FAULT INJECTION CONTROLLATA (PRIMA DELLE MUTAZIONI)
    if (m_testFaultInjection == PublishFaultInjectionPoint::FailBeforeMutations) {
        result.success = false;
        result.errorMessage = "Fault injection: fallimento prima delle mutazioni.";
        result.integrityStatus = AssetIntegrityStatus::Rejected;
        result.errorCode = AssetIntegrityErrorCode::None;
        result.wasRolledBack = false;
        return result;
    }

    // FASE 4: STAGING & SNAPSHOTTING PER TRANSAZIONALITA (ROLLBACK SICURO)
    if (targetVoxelId == 0 && existingId != 0) {
        targetVoxelId = existingId;
    }

    bool wasNewBlockAllocated = false;
    bool wasShapeRegistered = false;
    ShapeHandle assignedShape;
    std::optional<SimBlockDef> oldBlockSnapshot;
    std::optional<PBRMaterialDef> oldMaterialSnapshot;

    if (targetVoxelId == 0 || !bReg.IsBlockValid(targetVoxelId)) {
        auto allocRes = bReg.CreateBlock(package.manifest.assetId, package.manifest.displayName);
        if (!allocRes.success) {
            result.success = false;
            result.errorMessage = "Allocazione slot registro fallita: " + allocRes.message;
            result.integrityStatus = AssetIntegrityStatus::Rejected;
            result.errorCode = AssetIntegrityErrorCode::None;
            result.wasRolledBack = false;
            return result;
        }
        targetVoxelId = allocRes.allocatedId;
        wasNewBlockAllocated = true;
    } else {
        oldBlockSnapshot = bReg.GetBlock(targetVoxelId);
        oldMaterialSnapshot = mReg.GetMaterial(targetVoxelId);
    }

    // Helper lambda per rollback atomico in caso di errore (BM-F2C.1 / BM-F4)
    // Ordine inverso delle mutazioni: 1. MaterialRegistry -> 2. BlockRegistry -> 3. ShapeRegistry
    auto performRollback = [&](const std::string& errorMsg) -> AssetPublishTransactionResult {
        // 1. Rollback MaterialRegistry
        if (wasNewBlockAllocated) {
            mReg.ResetMaterial(targetVoxelId);
        } else {
            if (oldMaterialSnapshot.has_value()) {
                mReg.UpdateMaterial(targetVoxelId, *oldMaterialSnapshot);
            }
        }

        // 2. Rollback BlockRegistry
        if (wasNewBlockAllocated) {
            bReg.DeleteBlock(targetVoxelId);
        } else {
            if (oldBlockSnapshot.has_value()) {
                bReg.UpdateBlock(targetVoxelId, *oldBlockSnapshot);
            }
        }

        // 3. Rollback ShapeRegistry (soltanto se registrata in questa transazione e non piu' referenziata)
        if (wasShapeRegistered && assignedShape.IsValid()) {
            if (mReg.CountShapeUsers(assignedShape.id) == 0) {
                sReg.UnregisterShape(assignedShape);
            }
        }

        AssetPublishTransactionResult failRes;
        failRes.success = false;
        failRes.errorMessage = errorMsg;
        failRes.targetVoxelId = 0;
        failRes.integrityStatus = AssetIntegrityStatus::Rejected;
        failRes.errorCode = AssetIntegrityErrorCode::None;
        failRes.wasRolledBack = true;
        return failRes;
    };

    // FASE 5: MUTAZIONE STAGED NEI REGISTRI
    // 5.1 Registrazione Geometria in ShapeRegistry
    assignedShape = sReg.RegisterShape(package.shape);
    if (!assignedShape.IsValid()) {
        return performRollback("Registrazione Shape fallita in ShapeRegistry.");
    }
    wasShapeRegistered = true;

    if (m_testFaultInjection == PublishFaultInjectionPoint::FailAfterShapeRegister) {
        return performRollback("Fault injection: fallimento dopo registrazione Shape.");
    }

    // 5.2 Sincronizzazione Proprietà Fisiche e Trasformazione in BlockRegistry
    if (!wasNewBlockAllocated && bReg.IsBlockValid(targetVoxelId)) {
        const std::string& currentOwner = bReg.GetBlock(targetVoxelId).stringId;
        if (!currentOwner.empty() && currentOwner != package.manifest.assetId) {
            return performRollback("Violazione invariante di ownership: lo slot " + std::to_string(targetVoxelId) +
                                   " appartiene a '" + currentOwner + "' anziche' a '" + package.manifest.assetId + "'");
        }
    }
    SimBlockDef& bDef = bReg.GetBlockMutable(targetVoxelId);
    bDef.stringId = package.manifest.assetId;
    bDef.displayName = package.manifest.displayName;
    bDef.physics = package.compiledPhysics;
    bDef.localPosition = package.packagePosition;
    bDef.localRotation = package.packageRotation;
    bDef.localScale = package.packageScale;
    bDef.pivotOffset = package.packagePivot;
    bDef.SyncLegacyPhysicsFields();

    if (m_testFaultInjection == PublishFaultInjectionPoint::FailAfterBlockMutation) {
        return performRollback("Fault injection: fallimento dopo mutazione Block.");
    }

    // 5.3 Sincronizzazione Proprietà Materiale PBR in MaterialRegistry
    PBRMaterialDef& matDef = mReg.GetMaterialMutable(targetVoxelId);
    matDef.target_block_id = targetVoxelId;
    matDef.sharedShape = assignedShape;
    matDef.baseColorFallback = package.baseColorFallback;
    matDef.roughnessFallback = package.roughnessFallback;
    matDef.metallicFallback = package.metallicFallback;
    matDef.emissiveStrength = package.emissiveStrength;
    matDef.alphaFallback = package.alphaFallback;
    matDef.albedoPath = package.albedoMapUri;
    matDef.normalPath = package.normalMapUri;
    matDef.ormPath = package.ormMapUri;
    matDef.behaviors = package.behaviors;
    matDef.revision++;

    if (m_testFaultInjection == PublishFaultInjectionPoint::FailAfterMaterialMutation) {
        return performRollback("Fault injection: fallimento dopo mutazione Material.");
    }

    // FASE 6: AUDIT DI INTEGRITA POST-MUTAZIONE
    if (m_testFaultInjection == PublishFaultInjectionPoint::FailDuringPostAudit) {
        return performRollback("Fault injection: fallimento durante audit post-mutazione.");
    }

    bool bOk = (bReg.GetBlock(targetVoxelId).stringId == package.manifest.assetId);
    bool mOk = (mReg.GetMaterial(targetVoxelId).sharedShape == assignedShape) &&
               (mReg.GetMaterial(targetVoxelId).target_block_id == targetVoxelId);

    if (!bOk || !mOk) {
        return performRollback("Audit post-mutazione fallito: incongruenza registri runtime.");
    }

    // FASE 7: COMMIT TRANSAZIONE
    package.manifest.stage = RawAssetStage::Published;
    package.manifest.targetRuntimeVoxelId = targetVoxelId;
    if (!package.contentDigestSha256.empty()) {
        package.contentDigestSha256 = package.ComputeContentDigestSha256(effectiveRoot);
    }
    m_lastPublishedFingerprints[package.manifest.assetId] = package.packageFingerprint;
    m_lastPublishedRecords[package.manifest.assetId] = PublishedAssetRecord{
        package.packageFingerprint,
        package.contentDigestSha256,
        valReport.integrityStatus,
        effectivePolicy,
        targetVoxelId
    };

    result.success = true;
    result.targetVoxelId = targetVoxelId;
    result.assignedShape = assignedShape;
    result.integrityStatus = valReport.integrityStatus;
    result.errorCode = AssetIntegrityErrorCode::None;
    result.wasZeroWorkHit = false;
    result.wasRolledBack = false;

    return result;
}

bool AssetPackagingService::SavePackageToFile(const RawAssetPackage& package, const std::string& filepath) const {
    return package.SaveToFile(filepath);
}

bool AssetPackagingService::LoadPackageFromFile(const std::string& filepath, RawAssetPackage& outPackage) const {
    return outPackage.LoadFromFile(filepath);
}

bool AssetPackagingService::LoadAndValidatePackageFromFile(
    const std::string& filepath,
    RawAssetPackage& outPackage,
    RawAssetValidation& outValidation,
    const std::string& rootDir,
    std::optional<AssetTrustPolicy> policy
) const {
    if (!outPackage.LoadFromFile(filepath)) {
        outValidation.Reset();
        outValidation.AddError("Impossibile leggere il file del package da disco: " + filepath, AssetIntegrityErrorCode::FileReadFailure);
        return false;
    }
    outValidation = ValidatePackage(outPackage, rootDir, policy);
    return outValidation.isValid;
}

uint64_t AssetPackagingService::GetLastPublishedFingerprint(const std::string& assetId) const {
    auto it = m_lastPublishedFingerprints.find(assetId);
    return (it != m_lastPublishedFingerprints.end()) ? it->second : 0;
}

std::optional<AssetPackagingService::PublishedAssetRecord> AssetPackagingService::GetLastPublishedRecord(const std::string& assetId) const {
    auto it = m_lastPublishedRecords.find(assetId);
    if (it != m_lastPublishedRecords.end()) {
        return it->second;
    }
    return std::nullopt;
}

void AssetPackagingService::InvalidatePublicationCache() {
    m_lastPublishedFingerprints.clear();
    m_lastPublishedRecords.clear();
    m_textureValidationCache.clear();
}

void AssetPackagingService::InvalidateTextureValidationCache() {
    m_textureValidationCache.clear();
}

// =============================================================================
// BM-F4.3: IMPLEMENTAZIONE TRANSAZIONI PERSISTENTI E CRASH RECOVERY
// =============================================================================

inline void to_json(nlohmann::json& j, const AssetTransactionJournal& tj) {
    j = nlohmann::json{
        {"transactionId", tj.transactionId},
        {"assetId", tj.assetId},
        {"revision", tj.revision},
        {"state", static_cast<uint8_t>(tj.state)},
        {"stagingDirectory", tj.stagingDirectory},
        {"targetDirectory", tj.targetDirectory},
        {"packageFilename", tj.packageFilename},
        {"stagedFiles", tj.stagedFiles},
        {"contentDigestSha256", tj.contentDigestSha256},
        {"timestamp", tj.timestamp}
    };
}

inline void from_json(const nlohmann::json& j, AssetTransactionJournal& tj) {
    tj.transactionId = j.value("transactionId", "");
    tj.assetId = j.value("assetId", "");
    tj.revision = j.value("revision", static_cast<uint64_t>(0));
    tj.state = static_cast<AssetTransactionState>(j.value("state", static_cast<uint8_t>(0)));
    tj.stagingDirectory = j.value("stagingDirectory", "");
    tj.targetDirectory = j.value("targetDirectory", "");
    tj.packageFilename = j.value("packageFilename", "");
    if (j.contains("stagedFiles") && j["stagedFiles"].is_array()) {
        tj.stagedFiles = j["stagedFiles"].get<std::vector<std::string>>();
    }
    tj.contentDigestSha256 = j.value("contentDigestSha256", "");
    tj.timestamp = j.value("timestamp", static_cast<uint64_t>(0));
}

bool AssetTransactionJournal::SaveToFile(const std::string& filepath) const {
    try {
        nlohmann::json j;
        to_json(j, *this);
        std::string jsonStr = j.dump(4);
        std::string err;
        return AssetFilesystemAdapter::DurableWriteFile(filepath, jsonStr, false, &err);
    } catch (...) {
        return false;
    }
}

bool AssetTransactionJournal::LoadFromFile(const std::string& filepath) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;
        nlohmann::json j;
        file >> j;
        from_json(j, *this);
        return true;
    } catch (...) {
        return false;
    }
}

std::string AssetPackagingService::SanitizeAssetIdForPath(const std::string& assetId) {
    std::string s = assetId;
    for (char& c : s) {
        if (c == ':' || c == '/' || c == '\\' || c == '?' || c == '*' || c == '<' || c == '>' || c == '|' || c == '"') {
            c = '_';
        }
    }
    return s;
}

TransactionID AssetPackagingService::GenerateTransactionId(const std::string& assetId, uint64_t revision) {
    static uint64_t s_txCounter = 0;
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return "tx_" + SanitizeAssetIdForPath(assetId) + "_r" + std::to_string(revision) + "_" + std::to_string(now) + "_" + std::to_string(++s_txCounter);
}

std::string AssetPackagingService::GetPublishedPackagePath(const std::string& assetId, uint64_t revision, const std::string& rootDir) const {
    const std::string effectiveRoot = rootDir.empty() ? m_rootAssetDirectory : rootDir;
    std::string sanitized = SanitizeAssetIdForPath(assetId);
    if (revision == 0) {
        return (std::filesystem::path(effectiveRoot) / "published" / sanitized / "active.rawpkg").string();
    }
    return (std::filesystem::path(effectiveRoot) / "published" / sanitized / ("rev_" + std::to_string(revision)) / "package.rawpkg").string();
}

std::string AssetPackagingService::GetActivePublishedPackagePath(const std::string& assetId, const std::string& rootDir) const {
    return GetPublishedPackagePath(assetId, 0, rootDir);
}

bool AssetPackagingService::IsValidTransactionId(const TransactionID& txId) {
    if (txId.rfind("tx_", 0) != 0 || txId.size() < 6) return false;
    for (char c : txId) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

bool AssetPackagingService::IsValidAssetId(const std::string& assetId) {
    if (assetId.empty()) return false;
    if (assetId.find("..") != std::string::npos) return false;
    if (assetId.find('/') != std::string::npos || assetId.find('\\') != std::string::npos) return false;
    for (char c : assetId) {
        if (static_cast<unsigned char>(c) < 32 || c == '|' || c == '<' || c == '>' || c == '"' || c == '?' || c == '*') {
            return false;
        }
    }
    return true;
}

bool AssetPackagingService::IsRevisionCommitted(
    const std::string& assetId,
    uint64_t revision,
    const std::string& rootDir,
    std::string* outDigest
) const {
    const std::string effectiveRoot = rootDir.empty() ? m_rootAssetDirectory : rootDir;
    std::string sanitized = SanitizeAssetIdForPath(assetId);
    std::filesystem::path pubRevDir = std::filesystem::path(effectiveRoot) / "published" / sanitized / ("rev_" + std::to_string(revision));
    std::filesystem::path journalPath = pubRevDir / "commit.journal";
    std::filesystem::path pkgPath = pubRevDir / "package.rawpkg";

    std::error_code ec;
    if (!std::filesystem::exists(journalPath, ec) || !std::filesystem::exists(pkgPath, ec)) {
        return false;
    }

    AssetTransactionJournal tj;
    if (!tj.LoadFromFile(journalPath.string())) {
        return false;
    }

    if (tj.state != AssetTransactionState::Committed) {
        return false;
    }

    RawAssetPackage pkg;
    if (!pkg.LoadFromFile(pkgPath.string())) {
        return false;
    }

    if (pkg.manifest.revision != revision) {
        return false;
    }

    if (pkg.contentDigestSha256 != tj.contentDigestSha256 || pkg.contentDigestSha256.empty()) {
        return false;
    }

    if (outDigest) {
        *outDigest = tj.contentDigestSha256;
    }
    return true;
}

std::optional<uint64_t> AssetPackagingService::GetActiveRevision(
    const std::string& assetId,
    const std::string& rootDir
) const {
    const std::string effectiveRoot = rootDir.empty() ? m_rootAssetDirectory : rootDir;
    std::string activePkgPath = GetActivePublishedPackagePath(assetId, effectiveRoot);
    std::error_code ec;
    if (!std::filesystem::exists(activePkgPath, ec)) {
        return std::nullopt;
    }

    RawAssetPackage activePkg;
    if (!activePkg.LoadFromFile(activePkgPath)) {
        return std::nullopt;
    }

    // Regola cardine BM-F4.3.1: Commit Authority
    // L'active package è valido SE E SOLO SE la revisione dichiarata è committed con journal valido!
    if (!IsRevisionCommitted(assetId, activePkg.manifest.revision, effectiveRoot)) {
        return std::nullopt; // Divergenza: active.rawpkg non è supportato da commit.journal valido
    }

    return activePkg.manifest.revision;
}

std::optional<uint64_t> AssetPackagingService::GetHighestCommittedRevision(
    const std::string& assetId,
    const std::string& rootDir
) const {
    const std::string effectiveRoot = rootDir.empty() ? m_rootAssetDirectory : rootDir;
    std::string sanitized = SanitizeAssetIdForPath(assetId);
    std::filesystem::path pubAssetDir = std::filesystem::path(effectiveRoot) / "published" / sanitized;

    std::error_code ec;
    if (!std::filesystem::exists(pubAssetDir, ec)) {
        return std::nullopt;
    }

    std::optional<uint64_t> highest;
    for (const auto& entry : std::filesystem::directory_iterator(pubAssetDir, ec)) {
        if (entry.is_directory(ec)) {
            std::string name = entry.path().filename().string();
            if (name.rfind("rev_", 0) == 0) {
                try {
                    uint64_t rev = std::stoull(name.substr(4));
                    if (IsRevisionCommitted(assetId, rev, effectiveRoot)) {
                        if (!highest.has_value() || rev > *highest) {
                            highest = rev;
                        }
                    }
                } catch (...) {}
            }
        }
    }
    return highest;
}

bool AssetPackagingService::ReconcileActivePointer(
    const std::string& assetId,
    const std::string& rootDir,
    std::string* outMessage
) {
    const std::string effectiveRoot = rootDir.empty() ? m_rootAssetDirectory : rootDir;
    auto highestRev = GetHighestCommittedRevision(assetId, effectiveRoot);
    if (!highestRev.has_value()) {
        if (outMessage) *outMessage = "Nessuna revisione committed trovata per riconciliare: " + assetId;
        return false;
    }

    std::string sanitized = SanitizeAssetIdForPath(assetId);
    std::filesystem::path pubAssetDir = std::filesystem::path(effectiveRoot) / "published" / sanitized;
    std::filesystem::path revDir = pubAssetDir / ("rev_" + std::to_string(*highestRev));
    std::filesystem::path revPkgFile = revDir / "package.rawpkg";
    std::filesystem::path revJournalFile = revDir / "commit.journal";

    RawAssetPackage revPkg;
    if (!revPkg.LoadFromFile(revPkgFile.string())) {
        if (outMessage) *outMessage = "Impossibile caricare package da revisione: " + revPkgFile.string();
        return false;
    }

    AssetTransactionJournal tj;
    if (!tj.LoadFromFile(revJournalFile.string())) {
        if (outMessage) *outMessage = "Impossibile caricare commit.journal da revisione: " + revJournalFile.string();
        return false;
    }

    // Aggiornamento atomico del puntatore active
    std::string activePath = (pubAssetDir / "active.rawpkg").string();
    std::string journalPath = (pubAssetDir / "active_journal.json").string();

    std::string err;
    if (!revPkg.SaveToFile(activePath)) {
        if (outMessage) *outMessage = "Salvataggio atomico active.rawpkg fallito per: " + assetId;
        return false;
    }

    tj.SaveToFile(journalPath);

    if (outMessage) *outMessage = "Puntatore active riconciliato con successo alla rev_" + std::to_string(*highestRev);
    return true;
}

void AssetPackagingService::RebuildDependencyGraph(const std::string& rootDir) {
    const std::string effectiveRoot = rootDir.empty() ? m_rootAssetDirectory : rootDir;
    m_dependencyGraph = AssetDependencyGraph::BuildFromCommitted(effectiveRoot, *this);
}

AssetPublishTransactionResult AssetPackagingService::PublishPackagePersistent(
    RawAssetPackage& package,
    BlockRegistry& bReg,
    MaterialRegistry& mReg,
    ShapeRegistry& sReg,
    const std::string& rootDir,
    std::optional<AssetTrustPolicy> policy
) {
    AssetPublishTransactionResult result;
    const std::string effectiveRoot = rootDir.empty() ? m_rootAssetDirectory : rootDir;
    const AssetTrustPolicy effectivePolicy = policy.value_or(m_defaultTrustPolicy);

    // 1. Pre-validazione logica del package (Verify Before Commit)
    auto valReport = ValidatePackage(package, effectiveRoot, effectivePolicy);
    if (!valReport.isValid) {
        result.success = false;
        result.errorMessage = valReport.errors.empty() ? "Validazione package fallita." : valReport.errors[0];
        result.integrityStatus = valReport.integrityStatus;
        result.errorCode = valReport.primaryErrorCode;
        result.wasRolledBack = false;
        return result;
    }

    // 2. Controllo Zero-Work su package già committed e identico su disco
    std::string activePkgPath = GetActivePublishedPackagePath(package.manifest.assetId, effectiveRoot);
    std::error_code ec;
    if (std::filesystem::exists(activePkgPath, ec)) {
        RawAssetPackage existingCommitted;
        if (existingCommitted.LoadFromFile(activePkgPath)) {
            bool digestMatches = (existingCommitted.contentDigestSha256 == package.contentDigestSha256);
            if (!digestMatches && package.manifest.stage != RawAssetStage::Published) {
                RawAssetPackage simulated = package;
                simulated.manifest.stage = RawAssetStage::Published;
                uint8_t existingId = bReg.GetBlockIdByString(package.manifest.assetId);
                simulated.manifest.targetRuntimeVoxelId = existingId;
                if (simulated.ComputeContentDigestSha256(effectiveRoot) == existingCommitted.contentDigestSha256) {
                    digestMatches = true;
                }
            }

            if (existingCommitted.manifest.revision == package.manifest.revision &&
                existingCommitted.packageFingerprint == package.packageFingerprint &&
                digestMatches &&
                package.packageFingerprint != 0 &&
                existingCommitted.ComputeContentDigestSha256(effectiveRoot) == existingCommitted.contentDigestSha256) {
                uint8_t existingId = bReg.GetBlockIdByString(package.manifest.assetId);
                if (existingId != 0 && bReg.IsBlockValid(existingId)) {
                    result.success = true;
                    result.targetVoxelId = existingId;
                    result.assignedShape = mReg.GetMaterial(existingId).sharedShape;
                    result.integrityStatus = valReport.integrityStatus;
                    result.errorCode = AssetIntegrityErrorCode::None;
                    result.wasZeroWorkHit = true;
                    result.wasRolledBack = false;
                    return result;
                }
            }
        }
    }

    // 3. Allocazione Transazione e Staging Directory (FASE 2 / FASE 3)
    TransactionID txId = GenerateTransactionId(package.manifest.assetId, package.manifest.revision);
    result.transactionId = txId;
    std::filesystem::path stagingDir = std::filesystem::path(effectiveRoot) / ".staging" / txId;
    std::filesystem::create_directories(stagingDir, ec);

    std::string sanitizedId = SanitizeAssetIdForPath(package.manifest.assetId);
    std::filesystem::path pubAssetDir = std::filesystem::path(effectiveRoot) / "published" / sanitizedId;
    std::filesystem::path pubRevDir = pubAssetDir / ("rev_" + std::to_string(package.manifest.revision));

    AssetTransactionJournal journal;
    journal.transactionId = txId;
    journal.assetId = package.manifest.assetId;
    journal.revision = package.manifest.revision;
    journal.state = AssetTransactionState::Prepared;
    journal.stagingDirectory = stagingDir.string();
    journal.targetDirectory = pubRevDir.string();
    journal.packageFilename = "package.rawpkg";
    journal.contentDigestSha256 = package.contentDigestSha256;
    journal.timestamp = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    journal.SaveToFile((stagingDir / "tx_journal.json").string());

    // Fault injection durante staging (CRH-02)
    if (m_transactionFault == TransactionFaultPoint::FailDuringStaging) {
        journal.state = AssetTransactionState::Aborted;
        journal.SaveToFile((stagingDir / "tx_journal.json").string());
        std::filesystem::remove_all(stagingDir, ec);
        result.success = false;
        result.errorMessage = "Fault injection: fallimento simulato durante staging (CRH-02).";
        result.integrityStatus = AssetIntegrityStatus::Rejected;
        result.errorCode = AssetIntegrityErrorCode::None;
        result.wasRolledBack = false;
        return result;
    }

    // 4. Scrittura file nella staging directory (FASE 3 — STAGING)
    std::string stagedPkgFile = (stagingDir / "package.rawpkg").string();
    if (!package.SaveToFile(stagedPkgFile)) {
        journal.state = AssetTransactionState::Aborted;
        journal.SaveToFile((stagingDir / "tx_journal.json").string());
        std::filesystem::remove_all(stagingDir, ec);
        result.success = false;
        result.errorMessage = "Scrittura package in staging fallita: " + stagedPkgFile;
        result.integrityStatus = AssetIntegrityStatus::Rejected;
        result.errorCode = AssetIntegrityErrorCode::FileReadFailure;
        return result;
    }
    journal.stagedFiles.push_back("package.rawpkg");

    // Stage texture se presenti
    auto stageTexture = [&](const std::string& uri) {
        if (uri.empty()) return;
        std::filesystem::path src = std::filesystem::path(effectiveRoot) / uri;
        if (std::filesystem::exists(src, ec)) {
            std::filesystem::path dst = stagingDir / uri;
            std::filesystem::create_directories(dst.parent_path(), ec);
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
            journal.stagedFiles.push_back(uri);
        }
    };
    stageTexture(package.albedoMapUri);
    stageTexture(package.normalMapUri);
    stageTexture(package.ormMapUri);
    stageTexture(package.emissiveMapUri);

    journal.state = AssetTransactionState::FilesStaged;
    journal.SaveToFile((stagingDir / "tx_journal.json").string());

    // 5. Verifica dei file nello staging (FASE 4)
    RawAssetPackage stagedPkg;
    if (!stagedPkg.LoadFromFile(stagedPkgFile)) {
        journal.state = AssetTransactionState::Aborted;
        journal.SaveToFile((stagingDir / "tx_journal.json").string());
        std::filesystem::remove_all(stagingDir, ec);
        result.success = false;
        result.errorMessage = "Rilettura e verifica package in staging fallita.";
        result.integrityStatus = AssetIntegrityStatus::Rejected;
        return result;
    }

    auto stagedVal = ValidatePackage(stagedPkg, stagingDir.string(), effectivePolicy, effectiveRoot);
    if (!stagedVal.isValid) {
        journal.state = AssetTransactionState::Aborted;
        journal.SaveToFile((stagingDir / "tx_journal.json").string());
        std::filesystem::remove_all(stagingDir, ec);
        result.success = false;
        result.errorMessage = stagedVal.errors.empty() ? "Validazione package in staging fallita." : stagedVal.errors[0];
        result.integrityStatus = stagedVal.integrityStatus;
        result.errorCode = stagedVal.primaryErrorCode;
        return result;
    }

    journal.state = AssetTransactionState::Verified;
    journal.SaveToFile((stagingDir / "tx_journal.json").string());

    // Fault injection prima del commit (CRH-03)
    if (m_transactionFault == TransactionFaultPoint::FailBeforeCommit) {
        // Lascia lo staging in stato uncommitted / non pubblicato
        result.success = false;
        result.errorMessage = "Fault injection: fallimento simulato prima del commit durevole (CRH-03).";
        result.integrityStatus = AssetIntegrityStatus::Rejected;
        result.errorCode = AssetIntegrityErrorCode::None;
        result.wasRolledBack = false;
        return result;
    }

    // 6. COMMIT DUREVOLE (FASE 4 — ATOMIC PUBLICATION)
    std::filesystem::create_directories(pubRevDir, ec);
    std::filesystem::copy_file(stagedPkgFile, pubRevDir / "package.rawpkg", std::filesystem::copy_options::overwrite_existing, ec);

    // Copia texture nella directory attiva solo se non già presenti o per preservare immutabilità
    auto copyCommittedTexture = [&](const std::string& uri) {
        if (uri.empty()) return;
        std::filesystem::path dst = std::filesystem::path(effectiveRoot) / uri;
        if (!std::filesystem::exists(dst, ec)) {
            std::filesystem::path src = stagingDir / uri;
            if (std::filesystem::exists(src, ec)) {
                std::filesystem::create_directories(dst.parent_path(), ec);
                std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
            }
        }
    };
    copyCommittedTexture(package.albedoMapUri);
    copyCommittedTexture(package.normalMapUri);
    copyCommittedTexture(package.ormMapUri);
    copyCommittedTexture(package.emissiveMapUri);

    // Scrittura del commit journal nella directory della revisione
    journal.state = AssetTransactionState::Committed;
    journal.timestamp = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    journal.SaveToFile((pubRevDir / "commit.journal").string());

    // Fault injection tra commit journal e active pointer (CRH-H01)
    if (m_transactionFault == TransactionFaultPoint::FailBetweenCommitAndActivePointer) {
        result.success = false;
        result.errorMessage = "Fault injection: crash simulato tra commit.journal e active pointer (CRH-H01).";
        result.integrityStatus = AssetIntegrityStatus::Verified;
        result.errorCode = AssetIntegrityErrorCode::None;
        result.wasRolledBack = false;
        return result;
    }

    // Fault injection durante replace di active pointer (CRH-H04)
    if (m_transactionFault == TransactionFaultPoint::FailDuringActivePointerReplace) {
        result.success = false;
        result.errorMessage = "Fault injection: errore simulato durante replace active pointer (CRH-H04).";
        result.integrityStatus = AssetIntegrityStatus::Verified;
        result.errorCode = AssetIntegrityErrorCode::None;
        result.wasRolledBack = false;
        return result;
    }

    // Pubblicazione atomica dell'attivo: active.rawpkg via file temporaneo + rename
    std::string activePathStr = activePkgPath;
    std::filesystem::create_directories(pubAssetDir, ec);
    package.SaveToFile(activePathStr);
    journal.SaveToFile((pubAssetDir / "active_journal.json").string());

    // Fault injection subito dopo il commit durevole (CRH-04)
    if (m_transactionFault == TransactionFaultPoint::FailImmediatelyAfterCommit) {
        result.success = false;
        result.errorMessage = "Fault injection: crash simulato subito dopo commit durevole (CRH-04).";
        result.integrityStatus = AssetIntegrityStatus::Verified;
        result.errorCode = AssetIntegrityErrorCode::None;
        result.wasRolledBack = false;
        return result;
    }

    // 7. Sincronizzazione atomica nei registri runtime in-memory
    auto regRes = PublishPackageTransactional(package, bReg, mReg, sReg, effectiveRoot, effectivePolicy);
    if (!regRes.success) {
        result.success = false;
        result.errorMessage = "Errore durante sincronizzazione registri: " + regRes.errorMessage;
        result.wasRolledBack = true;
        return result;
    }

    // Sincronizzazione durevole dei file di pubblicazione con lo stato finale committed e pubblicato
    package.SaveToFile(activePathStr);
    package.SaveToFile((pubRevDir / "package.rawpkg").string());
    journal.contentDigestSha256 = package.contentDigestSha256;
    journal.SaveToFile((pubRevDir / "commit.journal").string());
    journal.SaveToFile((pubAssetDir / "active_journal.json").string());

    // 8. Pulizia staging completata con successo (SafeRemoveAll previene junction traversal)
    AssetFilesystemAdapter::SafeRemoveAll(stagingDir, effectiveRoot);

    // 9. Aggiornamento autoritativo del grafo delle dipendenze e invalidazione mirata Zero-Work (BM-F4.4)
    m_dependencyGraph.AddNode(
        package.manifest.assetId,
        package.manifest.revision,
        package.contentDigestSha256,
        package.manifest.dependencies,
        true,
        true,
        effectivePolicy,
        valReport.integrityStatus
    );

    // Invalida la cache Zero-Work solo per gli asset che dipendono da questo package (non globale)
    auto dependents = m_dependencyGraph.GetDependents(package.manifest.assetId, true);
    for (const auto& depId : dependents) {
        m_lastPublishedFingerprints.erase(depId);
        m_lastPublishedRecords.erase(depId);
    }

    result.success = true;
    result.targetVoxelId = regRes.targetVoxelId;
    result.assignedShape = regRes.assignedShape;
    result.integrityStatus = valReport.integrityStatus;
    result.errorCode = AssetIntegrityErrorCode::None;
    result.wasZeroWorkHit = false;
    result.wasRolledBack = false;

    return result;
}

CrashRecoveryReport AssetPackagingService::RunCrashRecovery(
    BlockRegistry* bReg,
    MaterialRegistry* mReg,
    ShapeRegistry* sReg,
    const std::string& rootDir,
    std::optional<AssetTrustPolicy> policy
) {
    CrashRecoveryReport report;
    report.success = true;
    const std::string effectiveRoot = rootDir.empty() ? m_rootAssetDirectory : rootDir;
    const AssetTrustPolicy effectivePolicy = policy.value_or(m_defaultTrustPolicy);
    std::error_code ec;

    // 1. Individuazione e scarto transazioni incomplete in .staging/
    std::filesystem::path stagingRoot = std::filesystem::path(effectiveRoot) / ".staging";
    if (std::filesystem::exists(stagingRoot, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(stagingRoot, ec)) {
            // Verifica di sicurezza: il percorso deve risiedere sotto effectiveRoot
            if (!AssetFilesystemAdapter::IsSafePathUnderRoot(entry.path(), effectiveRoot)) {
                report.messages.push_back("Rilevato path non sicuro in staging ignorato: " + entry.path().string());
                continue;
            }

            // CRH-H05: se l'entry in staging e' un symlink o junction, elimina il link in modo sicuro
            if (AssetFilesystemAdapter::IsSymlinkOrJunction(entry.path())) {
                std::string remErr;
                AssetFilesystemAdapter::SafeRemoveAll(entry.path(), effectiveRoot, &remErr);
                report.incompleteTransactionsDiscarded++;
                report.messages.push_back("Rimosso symlink/junction orfano in staging: " + entry.path().filename().string());
                continue;
            }

            if (entry.is_directory(ec)) {
                std::string folderName = entry.path().filename().string();
                std::filesystem::path journalPath = entry.path() / "tx_journal.json";
                bool shouldDiscard = false;

                if (std::filesystem::exists(journalPath, ec)) {
                    AssetTransactionJournal tj;
                    if (tj.LoadFromFile(journalPath.string())) {
                        if (tj.state != AssetTransactionState::Committed) {
                            shouldDiscard = true;
                            report.discardedTransactionIds.push_back(tj.transactionId);
                        }
                    } else {
                        shouldDiscard = true; // Journal corrotto in staging
                    }
                } else {
                    // Non ha journal: se e' una cartella di transazione valida tx_*
                    if (IsValidTransactionId(folderName)) {
                        shouldDiscard = true;
                    }
                }

                if (shouldDiscard) {
                    std::string remErr;
                    AssetFilesystemAdapter::SafeRemoveAll(entry.path(), effectiveRoot, &remErr);
                    report.incompleteTransactionsDiscarded++;
                    report.messages.push_back("Scartata transazione incompleta: " + folderName);
                }
            }
        }
    }

    // 2. Censimento e recupero dei package committed in published/
    std::filesystem::path pubRoot = std::filesystem::path(effectiveRoot) / "published";
    if (std::filesystem::exists(pubRoot, ec)) {
        std::vector<std::filesystem::path> assetDirs;
        for (const auto& entry : std::filesystem::directory_iterator(pubRoot, ec)) {
            if (entry.is_directory(ec)) {
                std::string assetId = entry.path().filename().string();
                if (IsValidAssetId(assetId)) {
                    assetDirs.push_back(entry.path());
                }
            }
        }
        std::sort(assetDirs.begin(), assetDirs.end());

        for (const auto& assetDirPath : assetDirs) {
            std::string assetId = assetDirPath.filename().string();

            std::filesystem::path activePkgFile = assetDirPath / "active.rawpkg";
            auto activeRev = GetActiveRevision(assetId, effectiveRoot);
            auto highestCommitted = GetHighestCommittedRevision(assetId, effectiveRoot);

            // Caso A: active.rawpkg esiste ma non punta a una revisione committed valida (CRH-H03)
            if (std::filesystem::exists(activePkgFile, ec) && !activeRev.has_value()) {
                report.uncommittedActivePointersDiscarded++;
                report.messages.push_back("Puntatore active non committed o corrotto per asset: " + assetId);
                if (highestCommitted.has_value()) {
                    std::string recMsg;
                    if (ReconcileActivePointer(assetId, effectiveRoot, &recMsg)) {
                        report.reconciledActivePointers++;
                        report.messages.push_back(recMsg);
                    }
                } else {
                    std::filesystem::remove(activePkgFile, ec);
                    std::filesystem::remove(assetDirPath / "active_journal.json", ec);
                }
            }
            // Caso B: active.rawpkg mancante ma revisione committed presente (CRH-H02)
            else if (!std::filesystem::exists(activePkgFile, ec) && highestCommitted.has_value()) {
                std::string recMsg;
                if (ReconcileActivePointer(assetId, effectiveRoot, &recMsg)) {
                    report.reconciledActivePointers++;
                    report.messages.push_back(recMsg);
                }
            }

            // Verifica finale dell'asset attivo recuperato o riconciliato
            activeRev = GetActiveRevision(assetId, effectiveRoot);
            if (activeRev.has_value() && std::filesystem::exists(activePkgFile, ec)) {
                RawAssetPackage pkg;
                if (pkg.LoadFromFile(activePkgFile.string())) {
                    auto val = ValidatePackage(pkg, effectiveRoot, effectivePolicy);
                    if (val.isValid) {
                        report.committedPackagesRecovered++;
                        report.recoveredAssetIds.push_back(pkg.manifest.assetId);

                        if (bReg && mReg && sReg) {
                            auto pub = PublishPackageTransactional(pkg, *bReg, *mReg, *sReg, effectiveRoot, effectivePolicy);
                            if (pub.success) {
                                pkg.SaveToFile(activePkgFile.string());
                            } else {
                                report.messages.push_back("Recovery runtime non riuscito per '" + pkg.manifest.assetId + "': " + pub.errorMessage);
                            }
                        }
                    } else {
                        report.corruptedPackagesFound++;
                        report.messages.push_back("Package committed corrotto: " + pkg.manifest.assetId);
                    }
                } else {
                    report.corruptedPackagesFound++;
                }
            }
        }
    }

    // Rebuild del grafo delle dipendenze al termine del recovery
    RebuildDependencyGraph(effectiveRoot);

    return report;
}

size_t AssetPackagingService::ReconstructRegistriesFromCommitted(
    BlockRegistry& bReg,
    MaterialRegistry& mReg,
    ShapeRegistry& sReg,
    const std::string& rootDir,
    std::optional<AssetTrustPolicy> policy
) {
    const std::string effectiveRoot = rootDir.empty() ? m_rootAssetDirectory : rootDir;
    const AssetTrustPolicy effectivePolicy = policy.value_or(m_defaultTrustPolicy);

    std::vector<RawAssetPackage> candidatePackages;
    std::error_code ec;
    std::filesystem::path pubRoot = std::filesystem::path(effectiveRoot) / "published";
    if (std::filesystem::exists(pubRoot, ec)) {
        for (const auto& assetDir : std::filesystem::directory_iterator(pubRoot, ec)) {
            if (assetDir.is_directory(ec)) {
                std::string assetId = assetDir.path().filename().string();
                if (!IsValidAssetId(assetId)) continue;

                // Trova la revisione autorevole: active revision oppure highest committed
                auto activeRev = GetActiveRevision(assetId, effectiveRoot);
                if (!activeRev.has_value()) {
                    activeRev = GetHighestCommittedRevision(assetId, effectiveRoot);
                }

                if (!activeRev.has_value()) {
                    continue; // Nessuna revisione committed valida
                }

                std::filesystem::path pkgPath = assetDir.path() / ("rev_" + std::to_string(*activeRev)) / "package.rawpkg";
                if (!std::filesystem::exists(pkgPath, ec)) {
                    pkgPath = assetDir.path() / "active.rawpkg";
                }

                if (std::filesystem::exists(pkgPath, ec)) {
                    RawAssetPackage pkg;
                    if (pkg.LoadFromFile(pkgPath.string())) {
                        auto val = ValidatePackage(pkg, effectiveRoot, effectivePolicy);
                        if (val.isValid && pkg.VerifyContentDigest(effectiveRoot, nullptr, nullptr, effectivePolicy)) {
                            candidatePackages.push_back(std::move(pkg));
                        }
                    }
                }
            }
        }
    }

    // 2. Ricostruisci il grafo delle dipendenze e ordina topologicamente i pacchetti da registrare (BM-F4.4)
    RebuildDependencyGraph(effectiveRoot);

    std::vector<std::string> topoOrder;
    std::string topoErr;
    if (m_dependencyGraph.GetTopologicalOrder(topoOrder, &topoErr) && !topoOrder.empty()) {
        std::unordered_map<std::string, size_t> orderMap;
        for (size_t i = 0; i < topoOrder.size(); ++i) {
            orderMap[topoOrder[i]] = i;
        }
        std::sort(candidatePackages.begin(), candidatePackages.end(), [&](const RawAssetPackage& a, const RawAssetPackage& b) {
            size_t idxA = orderMap.count(a.manifest.assetId) ? orderMap[a.manifest.assetId] : size_t(-1);
            size_t idxB = orderMap.count(b.manifest.assetId) ? orderMap[b.manifest.assetId] : size_t(-1);
            if (idxA != idxB) return idxA < idxB;
            return a.manifest.assetId < b.manifest.assetId;
        });
    } else {
        std::sort(candidatePackages.begin(), candidatePackages.end(), [](const RawAssetPackage& a, const RawAssetPackage& b) {
            return a.manifest.assetId < b.manifest.assetId;
        });
    }

    // Reset pulito e autoritativo dei registri in memoria
    sReg.Initialize();
    bReg.Initialize();
    mReg.Initialize();

    size_t count = 0;
    for (auto& pkg : candidatePackages) {
        auto pub = PublishPackageTransactional(pkg, bReg, mReg, sReg, effectiveRoot, effectivePolicy);
        if (pub.success) {
            count++;
        }
    }
    return count;
}

} // namespace fw
