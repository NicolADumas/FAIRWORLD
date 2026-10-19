#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include "RawAssetContract.h"
#include "AssetFilesystemAdapter.h"
#include "blocks/BlockRegistry.h"
#include "blocks/MaterialRegistry.h"
#include "blocks/ShapeRegistry.h"
#include "AssetDependencyGraph.h"

namespace fw {

// =============================================================================
// BM-F4.3: CONTRATTI DI TRANSAZIONE PERSISTENTE E CRASH RECOVERY
// =============================================================================

using TransactionID = std::string;

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
    AssetIntegrityStatus integrityStatus = AssetIntegrityStatus::Rejected;
    AssetIntegrityErrorCode errorCode = AssetIntegrityErrorCode::None;
    TransactionID transactionId;
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
// BM-F4.3: STATI TRANSAZIONE PERSISTENTE E FAULT POINTS
// =============================================================================

enum class AssetTransactionState : uint8_t {
    Prepared = 0,    // Transazione allocata, staging dir creata, manifest pronto
    FilesStaged = 1, // File (package JSON, textures, mesh) scritti in staging
    Verified = 2,    // Dipendenze e digest SHA-256 verificati con successo nello staging
    Committed = 3,   // Commit durevole eseguito su disco (visibile nel repository pubblicato)
    Aborted = 4      // Transazione annullata / fallita
};

inline const char* AssetTransactionStateToString(AssetTransactionState state) {
    switch (state) {
        case AssetTransactionState::Prepared: return "Prepared";
        case AssetTransactionState::FilesStaged: return "FilesStaged";
        case AssetTransactionState::Verified: return "Verified";
        case AssetTransactionState::Committed: return "Committed";
        case AssetTransactionState::Aborted: return "Aborted";
        default: return "Unknown";
    }
}

enum class TransactionFaultPoint : uint8_t {
    None = 0,
    FailDuringStaging,                 // Simula crash / errore durante la scrittura degli artefatti in staging (CRH-02)
    FailBeforeCommit,                  // Simula crash / errore dopo staging/verifica ma prima del commit durevole (CRH-03)
    FailImmediatelyAfterCommit,        // Simula crash subito dopo il commit su disco, prima del sync registri (CRH-04)
    FailBetweenCommitAndActivePointer, // Simula crash tra scrittura del commit.journal e aggiornamento active.rawpkg (CRH-H01)
    FailDuringActivePointerReplace,    // Simula errore durante il replace atomico di active.rawpkg (CRH-H04)
    FailDuringRecovery                 // Simula errore durante il recovery
};

struct AssetTransactionJournal {
    TransactionID transactionId;
    std::string assetId;
    uint64_t revision = 0;
    AssetTransactionState state = AssetTransactionState::Prepared;
    std::string stagingDirectory;
    std::string targetDirectory;
    std::string packageFilename;
    std::vector<std::string> stagedFiles;
    std::string contentDigestSha256;
    uint64_t timestamp = 0;

    bool SaveToFile(const std::string& filepath) const;
    bool LoadFromFile(const std::string& filepath);
};

struct CrashRecoveryReport {
    bool success = true;
    uint32_t incompleteTransactionsDiscarded = 0;
    uint32_t committedPackagesRecovered = 0;
    uint32_t corruptedPackagesFound = 0;
    uint32_t reconciledActivePointers = 0;
    uint32_t uncommittedActivePointersDiscarded = 0;
    std::vector<std::string> discardedTransactionIds;
    std::vector<std::string> recoveredAssetIds;
    std::vector<std::string> messages;
};

// =============================================================================
// BM-F2C / BM-F4.3: ASSET PACKAGING SERVICE
// Gestore transazionale per validazione, staging, serializzazione, pubblicazione atomica
// e crash recovery dei package definitivi nei registri runtime di FAIRWORLD.
// =============================================================================

class AssetPackagingService {
public:
    AssetPackagingService();
    ~AssetPackagingService() = default;

    void SetRootAssetDirectory(const std::string& dir) { m_rootAssetDirectory = dir; }
    const std::string& GetRootAssetDirectory() const { return m_rootAssetDirectory; }

    // Trust Policy di default per il servizio (BM-F4.2)
    void SetTrustPolicy(AssetTrustPolicy policy) { m_defaultTrustPolicy = policy; }
    AssetTrustPolicy GetTrustPolicy() const { return m_defaultTrustPolicy; }

    // Fault injection controllata in-memory per test di rollback e resilienza (BM-F2C.1)
    void SetTestFaultInjection(PublishFaultInjectionPoint pt) { m_testFaultInjection = pt; }
    PublishFaultInjectionPoint GetTestFaultInjection() const { return m_testFaultInjection; }

    // Fault injection controllata per crash recovery e durabilità transazionale (BM-F4.3)
    void SetTransactionFaultInjection(TransactionFaultPoint pt) { m_transactionFault = pt; }
    TransactionFaultPoint GetTransactionFaultInjection() const { return m_transactionFault; }

    // Validazione pre-pubblicazione di tutti i componenti del pacchetto
    RawAssetValidation ValidatePackage(
        const RawAssetPackage& package,
        const std::string& rootDir = "",
        std::optional<AssetTrustPolicy> policy = std::nullopt,
        const std::string& dependencyRootDir = ""
    ) const;

