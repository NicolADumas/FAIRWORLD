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
    SignedDistanceField
};

// Parameter structures
struct ShapeParamsCuboid { glm::vec3 size; };
struct ShapeParamsParallelepiped { glm::vec3 basisX, basisY, basisZ; };
struct ShapeParamsPyramid { float baseWidth, baseDepth, height; glm::vec2 apexOffset; };
struct ShapeParamsPyramidFrustum { float bottomWidth, bottomDepth, topWidth, topDepth, height; glm::vec2 apexOffset; };
struct ShapeParamsCylinder { float radius, height; };
struct ShapeParamsHollowCylinder { float outerRadius, innerRadius, height; };
struct ShapeParamsCone { float radius, height; };
struct ShapeParamsConeFrustum { float bottomRadius, topRadius, height; };
struct ShapeParamsSphere { float radius; };
struct ShapeParamsSphericalZone { float radius, bottomY, topY; };
struct ShapeParamsSphericalSegment { float radius, baseY; };
struct ShapeParamsSphericalSector { float radius, thetaRange, phiRange; };
struct ShapeParamsSphereWithCylindricalBore { float sphereRadius, cylinderRadius, cylinderHeight; };
struct ShapeParamsSphereWithConicalCavities { float sphereRadius, coneRadius, coneHeight; };
struct ShapeParamsSlicedCylinder { float radius, height; glm::vec3 cutNormal; };
struct ShapeParamsUngula { float radius, height, cutAngle; };
struct ShapeParamsBarrel { float midRadius, endRadius, height; };
struct ShapeParamsCapsule { float radius, halfHeight; };
struct ShapeParamsLegacySuperSphere { float n; };

struct ShapeParamsConvexHull { std::vector<glm::vec3> vertices; };
struct ShapeParamsTriangleMesh { std::vector<glm::vec3> vertices; std::vector<uint32_t> indices; };

struct CompoundChild {
    ShapeHandle handle;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};
struct ShapeParamsCompound { std::vector<CompoundChild> children; };

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
    ShapeParamsLegacySuperSphere
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
