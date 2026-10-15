#include "pch.h"
#include "AssetPackagingService.h"
#include "TextureBakingService.h"
#include "stb_image.h"
#include <filesystem>
#include <fstream>

namespace fw {

AssetPackagingService::AssetPackagingService() = default;

bool AssetPackagingService::ValidateTextureFile(const std::string& fullPath, const std::string& mapName, std::string* outError) const {
    std::error_code ec;
    if (!std::filesystem::exists(fullPath, ec) || std::filesystem::file_size(fullPath, ec) == 0) {
        if (outError) *outError = "File texture " + mapName + " non rintracciabile o vuoto su disco: " + fullPath;
        return false;
    }

    auto lastWrite = std::filesystem::last_write_time(fullPath, ec);
    auto fileSize = std::filesystem::file_size(fullPath, ec);

    // Verifica cache di validazione (Zero-Work decoding bypass)
    auto it = m_textureValidationCache.find(fullPath);
    if (it != m_textureValidationCache.end() &&
        it->second.lastWriteTime == lastWrite &&
        it->second.fileSize == fileSize) {
        if (!it->second.isValid && outError) {
            *outError = it->second.errorMessage;
        }
        return it->second.isValid;
    }

    // Lettura completa del file binario
    std::ifstream file(fullPath, std::ios::binary);
    if (!file.is_open()) {
        std::string err = "Impossibile aprire il file texture " + mapName + ": " + fullPath;
        m_textureValidationCache[fullPath] = { lastWrite, fileSize, false, err, 0, 0 };
        if (outError) *outError = err;
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
        return false;
    }

    // 2. Verifica payload PNG decodificabile e non troncato (BM-F4.1)
    int w = 0, h = 0, channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(buffer.data(), static_cast<int>(buffer.size()), &w, &h, &channels, 4);
    if (!decoded) {
        std::string err = "File texture " + mapName + " corrotto o troncato (impossibile decodificare il payload PNG): " + fullPath;
        m_textureValidationCache[fullPath] = { lastWrite, fileSize, false, err, 0, 0 };
        if (outError) *outError = err;
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

RawAssetValidation AssetPackagingService::ValidatePackage(const RawAssetPackage& package, const std::string& rootDir) const {
    RawAssetValidation report;
    report.isValid = true;

    const std::string effectiveRoot = rootDir.empty() ? m_rootAssetDirectory : rootDir;

    // 1. Validazione Metadati e Identità
    if (package.manifest.assetId.empty()) {
        report.AddError("Manifest assetId vuoto.");
    } else {
        std::string idErr;
        if (!BlockRegistry::ValidateStringId(package.manifest.assetId, &idErr)) {
            report.AddError("Identificatore asset non valido: " + idErr);
        }
    }

    // 2. Validazione Geometria Compilata
    if (package.geometry.vertices.empty()) {
        report.AddError("Geometria compilata assente: nessun vertice nel pacchetto.");
    } else if (package.geometry.vertexCount != package.geometry.vertices.size()) {
        report.AddError("Discrepanza conteggio vertici: atteso " + std::to_string(package.geometry.vertexCount) +
                        ", presenti " + std::to_string(package.geometry.vertices.size()));
    }
    if (package.geometrySource == GeometrySource::SculptedMesh && package.shape.type != ShapeType::TriangleMesh) {
        report.AddError("Disallineamento sorgente geometrica: dichiarato SculptedMesh ma ShapeDefinition non e' TriangleMesh.");
    }

    // 3. Validazione Parametri Fisici
    if (package.compiledPhysics.mass <= 0.0f && package.compiledPhysics.massMode == BlockMassMode::Manual) {
        report.AddError("Massa fisica deve essere strettamente positiva (> 0 kg).");
    }
    if (package.compiledPhysics.density <= 0.0f) {
        report.AddError("Densita fisica deve essere strettamente positiva (> 0 kg/m^3).");
    }
    if (package.compiledPhysics.friction < 0.0f || package.compiledPhysics.friction > 1.0f) {
        report.AddWarning("Coefficiente di attrito fuori scala [0, 1]. Clamp applicato.");
    }

    // 4. Validazione Mappe Texture Referenziate con Decodifica e Cache (BM-F4.1)
    if (!package.albedoMapUri.empty()) {
        std::string uriErr;
        if (!TextureBakingService::ValidatePortableUri(package.albedoMapUri, &uriErr)) {
            report.AddError("URI Albedo non valido: " + uriErr);
        } else {
            std::filesystem::path fullPath = std::filesystem::path(effectiveRoot) / package.albedoMapUri;
            std::string texErr;
            if (!ValidateTextureFile(fullPath.string(), "Albedo", &texErr)) {
                report.AddError(texErr);
            }
        }
    }

    if (!package.normalMapUri.empty()) {
        std::string uriErr;
        if (!TextureBakingService::ValidatePortableUri(package.normalMapUri, &uriErr)) {
            report.AddError("URI Normal map non valido: " + uriErr);
        } else {
            std::filesystem::path fullPath = std::filesystem::path(effectiveRoot) / package.normalMapUri;
            std::string texErr;
            if (!ValidateTextureFile(fullPath.string(), "Normal map", &texErr)) {
                report.AddError(texErr);
            }
        }
    }

    if (!package.ormMapUri.empty()) {
        std::string uriErr;
        if (!TextureBakingService::ValidatePortableUri(package.ormMapUri, &uriErr)) {
            report.AddError("URI ORM map non valido: " + uriErr);
        } else {
            std::filesystem::path fullPath = std::filesystem::path(effectiveRoot) / package.ormMapUri;
            std::string texErr;
            if (!ValidateTextureFile(fullPath.string(), "ORM map", &texErr)) {
                report.AddError(texErr);
            }
        }
    }

    if (!package.emissiveMapUri.empty()) {
        std::string uriErr;
        if (!TextureBakingService::ValidatePortableUri(package.emissiveMapUri, &uriErr)) {
            report.AddError("URI Emissive map non valido: " + uriErr);
        } else {
            std::filesystem::path fullPath = std::filesystem::path(effectiveRoot) / package.emissiveMapUri;
            std::string texErr;
            if (!ValidateTextureFile(fullPath.string(), "Emissive map", &texErr)) {
                report.AddError(texErr);
            }
        }
    }

    // 5. Stadio del ciclo di vita
    if (package.manifest.stage < RawAssetStage::Validated) {
        report.AddError("Stadio manifest insufficiente: l'asset deve essere almeno Validated o Packaged.");
    }

    // 6. Verifica Integrità Crittografica SHA-256 (BM-F4.1)
    // Distingue formalmente il calcolo del digest dalla verifica del valore memorizzato
    if (!package.contentDigestSha256.empty()) {
        std::string computedDigest = package.ComputeContentDigestSha256(effectiveRoot);
        if (package.contentDigestSha256 != computedDigest) {
            report.AddError("Violazione di integrita' crittografica (contentDigestSha256 mismatch): atteso " +
                            package.contentDigestSha256.substr(0, 16) +
                            "..., calcolato " + computedDigest.substr(0, 16) + "... Asset o texture alterati.");
        }
    }

    return report;
}

AssetPublishTransactionResult AssetPackagingService::PublishPackageTransactional(
    RawAssetPackage& package,
    BlockRegistry& bReg,
    MaterialRegistry& mReg,
    ShapeRegistry& sReg,
    const std::string& rootDir
) {
    AssetPublishTransactionResult result;
    const std::string effectiveRoot = rootDir.empty() ? m_rootAssetDirectory : rootDir;

    // FASE 1: PRE-VALIDAZIONE RIGOROSA
    auto valReport = ValidatePackage(package, effectiveRoot);
    if (!valReport.isValid) {
        result.success = false;
        result.errorMessage = valReport.errors.empty() ? "Validazione package fallita." : valReport.errors[0];
        result.wasRolledBack = false;
        return result;
    }

    // FASE 2: VERIFICA COLLISIONI STRING ID
    uint8_t existingId = bReg.GetBlockIdByString(package.manifest.assetId);
    uint8_t targetVoxelId = package.manifest.targetRuntimeVoxelId;

    if (existingId != 0 && targetVoxelId != 0 && existingId != targetVoxelId) {
        result.success = false;
        result.errorMessage = "Collisione ID: l'asset '" + package.manifest.assetId +
                              "' e' gia' registrato allo slot ID " + std::to_string(existingId) +
                              ", diverso dal target richiesto " + std::to_string(targetVoxelId);
        result.wasRolledBack = false;
        return result;
    }

    // FASE 3: VERIFICA ZERO-WORK
    auto fpIt = m_lastPublishedFingerprints.find(package.manifest.assetId);
    if (fpIt != m_lastPublishedFingerprints.end() && fpIt->second == package.packageFingerprint && package.packageFingerprint != 0) {
        if (existingId != 0 && bReg.IsBlockValid(existingId) && bReg.GetBlock(existingId).stringId == package.manifest.assetId) {
            result.success = true;
            result.targetVoxelId = existingId;
            result.wasZeroWorkHit = true;
            result.wasRolledBack = false;
            return result;
        }
    }

    // FASE 3b: FAULT INJECTION CONTROLLATA (PRIMA DELLE MUTAZIONI)
    if (m_testFaultInjection == PublishFaultInjectionPoint::FailBeforeMutations) {
        result.success = false;
        result.errorMessage = "Fault injection: fallimento prima delle mutazioni.";
        result.wasRolledBack = false;
        return result;
    }

    // FASE 4: STAGING & SNAPSHOTTING PER TRANSAZIONALITA (ROLLBACK SEGURO)
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
    auto performRollback = [&](const std::string& errorMsg) -> AssetPublishTransactionResult {
        if (wasNewBlockAllocated) {
            bReg.DeleteBlock(targetVoxelId);
        } else {
            if (oldBlockSnapshot.has_value()) {
                bReg.UpdateBlock(targetVoxelId, *oldBlockSnapshot);
            }
            if (oldMaterialSnapshot.has_value()) {
                mReg.UpdateMaterial(targetVoxelId, *oldMaterialSnapshot);
            }
        }
        // BM-F4: De-registra la forma SOLO se nessun altro materiale nei registri la sta referenziando
        if (wasShapeRegistered && assignedShape.IsValid()) {
            if (mReg.CountShapeUsers(assignedShape.id) == 0) {
                sReg.UnregisterShape(assignedShape);
            }
        }
        AssetPublishTransactionResult failRes;
        failRes.success = false;
        failRes.errorMessage = errorMsg;
        failRes.targetVoxelId = 0;
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
    m_lastPublishedFingerprints[package.manifest.assetId] = package.packageFingerprint;

    result.success = true;
    result.targetVoxelId = targetVoxelId;
    result.assignedShape = assignedShape;
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

uint64_t AssetPackagingService::GetLastPublishedFingerprint(const std::string& assetId) const {
    auto it = m_lastPublishedFingerprints.find(assetId);
    return (it != m_lastPublishedFingerprints.end()) ? it->second : 0;
}

void AssetPackagingService::InvalidatePublicationCache() {
    m_lastPublishedFingerprints.clear();
    m_textureValidationCache.clear();
}

void AssetPackagingService::InvalidateTextureValidationCache() {
    m_textureValidationCache.clear();
}

} // namespace fw