    // Pubblicazione transazionale con garanzia di atomicità in-memory (commit o rollback totale)
    AssetPublishTransactionResult PublishPackageTransactional(
        RawAssetPackage& package,
        BlockRegistry& bReg,
        MaterialRegistry& mReg,
        ShapeRegistry& sReg,
        const std::string& rootDir = "",
        std::optional<AssetTrustPolicy> policy = std::nullopt
    );

    // Pubblicazione transazionale persistente con staging isolato, atomic commit e durabilità (BM-F4.3)
    AssetPublishTransactionResult PublishPackagePersistent(
        RawAssetPackage& package,
        BlockRegistry& bReg,
        MaterialRegistry& mReg,
        ShapeRegistry& sReg,
        const std::string& rootDir = "",
        std::optional<AssetTrustPolicy> policy = std::nullopt
    );

    // Crash Recovery: individua transazioni incomplete, elimina staging orfani e sincronizza i registri (BM-F4.3)
    CrashRecoveryReport RunCrashRecovery(
        BlockRegistry* bReg = nullptr,
        MaterialRegistry* mReg = nullptr,
        ShapeRegistry* sReg = nullptr,
        const std::string& rootDir = "",
        std::optional<AssetTrustPolicy> policy = std::nullopt
    );

    // Ricostruzione autoritativa dei registri runtime a partire dallo stato persistente committed
    size_t ReconstructRegistriesFromCommitted(
        BlockRegistry& bReg,
        MaterialRegistry& mReg,
        ShapeRegistry& sReg,
        const std::string& rootDir = "",
        std::optional<AssetTrustPolicy> policy = std::nullopt
    );

    // Persistenza diretta del pacchetto compilato
    bool SavePackageToFile(const RawAssetPackage& package, const std::string& filepath) const;
    bool LoadPackageFromFile(const std::string& filepath, RawAssetPackage& outPackage) const;

    // Caricamento con verifica di integrità integrata (BM-F4.2 Verify-Before-Commit)
    bool LoadAndValidatePackageFromFile(
        const std::string& filepath,
        RawAssetPackage& outPackage,
        RawAssetValidation& outValidation,
        const std::string& rootDir = "",
        std::optional<AssetTrustPolicy> policy = std::nullopt
    ) const;

    // Zero-Work cache e record di pubblicazione verificata (BM-F4.2)
    struct PublishedAssetRecord {
        uint64_t packageFingerprint = 0;
        std::string contentDigestSha256;
        AssetIntegrityStatus integrityStatus = AssetIntegrityStatus::Rejected;
        AssetTrustPolicy trustPolicy = AssetTrustPolicy::Strict;
        uint8_t targetVoxelId = 0;
    };

    uint64_t GetLastPublishedFingerprint(const std::string& assetId) const;
    std::optional<PublishedAssetRecord> GetLastPublishedRecord(const std::string& assetId) const;
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

    bool ValidateTextureFile(
        const std::string& fullPath,
        const std::string& mapName,
        std::string* outError,
        AssetIntegrityErrorCode* outCode = nullptr
    ) const;
    void InvalidateTextureValidationCache();
    size_t GetTextureValidationCacheSize() const { return m_textureValidationCache.size(); }

    // Helper percorsi persistenti e validazione identificatori (BM-F4.3.1)
    static std::string SanitizeAssetIdForPath(const std::string& assetId);
    static TransactionID GenerateTransactionId(const std::string& assetId, uint64_t revision);
    static bool IsValidTransactionId(const TransactionID& txId);
    static bool IsValidAssetId(const std::string& assetId);

    std::string GetPublishedPackagePath(const std::string& assetId, uint64_t revision = 0, const std::string& rootDir = "") const;
    std::string GetActivePublishedPackagePath(const std::string& assetId, const std::string& rootDir = "") const;

    // Gestione autoritativa di commit e puntatore active (BM-F4.3.1)
    bool IsRevisionCommitted(const std::string& assetId, uint64_t revision, const std::string& rootDir = "", std::string* outDigest = nullptr) const;
    std::optional<uint64_t> GetActiveRevision(const std::string& assetId, const std::string& rootDir = "") const;
    std::optional<uint64_t> GetHighestCommittedRevision(const std::string& assetId, const std::string& rootDir = "") const;
    bool ReconcileActivePointer(const std::string& assetId, const std::string& rootDir = "", std::string* outMessage = nullptr);

    // Gestione grafo dipendenze e integrita' referenziale (BM-F4.4)
    const AssetDependencyGraph& GetDependencyGraph() const { return m_dependencyGraph; }
    AssetDependencyGraph& GetDependencyGraph() { return m_dependencyGraph; }
    void RebuildDependencyGraph(const std::string& rootDir = "");

private:
    std::string m_rootAssetDirectory = "assets";
    AssetTrustPolicy m_defaultTrustPolicy = AssetTrustPolicy::AllowLegacy;
    PublishFaultInjectionPoint m_testFaultInjection = PublishFaultInjectionPoint::None;
    TransactionFaultPoint m_transactionFault = TransactionFaultPoint::None;
    std::unordered_map<std::string, uint64_t> m_lastPublishedFingerprints;
    std::unordered_map<std::string, PublishedAssetRecord> m_lastPublishedRecords;
    mutable std::unordered_map<std::string, TextureValidationCacheEntry> m_textureValidationCache;
    AssetDependencyGraph m_dependencyGraph;
};

} // namespace fw
