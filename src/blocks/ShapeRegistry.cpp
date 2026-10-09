#include "pch.h"
#include "ShapeRegistry.h"
#include "json.hpp"
#include <iostream>
#include <fstream>
#include <cmath>

using json = nlohmann::json;

namespace glm {
    void to_json(nlohmann::json& j, const glm::vec2& v) { j = nlohmann::json::array({v.x, v.y}); }
    void from_json(const nlohmann::json& j, glm::vec2& v) { j.at(0).get_to(v.x); j.at(1).get_to(v.y); }
    void to_json(nlohmann::json& j, const glm::vec3& v) { j = nlohmann::json::array({v.x, v.y, v.z}); }
    void from_json(const nlohmann::json& j, glm::vec3& v) { j.at(0).get_to(v.x); j.at(1).get_to(v.y); j.at(2).get_to(v.z); }
    void to_json(nlohmann::json& j, const glm::quat& q) { j = nlohmann::json::array({q.w, q.x, q.y, q.z}); }
    void from_json(const nlohmann::json& j, glm::quat& q) { j.at(0).get_to(q.w); j.at(1).get_to(q.x); j.at(2).get_to(q.y); j.at(3).get_to(q.z); }
}

namespace fw {

static void SerializeShapeParams(json& j, ShapeType type, const ShapeParameters& params) {
    if (type == ShapeType::None) return;
    switch (type) {
        case ShapeType::Cuboid: { auto& p = std::get<ShapeParamsCuboid>(params); j["size"] = p.size; break; }
        case ShapeType::Parallelepiped: { auto& p = std::get<ShapeParamsParallelepiped>(params); j["basisX"] = p.basisX; j["basisY"] = p.basisY; j["basisZ"] = p.basisZ; break; }
        case ShapeType::Pyramid: { auto& p = std::get<ShapeParamsPyramid>(params); j["baseWidth"] = p.baseWidth; j["baseDepth"] = p.baseDepth; j["height"] = p.height; j["apexOffset"] = p.apexOffset; break; }
        case ShapeType::PyramidFrustum: { auto& p = std::get<ShapeParamsPyramidFrustum>(params); j["bottomWidth"] = p.bottomWidth; j["bottomDepth"] = p.bottomDepth; j["topWidth"] = p.topWidth; j["topDepth"] = p.topDepth; j["height"] = p.height; j["apexOffset"] = p.apexOffset; break; }
        case ShapeType::Cylinder: { auto& p = std::get<ShapeParamsCylinder>(params); j["radius"] = p.radius; j["height"] = p.height; break; }
        case ShapeType::HollowCylinder: { auto& p = std::get<ShapeParamsHollowCylinder>(params); j["outerRadius"] = p.outerRadius; j["innerRadius"] = p.innerRadius; j["height"] = p.height; break; }
        case ShapeType::Cone: { auto& p = std::get<ShapeParamsCone>(params); j["radius"] = p.radius; j["height"] = p.height; break; }
        case ShapeType::ConeFrustum: { auto& p = std::get<ShapeParamsConeFrustum>(params); j["bottomRadius"] = p.bottomRadius; j["topRadius"] = p.topRadius; j["height"] = p.height; break; }
        case ShapeType::Sphere: { auto& p = std::get<ShapeParamsSphere>(params); j["radius"] = p.radius; break; }
        case ShapeType::SphericalZone: { auto& p = std::get<ShapeParamsSphericalZone>(params); j["radius"] = p.radius; j["bottomY"] = p.bottomY; j["topY"] = p.topY; break; }
        case ShapeType::SphericalSegment: { auto& p = std::get<ShapeParamsSphericalSegment>(params); j["radius"] = p.radius; j["baseY"] = p.baseY; break; }
        case ShapeType::SphericalSector: { auto& p = std::get<ShapeParamsSphericalSector>(params); j["radius"] = p.radius; j["thetaRange"] = p.thetaRange; j["phiRange"] = p.phiRange; break; }
        case ShapeType::SphereWithCylindricalBore: { auto& p = std::get<ShapeParamsSphereWithCylindricalBore>(params); j["sphereRadius"] = p.sphereRadius; j["cylinderRadius"] = p.cylinderRadius; j["cylinderHeight"] = p.cylinderHeight; break; }
        case ShapeType::SphereWithConicalCavities: { auto& p = std::get<ShapeParamsSphereWithConicalCavities>(params); j["sphereRadius"] = p.sphereRadius; j["coneRadius"] = p.coneRadius; j["coneHeight"] = p.coneHeight; break; }
        case ShapeType::SlicedCylinder: { auto& p = std::get<ShapeParamsSlicedCylinder>(params); j["radius"] = p.radius; j["height"] = p.height; j["cutNormal"] = p.cutNormal; break; }
        case ShapeType::Ungula: { auto& p = std::get<ShapeParamsUngula>(params); j["radius"] = p.radius; j["height"] = p.height; j["cutAngle"] = p.cutAngle; break; }
        case ShapeType::Barrel: { auto& p = std::get<ShapeParamsBarrel>(params); j["midRadius"] = p.midRadius; j["endRadius"] = p.endRadius; j["height"] = p.height; break; }
        case ShapeType::Capsule: { auto& p = std::get<ShapeParamsCapsule>(params); j["radius"] = p.radius; j["halfHeight"] = p.halfHeight; break; }
        case ShapeType::ConvexHull: { auto& p = std::get<ShapeParamsConvexHull>(params); j["vertices"] = p.vertices; break; }
        case ShapeType::TriangleMesh: { auto& p = std::get<ShapeParamsTriangleMesh>(params); j["vertices"] = p.vertices; j["indices"] = p.indices; break; }
        case ShapeType::LegacySuperSphere: { auto& p = std::get<ShapeParamsLegacySuperSphere>(params); j["n"] = p.n; break; }
        case ShapeType::Compound: { 
            auto& p = std::get<ShapeParamsCompound>(params); 
            j["children"] = json::array();
            for (const auto& c : p.children) {
                json cj;
                cj["handleId"] = c.handle.id;
                cj["handleRev"] = c.handle.revision;
                cj["position"] = c.position;
                cj["rotation"] = c.rotation;
                cj["scale"] = c.scale;
                j["children"].push_back(cj);
            }
            break; 
        }
        default: break; // Cube, Heightfield, SignedDistanceField
    }
}

static ShapeParameters DeserializeShapeParams(const json& j, ShapeType type) {
    if (type == ShapeType::None || type == ShapeType::Cube || type == ShapeType::Heightfield || type == ShapeType::SignedDistanceField) return std::monostate{};
    
    try {
        switch (type) {
            case ShapeType::Cuboid: return ShapeParamsCuboid{j.at("size").get<glm::vec3>()};
            case ShapeType::Parallelepiped: return ShapeParamsParallelepiped{j.at("basisX").get<glm::vec3>(), j.at("basisY").get<glm::vec3>(), j.at("basisZ").get<glm::vec3>()};
            case ShapeType::Pyramid: return ShapeParamsPyramid{j.at("baseWidth").get<float>(), j.at("baseDepth").get<float>(), j.at("height").get<float>(), j.at("apexOffset").get<glm::vec2>()};
            case ShapeType::PyramidFrustum: return ShapeParamsPyramidFrustum{j.at("bottomWidth").get<float>(), j.at("bottomDepth").get<float>(), j.at("topWidth").get<float>(), j.at("topDepth").get<float>(), j.at("height").get<float>(), j.at("apexOffset").get<glm::vec2>()};
            case ShapeType::Cylinder: return ShapeParamsCylinder{j.at("radius").get<float>(), j.at("height").get<float>()};
            case ShapeType::HollowCylinder: return ShapeParamsHollowCylinder{j.at("outerRadius").get<float>(), j.at("innerRadius").get<float>(), j.at("height").get<float>()};
            case ShapeType::Cone: return ShapeParamsCone{j.at("radius").get<float>(), j.at("height").get<float>()};
            case ShapeType::ConeFrustum: return ShapeParamsConeFrustum{j.at("bottomRadius").get<float>(), j.at("topRadius").get<float>(), j.at("height").get<float>()};
            case ShapeType::Sphere: return ShapeParamsSphere{j.at("radius").get<float>()};
            case ShapeType::SphericalZone: return ShapeParamsSphericalZone{j.at("radius").get<float>(), j.at("bottomY").get<float>(), j.at("topY").get<float>()};
            case ShapeType::SphericalSegment: return ShapeParamsSphericalSegment{j.at("radius").get<float>(), j.at("baseY").get<float>()};
            case ShapeType::SphericalSector: return ShapeParamsSphericalSector{j.at("radius").get<float>(), j.at("thetaRange").get<float>(), j.at("phiRange").get<float>()};
            case ShapeType::SphereWithCylindricalBore: return ShapeParamsSphereWithCylindricalBore{j.at("sphereRadius").get<float>(), j.at("cylinderRadius").get<float>(), j.at("cylinderHeight").get<float>()};
            case ShapeType::SphereWithConicalCavities: return ShapeParamsSphereWithConicalCavities{j.at("sphereRadius").get<float>(), j.at("coneRadius").get<float>(), j.at("coneHeight").get<float>()};
            case ShapeType::SlicedCylinder: return ShapeParamsSlicedCylinder{j.at("radius").get<float>(), j.at("height").get<float>(), j.at("cutNormal").get<glm::vec3>()};
            case ShapeType::Ungula: return ShapeParamsUngula{j.at("radius").get<float>(), j.at("height").get<float>(), j.at("cutAngle").get<float>()};
            case ShapeType::Barrel: return ShapeParamsBarrel{j.at("midRadius").get<float>(), j.at("endRadius").get<float>(), j.at("height").get<float>()};
            case ShapeType::Capsule: return ShapeParamsCapsule{j.at("radius").get<float>(), j.at("halfHeight").get<float>()};
            case ShapeType::ConvexHull: return ShapeParamsConvexHull{j.at("vertices").get<std::vector<glm::vec3>>()};
            case ShapeType::TriangleMesh: return ShapeParamsTriangleMesh{j.at("vertices").get<std::vector<glm::vec3>>(), j.at("indices").get<std::vector<uint32_t>>()};
            case ShapeType::LegacySuperSphere: return ShapeParamsLegacySuperSphere{j.at("n").get<float>()};
            case ShapeType::Compound: {
                ShapeParamsCompound cmp;
                for (const auto& cj : j.at("children")) {
                    CompoundChild c;
                    c.handle.id = cj.at("handleId").get<ShapeID>();
                    c.handle.revision = cj.at("handleRev").get<ShapeRevision>();
                    c.position = cj.at("position").get<glm::vec3>();
                    c.rotation = cj.at("rotation").get<glm::quat>();
                    c.scale = cj.at("scale").get<glm::vec3>();
                    cmp.children.push_back(c);
                }
                return cmp;
            }
            default: return std::monostate{};
        }
    } catch (...) {
        return std::monostate{};
    }
}

void ShapeRegistry::Initialize() {
    // Register the default 1x1x1 cube
    ShapeDefinition def;
    def.id = m_nextId++;
    def.revision = 1;
    def.type = ShapeType::Cube;
    def.parameters = ShapeParamsCuboid{ glm::vec3(1.0f) };
    
    // Capabilities for P2.1 (placeholder)
    def.supportsVisualCompilation = false;
    def.supportsCollisionCompilation = false;
    
    m_shapes[def.id] = def;
    m_defaultCube = { def.id, def.revision };
}

bool ShapeRegistry::LoadFromJson(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;
    
    json j;
    try {
        file >> j;
        if (!j.contains("shapes") || !j["shapes"].is_array()) return false;
        if (j.value("schemaVersion", "") != "1.0.0") {
            std::cerr << "[ShapeRegistry] Unsupported schema version in shapes.json" << std::endl;
            return false;
        }
        
        std::unordered_map<ShapeID, ShapeDefinition> tempShapes;
        ShapeID maxId = m_nextId;
        
        // Phase 1: Parse all definitions and validate primitives
        for (const auto& jShape : j["shapes"]) {
            ShapeDefinition def;
            def.id = jShape.value("id", 0);
            def.revision = jShape.value("revision", 1);
            def.type = static_cast<ShapeType>(jShape.value("type", 0));
            
            if (def.id == 0) {
                std::cerr << "[ShapeRegistry] Rejected shape with ID 0 in JSON." << std::endl;
                return false;
            }
            if (tempShapes.find(def.id) != tempShapes.end() || m_shapes.find(def.id) != m_shapes.end()) {
                std::cerr << "[ShapeRegistry] Rejected duplicate shape ID " << def.id << " in JSON." << std::endl;
                return false;
            }
            
            // Real param deserialization
            if (jShape.contains("parameters")) {
                def.parameters = DeserializeShapeParams(jShape["parameters"], def.type);
            }
            
            // Check if deserialization returned monostate when it shouldn't
            if (def.type != ShapeType::None && def.type != ShapeType::Cube && def.type != ShapeType::Heightfield && def.type != ShapeType::SignedDistanceField) {
                if (std::holds_alternative<std::monostate>(def.parameters)) {
                    std::cerr << "[ShapeRegistry] Rejected shape with ID " << def.id << " due to malformed or mismatched parameters." << std::endl;
                    return false;
                }
            }
            tempShapes[def.id] = def;
            if (def.id >= maxId) maxId = def.id + 1;
        }
        
        // Phase 2: Validate against complete graph (including compounds)
        for (const auto& [id, def] : tempShapes) {
            auto valid = ValidateParameters(def.type, def.parameters, id, &tempShapes);
            if (!valid.isValid) {
                std::cerr << "[ShapeRegistry] Invalid shape loaded (ID " << id << "): " << valid.errorMessage << std::endl;
                return false;
            }
        }
        
        // Commit
        for (auto& [id, def] : tempShapes) {
            m_shapes[id] = def;
        }
        m_nextId = maxId;
        
    } catch (const std::exception& e) {
        std::cerr << "[ShapeRegistry] Exception loading shapes: " << e.what() << std::endl;
        return false;
    }
    return true;
}

bool ShapeRegistry::SaveToJson(const std::string& filepath) {
    json j;
    j["schemaVersion"] = "1.0.0";
    json jShapes = json::array();
    for (const auto& [id, def] : m_shapes) {
        json jShape;
        jShape["id"] = def.id;
        jShape["revision"] = def.revision;
        jShape["type"] = static_cast<uint8_t>(def.type);
        
        json jParams = json::object();
        SerializeShapeParams(jParams, def.type, def.parameters);
        if (!jParams.empty()) {
            jShape["parameters"] = jParams;
        }
        jShapes.push_back(jShape);
    }
    j["shapes"] = jShapes;
    
    std::ofstream file(filepath);
    if (!file.is_open()) return false;
    file << j.dump(4);
    return true;
}

static bool IsFinite(float v) { return !std::isnan(v) && !std::isinf(v); }
static bool IsFinite(const glm::vec3& v) { return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z); }
static bool IsFinite(const glm::vec2& v) { return IsFinite(v.x) && IsFinite(v.y); }
static bool IsFinite(const glm::quat& q) { return IsFinite(q.x) && IsFinite(q.y) && IsFinite(q.z) && IsFinite(q.w); }

