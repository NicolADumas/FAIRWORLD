#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <variant>

namespace fw {

// Persistent asset identity
using ShapeID = uint32_t;

// Definition revision
using ShapeRevision = uint32_t;

// Runtime registry reference
struct ShapeHandle {
    ShapeID id = 0;
    ShapeRevision revision = 0;
    
    bool IsValid() const { return id != 0; }
    
    bool operator==(const ShapeHandle& other) const {
        return id == other.id && revision == other.revision;
    }
};

// Complete mandatory shape catalog
enum class ShapeType : uint8_t {
    None = 0,
    Cube,
    Cuboid,
    Parallelepiped,
    Pyramid,
    PyramidFrustum,
    Cylinder,
    HollowCylinder,
    Cone,
    ConeFrustum,
    Sphere,
    SphericalZone,
    SphericalSegment,
    SphericalSector,
    SphereWithCylindricalBore,
    SphereWithConicalCavities,
    SlicedCylinder,
    Ungula,
    Barrel,
    Capsule,
    ConvexHull,
    Compound,
    TriangleMesh,
    LegacySuperSphere,
    Heightfield,
    SignedDistanceField,

    // --- BM-SHAPES MASTER CATALOG ---
    // A — STRUCTURAL PRIMITIVES
    Wedge = 26,
    Ramp = 27,
    ChamferedBox = 28,
    RoundedBox = 29,
    HexagonalPrism = 30,

    // B — ARCHITECTURAL SHAPES
    Arch = 31,
    Vault = 32,
    Dome = 33,
    Hemisphere = 34,
    HollowDome = 35,

    // C — TOROIDAL SHAPES
    Torus = 36,
    HalfTorus = 37,
    TorusArc = 38,
    EllipticalTorus = 39,
    TwistedTorus = 40,

    // D — POLYHEDRAL SHAPES
    Tetrahedron = 41,
    Octahedron = 42,
    Dodecahedron = 43,
    Icosahedron = 44,
    TruncatedCube = 45,

