#pragma once

#include "RawAssetContract.h"
#include "AssetPackagingService.h"
#include "AssetDependencyGraph.h"
#include "AssetFilesystemAdapter.h"
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <filesystem>
#include <chrono>

namespace fw {

// =============================================================================
// BM-F4.5: SCHEMA INSPECTION & MIGRATION STRUCTURES
// =============================================================================

struct SchemaInspectionResult {
    SchemaVersionState state = SchemaVersionState::UnsupportedSchema;
    uint32_t detectedVersion = 0;
    std::string digestSha256;
    bool hasValidDigest = false;
    bool hasMigrationProvenance = false;
    std::string message;
};

struct MigrationPlanStep {
    std::string stepName;
    std::string description;
    bool isCompleted = false;
};

struct MigrationPlan {
    uint32_t sourceVersion = 0;
    uint32_t targetVersion = CURRENT_RAW_ASSET_SCHEMA_VERSION;
    std::string assetId;
    RawAssetRevision sourceRevision = 1;
    RawAssetRevision targetRevision = 1;
    bool isFeasible = false;
    std::string failureReason;
    std::vector<MigrationPlanStep> steps;
    std::vector<std::string> warnings;
};

struct MigrationResult {
    bool success = false;
    bool wasZeroWorkHit = false;
    RawAssetPackage migratedPackage;
    std::string errorMessage;
    AssetIntegrityStatus finalIntegrityStatus = AssetIntegrityStatus::Rejected;
    std::vector<std::string> stepsExecuted;
};

// =============================================================================
// BM-F4.5: ASSET SCHEMA MIGRATION SERVICE
// Servizio deterministico e transazionale per la migrazione dei RawAssetPackage
// legacy (V1) verso lo schema corrente (V2), garantendo conservazione lossless
// di geometria, materiali, fisica, identità e integrità referenziale.
// =============================================================================

class AssetSchemaMigrationService {
public:
    explicit AssetSchemaMigrationService(AssetPackagingService* packagingService = nullptr);
    ~AssetSchemaMigrationService() = default;

    void SetRootAssetDirectory(const std::string& dir) { m_rootAssetDirectory = dir; }
    const std::string& GetRootAssetDirectory() const { return m_rootAssetDirectory; }

    // FASE 3: Operazioni fondamentali
    // 1. Ispezione dello schema
    SchemaInspectionResult InspectSchema(const RawAssetPackage& package, const std::string& rootDir = "") const;
    SchemaInspectionResult InspectSchemaFile(const std::string& packageFilePath, const std::string& rootDir = "") const;

    // 2. Verifica fattibilità migrazione
    bool CanMigrate(const RawAssetPackage& package, std::string* outReason = nullptr) const;
    bool CanMigrateFile(const std::string& packageFilePath, std::string* outReason = nullptr) const;

    // 3. Costruzione del piano di migrazione
    MigrationPlan BuildMigrationPlan(const RawAssetPackage& package, bool allocateNewRevision = true) const;

    // 4. Migrazione in-memory verso lo schema corrente V2
    MigrationResult MigrateToCurrent(
        const RawAssetPackage& legacyPackage,
        const std::string& rootDir = "",
        bool allocateNewRevision = true
    ) const;

    // 5. Validazione post-migrazione con policy Strict
    RawAssetValidation ValidateMigratedPackage(
        const RawAssetPackage& migratedPackage,
        const std::string& rootDir = ""
    ) const;

    // FASE 7: Migrazione persistente e transazionale (staging + atomic commit)
    MigrationResult MigrateAndPublishPersistent(
        const RawAssetPackage& legacyPackage,
        BlockRegistry& bReg,
        MaterialRegistry& mReg,
        ShapeRegistry& sReg,
        const std::string& rootDir = "",
        bool allocateNewRevision = true
    );

    // Carica package da file, migra se necessario e salva la nuova revisione in modo sicuro
    MigrationResult MigrateFilePersistent(
        const std::string& sourcePackagePath,
        BlockRegistry& bReg,
        MaterialRegistry& mReg,
        ShapeRegistry& sReg,
        const std::string& rootDir = "",
        bool allocateNewRevision = true
    );

private:
    std::string m_rootAssetDirectory;
    AssetPackagingService* m_externalPackagingService = nullptr;
    std::unique_ptr<AssetPackagingService> m_ownedPackagingService;

    AssetPackagingService& GetPackagingService();
    const AssetPackagingService& GetPackagingService() const;

    static uint64_t GetCurrentTimestampSec() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
    }
};

} // namespace fw