ShapeValidationResult ShapeRegistry::ValidateParameters(ShapeType type, const ShapeParameters& params, ShapeID selfId, const std::unordered_map<ShapeID, ShapeDefinition>* contextGraph) const {
    ShapeValidationResult res;
    res.isValid = true;

    auto fail = [&](const std::string& msg) {
        res.isValid = false;
        res.errorMessage = msg;
        return res;
    };

    if (type == ShapeType::Cylinder || type == ShapeType::Cone || type == ShapeType::Sphere || type == ShapeType::Capsule) {
        if (auto* p = std::get_if<ShapeParamsCylinder>(&params)) {
            if (!IsFinite(p->radius) || !IsFinite(p->height) || p->radius <= 0.0f || p->height <= 0.0f) return fail("Dimensions must be positive finite.");
        } else if (auto* p = std::get_if<ShapeParamsCapsule>(&params)) {
            if (!IsFinite(p->radius) || !IsFinite(p->halfHeight) || p->radius <= 0.0f || p->halfHeight < 0.0f) return fail("Dimensions must be positive finite.");
        } else if (auto* p = std::get_if<ShapeParamsSphere>(&params)) {
            if (!IsFinite(p->radius) || p->radius <= 0.0f) return fail("Radius must be positive finite.");
        }
    }
    else if (type == ShapeType::Cuboid) {
        if (auto* p = std::get_if<ShapeParamsCuboid>(&params)) {
            if (!IsFinite(p->size) || p->size.x <= 0.0f || p->size.y <= 0.0f || p->size.z <= 0.0f) return fail("Cuboid size must be positive finite.");
        }
    }
    else if (type == ShapeType::HollowCylinder) {
        if (auto* p = std::get_if<ShapeParamsHollowCylinder>(&params)) {
            if (!IsFinite(p->outerRadius) || !IsFinite(p->innerRadius) || !IsFinite(p->height) || p->outerRadius <= p->innerRadius || p->innerRadius <= 0.0f || p->height <= 0.0f) {
                return fail("HollowCylinder outer radius must exceed inner radius, and all must be positive finite.");
            }
        }
    }
    else if (type == ShapeType::Parallelepiped) {
        if (auto* p = std::get_if<ShapeParamsParallelepiped>(&params)) {
            if (!IsFinite(p->basisX) || !IsFinite(p->basisY) || !IsFinite(p->basisZ)) return fail("Basis vectors must be finite.");
            float det = glm::dot(p->basisX, glm::cross(p->basisY, p->basisZ));
            if (std::abs(det) < 1e-6f) return fail("Singular parallelepiped base (coplanar or zero vectors).");
        }
    }
    else if (type == ShapeType::SphericalSector) {
        if (auto* p = std::get_if<ShapeParamsSphericalSector>(&params)) {
            if (!IsFinite(p->radius) || !IsFinite(p->thetaRange) || !IsFinite(p->phiRange)) return fail("Parameters must be finite.");
            if (p->radius <= 0.0f || p->thetaRange <= 0.0f || p->phiRange <= 0.0f) return fail("Angular ranges and radius must be strictly positive.");
        }
    }
    else if (type == ShapeType::ConvexHull) {
        if (auto* p = std::get_if<ShapeParamsConvexHull>(&params)) {
            if (p->vertices.size() < 4) return fail("ConvexHull requires at least 4 vertices.");
            for (const auto& v : p->vertices) {
                if (!IsFinite(v)) return fail("ConvexHull vertices must be finite.");
            }
            // Deferred: Full geometry compiler for convex hull topology/degeneracy
        }
    }
    else if (type == ShapeType::TriangleMesh) {
        if (auto* p = std::get_if<ShapeParamsTriangleMesh>(&params)) {
            if (p->vertices.empty() || p->indices.empty()) return fail("TriangleMesh requires vertices and indices.");
            if (p->indices.size() % 3 != 0) return fail("TriangleMesh indices must be a multiple of 3.");
            for (const auto& v : p->vertices) {
                if (!IsFinite(v)) return fail("TriangleMesh vertices must be finite.");
            }
            for (uint32_t idx : p->indices) {
                if (idx >= p->vertices.size()) return fail("TriangleMesh index out of bounds.");
            }
        }
    }
    else if (type == ShapeType::Compound) {
        if (auto* p = std::get_if<ShapeParamsCompound>(&params)) {
            if (p->children.empty()) return fail("Compound shape must have at least one child.");
            if (p->children.size() > 64) return fail("Compound shape cannot have more than 64 children.");
            for (const auto& child : p->children) {
                if (!IsFinite(child.position) || !IsFinite(child.rotation) || !IsFinite(child.scale)) return fail("Child transform must be finite.");
                if (std::abs(child.scale.x) < 1e-6f || std::abs(child.scale.y) < 1e-6f || std::abs(child.scale.z) < 1e-6f) return fail("Child scale cannot be zero.");
                if (!child.handle.IsValid()) return fail("Child handle is invalid.");
                
                // Missing child check if context is provided
                if (contextGraph) {
                    if (contextGraph->find(child.handle.id) == contextGraph->end()) {
                        return fail("Compound shape references missing child ID.");
                    }
                } else {
                    if (m_shapes.find(child.handle.id) == m_shapes.end()) {
                        return fail("Compound shape references missing child ID.");
                    }
                }
                
                if (selfId != 0 && CheckCompoundCycle(child.handle.id, selfId, 0, contextGraph)) {
                    return fail("Compound shape contains a direct or indirect cycle.");
                }
            }
        }
    }

    return res;
}

