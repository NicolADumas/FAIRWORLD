#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cstdint>
#include <functional>
#include "ProductionStationContract.h"

namespace fw {

// Nodi tipizzati del grafo di dipendenza
enum class DependencyNodeID : uint8_t {
    ShapeGeometry = 0,       // Mesh / geometria poligonale pura
    TopologyUV = 1,          // Parametrizzazione e coordinate UV
    SurfaceLayout = 2,       // Identificazione facce e partizioni semantiche
    TextureResources = 3,    // File texture e binding GPU
    MaterialParameters = 4,  // Parametri PBR e fallbacks
    RawPhysicsProxy = 5,     // Collider proxy e parametri di massa
    QualityControlAudit = 6, // Report di conformità e validazione
    PackageArtifact = 7      // Pacchetto finale congelato
};

inline const char* GetDependencyNodeName(DependencyNodeID id) {
    switch (id) {
        case DependencyNodeID::ShapeGeometry:       return "ShapeGeometry";
        case DependencyNodeID::TopologyUV:          return "TopologyUV";
        case DependencyNodeID::SurfaceLayout:       return "SurfaceLayout";
        case DependencyNodeID::TextureResources:    return "TextureResources";
        case DependencyNodeID::MaterialParameters:  return "MaterialParameters";
        case DependencyNodeID::RawPhysicsProxy:     return "RawPhysicsProxy";
        case DependencyNodeID::QualityControlAudit: return "QualityControlAudit";
        case DependencyNodeID::PackageArtifact:     return "PackageArtifact";
        default:                                    return "UnknownNode";
    }
}

// =============================================================================
// ASSET DEPENDENCY GRAPH (BM-FC4)
// Grafo diretto e aciclico (DAG) con invalidazione selettiva e fingerprinting
// =============================================================================
class AssetDependencyGraph {
public:
    struct DependencyEdge {
        DependencyNodeID source; // Nodo a monte (prerequisito)
        bool isOptional = false; // Se opzionale, un'invalidazione non blocca l'esecuzione
    };

    struct NodeInfo {
        DependencyNodeID id;
        ProductionStationID station;
        std::vector<DependencyEdge> upstreamInputs;  // Nodi da cui dipende
        std::vector<DependencyNodeID> downstreamOutputs; // Nodi che dipendono da questo

        bool isDirty = true;
        uint64_t lastEvaluatedFingerprint = 0;
        uint64_t nodeRevision = 1;
    };

    AssetDependencyGraph() {
        InitializeStandardGraph();
    }

    // Configura la topologia canonica della pipeline di fabbrica
    void InitializeStandardGraph() {
        m_nodes.clear();

        // 1. Registrazione Nodi
        RegisterNode(DependencyNodeID::ShapeGeometry, ProductionStationID::Shape);
        RegisterNode(DependencyNodeID::SurfaceLayout, ProductionStationID::SurfaceUV);
        RegisterNode(DependencyNodeID::TopologyUV, ProductionStationID::SurfaceUV);
        RegisterNode(DependencyNodeID::TextureResources, ProductionStationID::Texture);
        RegisterNode(DependencyNodeID::MaterialParameters, ProductionStationID::Material);
        RegisterNode(DependencyNodeID::RawPhysicsProxy, ProductionStationID::RawPhysics);
        RegisterNode(DependencyNodeID::QualityControlAudit, ProductionStationID::QualityControl);
        RegisterNode(DependencyNodeID::PackageArtifact, ProductionStationID::Packaging);

        // 2. Definizione archi diretti di dipendenza (Upstream -> Downstream)
        // ShapeGeometry -> TopologyUV (le coordinate UV dipendono dalla geometria)
        AddDependency(DependencyNodeID::TopologyUV, DependencyNodeID::ShapeGeometry, false);

        // ShapeGeometry -> SurfaceLayout (la partizione semantica dipende dalla forma)
        AddDependency(DependencyNodeID::SurfaceLayout, DependencyNodeID::ShapeGeometry, false);

        // ShapeGeometry -> RawPhysicsProxy (il volume/bounds determina la massa derivata)
        AddDependency(DependencyNodeID::RawPhysicsProxy, DependencyNodeID::ShapeGeometry, false);

        // SurfaceLayout -> MaterialParameters (l'override dei materiali si ancora alle superfici)
        AddDependency(DependencyNodeID::MaterialParameters, DependencyNodeID::SurfaceLayout, false);

        // TextureResources -> MaterialParameters (i layer PBR consumano le texture opzionali)
        AddDependency(DependencyNodeID::MaterialParameters, DependencyNodeID::TextureResources, true);

        // Tutti i componenti a monte convergono nel QualityControlAudit
        AddDependency(DependencyNodeID::QualityControlAudit, DependencyNodeID::ShapeGeometry, false);
        AddDependency(DependencyNodeID::QualityControlAudit, DependencyNodeID::TopologyUV, false);
        AddDependency(DependencyNodeID::QualityControlAudit, DependencyNodeID::SurfaceLayout, false);
        AddDependency(DependencyNodeID::QualityControlAudit, DependencyNodeID::MaterialParameters, false);
        AddDependency(DependencyNodeID::QualityControlAudit, DependencyNodeID::RawPhysicsProxy, false);

        // QualityControlAudit -> PackageArtifact (solo se audit passa, si genera il Package)
        AddDependency(DependencyNodeID::PackageArtifact, DependencyNodeID::QualityControlAudit, false);
    }