    // E — ADVANCED SHAPES
    Ellipsoid = 46,
    Superellipsoid = 47,
    StarPrism = 48,
    TwistedPrism = 49,
    Helix = 50
};

// Parameter structures
struct ShapeParamsCuboid {
    glm::vec3 size;
    bool operator==(const ShapeParamsCuboid& o) const { return size == o.size; }
};
struct ShapeParamsParallelepiped {
    glm::vec3 basisX, basisY, basisZ;
    bool operator==(const ShapeParamsParallelepiped& o) const { return basisX == o.basisX && basisY == o.basisY && basisZ == o.basisZ; }
};
struct ShapeParamsPyramid {
    float baseWidth, baseDepth, height; glm::vec2 apexOffset;
    bool operator==(const ShapeParamsPyramid& o) const { return baseWidth == o.baseWidth && baseDepth == o.baseDepth && height == o.height && apexOffset == o.apexOffset; }
};
struct ShapeParamsPyramidFrustum {
    float bottomWidth, bottomDepth, topWidth, topDepth, height; glm::vec2 apexOffset;
    bool operator==(const ShapeParamsPyramidFrustum& o) const { return bottomWidth == o.bottomWidth && bottomDepth == o.bottomDepth && topWidth == o.topWidth && topDepth == o.topDepth && height == o.height && apexOffset == o.apexOffset; }
};
struct ShapeParamsCylinder {
    float radius, height;
    bool operator==(const ShapeParamsCylinder& o) const { return radius == o.radius && height == o.height; }
};
struct ShapeParamsHollowCylinder {
    float outerRadius, innerRadius, height;
    bool operator==(const ShapeParamsHollowCylinder& o) const { return outerRadius == o.outerRadius && innerRadius == o.innerRadius && height == o.height; }
};
struct ShapeParamsCone {
    float radius, height;
    bool operator==(const ShapeParamsCone& o) const { return radius == o.radius && height == o.height; }
};
struct ShapeParamsConeFrustum {
    float bottomRadius, topRadius, height;
    bool operator==(const ShapeParamsConeFrustum& o) const { return bottomRadius == o.bottomRadius && topRadius == o.topRadius && height == o.height; }
};
struct ShapeParamsSphere {
    float radius;
    bool operator==(const ShapeParamsSphere& o) const { return radius == o.radius; }
};
struct ShapeParamsSphericalZone {
    float radius, bottomY, topY;
    bool operator==(const ShapeParamsSphericalZone& o) const { return radius == o.radius && bottomY == o.bottomY && topY == o.topY; }
};
struct ShapeParamsSphericalSegment {
    float radius, baseY;
    bool operator==(const ShapeParamsSphericalSegment& o) const { return radius == o.radius && baseY == o.baseY; }
};
struct ShapeParamsSphericalSector {
    float radius, thetaRange, phiRange;
    bool operator==(const ShapeParamsSphericalSector& o) const { return radius == o.radius && thetaRange == o.thetaRange && phiRange == o.phiRange; }
};
struct ShapeParamsSphereWithCylindricalBore {
    float sphereRadius, cylinderRadius, cylinderHeight;
    bool operator==(const ShapeParamsSphereWithCylindricalBore& o) const { return sphereRadius == o.sphereRadius && cylinderRadius == o.cylinderRadius && cylinderHeight == o.cylinderHeight; }
};
struct ShapeParamsSphereWithConicalCavities {
    float sphereRadius, coneRadius, coneHeight;
    bool operator==(const ShapeParamsSphereWithConicalCavities& o) const { return sphereRadius == o.sphereRadius && coneRadius == o.coneRadius && coneHeight == o.coneHeight; }
};
struct ShapeParamsSlicedCylinder {
    float radius, height; glm::vec3 cutNormal;
    bool operator==(const ShapeParamsSlicedCylinder& o) const { return radius == o.radius && height == o.height && cutNormal == o.cutNormal; }
};
struct ShapeParamsUngula {
    float radius, height, cutAngle;
    bool operator==(const ShapeParamsUngula& o) const { return radius == o.radius && height == o.height && cutAngle == o.cutAngle; }
};
struct ShapeParamsBarrel {
    float midRadius, endRadius, height;
    bool operator==(const ShapeParamsBarrel& o) const { return midRadius == o.midRadius && endRadius == o.endRadius && height == o.height; }
};
struct ShapeParamsCapsule {
    float radius, halfHeight;
    bool operator==(const ShapeParamsCapsule& o) const { return radius == o.radius && halfHeight == o.halfHeight; }
};
struct ShapeParamsLegacySuperSphere {
    float n;
    bool operator==(const ShapeParamsLegacySuperSphere& o) const { return n == o.n; }
};

// BM-SHAPES-01 Parameter structures
struct ShapeParamsWedge {
    glm::vec3 size{1.0f, 1.0f, 1.0f};
    bool operator==(const ShapeParamsWedge& o) const { return size == o.size; }
};
struct ShapeParamsRoundedBox {
    glm::vec3 size{1.0f, 1.0f, 1.0f}; float radius{0.15f}; int segments{3};
    bool operator==(const ShapeParamsRoundedBox& o) const { return size == o.size && radius == o.radius && segments == o.segments; }
};
struct ShapeParamsTorus {
    float majorRadius{0.75f}; float minorRadius{0.25f}; int majorSegments{24}; int minorSegments{12};
    bool operator==(const ShapeParamsTorus& o) const { return majorRadius == o.majorRadius && minorRadius == o.minorRadius && majorSegments == o.majorSegments && minorSegments == o.minorSegments; }
};
struct ShapeParamsTetrahedron {
    float sideLength{1.0f};
    bool operator==(const ShapeParamsTetrahedron& o) const { return sideLength == o.sideLength; }
};
struct ShapeParamsEllipsoid {
    glm::vec3 radius{1.0f, 0.7f, 0.5f}; int rings{16}; int segments{24};
    bool operator==(const ShapeParamsEllipsoid& o) const { return radius == o.radius && rings == o.rings && segments == o.segments; }
};
struct ShapeParamsHemisphere {
    float radius{0.5f}; int rings{16}; int segments{32}; float rimHeight{0.05f};
    bool operator==(const ShapeParamsHemisphere& o) const {
        return radius == o.radius && rings == o.rings && segments == o.segments && rimHeight == o.rimHeight;
    }
};

struct ShapeParamsConvexHull {
    std::vector<glm::vec3> vertices;
    bool operator==(const ShapeParamsConvexHull& o) const { return vertices == o.vertices; }
};
struct ShapeParamsTriangleMesh {
    std::vector<glm::vec3> vertices; std::vector<uint32_t> indices;
    bool operator==(const ShapeParamsTriangleMesh& o) const { return vertices == o.vertices && indices == o.indices; }
};

struct CompoundChild {
    ShapeHandle handle;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    bool operator==(const CompoundChild& o) const {
        return handle == o.handle && position == o.position &&
               rotation.w == o.rotation.w && rotation.x == o.rotation.x &&
               rotation.y == o.rotation.y && rotation.z == o.rotation.z &&
               scale == o.scale;
    }
};
struct ShapeParamsCompound {
    std::vector<CompoundChild> children;
    bool operator==(const ShapeParamsCompound& o) const { return children == o.children; }
};

using ShapeParameters = std::variant<
    std::monostate,
    ShapeParamsCuboid,
    ShapeParamsParallelepiped,
    ShapeParamsPyramid,
    ShapeParamsPyramidFrustum,
    ShapeParamsCylinder,
    ShapeParamsHollowCylinder,
    ShapeParamsCone,
    ShapeParamsConeFrustum,
    ShapeParamsSphere,
    ShapeParamsSphericalZone,
    ShapeParamsSphericalSegment,
    ShapeParamsSphericalSector,
    ShapeParamsSphereWithCylindricalBore,
    ShapeParamsSphereWithConicalCavities,
    ShapeParamsSlicedCylinder,
    ShapeParamsUngula,
    ShapeParamsBarrel,
    ShapeParamsCapsule,
    ShapeParamsConvexHull,
    ShapeParamsCompound,
    ShapeParamsTriangleMesh,
    ShapeParamsLegacySuperSphere,
    ShapeParamsWedge,
    ShapeParamsRoundedBox,
    ShapeParamsTorus,
    ShapeParamsTetrahedron,
    ShapeParamsEllipsoid,
    ShapeParamsHemisphere
>;

struct ShapeValidationResult {
    bool isValid = false;
    std::string errorMessage;
};

struct ShapeDefinition {
    ShapeID id = 0;
    ShapeRevision revision = 1;
    ShapeType type = ShapeType::None;
    ShapeParameters parameters;

