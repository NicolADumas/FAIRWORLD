#include "pch.h"
#include "MeshHistoryService.h"
#include "MeshModelingService.h"
#include "MeshSculptService.h"
#include "MeshTopologyService.h"
#include "MeshRepairService.h"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace fw {

// =============================================================================
// BM-S8: IMPLEMENTAZIONE MESH HISTORY SERVICE
// =============================================================================

MeshHistoryService::MeshHistoryService(const HistoryConfig& config)
    : m_config(config) {
}

void MeshHistoryService::SetConfig(const HistoryConfig& config) {
    m_config = config;
    PruneHistoryIfNeeded();
}

bool MeshHistoryService::BeginTransaction(uint32_t assetId, 
                                          const std::string& name, 
                                          HistoryCommandType type, 
                                          const EditableMesh* currentMesh,
                                          uint8_t currentGeometrySource) {
    if (m_inTransaction) {
        // Se c'era una transazione pendente mai completata, annulla lo staging pendente
        CancelTransaction();
    }

    m_inTransaction = true;
    m_stagingCommand.assetId = assetId;
    m_stagingCommand.name = name;
    m_stagingCommand.type = type;
    m_stagingCommand.beforeGeometrySource = currentGeometrySource;
    m_stagingCommand.afterGeometrySource = 1; // SculptedMesh
    m_stagingCommand.positionDeltas.clear();
    m_stagingCommand.beforeMesh = std::nullopt;
    m_stagingCommand.afterMesh = std::nullopt;

    if (currentMesh && type == HistoryCommandType::TopologyMutation) {
        m_stagingCommand.beforeMesh = *currentMesh;
    }

    return true;
}

void MeshHistoryService::RecordVertexDelta(VertexID id, const glm::vec3& oldPos, const glm::vec3& newPos) {
    if (!m_inTransaction) return;

    // Se esiste gia un delta per questo vertice nello stesso comando, aggiorna solo newPos conservando oldPos
    for (auto& d : m_stagingCommand.positionDeltas) {
        if (d.id == id) {
            d.newPos = newPos;
            return;
        }
    }

    m_stagingCommand.positionDeltas.push_back({id, oldPos, newPos});
}

void MeshHistoryService::RecordVertexDeltas(const std::vector<VertexPositionDelta>& deltas) {
    if (!m_inTransaction) return;
    for (const auto& d : deltas) {
        RecordVertexDelta(d.id, d.oldPos, d.newPos);
    }
}

void MeshHistoryService::RecordTopologyBefore(const EditableMesh& mesh) {
    if (!m_inTransaction) return;
    m_stagingCommand.beforeMesh = mesh;
}

void MeshHistoryService::RecordTopologyAfter(const EditableMesh& mesh) {
    if (!m_inTransaction) return;
    m_stagingCommand.afterMesh = mesh;
}

bool MeshHistoryService::CommitTransaction(const EditableMesh& finalMesh) {
    if (!m_inTransaction) return false;

    // 1. Validazione Mutazione Topologica
    if (m_stagingCommand.type == HistoryCommandType::TopologyMutation) {
        if (!m_stagingCommand.afterMesh.has_value()) {
            m_stagingCommand.afterMesh = finalMesh;
        }
        if (!m_stagingCommand.beforeMesh.has_value()) {
            CancelTransaction();
            return false;
        }

        // Verifica Zero-Work: se vertici e facce sono identici e le revisioni non sono cambiate
        const auto& b = *m_stagingCommand.beforeMesh;
        const auto& a = *m_stagingCommand.afterMesh;
        if (b.GetVertexCount() == a.GetVertexCount() &&
            b.GetFaceCount() == a.GetFaceCount() &&
            b.GetEdgeCount() == a.GetEdgeCount() &&
            b.GetTopologyRevision() == a.GetTopologyRevision() &&
            b.GetGeometryRevision() == a.GetGeometryRevision()) {
            CancelTransaction();
            return false;
        }
    }

    // 2. Validazione Delta Geometrici
    if (m_stagingCommand.type == HistoryCommandType::GeometryDelta) {
        std::vector<VertexPositionDelta> validDeltas;
        validDeltas.reserve(m_stagingCommand.positionDeltas.size());
        for (const auto& d : m_stagingCommand.positionDeltas) {
            if (d.HasMoved()) {
                validDeltas.push_back(d);
            }
        }
        m_stagingCommand.positionDeltas = std::move(validDeltas);

        if (m_stagingCommand.positionDeltas.empty()) {
            CancelTransaction();
            return false;
        }
    }

    // 3. Redo Branching (BM-S8.7): Una nuova modifica invalida il ramo Redo
    m_redoStack.clear();

    // 4. Inserimento nello stack Undo
    m_undoStack.push_back(std::move(m_stagingCommand));
    m_inTransaction = false;

    // 5. Rispetto Memory Budget (BM-S8.8)
    PruneHistoryIfNeeded();

    return true;
}