    void RegisterNode(DependencyNodeID id, ProductionStationID station) {
        NodeInfo node;
        node.id = id;
        node.station = station;
        node.isDirty = true;
        m_nodes[id] = node;
    }

    void AddDependency(DependencyNodeID downstream, DependencyNodeID upstream, bool isOptional) {
        if (m_nodes.find(downstream) != m_nodes.end() && m_nodes.find(upstream) != m_nodes.end()) {
            m_nodes[downstream].upstreamInputs.push_back({upstream, isOptional});
            m_nodes[upstream].downstreamOutputs.push_back(downstream);
        }
    }

    // Invalida selettivamente un nodo e propaga lo stato dirty ESCLUSIVAMENTE verso il basso
    bool InvalidateNode(DependencyNodeID id, uint64_t newFingerprint) {
        auto it = m_nodes.find(id);
        if (it == m_nodes.end()) return false;

        // Invariante Zero-Work: se il fingerprint di input è identico, nessun ricalcolo
        if (it->second.lastEvaluatedFingerprint == newFingerprint && !it->second.isDirty) {
            return false;
        }

        it->second.isDirty = true;
        it->second.lastEvaluatedFingerprint = newFingerprint;
        it->second.nodeRevision++;

        // Propaga ricorsivamente solo ai nodi a valle
        std::unordered_set<DependencyNodeID> visited;
        PropagateDirtyDownstream(id, visited);
        return true;
    }

    // Contrassegna un nodo come calcolato e pulito
    void MarkNodeClean(DependencyNodeID id, uint64_t evaluatedFingerprint) {
        auto it = m_nodes.find(id);
        if (it != m_nodes.end()) {
            it->second.isDirty = false;
            it->second.lastEvaluatedFingerprint = evaluatedFingerprint;
        }
    }

    bool IsNodeDirty(DependencyNodeID id) const {
        auto it = m_nodes.find(id);
        return (it != m_nodes.end()) ? it->second.isDirty : false;
    }

    uint64_t GetNodeRevision(DependencyNodeID id) const {
        auto it = m_nodes.find(id);
        return (it != m_nodes.end()) ? it->second.nodeRevision : 0;
    }

    // Restituisce l'ordine topologico di esecuzione dei soli nodi dirty
    std::vector<DependencyNodeID> GetEvaluationOrder() const {
        std::vector<DependencyNodeID> order;
        std::unordered_set<DependencyNodeID> visited;
        std::unordered_set<DependencyNodeID> inStack;

        std::function<void(DependencyNodeID)> dfs = [&](DependencyNodeID current) {
            if (visited.count(current)) return;
            inStack.insert(current);

            auto it = m_nodes.find(current);
            if (it != m_nodes.end()) {
                for (const auto& edge : it->second.upstreamInputs) {
                    dfs(edge.source);
                }
            }

            inStack.erase(current);
            visited.insert(current);
            order.push_back(current);
        };

        for (const auto& pair : m_nodes) {
            if (!visited.count(pair.first)) {
                dfs(pair.first);
            }
        }

        // Filtra lasciando solo i nodi che risultano effettivamente sporchi
        std::vector<DependencyNodeID> dirtyOrder;
        for (auto id : order) {
            if (IsNodeDirty(id)) {
                dirtyOrder.push_back(id);
            }
        }
        return dirtyOrder;
    }

    // Verifica se tutti i prerequisiti obbligatori di un nodo sono puliti
    bool ArePrerequisitesSatisfied(DependencyNodeID id) const {
        auto it = m_nodes.find(id);
        if (it == m_nodes.end()) return false;

        for (const auto& edge : it->second.upstreamInputs) {
            if (!edge.isOptional && IsNodeDirty(edge.source)) {
                return false;
            }
        }
        return true;
    }

    const NodeInfo* GetNodeInfo(DependencyNodeID id) const {
        auto it = m_nodes.find(id);
        return (it != m_nodes.end()) ? &it->second : nullptr;
    }

private:
    void PropagateDirtyDownstream(DependencyNodeID current, std::unordered_set<DependencyNodeID>& visited) {
        if (visited.count(current)) return;
        visited.insert(current);

        auto it = m_nodes.find(current);
        if (it == m_nodes.end()) return;

        for (auto downstreamId : it->second.downstreamOutputs) {
            auto dIt = m_nodes.find(downstreamId);
            if (dIt != m_nodes.end()) {
                dIt->second.isDirty = true;
                dIt->second.nodeRevision++;
                PropagateDirtyDownstream(downstreamId, visited);
            }
        }
    }

    std::unordered_map<DependencyNodeID, NodeInfo> m_nodes;
};

} // namespace fw
