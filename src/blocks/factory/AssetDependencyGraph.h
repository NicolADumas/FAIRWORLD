#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include "RawAssetContract.h"

namespace fw {

class AssetPackagingService;

// =============================================================================
// BM-F4.4: REPORT DI VALIDAZIONE GRAFO DELLE DIPENDENZE
// =============================================================================

struct DependencyGraphValidation {
    bool isValid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<std::vector<std::string>> detectedCycles;
    std::vector<std::string> missingDependencies;
    std::vector<std::string> incompatibleRevisions;
    std::vector<std::string> digestMismatches;
    AssetIntegrityErrorCode primaryErrorCode = AssetIntegrityErrorCode::None;

    void AddError(const std::string& err, AssetIntegrityErrorCode code = AssetIntegrityErrorCode::MissingDependency) {
        errors.push_back(err);
        isValid = false;
        if (primaryErrorCode == AssetIntegrityErrorCode::None) {
            primaryErrorCode = code;
        }
    }

    void AddWarning(const std::string& warn) {
        warnings.push_back(warn);
    }
};

// =============================================================================
// BM-F4.4: INFORMAZIONI NODO GRAFO DIPENDENZE
// =============================================================================

struct DependencyNodeInfo {
    std::string assetId;
    uint64_t revision = 0;
    std::string contentDigestSha256;
    std::vector<AssetDependency> dependencies;
    bool isCommitted = false;
    bool isActive = false;
    AssetTrustPolicy trustPolicy = AssetTrustPolicy::Strict;
    AssetIntegrityStatus integrityStatus = AssetIntegrityStatus::Verified;
};

// =============================================================================
// BM-F4.4: ASSET DEPENDENCY GRAPH & REFERENTIAL INTEGRITY
// Gestisce nodi, archi diretti/inversi, ordinamento topologico deterministico,
// rilevamento cicli, validazione referenziale e invalidazione mirata Zero-Work.
// =============================================================================

class AssetDependencyGraph {
public:
    AssetDependencyGraph() = default;
    ~AssetDependencyGraph() = default;

    // Registrazione / aggiornamento nodo nel grafo
    void AddNode(
        const std::string& assetId,
        uint64_t revision,
        const std::string& digest,
        const std::vector<AssetDependency>& deps,
        bool isCommitted = true,
        bool isActive = true,
        AssetTrustPolicy trustPolicy = AssetTrustPolicy::Strict,
        AssetIntegrityStatus integrityStatus = AssetIntegrityStatus::Verified
    );

    void RemoveNode(const std::string& assetId);
    void Clear();

    bool HasNode(const std::string& assetId) const;
    const DependencyNodeInfo* GetNode(const std::string& assetId) const;
    size_t GetNodeCount() const { return m_nodes.size(); }
    const std::unordered_map<std::string, DependencyNodeInfo>& GetNodes() const { return m_nodes; }

    // Rilevamento cicli (DFS deterministico con colorazione White/Gray/Black)
    bool DetectCycles(std::vector<std::vector<std::string>>* outCycles = nullptr) const;

    // Ordinamento topologico deterministico (Kahn / DFS post-order invertito)
    // Ritorna false se e' presente almeno un ciclo
    bool GetTopologicalOrder(std::vector<std::string>& outOrder, std::string* outError = nullptr) const;

    // Dipendenze dirette o transitive di un asset (assetId dipende da...)
    std::vector<std::string> GetDependencies(const std::string& assetId, bool recursive = true) const;

    // Dipendenti diretti o transitivi (chi dipende da assetId) per Zero-Work
    std::vector<std::string> GetDependents(const std::string& assetId, bool recursive = true) const;

    // Conteggio referenze (quanti nodi dipendono da targetId)
    size_t GetReferenceCount(const std::string& assetId) const;
    bool IsReferenced(const std::string& assetId) const;

    // Validazione referenziale completa di tutti i nodi del grafo
    DependencyGraphValidation ValidateGraph(
        const std::string& rootDir,
        const AssetPackagingService& service,
        AssetTrustPolicy policy = AssetTrustPolicy::Strict
    ) const;

    // Validazione preliminare delle dipendenze di un package prima del commit (Verify Before Commit)
    static DependencyGraphValidation ValidatePackageDependencies(
        const RawAssetPackage& package,
        const std::string& rootDir,
        const AssetPackagingService& service,
        AssetTrustPolicy policy = AssetTrustPolicy::Strict
    );

    // Costruzione del grafo completo da tutti i package committed in published/
    static AssetDependencyGraph BuildFromCommitted(
        const std::string& rootDir,
        const AssetPackagingService& service
    );

private:
    std::unordered_map<std::string, DependencyNodeInfo> m_nodes;
    std::unordered_map<std::string, std::vector<AssetDependency>> m_adjacency;
    std::unordered_map<std::string, std::vector<std::string>> m_reverseAdjacency;
};

} // namespace fw