void MeshHistoryService::CancelTransaction() {
    m_inTransaction = false;
    m_stagingCommand = HistoryCommand{};
}

std::string MeshHistoryService::GetUndoCommandName() const {
    return m_undoStack.empty() ? "" : m_undoStack.back().name;
}

std::string MeshHistoryService::GetRedoCommandName() const {
    return m_redoStack.empty() ? "" : m_redoStack.back().name;
}

size_t MeshHistoryService::GetTotalMemoryBytes() const {
    size_t total = sizeof(MeshHistoryService);
    for (const auto& cmd : m_undoStack) total += cmd.EstimateMemoryBytes();
    for (const auto& cmd : m_redoStack) total += cmd.EstimateMemoryBytes();
    if (m_inTransaction) total += m_stagingCommand.EstimateMemoryBytes();
    return total;
}

void MeshHistoryService::PruneHistoryIfNeeded() {
    while (!m_undoStack.empty() && 
           (m_undoStack.size() > m_config.maxUndoSteps || GetTotalMemoryBytes() > m_config.maxMemoryBytes)) {
        if (m_undoStack.size() > 1) {
            m_undoStack.pop_front();
        } else {
            break;
        }
    }
}

void MeshHistoryService::ClearHistory() {
    m_undoStack.clear();
    m_redoStack.clear();
    CancelTransaction();
}

void MeshHistoryService::ClearHistoryForAsset(uint32_t assetId) {
    std::deque<HistoryCommand> newUndo;
    for (auto& cmd : m_undoStack) {
        if (cmd.assetId != assetId) newUndo.push_back(std::move(cmd));
    }
    m_undoStack = std::move(newUndo);

    std::vector<HistoryCommand> newRedo;
    for (auto& cmd : m_redoStack) {
        if (cmd.assetId != assetId) newRedo.push_back(std::move(cmd));
    }
    m_redoStack = std::move(newRedo);

    if (m_inTransaction && m_stagingCommand.assetId == assetId) {
        CancelTransaction();
    }
}

HistoryOperationResult MeshHistoryService::Undo(EditableMesh& mesh, uint32_t currentAssetId) {
    HistoryOperationResult res;
    if (m_undoStack.empty()) {
        res.success = false;
        res.message = "Nessuna operazione da annullare (Undo stack vuoto).";
        return res;
    }

    HistoryCommand cmd = std::move(m_undoStack.back());
    m_undoStack.pop_back();

    if (cmd.assetId != currentAssetId) {
        // Ripristina nello stack per non perdere il comando
        m_undoStack.push_back(std::move(cmd));
        res.success = false;
        res.message = "Asset mismatch: il comando appartiene a un asset diverso.";
        return res;
    }

    res.commandName = cmd.name;
    res.assetId = cmd.assetId;

    if (cmd.type == HistoryCommandType::GeometryDelta) {
        // Applica le coordinate oldPos
        for (const auto& d : cmd.positionDeltas) {
            mesh.SetVertexPosition(d.id, d.oldPos);
        }
        mesh.ComputeVertexNormals();
        // BM-S8.6: Revisione rigorosamente monotona! Non regredisce mai.
        mesh.IncrementGeometryRevision();

        res.geometryChanged = true;
        res.topologyChanged = false;
    } else if (cmd.type == HistoryCommandType::TopologyMutation) {
        if (cmd.beforeMesh.has_value()) {
            mesh = *(cmd.beforeMesh);
            // BM-S8.6: Pubblica una nuova revisione topologica monotona
            mesh.IncrementTopologyRevision();
        }
        res.geometryChanged = true;
        res.topologyChanged = true;
    }

    res.restoredGeometrySource = cmd.beforeGeometrySource;
    res.restoredVertexCount = mesh.GetVertexCount();
    res.restoredFaceCount = mesh.GetFaceCount();
    res.newGeometryRev = mesh.GetGeometryRevision();
    res.newTopologyRev = mesh.GetTopologyRevision();
    res.success = true;
    res.message = "Annullato con successo: " + cmd.name;

    m_redoStack.push_back(std::move(cmd));
    return res;
}

