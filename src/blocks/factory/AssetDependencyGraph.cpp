#include "pch.h"
#include "AssetDependencyGraph.h"
#include "AssetPackagingService.h"
#include <algorithm>
#include <queue>
#include <filesystem>
#include <iostream>

namespace fw {

void AssetDependencyGraph::AddNode(
    const std::string& assetId,
    uint64_t revision,
    const std::string& digest,
    const std::vector<AssetDependency>& deps,
    bool isCommitted,
    bool isActive,
    AssetTrustPolicy trustPolicy,
    AssetIntegrityStatus integrityStatus
) {
    RemoveNode(assetId);

    DependencyNodeInfo info;
    info.assetId = assetId;
    info.revision = revision;
    info.contentDigestSha256 = digest;
    info.dependencies = deps;
    info.isCommitted = isCommitted;
    info.isActive = isActive;
    info.trustPolicy = trustPolicy;
    info.integrityStatus = integrityStatus;

    m_nodes[assetId] = info;
    m_adjacency[assetId] = deps;

    for (const auto& dep : deps) {
        if (!dep.targetId.empty() && dep.targetId != assetId) {
            m_reverseAdjacency[dep.targetId].push_back(assetId);
        }
    }
}

void AssetDependencyGraph::RemoveNode(const std::string& assetId) {
    auto it = m_nodes.find(assetId);
    if (it == m_nodes.end()) return;

    // Rimuovi da reverseAdjacency delle dipendenze uscenti
    auto adjIt = m_adjacency.find(assetId);
    if (adjIt != m_adjacency.end()) {
        for (const auto& dep : adjIt->second) {
            auto revIt = m_reverseAdjacency.find(dep.targetId);
            if (revIt != m_reverseAdjacency.end()) {
                auto& vec = revIt->second;
                vec.erase(std::remove(vec.begin(), vec.end(), assetId), vec.end());
                if (vec.empty()) {
                    m_reverseAdjacency.erase(revIt);
                }
            }
        }
        m_adjacency.erase(adjIt);
    }

    m_nodes.erase(it);
}

void AssetDependencyGraph::Clear() {
    m_nodes.clear();
    m_adjacency.clear();
    m_reverseAdjacency.clear();
}

bool AssetDependencyGraph::HasNode(const std::string& assetId) const {
    return m_nodes.find(assetId) != m_nodes.end();
}

const DependencyNodeInfo* AssetDependencyGraph::GetNode(const std::string& assetId) const {
    auto it = m_nodes.find(assetId);
    if (it != m_nodes.end()) {
        return &it->second;
    }
    return nullptr;
}

bool AssetDependencyGraph::DetectCycles(std::vector<std::vector<std::string>>* outCycles) const {
    // DFS deterministico: 0 = White, 1 = Gray, 2 = Black
    std::unordered_map<std::string, uint8_t> colors;
    for (const auto& [id, _] : m_nodes) {
        colors[id] = 0;
    }

    std::vector<std::string> sortedNodes;
    sortedNodes.reserve(m_nodes.size());
    for (const auto& [id, _] : m_nodes) {
        sortedNodes.push_back(id);
    }
    std::sort(sortedNodes.begin(), sortedNodes.end());

    std::vector<std::string> path;
    bool foundCycle = false;

    auto dfs = [&](auto& self, const std::string& u, size_t depth) -> void {
        if (depth > 2048) {
            foundCycle = true;
            return;
        }
        colors[u] = 1; // Gray
        path.push_back(u);

        auto adjIt = m_adjacency.find(u);
        if (adjIt != m_adjacency.end()) {
            // Ordina dipendenze per determinismo
            std::vector<std::string> neighbors;
            for (const auto& dep : adjIt->second) {
                if (dep.dependencyType != "texture" && !dep.targetId.empty()) {
                    neighbors.push_back(dep.targetId);
                }
            }
            std::sort(neighbors.begin(), neighbors.end());

            for (const auto& v : neighbors) {
                auto colIt = colors.find(v);
                if (colIt != colors.end()) {
                    if (colIt->second == 1) { // Rilevato ciclo verso nodo Gray
                        foundCycle = true;
                        if (outCycles) {
                            std::vector<std::string> cycle;
                            auto cycleStart = std::find(path.begin(), path.end(), v);
                            if (cycleStart != path.end()) {
                                cycle.assign(cycleStart, path.end());
                            } else {
                                cycle = path;
                            }
                            cycle.push_back(v);
                            outCycles->push_back(cycle);
                        }
                    } else if (colIt->second == 0) {
                        self(self, v, depth + 1);
                    }
                }
            }
        }

        path.pop_back();
        colors[u] = 2; // Black
    };

    for (const auto& node : sortedNodes) {
        if (colors[node] == 0) {
            dfs(dfs, node, 0);
        }
    }

    return foundCycle;
}

bool AssetDependencyGraph::GetTopologicalOrder(std::vector<std::string>& outOrder, std::string* outError) const {
    outOrder.clear();

    if (DetectCycles(nullptr)) {
        if (outError) *outError = "Impossibile calcolare ordine topologico: rilevato ciclo di dipendenze.";
        return false;
    }

    // Calcolo in-degree: quanti nodi interni al grafo sono dipendenze di u
    // In un grafo dove u -> v (u dipende da v), per il caricamento v deve venire PRIMA di u.
    // Dunque v ha in-degree 0 per la relazione di precedenza (v abilita u).
    std::unordered_map<std::string, size_t> remainingDependencies;
    for (const auto& [u, _] : m_nodes) {
        remainingDependencies[u] = 0;
    }

    for (const auto& [u, deps] : m_adjacency) {
        for (const auto& dep : deps) {
            if (dep.dependencyType != "texture" && HasNode(dep.targetId)) {
                remainingDependencies[u]++;
            }
        }
    }

    // Coda nodi senza dipendenze irrisolte (ordinata alfabeticamente per determinismo)
    std::vector<std::string> ready;
    for (const auto& [u, count] : remainingDependencies) {
        if (count == 0) {
            ready.push_back(u);
        }
    }
    std::sort(ready.begin(), ready.end());

    std::vector<std::string> order;
    order.reserve(m_nodes.size());

    while (!ready.empty()) {
        std::string curr = ready.front();
        ready.erase(ready.begin());
        order.push_back(curr);

        // Per ogni nodo dependent che aspetta curr:
        auto revIt = m_reverseAdjacency.find(curr);
        if (revIt != m_reverseAdjacency.end()) {
            for (const auto& depU : revIt->second) {
                auto depCountIt = remainingDependencies.find(depU);
                if (depCountIt != remainingDependencies.end() && depCountIt->second > 0) {
                    depCountIt->second--;
                    if (depCountIt->second == 0) {
                        ready.push_back(depU);
                        std::sort(ready.begin(), ready.end());
                    }
                }
            }
        }
    }

    if (order.size() != m_nodes.size()) {
        if (outError) *outError = "Risoluzione topologica incompleta: dipendenze circolari o non risolvibili.";
        return false;
    }

    outOrder = std::move(order);
    return true;
}

std::vector<std::string> AssetDependencyGraph::GetDependencies(const std::string& assetId, bool recursive) const {
    std::vector<std::string> result;
    std::unordered_set<std::string> visited;
    std::queue<std::string> q;

    visited.insert(assetId);
    q.push(assetId);

    while (!q.empty()) {
        std::string curr = q.front();
        q.pop();

        auto it = m_adjacency.find(curr);
        if (it != m_adjacency.end()) {
            for (const auto& dep : it->second) {
                if (!dep.targetId.empty() && dep.dependencyType != "texture") {
                    if (visited.find(dep.targetId) == visited.end()) {
                        visited.insert(dep.targetId);
                        result.push_back(dep.targetId);
                        if (recursive) {
                            q.push(dep.targetId);
                        }
                    }
                }
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> AssetDependencyGraph::GetDependents(const std::string& assetId, bool recursive) const {
    std::vector<std::string> result;
    std::unordered_set<std::string> visited;
    std::queue<std::string> q;

    visited.insert(assetId);
    q.push(assetId);

    while (!q.empty()) {
        std::string curr = q.front();
        q.pop();

        auto it = m_reverseAdjacency.find(curr);
        if (it != m_reverseAdjacency.end()) {
            for (const auto& dependent : it->second) {
                if (visited.find(dependent) == visited.end()) {
                    visited.insert(dependent);
                    result.push_back(dependent);
                    if (recursive) {
                        q.push(dependent);
                    }
                }
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

size_t AssetDependencyGraph::GetReferenceCount(const std::string& assetId) const {
    auto it = m_reverseAdjacency.find(assetId);
    if (it != m_reverseAdjacency.end()) {
        return it->second.size();
    }
    return 0;
}

bool AssetDependencyGraph::IsReferenced(const std::string& assetId) const {
    return GetReferenceCount(assetId) > 0;
}

DependencyGraphValidation AssetDependencyGraph::ValidateGraph(
    const std::string& rootDir,
    const AssetPackagingService& service,
    AssetTrustPolicy policy
) const {
    DependencyGraphValidation report;
    report.isValid = true;

    // 1. Rilevamento cicli
    std::vector<std::vector<std::string>> cycles;
    if (DetectCycles(&cycles)) {
        report.detectedCycles = cycles;
        for (const auto& cycle : cycles) {
            std::string cycleStr;
            for (size_t i = 0; i < cycle.size(); ++i) {
                cycleStr += cycle[i];
                if (i + 1 < cycle.size()) cycleStr += " -> ";
            }
            report.AddError("Rilevata dipendenza ciclica tra asset: " + cycleStr, AssetIntegrityErrorCode::UnsupportedSchema);
        }
    }

    // 2. Validazione di ogni arco del grafo
    for (const auto& [nodeId, nodeInfo] : m_nodes) {
        for (const auto& dep : nodeInfo.dependencies) {
            if (dep.dependencyType == "texture") continue;

            if (dep.targetId.empty()) {
                report.AddError("Asset " + nodeId + " dichiara dipendenza con targetId vuoto.");
                continue;
            }

            if (dep.targetId == nodeId) {
                report.AddError("Asset " + nodeId + " dichiara auto-dipendenza circolare.", AssetIntegrityErrorCode::UnsupportedSchema);
                continue;
            }

            // Verifica esistenza nel grafo o su disco committed
            bool targetFound = false;
            std::string targetDigest;
            uint64_t targetRev = 0;

            if (HasNode(dep.targetId)) {
                const auto* targetNode = GetNode(dep.targetId);
                targetRev = targetNode->revision;
                targetDigest = targetNode->contentDigestSha256;
                targetFound = true;

                // Verifica revisione
                if (dep.revisionPolicy == DependencyRevisionPolicy::Exact) {
                    if (targetRev != dep.requiredRevision) {
                        report.incompatibleRevisions.push_back(dep.targetId);
                        if (!dep.isOptional) {
                            report.AddError("Revisione non corrispondente per dipendenza " + dep.targetId +
                                            ": attesa rev_" + std::to_string(dep.requiredRevision) +
                                            ", trovata rev_" + std::to_string(targetRev));
                        }
                    }
                }

                // Verifica Trust Policy
                if (policy == AssetTrustPolicy::Strict && targetNode->trustPolicy != AssetTrustPolicy::Strict &&
                    targetNode->integrityStatus != AssetIntegrityStatus::Verified) {
                    if (!dep.isOptional) {
                        report.AddError("Incompatibilita' Trust Policy: asset " + nodeId +
                                        " richiede Strict ma la dipendenza " + dep.targetId + " non e' verificata.",
                                        AssetIntegrityErrorCode::LegacyUnverified);
                    }
                }
            } else {
                // Ricerca su filesystem committed tramite il servizio
                if (dep.revisionPolicy == DependencyRevisionPolicy::Exact) {
                    if (service.IsRevisionCommitted(dep.targetId, dep.requiredRevision, rootDir, &targetDigest)) {
                        targetFound = true;
                        targetRev = dep.requiredRevision;
                    }
                } else {
                    auto activeRev = service.GetActiveRevision(dep.targetId, rootDir);
                    if (activeRev.has_value()) {
                        if (service.IsRevisionCommitted(dep.targetId, *activeRev, rootDir, &targetDigest)) {
                            targetFound = true;
                            targetRev = *activeRev;
                        }
                    }
                }
            }

            if (!targetFound) {
                report.missingDependencies.push_back(dep.targetId);
                if (!dep.isOptional) {
                    report.AddError("Dipendenza obbligatoria non rintracciabile o non committed: " + dep.targetId +
                                    (dep.revisionPolicy == DependencyRevisionPolicy::Exact ? " (rev_" + std::to_string(dep.requiredRevision) + ")" : " (active)"),
                                    AssetIntegrityErrorCode::MissingDependency);
                }
            } else {
                // Verifica digest SHA-256 atteso
                if (!dep.expectedDigestSha256.empty() && !targetDigest.empty()) {
                    if (dep.expectedDigestSha256 != targetDigest) {
                        report.digestMismatches.push_back(dep.targetId);
                        report.AddError("Digest SHA-256 della dipendenza " + dep.targetId +
                                        " alterato rispetto all'atteso. Atteso " + dep.expectedDigestSha256.substr(0, 16) +
                                        "..., effettivo " + targetDigest.substr(0, 16) + "...",
                                        AssetIntegrityErrorCode::DigestMismatch);
                    }
                }
            }
        }
    }

    return report;
}

DependencyGraphValidation AssetDependencyGraph::ValidatePackageDependencies(
    const RawAssetPackage& package,
    const std::string& rootDir,
    const AssetPackagingService& service,
    AssetTrustPolicy policy
) {
    DependencyGraphValidation report;
    report.isValid = true;

    for (const auto& dep : package.manifest.dependencies) {
        if (dep.targetId.empty()) {
            report.AddError("Dichiarata dipendenza con targetId vuoto.");
            continue;
        }

        if (dep.targetId == package.manifest.assetId) {
            report.AddError("Auto-dipendenza circolare non consentita: " + dep.targetId, AssetIntegrityErrorCode::UnsupportedSchema);
            continue;
        }

        if (dep.dependencyType == "texture") {
            // Mappe texture gestite separatamente con decode PNG
            continue;
        }

        std::string actualDigest;
        bool found = false;

        if (dep.revisionPolicy == DependencyRevisionPolicy::Exact) {
            if (service.IsRevisionCommitted(dep.targetId, dep.requiredRevision, rootDir, &actualDigest)) {
                found = true;
            }
        } else {
            auto activeRev = service.GetActiveRevision(dep.targetId, rootDir);
            if (activeRev.has_value() && service.IsRevisionCommitted(dep.targetId, *activeRev, rootDir, &actualDigest)) {
                found = true;
            }
        }

        if (!found) {
            report.missingDependencies.push_back(dep.targetId);
            if (!dep.isOptional) {
                report.AddError("Dipendenza obbligatoria mancante o non committed su disco: " + dep.targetId +
                                (dep.revisionPolicy == DependencyRevisionPolicy::Exact ? " rev_" + std::to_string(dep.requiredRevision) : " active"),
                                AssetIntegrityErrorCode::MissingDependency);
            }
        } else {
            if (!dep.expectedDigestSha256.empty() && !actualDigest.empty()) {
                if (dep.expectedDigestSha256 != actualDigest) {
                    report.digestMismatches.push_back(dep.targetId);
                    report.AddError("Digest SHA-256 della dipendenza " + dep.targetId + " non corrispondente.",
                                    AssetIntegrityErrorCode::DigestMismatch);
                }
            }
        }
    }

    return report;
}

AssetDependencyGraph AssetDependencyGraph::BuildFromCommitted(
    const std::string& rootDir,
    const AssetPackagingService& service
) {
    AssetDependencyGraph graph;
    std::filesystem::path pubRoot = std::filesystem::path(rootDir) / "published";
    std::error_code ec;

    if (!std::filesystem::exists(pubRoot, ec)) {
        return graph;
    }

    for (const auto& assetDir : std::filesystem::directory_iterator(pubRoot, ec)) {
        if (assetDir.is_directory(ec)) {
            std::string assetId = assetDir.path().filename().string();
            auto activeRev = service.GetActiveRevision(assetId, rootDir);
            if (!activeRev.has_value()) {
                activeRev = service.GetHighestCommittedRevision(assetId, rootDir);
            }

            if (activeRev.has_value()) {
                std::filesystem::path pkgFile = assetDir.path() / ("rev_" + std::to_string(*activeRev)) / "package.rawpkg";
                if (!std::filesystem::exists(pkgFile, ec)) {
                    pkgFile = assetDir.path() / "active.rawpkg";
                }

                if (std::filesystem::exists(pkgFile, ec)) {
                    RawAssetPackage pkg;
                    if (pkg.LoadFromFile(pkgFile.string())) {
                        graph.AddNode(
                            pkg.manifest.assetId,
                            pkg.manifest.revision,
                            pkg.contentDigestSha256,
                            pkg.manifest.dependencies,
                            true,
                            true,
                            AssetTrustPolicy::Strict,
                            AssetIntegrityStatus::Verified
                        );
                    }
                }
            }
        }
    }

    return graph;
}

// =============================================================================
// BM-FC4: DRAFT PRODUCTION PIPELINE DAG IMPLEMENTATION
// =============================================================================

void AssetDependencyGraph::InitializeStandardGraph() {
    m_pipelineNodes.clear();
    m_pipelineDownstream.clear();

    for (uint8_t i = 0; i < static_cast<uint8_t>(DependencyNodeID::Count); ++i) {
        DependencyNodeID id = static_cast<DependencyNodeID>(i);
        m_pipelineNodes[id] = { false, 0 };
    }

    m_pipelineDownstream[DependencyNodeID::ShapeGeometry] = {
        DependencyNodeID::TopologyUV,
        DependencyNodeID::SurfaceLayout,
        DependencyNodeID::RawPhysicsProxy,
        DependencyNodeID::QualityControlAudit
    };

    m_pipelineDownstream[DependencyNodeID::SculptGeometry] = {
        DependencyNodeID::TopologyUV,
        DependencyNodeID::SurfaceLayout,
        DependencyNodeID::RawPhysicsProxy,
        DependencyNodeID::QualityControlAudit
    };

    m_pipelineDownstream[DependencyNodeID::SurfaceLayout] = {
        DependencyNodeID::TextureResources,
        DependencyNodeID::PaintLayers
    };

    m_pipelineDownstream[DependencyNodeID::TextureResources] = {
        DependencyNodeID::MaterialParameters
    };

    m_pipelineDownstream[DependencyNodeID::PaintLayers] = {
        DependencyNodeID::QualityControlAudit,
        DependencyNodeID::PackageArtifact
    };

    m_pipelineDownstream[DependencyNodeID::MaterialParameters] = {
        DependencyNodeID::QualityControlAudit,
        DependencyNodeID::PackageArtifact
    };

    m_pipelineDownstream[DependencyNodeID::RawPhysicsProxy] = {
        DependencyNodeID::QualityControlAudit,
        DependencyNodeID::PackageArtifact
    };

    m_pipelineDownstream[DependencyNodeID::TopologyUV] = {
        DependencyNodeID::QualityControlAudit,
        DependencyNodeID::PackageArtifact
    };

    m_pipelineDownstream[DependencyNodeID::QualityControlAudit] = {
        DependencyNodeID::PackageArtifact
    };
}

bool AssetDependencyGraph::InvalidateNode(DependencyNodeID node, uint64_t fingerprint) {
    if (m_pipelineDownstream.empty()) {
        InitializeStandardGraph();
    }

    auto& st = m_pipelineNodes[node];
    if (!st.dirty && st.fingerprint == fingerprint && fingerprint != 0) {
        return false;
    }

    st.dirty = true;
    st.fingerprint = fingerprint;

    std::vector<DependencyNodeID> queue;
    auto it = m_pipelineDownstream.find(node);
    if (it != m_pipelineDownstream.end()) {
        queue = it->second;
    }

    std::unordered_set<DependencyNodeID> visited;
    for (auto d : queue) visited.insert(d);

    size_t qIdx = 0;
    while (qIdx < queue.size()) {
        DependencyNodeID curr = queue[qIdx++];
        m_pipelineNodes[curr].dirty = true;

        auto dit = m_pipelineDownstream.find(curr);
        if (dit != m_pipelineDownstream.end()) {
            for (auto nextDown : dit->second) {
                if (visited.insert(nextDown).second) {
                    queue.push_back(nextDown);
                }
            }
        }
    }

    return true;
}

void AssetDependencyGraph::MarkNodeClean(DependencyNodeID node, uint64_t fingerprint) {
    auto& st = m_pipelineNodes[node];
    st.dirty = false;
    st.fingerprint = fingerprint;
}

bool AssetDependencyGraph::IsNodeDirty(DependencyNodeID node) const {
    auto it = m_pipelineNodes.find(node);
    if (it != m_pipelineNodes.end()) {
        return it->second.dirty;
    }
    return true;
}

uint64_t AssetDependencyGraph::GetNodeFingerprint(DependencyNodeID node) const {
    auto it = m_pipelineNodes.find(node);
    if (it != m_pipelineNodes.end()) {
        return it->second.fingerprint;
    }
    return 0;
}

std::vector<DependencyNodeID> AssetDependencyGraph::GetEvaluationOrder() const {
    return {
        DependencyNodeID::ShapeGeometry,
        DependencyNodeID::SculptGeometry,
        DependencyNodeID::SurfaceLayout,
        DependencyNodeID::TextureResources,
        DependencyNodeID::PaintLayers,
        DependencyNodeID::MaterialParameters,
        DependencyNodeID::RawPhysicsProxy,
        DependencyNodeID::TopologyUV,
        DependencyNodeID::QualityControlAudit,
        DependencyNodeID::PackageArtifact
    };
}

} // namespace fw