    // Capability Contract (P2.1)
    bool supportsVisualCompilation = false;
    bool supportsCollisionCompilation = false;
    bool supportsStaticOverlap = false;
    bool supportsDistanceQuery = false;
    bool supportsPenetrationResolution = false;
    bool supportsContinuousShapeCast = false;
};

class ShapeRegistry {
public:
    ShapeRegistry() = default;
    ~ShapeRegistry() = default;

    void Initialize();
    bool LoadFromJson(const std::string& filepath);
    bool SaveToJson(const std::string& filepath);
    
    // Core registry operations
    ShapeHandle RegisterShape(const ShapeDefinition& def);
    bool UnregisterShape(ShapeHandle handle);
    bool UpdateShape(ShapeHandle handle, const ShapeParameters& newParams);
    
    ShapeValidationResult ValidateParameters(ShapeType type, const ShapeParameters& params, ShapeID selfId = 0, const std::unordered_map<ShapeID, ShapeDefinition>* contextGraph = nullptr) const;
    
    const ShapeDefinition* GetShapeDef(ShapeHandle handle) const;
    std::vector<ShapeDefinition> GetAllShapes() const;
    
    // Cycle detection helper
    bool CheckCompoundCycle(ShapeID currentId, ShapeID targetId, int depth, const std::unordered_map<ShapeID, ShapeDefinition>* contextGraph = nullptr) const;
    
    // Built-in singletons
    ShapeHandle GetDefaultCube() const { return m_defaultCube; }
    
private:
    ShapeID m_nextId = 1;
    std::unordered_map<ShapeID, ShapeDefinition> m_shapes;
    
    ShapeHandle m_defaultCube;
};

} // namespace fw