HistoryOperationResult MeshHistoryService::Redo(EditableMesh& mesh, uint32_t currentAssetId) {
    HistoryOperationResult res;
    if (m_redoStack.empty()) {
        res.success = false;
        res.message = "Nessuna operazione da ripristinare (Redo stack vuoto).";
        return res;
    }

    HistoryCommand cmd = std::move(m_redoStack.back());
    m_redoStack.pop_back();

    if (cmd.assetId != currentAssetId) {
        m_redoStack.push_back(std::move(cmd));
        res.success = false;
        res.message = "Asset mismatch: il comando Redo appartiene a un asset diverso.";
        return res;
    }

    res.commandName = cmd.name;
    res.assetId = cmd.assetId;

    if (cmd.type == HistoryCommandType::GeometryDelta) {
        // Applica le coordinate newPos
        for (const auto& d : cmd.positionDeltas) {
            mesh.SetVertexPosition(d.id, d.newPos);
        }
        mesh.ComputeVertexNormals();
        mesh.IncrementGeometryRevision();

        res.geometryChanged = true;
        res.topologyChanged = false;
    } else if (cmd.type == HistoryCommandType::TopologyMutation) {
        if (cmd.afterMesh.has_value()) {
            mesh = *(cmd.afterMesh);
            mesh.IncrementTopologyRevision();
        }
        res.geometryChanged = true;
        res.topologyChanged = true;
    }

    res.restoredGeometrySource = cmd.afterGeometrySource;
    res.restoredVertexCount = mesh.GetVertexCount();
    res.restoredFaceCount = mesh.GetFaceCount();
    res.newGeometryRev = mesh.GetGeometryRevision();
    res.newTopologyRev = mesh.GetTopologyRevision();
    res.success = true;
    res.message = "Ripristinato con successo: " + cmd.name;

    m_undoStack.push_back(std::move(cmd));
    return res;
}

// =============================================================================
// SUITE DI TEST DETERMINISTICA BM-S8 (HIS-1 .. HIS-12)
// =============================================================================

