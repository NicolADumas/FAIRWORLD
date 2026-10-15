#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include "RawAssetContract.h"
#include "blocks/BlockRegistry.h"
#include "blocks/MaterialRegistry.h"
#include "blocks/ShapeRegistry.h"

namespace fw {

// =============================================================================
// BM-F2C: RISULTATO TRANSAZIONE DI PUBBLICAZIONE
// =============================================================================

struct AssetPublishTransactionResult {
    bool success = false;
    std::string errorMessage;
    uint8_t targetVoxelId = 0;
    ShapeHandle assignedShape;
    bool wasZeroWorkHit = false;
    bool wasRolledBack = false;
};

// =============================================================================
// BM-F2C.1: PUNTI DI FAULT INJECTION CONTROLLATA PER AUDIT TRANSAZIONALE
// =============================================================================

enum class PublishFaultInjectionPoint : uint8_t {
    None = 0,
    FailBeforeMutations,       // Fails in PreValidation / before any registry write
    FailAfterShapeRegister,    // Fails right after sReg.RegisterShape
    FailAfterBlockMutation,    // Fails right after bReg mutation
    FailAfterMaterialMutation, // Fails right after mReg mutation
    FailDuringPostAudit        // Fails during post-mutation audit
};

// =============================================================================
// BM-F2C: ASSET PACKAGING SERVICE
// Gestore transazionale per validazione, staging, serializzazione e pubblicazione
// dei package definitivi nei registri runtime di FAIRWORLD.
// =============================================================================

class AssetPackagingService {
public:
    AssetPackagingService();
    ~AssetPackagingService() = default;

    void SetRootAssetDirectory(const std::string& dir) { m_rootAssetDirectory = dir; }
    const std::string& GetRootAssetDirectory() const { return m_rootAssetDirectory; }

    // Fault injection controllata per test di rollback e resilienza (BM-F2C.1)
    void SetTestFaultInjection(PublishFaultInjectionPoint pt) { m_testFaultInjection = pt; }
    PublishFaultInjectionPoint GetTestFaultInjection() const { return m_testFaultInjection; }

    // Validazione pre-pubblicazione di tutti i componenti del pacchetto
    RawAssetValidation ValidatePackage(const RawAssetPackage& package, const std::string& rootDir = "") const;

    // Pubblicazione transazionale con garanzia di atomicità (commit o rollback totale)
    AssetPublishTransactionResult PublishPackageTransactional(
        RawAssetPackage& package,
        BlockRegistry& bReg,
        MaterialRegistry& mReg,
        ShapeRegistry& sReg,
        const std::string& rootDir = ""
    );

    // Persistenza diretta del pacchetto compilato
    bool SavePackageToFile(const RawAssetPackage& package, const std::string& filepath) const;
    bool LoadPackageFromFile(const std::string& filepath, RawAssetPackage& outPackage) const;

    // Zero-Work cache
    uint64_t GetLastPublishedFingerprint(const std::string& assetId) const;
    void InvalidatePublicationCache();

    // Cache validazione texture (BM-F4.1)
    struct TextureValidationCacheEntry {
        std::filesystem::file_time_type lastWriteTime;
        uintmax_t fileSize = 0;
        bool isValid = false;
        std::string errorMessage;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    bool ValidateTextureFile(const std::string& fullPath, const std::string& mapName, std::string* outError) const;
    void InvalidateTextureValidationCache();
    size_t GetTextureValidationCacheSize() const { return m_textureValidationCache.size(); }

private:
    std::string m_rootAssetDirectory = "assets";
    PublishFaultInjectionPoint m_testFaultInjection = PublishFaultInjectionPoint::None;
    std::unordered_map<std::string, uint64_t> m_lastPublishedFingerprints;
    mutable std::unordered_map<std::string, TextureValidationCacheEntry> m_textureValidationCache;
};

} // namespace fw
