#pragma once

#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <optional>
#include <limits>
#include <cmath>
#include <glm/glm.hpp>
#include "EditableMesh.h"

namespace fw {

// =============================================================================
// BM-S5: SCULPT BRUSH ENGINE — DATA CONTRACT & PARAMETERS
// =============================================================================

enum class SculptBrushType : uint8_t {
    Grab = 0,    // Trascinamento locale da punto di presa iniziale
    Smooth = 1,  // Levigatura / rilassamento verso il baricentro dei vicini 1-ring
    Inflate = 2, // Espansione / sgonfiamento lungo le normali
    Flatten = 3, // Appiattimento su piano tangente della superficie
    Clay = 4     // Accumulo progressivo di materiale con tetto massimo controllato
};

enum class BrushFalloffType : uint8_t {
    Smooth = 0,   // Hermite smoothstep: 3t^2 - 2t^3
    Linear = 1,   // 1 - t
    Sphere = 2,   // sqrt(1 - t^2)
    Sharp = 3,    // (1 - t)^3
    Constant = 4  // 1.0 ovunque nel raggio
};

enum class SymmetryPlane : uint8_t {
    None = 0,
    X = 1, // Specchia rispetto a X=0 (x -> -x)
    Y = 2, // Specchia rispetto a Y=0 (y -> -y)
    Z = 3  // Specchia rispetto a Z=0 (z -> -z)
};

// Parametri di configurazione del pennello
struct SculptBrushSettings {
    SculptBrushType brushType = SculptBrushType::Inflate;
    float radius = 0.25f;
    float strength = 0.5f;
    BrushFalloffType falloff = BrushFalloffType::Smooth;
    float spacing = 0.1f; // Frazione del raggio per distanziare i campionamenti dello stroke
    SymmetryPlane symmetry = SymmetryPlane::None;
    float clayCeiling = 0.2f; // Altezza massima accumulo clay
};

// Campionamento del tratto (stroke sample)
struct StrokePoint {
    glm::vec3 hitPoint{0.0f};      // Punto di impatto in object-space
    glm::vec3 surfaceNormal{0.0f, 1.0f, 0.0f}; // Normale della superficie al punto di impatto
    glm::vec3 strokeDirection{0.0f}; // Direzione corrente del drag
    FaceID hitFace = INVALID_FACE_ID;
};

// Risultato di un'operazione o di uno stroke completato
struct SculptStrokeResult {
    bool success = false;
    std::string errorMessage;
    std::vector<VertexID> modifiedVertices;

    // Delta di posizione vertici per il futuro History Engine BM-S8
    struct VertexPositionDelta {
        VertexID id;
        glm::vec3 oldPos{0.0f};
        glm::vec3 newPos{0.0f};
    };
    std::vector<VertexPositionDelta> positionDeltas;

    uint64_t oldGeometryRev = 0;
    uint64_t newGeometryRev = 0;
    bool geometryChanged = false;
};

// =============================================================================
// SPATIAL ACCELERATION INDEX (BM-S5.4)
// Griglia spaziale a celle uniformi per query di sfera O(k) veloci
// =============================================================================
class SpatialGridIndex {
public:
    SpatialGridIndex() = default;
    ~SpatialGridIndex() = default;

    void Build(const EditableMesh& mesh, float cellSize);
    std::vector<VertexID> QuerySphere(const glm::vec3& center, float radius, const EditableMesh& mesh) const;
    void Clear();
    bool IsBuilt() const { return m_built; }

    // Refit selettivo O(m) per i vertici modificati, senza rebuild globale
    void UpdateVertexPositions(const EditableMesh& mesh, const std::vector<VertexID>& modifiedVertices);
    bool ContainsVertexInCell(VertexID vid, const glm::vec3& pos) const;
    size_t GetCellCount() const { return m_cells.size(); }
    float GetCellSize() const { return m_cellSize; }

    struct GridKey {
        int x, y, z;
        bool operator==(const GridKey& o) const noexcept {
            return x == o.x && y == o.y && z == o.z;
        }
    };
    struct GridKeyHash {
        size_t operator()(const GridKey& k) const noexcept {
            return std::hash<int>()(k.x) ^ (std::hash<int>()(k.y) << 1) ^ (std::hash<int>()(k.z) << 2);
        }
    };

    GridKey ComputeKey(const glm::vec3& pos) const {
        return {
            static_cast<int>(std::floor(pos.x / m_cellSize)),
            static_cast<int>(std::floor(pos.y / m_cellSize)),
            static_cast<int>(std::floor(pos.z / m_cellSize))
        };
    }

private:
    float m_cellSize = 0.25f;
    bool m_built = false;
    std::unordered_map<GridKey, std::vector<VertexID>, GridKeyHash> m_cells;
    std::unordered_map<VertexID, GridKey> m_vertexCellMap;
};

// =============================================================================
// MESH SCULPT SERVICE (BM-S5)
// Motore di scultura deterministico CPU-authoritative
// =============================================================================
class MeshSculptService {
public:
    MeshSculptService();
    ~MeshSculptService() = default;

    // Configurazione pennello attiva
    void SetSettings(const SculptBrushSettings& settings) { m_settings = settings; }
    const SculptBrushSettings& GetSettings() const { return m_settings; }
    SculptBrushSettings& GetSettingsMutable() { return m_settings; }

    const SpatialGridIndex& GetSpatialIndex() const { return m_spatialIndex; }

    // --- Transazioni dello Stroke (BM-S5.6) ---
    bool BeginStroke(EditableMesh& mesh, const StrokePoint& startPoint);
    bool UpdateStroke(EditableMesh& mesh, const StrokePoint& currentPoint);
    SculptStrokeResult EndStroke(EditableMesh& mesh);
    void CancelStroke(EditableMesh& mesh);

    bool IsInStroke() const { return m_inStroke; }

    // Applicazione atomica immediata di un singolo dab
    bool ApplyBrushDab(EditableMesh& mesh, 
                       const StrokePoint& point, 
                       const SculptBrushSettings& settings, 
                       std::vector<VertexID>* outModifiedVertices = nullptr);

    // Valutazione matematica della curva di falloff
    static float EvaluateFalloff(float normalizedDist, BrushFalloffType type);

    // --- Suite di Validazione Deterministica BM-S5 (SCL-1 .. SCL-12) ---
    struct SculptTestResult {
        std::string testId;
        std::string testName;
        std::string inputDesc;
        std::string expectedDesc;
        std::string observedDesc;
        bool passed = false;
        std::string details;
    };
    static std::vector<SculptTestResult> RunSculptContractTests();

private:
    SculptBrushSettings m_settings;
    SpatialGridIndex m_spatialIndex;

    bool m_inStroke = false;
    StrokePoint m_strokeStartPoint;
    StrokePoint m_lastDabPoint;
    bool m_hasLastDab = false;

    // Snapshot posizioni iniziali pre-stroke per Grab e CancelStroke
    std::unordered_map<VertexID, glm::vec3> m_strokeBaselinePositions;
    std::unordered_set<VertexID> m_strokeModifiedVertices;
    std::vector<VertexID> m_grabCandidates;
    std::vector<VertexID> m_symGrabCandidates;
    uint64_t m_strokeInitialGeoRev = 0;
    uint64_t m_strokeInitialTopoRev = 0;

    void ApplyDeformationInternal(EditableMesh& mesh, 
                                  const StrokePoint& point, 
                                  const SculptBrushSettings& settings,
                                  const std::vector<VertexID>& candidateVerts);
};

} // namespace fw