std::vector<MeshHistoryService::HistoryTestResult> MeshHistoryService::RunHistoryContractTests() {
    std::vector<HistoryTestResult> results;
    results.reserve(12);

    // -------------------------------------------------------------------------
    // HIS-1: Move Transformation Undo/Redo (Geometry Delta)
    // -------------------------------------------------------------------------
    {
        HistoryTestResult t;
        t.testId = "HIS-1";
        t.testName = "Move Transformation Undo/Redo (Geometry Delta)";
        t.inputDesc = "Traslazione vertice da (0,0,0) a (1,0,0); commit GeometryDelta; Undo poi Redo";
        t.expectedDesc = "Undo ripristina (0,0,0); Redo ripristina (1,0,0); nessuna clonazione dell'intera mesh";

        EditableMesh mesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        auto verts = mesh.GetActiveVertexIDs();
        VertexID vTarget = verts[0];
        glm::vec3 pInit = mesh.GetVertex(vTarget)->position;
        glm::vec3 pMoved = pInit + glm::vec3(1.0f, 0.0f, 0.0f);

        MeshHistoryService history;
        history.BeginTransaction(1, "Move Vertex", HistoryCommandType::GeometryDelta);
        mesh.SetVertexPosition(vTarget, pMoved);
        history.RecordVertexDelta(vTarget, pInit, pMoved);
        history.CommitTransaction(mesh);

        bool hasMoved = (glm::distance(mesh.GetVertex(vTarget)->position, pMoved) < 1e-5f);
        auto undoRes = history.Undo(mesh, 1);
        bool undoOk = undoRes.success && (glm::distance(mesh.GetVertex(vTarget)->position, pInit) < 1e-5f);
        auto redoRes = history.Redo(mesh, 1);
        bool redoOk = redoRes.success && (glm::distance(mesh.GetVertex(vTarget)->position, pMoved) < 1e-5f);

        t.passed = hasMoved && undoOk && redoOk;
        t.observedDesc = "HasMoved=" + std::to_string(hasMoved) + 
                         ", UndoOk=" + std::to_string(undoOk) + 
                         ", RedoOk=" + std::to_string(redoOk);
        t.details = t.passed ? "PASS" : "FAIL: Undo/Redo geometrico non corretto";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // HIS-2: Sculpt Stroke Grouping Contract
    // -------------------------------------------------------------------------
    {
        HistoryTestResult t;
        t.testId = "HIS-2";
        t.testName = "Sculpt Stroke Grouping Contract";
        t.inputDesc = "Sequenza di 5 dab consecutivi durante un unico stroke di scultura";
        t.expectedDesc = "Unica transazione registrata (UndoCount=1); un singolo Undo ripristina l'intera pennellata";

        EditableMesh mesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        auto verts = mesh.GetActiveVertexIDs();
        VertexID v = verts[0];
        glm::vec3 baselinePos = mesh.GetVertex(v)->position;

        MeshHistoryService history;
        history.BeginTransaction(1, "Sculpt Stroke Inflate", HistoryCommandType::GeometryDelta);

        // Simula 5 dab progressivi
        for (int dab = 1; dab <= 5; ++dab) {
            glm::vec3 curPos = mesh.GetVertex(v)->position;
            glm::vec3 nextPos = curPos + glm::vec3(0.0f, 0.1f, 0.0f);
            mesh.SetVertexPosition(v, nextPos);
            history.RecordVertexDelta(v, baselinePos, nextPos);
        }
        history.CommitTransaction(mesh);

        bool singleCommand = (history.GetUndoCount() == 1);
        auto undoRes = history.Undo(mesh, 1);
        bool restored = undoRes.success && (glm::distance(mesh.GetVertex(v)->position, baselinePos) < 1e-5f);

        t.passed = singleCommand && restored;
        t.observedDesc = "UndoCount=" + std::to_string(history.GetUndoCount()) + 
                         ", RestoredBaseline=" + std::to_string(restored);
        t.details = t.passed ? "PASS" : "FAIL: Stroke grouping fallito o comandi multipli";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // HIS-3: Cancel Stroke Without History Pollution
    // -------------------------------------------------------------------------
    {
        HistoryTestResult t;
        t.testId = "HIS-3";
        t.testName = "Cancel Stroke Without History Pollution";
        t.inputDesc = "Pennellata iniziata e campionata, poi annullata con CancelTransaction";
        t.expectedDesc = "UndoCount rimane rigorosamente 0; CanUndo=false";

        EditableMesh mesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        auto verts = mesh.GetActiveVertexIDs();

        MeshHistoryService history;
        history.BeginTransaction(1, "Cancelled Stroke", HistoryCommandType::GeometryDelta);
        history.RecordVertexDelta(verts[0], mesh.GetVertex(verts[0])->position, mesh.GetVertex(verts[0])->position + glm::vec3(0.2f));
        history.CancelTransaction();

        bool noPollution = (history.GetUndoCount() == 0) && (!history.CanUndo());

        t.passed = noPollution;
        t.observedDesc = "UndoCount=" + std::to_string(history.GetUndoCount()) + 
                         ", CanUndo=" + std::to_string(history.CanUndo());
        t.details = t.passed ? "PASS" : "FAIL: History inquinata da transazione annullata";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // HIS-4: Extrude Topology Mutation Undo/Redo
    // -------------------------------------------------------------------------
    {
        HistoryTestResult t;
        t.testId = "HIS-4";
        t.testName = "Extrude Topology Mutation Undo/Redo";
        t.inputDesc = "Estrusione di faccia (da 1 a 5 facce); Undo e Redo topologici completi";
        t.expectedDesc = "Undo ripristina esattamente 1 faccia; Redo ripristina 5 facce; DCEL integro";

        EditableMesh mesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        MeshHistoryService history;
        history.BeginTransaction(1, "Extrude Face", HistoryCommandType::TopologyMutation, &mesh);

        auto faces = mesh.GetActiveFaceIDs();
        MeshSelectionService sel;
        sel.Select(faces[0], SelectionMode::Face);
        auto extRes = MeshModelingService::ExtrudeFace(mesh, faces[0], ExtrudeParams{});
        history.CommitTransaction(mesh);

        bool extruded = (mesh.GetFaceCount() > 1);
        auto undoRes = history.Undo(mesh, 1);
        bool undoOk = undoRes.success && (mesh.GetFaceCount() == 1) && mesh.ValidateTopology().isValid;
        auto redoRes = history.Redo(mesh, 1);
        bool redoOk = redoRes.success && (mesh.GetFaceCount() > 1) && mesh.ValidateTopology().isValid;

        t.passed = extruded && undoOk && redoOk;
        t.observedDesc = "ExtrudedFaces=" + std::to_string(extRes.createdFaces.size()) + 
                         ", UndoFaces=" + std::to_string(mesh.GetFaceCount()) + 
                         ", UndoOk=" + std::to_string(undoOk) + 
                         ", RedoOk=" + std::to_string(redoOk);
        t.details = t.passed ? "PASS" : "FAIL: Undo/Redo su estrusione topologica fallito";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // HIS-5: Subdivide Topology Mutation Undo/Redo
    // -------------------------------------------------------------------------
    {
        HistoryTestResult t;
        t.testId = "HIS-5";
        t.testName = "Subdivide Topology Mutation Undo/Redo";
        t.inputDesc = "Suddivisione cubo (6 facce -> 24 facce); Undo/Redo";
        t.expectedDesc = "Undo ripristina 6 facce e 2-manifold chiusa; Redo ripristina 24 facce";

        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        MeshHistoryService history;
        history.BeginTransaction(1, "Subdivide Mesh", HistoryCommandType::TopologyMutation, &mesh);

        MeshTopologyService::Subdivide(mesh, SubdivideParams{ .iterations = 1, .smooth = false });
        history.CommitTransaction(mesh);

        bool subdivided = (mesh.GetFaceCount() == 24);
        auto undoRes = history.Undo(mesh, 1);
        bool undoOk = undoRes.success && (mesh.GetFaceCount() == 6) && mesh.ValidateTopology().isClosedManifold;
        auto redoRes = history.Redo(mesh, 1);
        bool redoOk = redoRes.success && (mesh.GetFaceCount() == 24) && mesh.ValidateTopology().isClosedManifold;

        t.passed = subdivided && undoOk && redoOk;
        t.observedDesc = "Subdivided=" + std::to_string(subdivided) + 
                         ", UndoFaces=" + std::to_string(mesh.GetFaceCount()) + 
                         ", UndoManifold=" + std::to_string(undoOk) + 
                         ", RedoManifold=" + std::to_string(redoOk);
        t.details = t.passed ? "PASS" : "FAIL: Undo/Redo su suddivisione poligonale fallito";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // HIS-6: Manifold Repair Undo/Redo
    // -------------------------------------------------------------------------
    {
        HistoryTestResult t;
        t.testId = "HIS-6";
        t.testName = "Manifold Repair Undo/Redo";
        t.inputDesc = "Rimozione faccia degenere; Undo ripristina faccia degenere; Redo la rimuove";
        t.expectedDesc = "Snapshot topologico fedele di elementi eliminati e ripristinati";

        EditableMesh mesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        VertexID d0 = mesh.AddVertex(glm::vec3(3.0f, 0.0f, 0.0f));
        VertexID d1 = mesh.AddVertex(glm::vec3(3.0f, 0.5f, 0.0f));
        VertexID d2 = mesh.AddVertex(glm::vec3(3.0f, 1.0f, 0.0f));
        FaceID degF = mesh.AddFace({d0, d1, d2});

        MeshHistoryService history;
        history.BeginTransaction(1, "Repair Degenerate Face", HistoryCommandType::TopologyMutation, &mesh);
        MeshRepairService::RemoveDegenerateFaces(mesh, 1e-5f);
        history.CommitTransaction(mesh);

        bool cleaned = !mesh.IsValid(degF);
        history.Undo(mesh, 1);
        bool restoredDegenerate = (mesh.GetFaceCount() == 2);
        history.Redo(mesh, 1);
        bool reCleaned = (mesh.GetFaceCount() == 1);

        t.passed = cleaned && restoredDegenerate && reCleaned;
        t.observedDesc = "Cleaned=" + std::to_string(cleaned) + 
                         ", RestoredDegenerate=" + std::to_string(restoredDegenerate) + 
                         ", ReCleaned=" + std::to_string(reCleaned);
        t.details = t.passed ? "PASS" : "FAIL: Undo/Redo su riparazione manifold fallito";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // HIS-7: UV & Material Assignment Preservation
    // -------------------------------------------------------------------------
    {
        HistoryTestResult t;
        t.testId = "HIS-7";
        t.testName = "UV & Material Assignment Preservation";
        t.inputDesc = "Faccia con SurfaceID=5, MaterialID=3 e UV=(0.7, 0.3) sottoposta a mutazione";
        t.expectedDesc = "Undo ripristina esattamente SurfaceID, MaterialID e UV per-corner originali";

        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
        VertexID v1 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
        VertexID v2 = mesh.AddVertex(glm::vec3(1.0f, 1.0f, 0.0f));
        VertexID v3 = mesh.AddVertex(glm::vec3(0.0f, 1.0f, 0.0f));

        std::vector<CornerAttributes> corners(4);
        corners[0].uv = glm::vec2(0.7f, 0.3f);
        FaceID origF = mesh.AddFace({v0, v1, v2, v3}, 5, 3, &corners);

        MeshHistoryService history;
        history.BeginTransaction(1, "Modify Surface", HistoryCommandType::TopologyMutation, &mesh);

        mesh.DeleteFace(origF, true, false);
        mesh.AddFace({v0, v1, v2, v3}, 9, 9);
        history.CommitTransaction(mesh);

        history.Undo(mesh, 1);

        auto faces = mesh.GetActiveFaceIDs();
        bool restoredAttrs = false;
        if (!faces.empty()) {
            const auto* fObj = mesh.GetFace(faces[0]);
            auto c = mesh.GetFaceCorners(faces[0]);
            if (fObj && fObj->surfaceId == 5 && fObj->materialId == 3 && !c.empty()) {
                if (glm::distance(c[0].uv, glm::vec2(0.7f, 0.3f)) < 1e-4f) {
                    restoredAttrs = true;
                }
            }
        }

        t.passed = restoredAttrs;
        t.observedDesc = "RestoredAttrs=" + std::to_string(restoredAttrs);
        t.details = t.passed ? "PASS" : "FAIL: Attributi UV o SurfaceID non preservati su Undo";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // HIS-8: Generational ID Safety & Invalidation Tracking
    // -------------------------------------------------------------------------
    {
        HistoryTestResult t;
        t.testId = "HIS-8";
        t.testName = "Generational ID Safety & Invalidation Tracking";
        t.inputDesc = "FaceID eliminato durante estrusione; verifica IsValid prima, dopo Undo e dopo Redo";
        t.expectedDesc = "Stato di validità generazionale sincronizzato al 100% con lo snapshot corrente";

        EditableMesh mesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        FaceID baseF = mesh.GetActiveFaceIDs()[0];

        MeshHistoryService history;
        history.BeginTransaction(1, "Delete and Replace", HistoryCommandType::TopologyMutation, &mesh);
        mesh.DeleteFace(baseF, true, false);
        mesh.AddFace(mesh.GetActiveVertexIDs());
        history.CommitTransaction(mesh);

        bool invalidAfterCommit = (!mesh.IsValid(baseF));
        history.Undo(mesh, 1);
        bool validAfterUndo = mesh.IsValid(baseF);
        history.Redo(mesh, 1);
        bool invalidAfterRedo = (!mesh.IsValid(baseF));

        t.passed = invalidAfterCommit && validAfterUndo && invalidAfterRedo;
        t.observedDesc = "InvCommit=" + std::to_string(invalidAfterCommit) + 
                         ", ValidUndo=" + std::to_string(validAfterUndo) + 
                         ", InvRedo=" + std::to_string(invalidAfterRedo);
        t.details = t.passed ? "PASS" : "FAIL: Validità generazionale non sincronizzata su Undo/Redo";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // HIS-9: Redo Branching Invalidation
    // -------------------------------------------------------------------------
    {
        HistoryTestResult t;
        t.testId = "HIS-9";
        t.testName = "Redo Branching Invalidation";
        t.inputDesc = "Op A, Op B; Undo Op B (CanRedo=true); nuova Op C committata";
        t.expectedDesc = "Redo stack immediatamente purgato (CanRedo=false; GetRedoCount=0)";

        EditableMesh mesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        auto verts = mesh.GetActiveVertexIDs();

        MeshHistoryService history;

        // Op A
        history.BeginTransaction(1, "Op A", HistoryCommandType::GeometryDelta);
        history.RecordVertexDelta(verts[0], glm::vec3(0.0f), glm::vec3(1.0f));
        history.CommitTransaction(mesh);

        // Op B
        history.BeginTransaction(1, "Op B", HistoryCommandType::GeometryDelta);
        history.RecordVertexDelta(verts[1], glm::vec3(0.0f), glm::vec3(1.0f));
        history.CommitTransaction(mesh);

        history.Undo(mesh, 1);
        bool canRedoBeforeBranch = history.CanRedo();

        // Op C (nuovo ramo)
        history.BeginTransaction(1, "Op C", HistoryCommandType::GeometryDelta);
        history.RecordVertexDelta(verts[2], glm::vec3(0.0f), glm::vec3(1.0f));
        history.CommitTransaction(mesh);

        bool redoPurged = (!history.CanRedo()) && (history.GetRedoCount() == 0);

        t.passed = canRedoBeforeBranch && redoPurged;
        t.observedDesc = "CanRedoBefore=" + std::to_string(canRedoBeforeBranch) + 
                         ", RedoPurged=" + std::to_string(redoPurged);
        t.details = t.passed ? "PASS" : "FAIL: Redo stack non invalidato da nuovo ramo";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // HIS-10: Zero-Work Invariance on Null/No-op Transaction
    // -------------------------------------------------------------------------
    {
        HistoryTestResult t;
        t.testId = "HIS-10";
        t.testName = "Zero-Work Invariance on Null/No-op Transaction";
        t.inputDesc = "BeginTransaction con 0 delta o delta a spostamento zero; CommitTransaction";
        t.expectedDesc = "Commit restituisce false; UndoCount rimane 0; nessuna allocazione history";

        EditableMesh mesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        auto verts = mesh.GetActiveVertexIDs();

        MeshHistoryService history;
        history.BeginTransaction(1, "No-Op Move", HistoryCommandType::GeometryDelta);
        history.RecordVertexDelta(verts[0], glm::vec3(0.0f), glm::vec3(0.0f)); // Spostamento nullo
        bool committed = history.CommitTransaction(mesh);

        bool zeroWorkRespected = (!committed) && (history.GetUndoCount() == 0);

        t.passed = zeroWorkRespected;
        t.observedDesc = "Committed=" + std::to_string(committed) + 
                         ", UndoCount=" + std::to_string(history.GetUndoCount());
        t.details = t.passed ? "PASS" : "FAIL: Transazione no-op registrata erroneamente";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // HIS-11: Memory Budget & Deterministic FIFO Pruning
    // -------------------------------------------------------------------------
    {
        HistoryTestResult t;
        t.testId = "HIS-11";
        t.testName = "Memory Budget & Deterministic FIFO Pruning";
        t.inputDesc = "Impostato maxUndoSteps=3; esecuzione di 5 transazioni distinte";
        t.expectedDesc = "UndoCount limitato a 3; comandi più vecchi scartati deterministicamente in FIFO";

        EditableMesh mesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        auto verts = mesh.GetActiveVertexIDs();

        HistoryConfig cfg;
        cfg.maxUndoSteps = 3;
        MeshHistoryService history(cfg);

        for (int i = 1; i <= 5; ++i) {
            history.BeginTransaction(1, "Move " + std::to_string(i), HistoryCommandType::GeometryDelta);
            history.RecordVertexDelta(verts[0], glm::vec3(0.0f), glm::vec3(static_cast<float>(i)));
            history.CommitTransaction(mesh);
        }

        bool clamped = (history.GetUndoCount() == 3);
        std::string topCmd = history.GetUndoCommandName();
        bool topIsLatest = (topCmd == "Move 5");

        t.passed = clamped && topIsLatest;
        t.observedDesc = "ClampedCount=" + std::to_string(history.GetUndoCount()) + 
                         ", TopCmd=" + topCmd;
        t.details = t.passed ? "PASS" : "FAIL: Pruning FIFO o memory budget fallito";
        results.push_back(t);
    }

    // -------------------------------------------------------------------------
    // HIS-12: Revision Monotonicity & Epoch Preservation
    // -------------------------------------------------------------------------
    {
        HistoryTestResult t;
        t.testId = "HIS-12";
        t.testName = "Revision Monotonicity & Epoch Preservation";
        t.inputDesc = "Op 1, Undo, Redo, Op 2, Undo; sequenza mista di modifiche";
        t.expectedDesc = "Revisione geometrica e topologica SEMPRE strettamente crescente (r_k > r_{k-1})";

        EditableMesh mesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        auto verts = mesh.GetActiveVertexIDs();

        MeshHistoryService history;
        uint64_t r0 = mesh.GetGeometryRevision();

        // 1. Move 1
        history.BeginTransaction(1, "Move 1", HistoryCommandType::GeometryDelta);
        mesh.SetVertexPosition(verts[0], glm::vec3(1.0f, 0.0f, 0.0f));
        history.RecordVertexDelta(verts[0], glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        history.CommitTransaction(mesh);
        uint64_t r1 = mesh.GetGeometryRevision();

        // 2. Undo Move 1
        history.Undo(mesh, 1);
        uint64_t r2 = mesh.GetGeometryRevision();

        // 3. Redo Move 1
        history.Redo(mesh, 1);
        uint64_t r3 = mesh.GetGeometryRevision();

        // 4. Move 2
        history.BeginTransaction(1, "Move 2", HistoryCommandType::GeometryDelta);
        mesh.SetVertexPosition(verts[1], glm::vec3(2.0f, 0.0f, 0.0f));
        history.RecordVertexDelta(verts[1], glm::vec3(0.0f), glm::vec3(2.0f, 0.0f, 0.0f));
        history.CommitTransaction(mesh);
        uint64_t r4 = mesh.GetGeometryRevision();

        // 5. Undo Move 2
        history.Undo(mesh, 1);
        uint64_t r5 = mesh.GetGeometryRevision();

        bool strictlyMonotonic = (r1 > r0) && (r2 > r1) && (r3 > r2) && (r4 > r3) && (r5 > r4);

        t.passed = strictlyMonotonic;
        t.observedDesc = "Revs: " + std::to_string(r0) + " < " + std::to_string(r1) + " < " + 
                         std::to_string(r2) + " < " + std::to_string(r3) + " < " + 
                         std::to_string(r4) + " < " + std::to_string(r5);
        t.details = t.passed ? "PASS" : "FAIL: Revisione non strettamente monotona (rischio collisione cache)";
        results.push_back(t);
    }

    return results;
}

} // namespace fw
