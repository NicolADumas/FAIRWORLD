#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <glm/glm.hpp>
#include "blocks/sculpt/EditableMesh.h"
#include "blocks/ShapeRegistry.h"
#include "blocks/uv/UVLayout.h"

namespace fw {

// =============================================================================
// OPZIONI DI GENERAZIONE PRIMITIVE UV (BM-UV2)
// =============================================================================

struct PrimitiveUVOptions {
    UVMappingStrategy strategy = UVMappingStrategy::Auto;
    bool normalizeTexelDensity = true;
    bool markSeamEdges = true;
    bool preventSeamWrapStretch = true;   // Corregge il salto U=1.0 -> U=0.0 alla cucitura
    bool separateCapIslands = true;       // Separa i tappi/basi in isole dedicate
    bool separateCavityIslands = true;    // Separa le cavità interne in isole dedicate
    float paddingPixels = 4.0f;
    uint32_t atlasTargetResolution = 1024;
};

// =============================================================================
// ESITO DI MAPPATURA PRIMITIVA (BM-UV2.9)
// =============================================================================

struct PrimitiveMappingResult {
    bool success = false;
    PrimitiveMappingStatus status = PrimitiveMappingStatus::SUCCESS;
    UVLayout layout;
    UVValidationReport validation;
    std::string diagnosticMessage;
};

// =============================================================================
// PRIMITIVE UV MAPPER (BM-UV2.3)
// Motore deterministico headless per generazione UV analitiche e parametriche
// =============================================================================

class PrimitiveUVMapper {
public:
    PrimitiveUVMapper() = default;
    ~PrimitiveUVMapper() = default;

    // --- Master Dispatcher & Analyzer ---
    // Analizza la mesh e la forma e genera il layout UV completo
    PrimitiveMappingResult AnalyzeAndMap(EditableMesh& mesh, 
                                        ShapeType type, 
                                        const ShapeParameters& params, 
                                        const PrimitiveUVOptions& options = {});

    // Mappatura automatica basata sulla sola geometria di EditableMesh (quando ShapeType e ignoto)
    PrimitiveMappingResult MapEditableMesh(EditableMesh& mesh, 
                                          const PrimitiveUVOptions& options = {});

    // Analizza le caratteristiche geometriche della primitiva
    UVMappingStrategy AnalyzePrimitive(const EditableMesh& mesh, 
                                       ShapeType shapeHint = ShapeType::None, 
                                       const ShapeParameters* params = nullptr);

    // --- Mappature Specializzate per Famiglia Geometrica ---

    // 1. Planare pura per proiezioni arbitrarie
    PrimitiveMappingResult GeneratePlanarMapping(EditableMesh& mesh, 
                                                UVLayout& outLayout, 
                                                const glm::vec3& planeNormal = glm::vec3(0, 1, 0),
                                                const PrimitiveUVOptions& options = {});

    // 2. Forme Poligonali: Cube, Cuboid, Parallelepiped, Ungula, ConvexHull
    PrimitiveMappingResult GenerateBoxMapping(EditableMesh& mesh, 
                                             UVLayout& outLayout, 
                                             const glm::vec3& size = glm::vec3(1.0f),
                                             const PrimitiveUVOptions& options = {});

    // 3. Forme Piramidali: Pyramid, PyramidFrustum
    PrimitiveMappingResult GeneratePyramidMapping(EditableMesh& mesh, 
                                                 UVLayout& outLayout, 
                                                 float width, float depth, float height,
                                                 const glm::vec2& apexOffset = glm::vec2(0.0f),
                                                 bool isFrustum = false,
                                                 float topW = 0.0f, float topD = 0.0f,
                                                 const PrimitiveUVOptions& options = {});

    // BM-SHAPES-01: Mappature Specializzate Nuove Primitive
    PrimitiveMappingResult GenerateWedgeMapping(EditableMesh& mesh, 
                                                UVLayout& outLayout, 
                                                const glm::vec3& size = glm::vec3(1.0f),
                                                const PrimitiveUVOptions& options = {});

    PrimitiveMappingResult GenerateTetrahedronMapping(EditableMesh& mesh, 
                                                      UVLayout& outLayout, 
                                                      float sideLength = 1.0f,
                                                      const PrimitiveUVOptions& options = {});

    PrimitiveMappingResult GenerateTorusMapping(EditableMesh& mesh, 
                                                UVLayout& outLayout, 
                                                float majorRadius = 0.75f, float minorRadius = 0.25f,
                                                const PrimitiveUVOptions& options = {});

    PrimitiveMappingResult GenerateEllipsoidMapping(EditableMesh& mesh, 
                                                    UVLayout& outLayout, 
                                                    const glm::vec3& radius = glm::vec3(1.0f),
                                                    const PrimitiveUVOptions& options = {});

    // 4. Forme Cilindriche e Coniche: Cylinder, HollowCylinder, SlicedCylinder, Barrel
    PrimitiveMappingResult GenerateCylindricalMapping(EditableMesh& mesh, 
                                                     UVLayout& outLayout, 
                                                     float radius, float height,
                                                     bool hasInnerWall = false, float innerRadius = 0.0f,
                                                     bool isSliced = false, const glm::vec3& cutNormal = glm::vec3(0, 1, 0),
                                                     const PrimitiveUVOptions& options = {});

    // 5. Forme Coniche: Cone, ConeFrustum
    PrimitiveMappingResult GenerateConicalMapping(EditableMesh& mesh, 
                                                 UVLayout& outLayout, 
                                                 float botRadius, float topRadius, float height,
                                                 const PrimitiveUVOptions& options = {});

    // 6. Forme Sferiche e Curve: Sphere, SphericalZone, Segment, Sector, Capsule, SuperSphere
    PrimitiveMappingResult GenerateSphericalMapping(EditableMesh& mesh, 
                                                   UVLayout& outLayout, 
                                                   float radius, 
                                                   UVMappingStrategy strat = UVMappingStrategy::Hemisphere,
                                                   float bottomY = -1.0f, float topY = 1.0f,
                                                   float thetaRange = glm::two_pi<float>(), 
                                                   float phiRange = glm::pi<float>(),
                                                   const PrimitiveUVOptions& options = {});

    // 7. Forme con Cavità: SphereWithCylindricalBore, SphereWithConicalCavities
    PrimitiveMappingResult GenerateCavityMapping(EditableMesh& mesh, 
                                                UVLayout& outLayout, 
                                                ShapeType type, 
                                                const ShapeParameters& params,
                                                const PrimitiveUVOptions& options = {});

    // 8. Forme Parametriche Generiche
    PrimitiveMappingResult GenerateParametricMapping(EditableMesh& mesh, 
                                                    UVLayout& outLayout, 
                                                    ShapeType type, 
                                                    const ShapeParameters& params,
                                                    const PrimitiveUVOptions& options = {});

    // --- Validazione e Diagnostica (BM-UV2.10) ---
    static bool ValidateGeneratedUV(const EditableMesh& mesh, 
                                    const UVLayout& layout, 
                                    UVValidationReport& report);

    // --- Gestione Seams (BM-UV2.8) ---
    static void MarkSeamEdge(EditableMesh& mesh, EdgeID edgeId, float weight = 1.0f);
    static void ClearDerivedSeams(EditableMesh& mesh);

private:
    // Helper interni di calcolo metriche
    static float ComputeTriangleArea2D(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c);
    static IslandDistortion EvaluateFaceDistortion(const EditableMesh& mesh, FaceID f);
    static void CorrectSeamWrapQuad(glm::vec2& uv0, glm::vec2& uv1, glm::vec2& uv2, glm::vec2& uv3);
};

} // namespace fw