bool ShapeRegistry::CheckCompoundCycle(ShapeID currentId, ShapeID targetId, int depth, const std::unordered_map<ShapeID, ShapeDefinition>* contextGraph) const {
    if (currentId == targetId) return true;
    if (depth > 16) return true; // Max depth to prevent infinite loops even if unhandled cycle exists
    
    const ShapeDefinition* def = nullptr;
    if (contextGraph) {
        auto it = contextGraph->find(currentId);
        if (it != contextGraph->end()) def = &it->second;
    } else {
        auto it = m_shapes.find(currentId);
        if (it != m_shapes.end()) def = &it->second;
    }
    
    if (!def) return false; // Forward reference or missing. Missing is caught by ValidateParameters.
    
    if (def->type == ShapeType::Compound) {
        if (auto* p = std::get_if<ShapeParamsCompound>(&def->parameters)) {
            for (const auto& child : p->children) {
                if (CheckCompoundCycle(child.handle.id, targetId, depth + 1, contextGraph)) {
                    return true;
                }
            }
        }
    }
    return false;
}

ShapeHandle ShapeRegistry::RegisterShape(const ShapeDefinition& def) {
    auto valid = ValidateParameters(def.type, def.parameters, def.id);
    if (!valid.isValid) {
        std::cerr << "[ShapeRegistry] Invalid shape registration: " << valid.errorMessage << std::endl;
        return {0, 0};
    }
    
    ShapeID newId = def.id;
    if (newId == 0) {
        newId = m_nextId++;
    } else if (newId >= m_nextId) {
        m_nextId = newId + 1;
    }
    
    ShapeDefinition newDef = def;
    newDef.id = newId;
    newDef.revision = 1;
    
    m_shapes[newId] = newDef;
    return { newId, newDef.revision };
}

bool ShapeRegistry::UpdateShape(ShapeHandle handle, const ShapeParameters& newParams) {
    auto it = m_shapes.find(handle.id);
    if (it == m_shapes.end()) return false;
    
    auto valid = ValidateParameters(it->second.type, newParams, handle.id);
    if (!valid.isValid) return false;
    
    it->second.parameters = newParams;
    it->second.revision++;
    
    return true;
}

const ShapeDefinition* ShapeRegistry::GetShapeDef(ShapeHandle handle) const {
    auto it = m_shapes.find(handle.id);
    if (it != m_shapes.end()) {
        // Option: we could enforce revision matching here, but generally returning the latest is fine
        // unless strict revision tracking is requested at read time.
        return &(it->second);
    }
    return nullptr;
}

} // namespace fw
