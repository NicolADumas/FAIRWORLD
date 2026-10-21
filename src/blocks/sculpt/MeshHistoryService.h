#pragma once

#include <vector>
#include <deque>
#include <string>
#include <memory>
#include <optional>
#include <cstdint>
#include <glm/glm.hpp>
#include "EditableMesh.h"

namespace fw {

// =============================================================================
// BM-S8: HISTORY & UNDO/REDO ENGINE CONTRACT
// =============================================================================

enum class HistoryCommandType : uint8_t {
    GeometryDelta = 0,    // Modifiche puramente geometriche (Move, Rotate, Scale, Sculpt dab/stroke, Proportional Edit)
    TopologyMutation = 1, // Mutazioni topologiche (Extrude, Inset, Bevel, Subdivide, LoopCut, Mirror, Repair, Remesh)
    Custom = 2
};

// Delta di coordinate per singolo vertice (memorizzazione compatta per trasformazioni/sculpt)
struct VertexPositionDelta {
    VertexID id = INVALID_VERTEX_ID;
    glm::vec3 oldPos{0.0f};
    glm::vec3 newPos{0.0f};

    bool HasMoved(float eps = 1e-6f) const {
        glm::vec3 diff = newPos - oldPos;
        return glm::dot(diff, diff) > (eps * eps);
    }
};

// Comando atomico registrato nella cronologia
struct HistoryCommand {
    uint32_t assetId = 0;              // Identità dell'asset RawAssetDraft proprietario
    std::string name;                  // Nome user-facing (es. "Move (4 vertici)", "Sculpt Inflate", "Extrude Face")
    HistoryCommandType type = HistoryCommandType::GeometryDelta;
    uint64_t timestamp = 0;
    uint8_t beforeGeometrySource = 0;  // 0 = Primitive, 1 = SculptedMesh
    uint8_t afterGeometrySource = 1;   // 0 = Primitive, 1 = SculptedMesh

    // 1. Dati per modifiche solo geometriche
    std::vector<VertexPositionDelta> positionDeltas;

    // 2. Dati per mutazioni topologiche (Snapshot prima e dopo)
    std::optional<EditableMesh> beforeMesh;
    std::optional<EditableMesh> afterMesh;

    size_t EstimateMemoryBytes() const {
        size_t bytes = sizeof(HistoryCommand);
        bytes += positionDeltas.size() * sizeof(VertexPositionDelta);
        if (beforeMesh.has_value()) {
            bytes += sizeof(EditableMesh) + 
                     beforeMesh->GetVertexCount() * sizeof(MeshVertex) +
                     beforeMesh->GetHalfEdgeCount() * sizeof(MeshHalfEdge) +
                     beforeMesh->GetEdgeCount() * sizeof(MeshEdge) +
                     beforeMesh->GetFaceCount() * sizeof(MeshFace);
        }
        if (afterMesh.has_value()) {
            bytes += sizeof(EditableMesh) + 
                     afterMesh->GetVertexCount() * sizeof(MeshVertex) +
                     afterMesh->GetHalfEdgeCount() * sizeof(MeshHalfEdge) +
                     afterMesh->GetEdgeCount() * sizeof(MeshEdge) +
                     afterMesh->GetFaceCount() * sizeof(MeshFace);
        }
        return bytes;
    }
};

// Risultato dell'esecuzione di Undo o Redo
struct HistoryOperationResult {
    bool success = false;
    std::string message;
    bool topologyChanged = false;
    bool geometryChanged = false;
    uint32_t assetId = 0;
    std::string commandName;
    uint64_t newTopologyRev = 0;
    uint64_t newGeometryRev = 0;
    uint8_t restoredGeometrySource = 1;
    size_t restoredVertexCount = 0;
    size_t restoredFaceCount = 0;
};

// Configurazione budget memoria e capienza
struct HistoryConfig {
    size_t maxUndoSteps = 32;
    size_t maxMemoryBytes = 64 * 1024 * 1024; // 64 MB budget default
};

// =============================================================================
// MESH HISTORY SERVICE (BM-S8)
// Servizio transazionale headless indipendente da UI, ImGui e Vulkan
// =============================================================================
class MeshHistoryService {
public:
    explicit MeshHistoryService(const HistoryConfig& config = HistoryConfig{});
    ~MeshHistoryService() = default;

    // --- Configurazione Budget ---
    void SetConfig(const HistoryConfig& config);
    const HistoryConfig& GetConfig() const { return m_config; }

    // --- Gestione Transazionale ---
    bool BeginTransaction(uint32_t assetId, 
                          const std::string& name, 
                          HistoryCommandType type, 
                          const EditableMesh* currentMesh = nullptr,
                          uint8_t currentGeometrySource = 1);

    void RecordVertexDelta(VertexID id, const glm::vec3& oldPos, const glm::vec3& newPos);
    void RecordVertexDeltas(const std::vector<VertexPositionDelta>& deltas);
    void RecordTopologyBefore(const EditableMesh& mesh);
    void RecordTopologyAfter(const EditableMesh& mesh);

    bool CommitTransaction(const EditableMesh& finalMesh);
    void CancelTransaction();
    bool IsInTransaction() const { return m_inTransaction; }

    // --- Operazioni Undo / Redo ---
    bool CanUndo() const { return !m_undoStack.empty(); }
    bool CanRedo() const { return !m_redoStack.empty(); }

    std::string GetUndoCommandName() const;
    std::string GetRedoCommandName() const;

    size_t GetUndoCount() const { return m_undoStack.size(); }
    size_t GetRedoCount() const { return m_redoStack.size(); }
    size_t GetTotalMemoryBytes() const;

    HistoryOperationResult Undo(EditableMesh& mesh, uint32_t currentAssetId);
    HistoryOperationResult Redo(EditableMesh& mesh, uint32_t currentAssetId);

    void ClearHistory();
    void ClearHistoryForAsset(uint32_t assetId);

    // --- Suite di Validazione Deterministica BM-S8 (HIS-1 .. HIS-12) ---
    struct HistoryTestResult {
        std::string testId;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        bool passed = false;
        std::string details;
    };
    static std::vector<HistoryTestResult> RunHistoryContractTests();

private:
    void PruneHistoryIfNeeded();

    HistoryConfig m_config;
    std::deque<HistoryCommand> m_undoStack;
    std::vector<HistoryCommand> m_redoStack;

    // Staging transazione corrente
    bool m_inTransaction = false;
    HistoryCommand m_stagingCommand;
};

} // namespace fw
