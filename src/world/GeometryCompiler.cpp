#include "pch.h"
#include "GeometryCompiler.h"
#include "blocks/sculpt/EditableMesh.h"
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace fw {

static float sgn(float val) {
    return (val > 0.0f) ? 1.0f : ((val < 0.0f) ? -1.0f : 0.0f);
}
static float ppow(float val, float p) {
    return sgn(val) * std::pow(std::abs(val), p);
}

static glm::vec3 toGlm(const fw::Vec3& v) {
    return glm::vec3(v.x, v.y, v.z);
}

Vertex GeometryCompiler::CreateVertex(const glm::vec3& pos, const glm::vec3& normal) {
    Vertex v{};
    v.position = {pos.x, pos.y, pos.z};
    glm::vec3 n = glm::normalize(normal);
    v.normal = {n.x, n.y, n.z};
    v.color = {1.0f, 1.0f, 1.0f, 1.0f};
    v.roughMetal = {0.5f, 0.0f};
    v.materialID = 0;
    v.ao = 1.0f;
    v.light = 1.0f;
    v.emissive = 0.0f;
    return v;
}

GeometryCompileResult GeometryCompiler::Compile(const ShapeDefinition& definition, const GeometryCompileOptions& options) {
    GeometryCompileResult result;
    result.mesh.name = "ParametricShape";
    result.mesh.type = MeshType::Editor;

    std::vector<Vertex> vBuffer;
    std::vector<uint32_t> iBuffer;

    struct SurfaceContext {
        uint32_t materialId = 0;
        UVProjectionMode projection = UVProjectionMode::Default;
        UVTransform uvTransform;
        bool hasPbrOverride = false;
        glm::vec3 baseColor{1.0f};
        float roughness = 1.0f;
        float metallic = 0.0f;
        float emissive = 0.0f;
    };

    auto resolveSurface = [&](uint32_t surfaceId) -> SurfaceContext {
        SurfaceContext sc;
        sc.materialId = options.defaultMaterialId;
        sc.projection = options.defaultProjection;
        if (options.defaultUVTransform) sc.uvTransform = *options.defaultUVTransform;
        
        if (options.surfaceOverrides) {
            for (const auto& so : *options.surfaceOverrides) {
                if (so.surfaceId == surfaceId) {
                    if (so.useExplicitMaterial) sc.materialId = so.materialId;
                    if (so.projection != UVProjectionMode::Default) sc.projection = so.projection;
                    if (!so.uvTransform.IsIdentity()) sc.uvTransform = so.uvTransform;
                    if (so.overridePbr) {
                        sc.hasPbrOverride = true;
                        sc.baseColor = so.baseColorFallback;
                        sc.roughness = so.roughness;
                        sc.metallic = so.metallic;
                        sc.emissive = so.emissive;
                    }
                    break;
                }
            }
        }
        return sc;
    };

    auto createSurfVertex = [&](const glm::vec3& pos, const glm::vec3& norm, uint32_t surfaceId = 0, float paramHeight = 1.0f) -> Vertex {
        SurfaceContext sc = resolveSurface(surfaceId);
        
        glm::vec2 rawUv(0.0f);
        if (sc.projection == UVProjectionMode::Cylindrical) {
            rawUv = CalculateCylindricalUV(pos, paramHeight, surfaceId);
        } else if (sc.projection == UVProjectionMode::Spherical) {
            rawUv = CalculateSphericalUV(norm);
        } else {
            rawUv = CalculatePlanarUV(pos, norm);
        }

        glm::vec2 finalUv = sc.uvTransform.Transform(rawUv);

        Vertex v{};
        v.position = {pos.x, pos.y, pos.z};
        glm::vec3 n = glm::normalize(norm);
        v.normal = {n.x, n.y, n.z};
        v.materialID = sc.materialId;
        v.ao = 1.0f;
        v.light = 1.0f;

        if (sc.hasPbrOverride) {
            v.color = {sc.baseColor.x, sc.baseColor.y, sc.baseColor.z, 1.0f};
            v.roughMetal = {sc.roughness, sc.metallic};
            v.emissive = sc.emissive;
        } else {
            v.color = {1.0f, 1.0f, 1.0f, 1.0f};
            v.roughMetal = {finalUv.x, finalUv.y};
            v.emissive = 0.0f;
        }
        return v;
    };

    auto addTri = [&](uint32_t a, uint32_t b, uint32_t c) {
        iBuffer.push_back(a);
        iBuffer.push_back(b);
        iBuffer.push_back(c);
    };

    auto addQuad = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        addTri(a, b, c);
        addTri(a, c, d);
    };

    auto addTriFace = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t surfaceId, const glm::vec3& faceNorm) {
        addTri(a, b, c);
        Face f;
        f.indices = {(int)a, (int)b, (int)c};
        f.faceNormal = {faceNorm.x, faceNorm.y, faceNorm.z};
        f.matIndex = (int)surfaceId;
        result.mesh.faces.push_back(f);
    };

    auto addQuadFace = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t surfaceId, const glm::vec3& faceNorm) {
        addTri(a, b, c);
        addTri(a, c, d);
        Face f;
        f.indices = {(int)a, (int)b, (int)c, (int)a, (int)c, (int)d};
        f.faceNormal = {faceNorm.x, faceNorm.y, faceNorm.z};
        f.matIndex = (int)surfaceId;
        result.mesh.faces.push_back(f);
    };

    if (definition.type == ShapeType::Cube) {
        float h = 0.5f; // Standard 1.0 size
        glm::vec3 n[6] = {
            {0,0,-1}, {0,0,1}, {-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}
        };
        glm::vec3 p[8] = {
            {-h,-h,-h}, {h,-h,-h}, {h,h,-h}, {-h,h,-h},
            {-h,-h,h},  {h,-h,h},  {h,h,h},  {-h,h,h}
        };
        uint32_t idx = 0;
        // -Z (Surface 0)
        vBuffer.push_back(createSurfVertex(p[0], n[0], 0)); vBuffer.push_back(createSurfVertex(p[1], n[0], 0)); vBuffer.push_back(createSurfVertex(p[2], n[0], 0)); vBuffer.push_back(createSurfVertex(p[3], n[0], 0)); addQuadFace(idx, idx+1, idx+2, idx+3, 0, n[0]); idx+=4;
        // +Z (Surface 1)
        vBuffer.push_back(createSurfVertex(p[5], n[1], 1)); vBuffer.push_back(createSurfVertex(p[4], n[1], 1)); vBuffer.push_back(createSurfVertex(p[7], n[1], 1)); vBuffer.push_back(createSurfVertex(p[6], n[1], 1)); addQuadFace(idx, idx+1, idx+2, idx+3, 1, n[1]); idx+=4;
        // -X (Surface 2)
        vBuffer.push_back(createSurfVertex(p[4], n[2], 2)); vBuffer.push_back(createSurfVertex(p[0], n[2], 2)); vBuffer.push_back(createSurfVertex(p[3], n[2], 2)); vBuffer.push_back(createSurfVertex(p[7], n[2], 2)); addQuadFace(idx, idx+1, idx+2, idx+3, 2, n[2]); idx+=4;
        // +X (Surface 3)
        vBuffer.push_back(createSurfVertex(p[1], n[3], 3)); vBuffer.push_back(createSurfVertex(p[5], n[3], 3)); vBuffer.push_back(createSurfVertex(p[6], n[3], 3)); vBuffer.push_back(createSurfVertex(p[2], n[3], 3)); addQuadFace(idx, idx+1, idx+2, idx+3, 3, n[3]); idx+=4;
        // -Y (Surface 4)
        vBuffer.push_back(createSurfVertex(p[4], n[4], 4)); vBuffer.push_back(createSurfVertex(p[5], n[4], 4)); vBuffer.push_back(createSurfVertex(p[1], n[4], 4)); vBuffer.push_back(createSurfVertex(p[0], n[4], 4)); addQuadFace(idx, idx+1, idx+2, idx+3, 4, n[4]); idx+=4;
        // +Y (Surface 5)
        vBuffer.push_back(createSurfVertex(p[3], n[5], 5)); vBuffer.push_back(createSurfVertex(p[2], n[5], 5)); vBuffer.push_back(createSurfVertex(p[6], n[5], 5)); vBuffer.push_back(createSurfVertex(p[7], n[5], 5)); addQuadFace(idx, idx+1, idx+2, idx+3, 5, n[5]); idx+=4;
    }
    else if (definition.type == ShapeType::Cuboid || definition.type == ShapeType::Parallelepiped) {
        glm::vec3 vx={1,0,0}, vy={0,1,0}, vz={0,0,1};
        if (definition.type == ShapeType::Cuboid) {
            const auto* p = std::get_if<ShapeParamsCuboid>(&definition.parameters);
            if(p) { vx *= p->size.x; vy *= p->size.y; vz *= p->size.z; }
        } else {
            const auto* p = std::get_if<ShapeParamsParallelepiped>(&definition.parameters);
            if(p) { vx = p->basisX; vy = p->basisY; vz = p->basisZ; }
        }
        glm::vec3 p[8];
        for(int i=0;i<8;i++) {
            p[i] = ((i&1)?vx:-vx)*0.5f + ((i&2)?vy:-vy)*0.5f + ((i&4)?vz:-vz)*0.5f;
        }
        glm::vec3 n[6] = { glm::normalize(glm::cross(vy, vx)), glm::normalize(glm::cross(vx, vy)), 
                           glm::normalize(glm::cross(vy, vz)), glm::normalize(glm::cross(vz, vy)), 
                           glm::normalize(glm::cross(vx, vz)), glm::normalize(glm::cross(vz, vx)) };
        uint32_t idx = 0;
        // Front (-Z, Surface 0)
        vBuffer.push_back(createSurfVertex(p[0], n[0], 0)); vBuffer.push_back(createSurfVertex(p[1], n[0], 0)); vBuffer.push_back(createSurfVertex(p[3], n[0], 0)); vBuffer.push_back(createSurfVertex(p[2], n[0], 0)); addQuadFace(idx, idx+1, idx+2, idx+3, 0, n[0]); idx+=4;
        // Back (+Z, Surface 1)
        vBuffer.push_back(createSurfVertex(p[5], n[1], 1)); vBuffer.push_back(createSurfVertex(p[4], n[1], 1)); vBuffer.push_back(createSurfVertex(p[6], n[1], 1)); vBuffer.push_back(createSurfVertex(p[7], n[1], 1)); addQuadFace(idx, idx+1, idx+2, idx+3, 1, n[1]); idx+=4;
        // Left (-X, Surface 2)
        vBuffer.push_back(createSurfVertex(p[4], n[2], 2)); vBuffer.push_back(createSurfVertex(p[0], n[2], 2)); vBuffer.push_back(createSurfVertex(p[2], n[2], 2)); vBuffer.push_back(createSurfVertex(p[6], n[2], 2)); addQuadFace(idx, idx+1, idx+2, idx+3, 2, n[2]); idx+=4;
        // Right (+X, Surface 3)
        vBuffer.push_back(createSurfVertex(p[1], n[3], 3)); vBuffer.push_back(createSurfVertex(p[5], n[3], 3)); vBuffer.push_back(createSurfVertex(p[7], n[3], 3)); vBuffer.push_back(createSurfVertex(p[3], n[3], 3)); addQuadFace(idx, idx+1, idx+2, idx+3, 3, n[3]); idx+=4;
        // Bottom (-Y, Surface 4)
        vBuffer.push_back(createSurfVertex(p[4], n[4], 4)); vBuffer.push_back(createSurfVertex(p[5], n[4], 4)); vBuffer.push_back(createSurfVertex(p[1], n[4], 4)); vBuffer.push_back(createSurfVertex(p[0], n[4], 4)); addQuadFace(idx, idx+1, idx+2, idx+3, 4, n[4]); idx+=4;
        // Top (+Y, Surface 5)
        vBuffer.push_back(createSurfVertex(p[2], n[5], 5)); vBuffer.push_back(createSurfVertex(p[3], n[5], 5)); vBuffer.push_back(createSurfVertex(p[7], n[5], 5)); vBuffer.push_back(createSurfVertex(p[6], n[5], 5)); addQuadFace(idx, idx+1, idx+2, idx+3, 5, n[5]); idx+=4;
    }
    else if (definition.type == ShapeType::Cylinder || definition.type == ShapeType::Cone || definition.type == ShapeType::ConeFrustum) {
        float rTop = 1.0f, rBot = 1.0f, h = 1.0f;
        if (definition.type == ShapeType::Cylinder) {
            const auto* p = std::get_if<ShapeParamsCylinder>(&definition.parameters);
            if(p) { rTop = p->radius; rBot = p->radius; h = p->height; }
        } else if (definition.type == ShapeType::Cone) {
            const auto* p = std::get_if<ShapeParamsCone>(&definition.parameters);
            if(p) { rTop = 0.0f; rBot = p->radius; h = p->height; }
        } else if (definition.type == ShapeType::ConeFrustum) {
            const auto* p = std::get_if<ShapeParamsConeFrustum>(&definition.parameters);
            if(p) { rTop = p->topRadius; rBot = p->bottomRadius; h = p->height; }
        }
        int segments = options.maxResolution;
        float halfH = h * 0.5f;
        for (int i = 0; i < segments; i++) {
            float a1 = (float)i / segments * glm::two_pi<float>();
            float a2 = (float)(i+1) / segments * glm::two_pi<float>();
            glm::vec3 n1(std::cos(a1), 0, std::sin(a1));
            glm::vec3 n2(std::cos(a2), 0, std::sin(a2));
            // Adjust normal for cone slope
            float slope = (rBot - rTop) / h;
            glm::vec3 sn1 = glm::normalize(glm::vec3(n1.x, slope, n1.z));
            glm::vec3 sn2 = glm::normalize(glm::vec3(n2.x, slope, n2.z));
            
            uint32_t idx = (uint32_t)vBuffer.size();
            vBuffer.push_back(createSurfVertex(glm::vec3(n1.x*rBot, -halfH, n1.z*rBot), sn1, 0, h));
            vBuffer.push_back(createSurfVertex(glm::vec3(n2.x*rBot, -halfH, n2.z*rBot), sn2, 0, h));
            vBuffer.push_back(createSurfVertex(glm::vec3(n2.x*rTop,  halfH, n2.z*rTop), sn2, 0, h));
            vBuffer.push_back(createSurfVertex(glm::vec3(n1.x*rTop,  halfH, n1.z*rTop), sn1, 0, h));
            if (rTop == 0.0f) { addTriFace(idx, idx+1, idx+2, 0, sn1); }
            else { addQuadFace(idx, idx+1, idx+2, idx+3, 0, sn1); }

            // Caps
            if (rBot > 0.0f) {
                uint32_t bIdx = (uint32_t)vBuffer.size();
                vBuffer.push_back(createSurfVertex(glm::vec3(0, -halfH, 0), glm::vec3(0,-1,0), 2, h));
                vBuffer.push_back(createSurfVertex(glm::vec3(n2.x*rBot, -halfH, n2.z*rBot), glm::vec3(0,-1,0), 2, h));
                vBuffer.push_back(createSurfVertex(glm::vec3(n1.x*rBot, -halfH, n1.z*rBot), glm::vec3(0,-1,0), 2, h));
                addTriFace(bIdx, bIdx+1, bIdx+2, 2, glm::vec3(0,-1,0));
            }
            if (rTop > 0.0f) {
                uint32_t tIdx = (uint32_t)vBuffer.size();
                vBuffer.push_back(createSurfVertex(glm::vec3(0, halfH, 0), glm::vec3(0,1,0), 1, h));
                vBuffer.push_back(createSurfVertex(glm::vec3(n1.x*rTop, halfH, n1.z*rTop), glm::vec3(0,1,0), 1, h));
                vBuffer.push_back(createSurfVertex(glm::vec3(n2.x*rTop, halfH, n2.z*rTop), glm::vec3(0,1,0), 1, h));
                addTriFace(tIdx, tIdx+1, tIdx+2, 1, glm::vec3(0,1,0));
            }
        }
    }
    else if (definition.type == ShapeType::HollowCylinder) {
        float rOut = 1.0f, rIn = 0.5f, h = 1.0f;
        const auto* p = std::get_if<ShapeParamsHollowCylinder>(&definition.parameters);
        if(p) { rOut = p->outerRadius; rIn = p->innerRadius; h = p->height; }
        
        int segments = options.maxResolution;
        float halfH = h * 0.5f;
        for (int i = 0; i < segments; i++) {
            float a1 = (float)i / segments * glm::two_pi<float>();
            float a2 = (float)(i+1) / segments * glm::two_pi<float>();
            glm::vec3 n1(std::cos(a1), 0, std::sin(a1));
            glm::vec3 n2(std::cos(a2), 0, std::sin(a2));
            
            // Outer wall
            uint32_t oIdx = vBuffer.size();
            vBuffer.push_back(CreateVertex(glm::vec3(n1.x*rOut, -halfH, n1.z*rOut), n1));
            vBuffer.push_back(CreateVertex(glm::vec3(n2.x*rOut, -halfH, n2.z*rOut), n2));
            vBuffer.push_back(CreateVertex(glm::vec3(n2.x*rOut,  halfH, n2.z*rOut), n2));
            vBuffer.push_back(CreateVertex(glm::vec3(n1.x*rOut,  halfH, n1.z*rOut), n1));
            addQuad(oIdx, oIdx+1, oIdx+2, oIdx+3);

            // Inner wall (inverted normals)
            uint32_t iIdx = vBuffer.size();
            vBuffer.push_back(CreateVertex(glm::vec3(n2.x*rIn, -halfH, n2.z*rIn), -n2));
            vBuffer.push_back(CreateVertex(glm::vec3(n1.x*rIn, -halfH, n1.z*rIn), -n1));
            vBuffer.push_back(CreateVertex(glm::vec3(n1.x*rIn,  halfH, n1.z*rIn), -n1));
            vBuffer.push_back(CreateVertex(glm::vec3(n2.x*rIn,  halfH, n2.z*rIn), -n2));
            addQuad(iIdx, iIdx+1, iIdx+2, iIdx+3);

            // Top Cap
            uint32_t tIdx = vBuffer.size();
            vBuffer.push_back(CreateVertex(glm::vec3(n1.x*rIn,  halfH, n1.z*rIn), glm::vec3(0,1,0)));
            vBuffer.push_back(CreateVertex(glm::vec3(n2.x*rIn,  halfH, n2.z*rIn), glm::vec3(0,1,0)));
            vBuffer.push_back(CreateVertex(glm::vec3(n2.x*rOut, halfH, n2.z*rOut), glm::vec3(0,1,0)));
            vBuffer.push_back(CreateVertex(glm::vec3(n1.x*rOut, halfH, n1.z*rOut), glm::vec3(0,1,0)));
            addQuad(tIdx, tIdx+1, tIdx+2, tIdx+3);

            // Bottom Cap
            uint32_t bIdx = vBuffer.size();
            vBuffer.push_back(CreateVertex(glm::vec3(n2.x*rIn,  -halfH, n2.z*rIn), glm::vec3(0,-1,0)));
            vBuffer.push_back(CreateVertex(glm::vec3(n1.x*rIn,  -halfH, n1.z*rIn), glm::vec3(0,-1,0)));
            vBuffer.push_back(CreateVertex(glm::vec3(n1.x*rOut, -halfH, n1.z*rOut), glm::vec3(0,-1,0)));
            vBuffer.push_back(CreateVertex(glm::vec3(n2.x*rOut, -halfH, n2.z*rOut), glm::vec3(0,-1,0)));
            addQuad(bIdx, bIdx+1, bIdx+2, bIdx+3);
        }
    }
    else if (definition.type == ShapeType::Pyramid || definition.type == ShapeType::PyramidFrustum) {
        float wB = 1.0f, dB = 1.0f, wT = 0.0f, dT = 0.0f, h = 1.0f;
        glm::vec2 apexOffset(0.0f);
        if (definition.type == ShapeType::Pyramid) {
            const auto* p = std::get_if<ShapeParamsPyramid>(&definition.parameters);
            if(p) { wB = p->baseWidth; dB = p->baseDepth; h = p->height; apexOffset = p->apexOffset; }
        } else {
            const auto* p = std::get_if<ShapeParamsPyramidFrustum>(&definition.parameters);
            if(p) { wB = p->bottomWidth; dB = p->bottomDepth; wT = p->topWidth; dT = p->topDepth; h = p->height; apexOffset = p->apexOffset; }
        }
        float halfH = h * 0.5f;
        glm::vec3 p0(-wB*0.5f, -halfH, -dB*0.5f);
        glm::vec3 p1( wB*0.5f, -halfH, -dB*0.5f);
        glm::vec3 p2( wB*0.5f, -halfH,  dB*0.5f);
        glm::vec3 p3(-wB*0.5f, -halfH,  dB*0.5f);
        glm::vec3 t0 = glm::vec3(-wT*0.5f, halfH, -dT*0.5f) + glm::vec3(apexOffset.x, 0, apexOffset.y);
        glm::vec3 t1 = glm::vec3( wT*0.5f, halfH, -dT*0.5f) + glm::vec3(apexOffset.x, 0, apexOffset.y);
        glm::vec3 t2 = glm::vec3( wT*0.5f, halfH,  dT*0.5f) + glm::vec3(apexOffset.x, 0, apexOffset.y);
        glm::vec3 t3 = glm::vec3(-wT*0.5f, halfH,  dT*0.5f) + glm::vec3(apexOffset.x, 0, apexOffset.y);

        auto buildFace = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
            glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
            uint32_t idx = vBuffer.size();
            vBuffer.push_back(CreateVertex(a, n)); vBuffer.push_back(CreateVertex(b, n));
            vBuffer.push_back(CreateVertex(c, n)); vBuffer.push_back(CreateVertex(d, n));
            addQuad(idx, idx+1, idx+2, idx+3);
        };
        auto buildTriFace = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
            glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
            uint32_t idx = vBuffer.size();
            vBuffer.push_back(CreateVertex(a, n)); vBuffer.push_back(CreateVertex(b, n)); vBuffer.push_back(CreateVertex(c, n));
            addTri(idx, idx+1, idx+2);
        };

        if (wT == 0 && dT == 0) { // true pyramid
            buildTriFace(p1, p0, t0); buildTriFace(p2, p1, t0); buildTriFace(p3, p2, t0); buildTriFace(p0, p3, t0);
        } else {
            buildFace(p1, p0, t0, t1); buildFace(p2, p1, t1, t2); buildFace(p3, p2, t2, t3); buildFace(p0, p3, t3, t0);
            buildFace(t0, t3, t2, t1); // top
        }
        buildFace(p0, p1, p2, p3); // bottom
    }
    else if (definition.type == ShapeType::Sphere || definition.type == ShapeType::LegacySuperSphere) {
        float radius = 1.0f;
        float exp = 2.0f;
        if (definition.type == ShapeType::Sphere) {
            const auto* p = std::get_if<ShapeParamsSphere>(&definition.parameters);
            if(p) { radius = p->radius; }
        } else {
            const auto* p = std::get_if<ShapeParamsLegacySuperSphere>(&definition.parameters);
            if(p) { exp = p->n; } // radius = 1.0 by default in super sphere legacy
        }

        int rings = options.maxResolution;
        int segs = options.maxResolution * 2;
        float power = 2.0f / exp;

        std::vector<Vertex> grid;
        grid.reserve((rings + 1) * (segs + 1));

        for (int r = 0; r <= rings; ++r) {
            float eta = -glm::half_pi<float>() + (glm::pi<float>() * r / rings);
            float y = radius * ppow(std::sin(eta), power);
            float rCross = radius * ppow(std::cos(eta), power);

            for (int s = 0; s <= segs; ++s) {
                float omega = -glm::pi<float>() + (2.0f * glm::pi<float>() * s / segs);
                float x = rCross * ppow(std::cos(omega), power);
                float z = rCross * ppow(std::sin(omega), power);
                
                float nx = ppow(x / radius, exp - 1.0f);
                float ny = ppow(y / radius, exp - 1.0f);
                float nz = ppow(z / radius, exp - 1.0f);
                
                uint32_t idx = vBuffer.size();
                vBuffer.push_back(CreateVertex({x,y,z}, {nx, ny, nz}));
            }
        }
        for (int r = 0; r < rings; ++r) {
            for (int s = 0; s < segs; ++s) {
                int a = r * (segs + 1) + s;
                int b = a + 1;
                int c = (r + 1) * (segs + 1) + s;
                int d = c + 1;
                addTri(a, b, c);
                addTri(c, b, d);
            }
        }
    }
    else if (definition.type == ShapeType::Capsule) {
        float r = 0.5f, hh = 1.0f;
        const auto* p = std::get_if<ShapeParamsCapsule>(&definition.parameters);
        if(p) { r = p->radius; hh = p->halfHeight; }

        int rings = options.maxResolution / 2;
        int segs = options.maxResolution;

        // Bottom hemisphere
        int startBot = vBuffer.size();
        for (int rIdx = 0; rIdx <= rings; ++rIdx) {
            float eta = -glm::half_pi<float>() + (glm::half_pi<float>() * rIdx / rings);
            float y = -hh + r * std::sin(eta);
            float rCross = r * std::cos(eta);
            for (int s = 0; s <= segs; ++s) {
                float omega = -glm::pi<float>() + (2.0f * glm::pi<float>() * s / segs);
                float x = rCross * std::cos(omega);
                float z = rCross * std::sin(omega);
                vBuffer.push_back(CreateVertex({x,y,z}, {std::cos(eta)*std::cos(omega), std::sin(eta), std::cos(eta)*std::sin(omega)}));
            }
        }
        for (int rIdx = 0; rIdx < rings; ++rIdx) {
            for (int s = 0; s < segs; ++s) {
                int a = startBot + rIdx * (segs + 1) + s;
                addTri(a, a+1, a+(segs+1));
                addTri(a+(segs+1), a+1, a+(segs+1)+1);
            }
        }

        // Cylinder body
        int startBody = vBuffer.size();
        for (int s = 0; s <= segs; ++s) {
            float omega = -glm::pi<float>() + (2.0f * glm::pi<float>() * s / segs);
            float nx = std::cos(omega), nz = std::sin(omega);
            vBuffer.push_back(CreateVertex({nx*r, -hh, nz*r}, {nx, 0, nz}));
            vBuffer.push_back(CreateVertex({nx*r,  hh, nz*r}, {nx, 0, nz}));
        }
        for (int s = 0; s < segs; ++s) {
            int a = startBody + s*2;
            addTri(a, a+2, a+1);
            addTri(a+1, a+2, a+3);
        }

        // Top hemisphere
        int startTop = vBuffer.size();
        for (int rIdx = 0; rIdx <= rings; ++rIdx) {
            float eta = (glm::half_pi<float>() * rIdx / rings);
            float y = hh + r * std::sin(eta);
            float rCross = r * std::cos(eta);
            for (int s = 0; s <= segs; ++s) {
                float omega = -glm::pi<float>() + (2.0f * glm::pi<float>() * s / segs);
                float x = rCross * std::cos(omega);
                float z = rCross * std::sin(omega);
                vBuffer.push_back(CreateVertex({x,y,z}, {std::cos(eta)*std::cos(omega), std::sin(eta), std::cos(eta)*std::sin(omega)}));
            }
        }
        for (int rIdx = 0; rIdx < rings; ++rIdx) {
            for (int s = 0; s < segs; ++s) {
                int a = startTop + rIdx * (segs + 1) + s;
                addTri(a, a+1, a+(segs+1));
                addTri(a+(segs+1), a+1, a+(segs+1)+1);
            }
        }
    }
    else if (definition.type == ShapeType::SphericalZone || definition.type == ShapeType::SphericalSegment) {
        float radius = 1.0f;
        float topY = 1.0f;
        float bottomY = -1.0f;
        if (definition.type == ShapeType::SphericalZone) {
            const auto* p = std::get_if<ShapeParamsSphericalZone>(&definition.parameters);
            if(p) { radius = p->radius; topY = p->topY; bottomY = p->bottomY; }
        } else {
            const auto* p = std::get_if<ShapeParamsSphericalSegment>(&definition.parameters);
            if(p) { radius = p->radius; bottomY = p->baseY; topY = p->radius; } // Assumes top segment
        }
        if (bottomY > topY) std::swap(bottomY, topY);
        bottomY = glm::clamp(bottomY, -radius, radius);
        topY = glm::clamp(topY, -radius, radius);

        int rings = options.maxResolution;
        int segs = options.maxResolution * 2;
        
        int startVert = vBuffer.size();
        for (int r = 0; r <= rings; ++r) {
            float y = bottomY + (topY - bottomY) * r / rings;
            float rCross = std::sqrt(std::max(0.0f, radius * radius - y * y));
            for (int s = 0; s <= segs; ++s) {
                float omega = -glm::pi<float>() + (2.0f * glm::pi<float>() * s / segs);
                float x = rCross * std::cos(omega);
                float z = rCross * std::sin(omega);
                glm::vec3 pos{x, y, z};
                vBuffer.push_back(CreateVertex(pos, glm::normalize(pos)));
            }
        }
        for (int r = 0; r < rings; ++r) {
            for (int s = 0; s < segs; ++s) {
                int a = startVert + r * (segs + 1) + s;
                int b = a + 1;
                int c = startVert + (r + 1) * (segs + 1) + s;
                int d = c + 1;
                addTri(a, b, c);
                addTri(c, b, d);
            }
        }
        // Top Cap
        if (topY < radius - 1e-4f) {
            int topCenter = vBuffer.size();
            vBuffer.push_back(CreateVertex({0, topY, 0}, {0, 1, 0}));
            int capRingStart = startVert + rings * (segs + 1);
            for (int s = 0; s < segs; ++s) {
                int start = vBuffer.size();
                vBuffer.push_back(CreateVertex(toGlm(vBuffer[capRingStart + s].position), {0, 1, 0}));
                vBuffer.push_back(CreateVertex(toGlm(vBuffer[capRingStart + s + 1].position), {0, 1, 0}));
                addTri(topCenter, start + 1, start);
            }
        }
        // Bottom Cap
        if (bottomY > -radius + 1e-4f) {
            int botCenter = vBuffer.size();
            vBuffer.push_back(CreateVertex({0, bottomY, 0}, {0, -1, 0}));
            int capRingStart = startVert;
            for (int s = 0; s < segs; ++s) {
                int start = vBuffer.size();
                vBuffer.push_back(CreateVertex(toGlm(vBuffer[capRingStart + s].position), {0, -1, 0}));
                vBuffer.push_back(CreateVertex(toGlm(vBuffer[capRingStart + s + 1].position), {0, -1, 0}));
                addTri(botCenter, start, start + 1);
            }
        }
    }
    else if (definition.type == ShapeType::SphericalSector) {
        float r = 1.0f, thetaR = glm::pi<float>(), phiR = glm::half_pi<float>();
        const auto* p = std::get_if<ShapeParamsSphericalSector>(&definition.parameters);
        if(p) { r = p->radius; thetaR = p->thetaRange; phiR = p->phiRange; }
        
        int rings = options.maxResolution;
        int segs = options.maxResolution;
        
        int startSurf = vBuffer.size();
        for (int ri = 0; ri <= rings; ++ri) {
            float phi = phiR * ri / rings; // 0 (North Pole) to phiR
            float y = r * std::cos(phi);
            float rCross = r * std::sin(phi);
            for (int s = 0; s <= segs; ++s) {
                float theta = -thetaR * 0.5f + thetaR * s / segs;
                float x = rCross * std::cos(theta);
                float z = rCross * std::sin(theta);
                glm::vec3 pos{x, y, z};
                vBuffer.push_back(CreateVertex(pos, glm::normalize(pos)));
            }
        }
        for (int ri = 0; ri < rings; ++ri) {
            for (int s = 0; s < segs; ++s) {
                int a = startSurf + ri * (segs + 1) + s;
                addTri(a, a + 1, a + segs + 1);
                addTri(a + segs + 1, a + 1, a + segs + 2);
            }
        }
        
        // Conical bottom (connects phiR ring to origin)
        int center = vBuffer.size();
        vBuffer.push_back(CreateVertex({0,0,0}, {0, -1, 0})); // Placeholder normal for center
        int botRingStart = startSurf + rings * (segs + 1);
        for (int s = 0; s < segs; ++s) {
            glm::vec3 p0 = toGlm(vBuffer[botRingStart + s].position);
            glm::vec3 p1 = toGlm(vBuffer[botRingStart + s + 1].position);
            glm::vec3 norm = glm::normalize(glm::cross(p1 - p0, p0 - glm::vec3(0,0,0)));
            int st = vBuffer.size();
            vBuffer.push_back(CreateVertex(p0, norm));
            vBuffer.push_back(CreateVertex(p1, norm));
            vBuffer.push_back(CreateVertex({0,0,0}, norm));
            addTri(st, st+1, st+2);
        }
        
        // Side walls (if thetaR < 2*PI)
        if (thetaR < glm::two_pi<float>() - 1e-4f) {
            // Left side
            for (int ri = 0; ri < rings; ++ri) {
                glm::vec3 p0 = toGlm(vBuffer[startSurf + ri * (segs + 1)].position);
                glm::vec3 p1 = toGlm(vBuffer[startSurf + (ri + 1) * (segs + 1)].position);
                glm::vec3 norm = glm::normalize(glm::cross(p0 - glm::vec3(0,0,0), p1 - glm::vec3(0,0,0)));
                int st = vBuffer.size();
                vBuffer.push_back(CreateVertex({0,0,0}, norm));
                vBuffer.push_back(CreateVertex(p1, norm));
                vBuffer.push_back(CreateVertex(p0, norm));
                addTri(st, st+1, st+2);
            }
            // Right side
            for (int ri = 0; ri < rings; ++ri) {
                glm::vec3 p0 = toGlm(vBuffer[startSurf + ri * (segs + 1) + segs].position);
                glm::vec3 p1 = toGlm(vBuffer[startSurf + (ri + 1) * (segs + 1) + segs].position);
                glm::vec3 norm = glm::normalize(glm::cross(p1 - glm::vec3(0,0,0), p0 - glm::vec3(0,0,0)));
                int st = vBuffer.size();
                vBuffer.push_back(CreateVertex({0,0,0}, norm));
                vBuffer.push_back(CreateVertex(p0, norm));
                vBuffer.push_back(CreateVertex(p1, norm));
                addTri(st, st+1, st+2);
            }
        }
    }
    else if (definition.type == ShapeType::SphereWithCylindricalBore) {
        float rS = 1.0f, rC = 0.5f;
        const auto* p = std::get_if<ShapeParamsSphereWithCylindricalBore>(&definition.parameters);
        if(p) { rS = p->sphereRadius; rC = p->cylinderRadius; }
        
        float yMax = std::sqrt(std::max(0.0f, rS*rS - rC*rC));
        int rings = options.maxResolution;
        int segs = options.maxResolution * 2;
        
        // Outer spherical surface
        int outStart = vBuffer.size();
        for (int rIdx = 0; rIdx <= rings; ++rIdx) {
            float y = -yMax + (2.0f * yMax) * rIdx / rings;
            float rCross = std::sqrt(std::max(0.0f, rS * rS - y * y));
            for (int s = 0; s <= segs; ++s) {
                float omega = (2.0f * glm::pi<float>() * s / segs);
                glm::vec3 pos{rCross * std::cos(omega), y, rCross * std::sin(omega)};
                vBuffer.push_back(CreateVertex(pos, glm::normalize(pos)));
            }
        }
        for (int rIdx = 0; rIdx < rings; ++rIdx) {
            for (int s = 0; s < segs; ++s) {
                int a = outStart + rIdx * (segs + 1) + s;
                addTri(a, a + 1, a + segs + 1);
                addTri(a + segs + 1, a + 1, a + segs + 2);
            }
        }
        
        // Inner cylindrical bore
        int inStart = vBuffer.size();
        for (int rIdx = 0; rIdx <= rings; ++rIdx) {
            float y = yMax - (2.0f * yMax) * rIdx / rings; // Inverted Y to flip normals naturally
            for (int s = 0; s <= segs; ++s) {
                float omega = (2.0f * glm::pi<float>() * s / segs);
                glm::vec3 pos{rC * std::cos(omega), y, rC * std::sin(omega)};
                vBuffer.push_back(CreateVertex(pos, glm::normalize(glm::vec3{-pos.x, 0.0f, -pos.z})));
            }
        }
        for (int rIdx = 0; rIdx < rings; ++rIdx) {
            for (int s = 0; s < segs; ++s) {
                int a = inStart + rIdx * (segs + 1) + s;
                addTri(a, a + 1, a + segs + 1);
                addTri(a + segs + 1, a + 1, a + segs + 2);
            }
        }
        
        // Cap top (annular)
        int topStart = vBuffer.size();
        for (int s = 0; s <= segs; ++s) {
            float omega = (2.0f * glm::pi<float>() * s / segs);
            glm::vec3 pIn{rC * std::cos(omega), yMax, rC * std::sin(omega)};
            glm::vec3 pOut{std::sqrt(rS*rS - yMax*yMax) * std::cos(omega), yMax, std::sqrt(rS*rS - yMax*yMax) * std::sin(omega)};
            vBuffer.push_back(CreateVertex(pIn, {0, 1, 0}));
            vBuffer.push_back(CreateVertex(pOut, {0, 1, 0}));
        }
        for (int s = 0; s < segs; ++s) {
            int a = topStart + s * 2;
            addTri(a, a + 1, a + 2);
            addTri(a + 2, a + 1, a + 3);
        }
        
        // Cap bottom (annular)
        int botStart = vBuffer.size();
        for (int s = 0; s <= segs; ++s) {
            float omega = (2.0f * glm::pi<float>() * s / segs);
            glm::vec3 pIn{rC * std::cos(omega), -yMax, rC * std::sin(omega)};
            glm::vec3 pOut{std::sqrt(rS*rS - yMax*yMax) * std::cos(omega), -yMax, std::sqrt(rS*rS - yMax*yMax) * std::sin(omega)};
            vBuffer.push_back(CreateVertex(pOut, {0, -1, 0}));
            vBuffer.push_back(CreateVertex(pIn, {0, -1, 0}));
        }
        for (int s = 0; s < segs; ++s) {
            int a = botStart + s * 2;
            addTri(a, a + 1, a + 2);
            addTri(a + 2, a + 1, a + 3);
        }
    }
    else if (definition.type == ShapeType::SphereWithConicalCavities) {
        float rS = 1.0f, rC = 0.5f, hC = 0.5f;
        const auto* p = std::get_if<ShapeParamsSphereWithConicalCavities>(&definition.parameters);
        if(p) { rS = p->sphereRadius; rC = p->coneRadius; hC = p->coneHeight; }
        
        float yMax = std::sqrt(std::max(0.0f, rS*rS - rC*rC));
        int rings = options.maxResolution;
        int segs = options.maxResolution * 2;
        
        // Outer spherical zone
        int outStart = vBuffer.size();
        for (int rIdx = 0; rIdx <= rings; ++rIdx) {
            float y = -yMax + (2.0f * yMax) * rIdx / rings;
            float rCross = std::sqrt(std::max(0.0f, rS * rS - y * y));
            for (int s = 0; s <= segs; ++s) {
                float omega = (2.0f * glm::pi<float>() * s / segs);
                glm::vec3 pos{rCross * std::cos(omega), y, rCross * std::sin(omega)};
                vBuffer.push_back(CreateVertex(pos, glm::normalize(pos)));
            }
        }
        for (int rIdx = 0; rIdx < rings; ++rIdx) {
            for (int s = 0; s < segs; ++s) {
                int a = outStart + rIdx * (segs + 1) + s;
                addTri(a, a + 1, a + segs + 1);
                addTri(a + segs + 1, a + 1, a + segs + 2);
            }
        }
        
        // Top Conical Cavity
        int topConeCenter = vBuffer.size();
        vBuffer.push_back(CreateVertex({0, yMax - hC, 0}, {0, 1, 0})); // Center pointing generally up
        int topRingStart = outStart + rings * (segs + 1); // Top ring of outer sphere
        for (int s = 0; s < segs; ++s) {
            glm::vec3 p0 = toGlm(vBuffer[topRingStart + s].position);
            glm::vec3 p1 = toGlm(vBuffer[topRingStart + s + 1].position);
            glm::vec3 pC{0, yMax - hC, 0};
            glm::vec3 norm = glm::normalize(glm::cross(p1 - pC, p0 - pC));
            int st = vBuffer.size();
            vBuffer.push_back(CreateVertex(pC, norm));
            vBuffer.push_back(CreateVertex(p1, norm));
            vBuffer.push_back(CreateVertex(p0, norm));
            addTri(st, st+1, st+2);
        }
        
        // Bottom Conical Cavity
        int botConeCenter = vBuffer.size();
        vBuffer.push_back(CreateVertex({0, -yMax + hC, 0}, {0, -1, 0}));
        int botRingStart = outStart; // Bottom ring of outer sphere
        for (int s = 0; s < segs; ++s) {
            glm::vec3 p0 = toGlm(vBuffer[botRingStart + s].position);
            glm::vec3 p1 = toGlm(vBuffer[botRingStart + s + 1].position);
            glm::vec3 pC{0, -yMax + hC, 0};
            glm::vec3 norm = glm::normalize(glm::cross(p0 - pC, p1 - pC));
            int st = vBuffer.size();
            vBuffer.push_back(CreateVertex(pC, norm));
            vBuffer.push_back(CreateVertex(p0, norm));
            vBuffer.push_back(CreateVertex(p1, norm));
            addTri(st, st+1, st+2);
        }
    }
    else if (definition.type == ShapeType::SlicedCylinder || definition.type == ShapeType::Ungula) {
        float r = 1.0f, h = 1.0f;
        glm::vec3 cutNorm{0, 1, 1};
        if (definition.type == ShapeType::SlicedCylinder) {
            const auto* p = std::get_if<ShapeParamsSlicedCylinder>(&definition.parameters);
            if(p) { r = p->radius; h = p->height; cutNorm = glm::normalize(p->cutNormal); }
        } else {
            const auto* p = std::get_if<ShapeParamsUngula>(&definition.parameters);
            if(p) { r = p->radius; h = p->height; float a = p->cutAngle; cutNorm = glm::normalize(glm::vec3(0, std::cos(a), std::sin(a))); }
        }
        if (cutNorm.y < 0) cutNorm = -cutNorm;
        if (std::abs(cutNorm.y) < 1e-4f) cutNorm.y = 1e-4f; // Prevent vertical slice
        
        int rings = options.maxResolution;
        int segs = options.maxResolution * 2;
        
        int startOut = vBuffer.size();
        for (int rIdx = 0; rIdx <= rings; ++rIdx) {
            for (int s = 0; s <= segs; ++s) {
                float omega = (2.0f * glm::pi<float>() * s / segs);
                float px = r * std::cos(omega);
                float pz = r * std::sin(omega);
                float botY = -h/2.0f;
                float topY = h/2.0f - (px * cutNorm.x + pz * cutNorm.z) / cutNorm.y;
                float y = botY + (topY - botY) * rIdx / rings;
                vBuffer.push_back(CreateVertex({px, y, pz}, {px/r, 0, pz/r}));
            }
        }
        for (int rIdx = 0; rIdx < rings; ++rIdx) {
            for (int s = 0; s < segs; ++s) {
                int a = startOut + rIdx * (segs + 1) + s;
                addTri(a, a + 1, a + segs + 1);
                addTri(a + segs + 1, a + 1, a + segs + 2);
            }
        }
        
        // Top Cap (Oblique)
        int topStart = vBuffer.size();
        for (int s = 0; s < segs; ++s) {
            glm::vec3 p0 = toGlm(vBuffer[startOut + rings * (segs + 1) + s].position);
            glm::vec3 p1 = toGlm(vBuffer[startOut + rings * (segs + 1) + s + 1].position);
            glm::vec3 c = {0, h/2.0f, 0};
            int st = vBuffer.size();
            vBuffer.push_back(CreateVertex(c, cutNorm));
            vBuffer.push_back(CreateVertex(p1, cutNorm));
            vBuffer.push_back(CreateVertex(p0, cutNorm));
            addTri(st, st+1, st+2);
        }
        
        // Bottom Cap (Flat)
        int botStart = vBuffer.size();
        for (int s = 0; s < segs; ++s) {
            glm::vec3 p0 = toGlm(vBuffer[startOut + s].position);
            glm::vec3 p1 = toGlm(vBuffer[startOut + s + 1].position);
            glm::vec3 c = {0, -h/2.0f, 0};
            int st = vBuffer.size();
            vBuffer.push_back(CreateVertex(c, {0, -1, 0}));
            vBuffer.push_back(CreateVertex(p0, {0, -1, 0}));
            vBuffer.push_back(CreateVertex(p1, {0, -1, 0}));
            addTri(st, st+1, st+2);
        }
    }
    else if (definition.type == ShapeType::Barrel) {
        float midR = 1.0f, endR = 0.8f, h = 2.0f;
        const auto* p = std::get_if<ShapeParamsBarrel>(&definition.parameters);
        if(p) { midR = p->midRadius; endR = p->endRadius; h = p->height; }
        
        int rings = options.maxResolution;
        int segs = options.maxResolution * 2;
        
        int startOut = vBuffer.size();
        for (int rIdx = 0; rIdx <= rings; ++rIdx) {
            float y = -h/2.0f + h * rIdx / rings;
            // Parabolic profile: r(y) = a*y^2 + b*y + c
            // r(0) = midR => c = midR
            // r(h/2) = endR => a*(h/2)^2 + midR = endR => a = (endR - midR) / (h*h/4)
            float a = (endR - midR) / ((h*h) / 4.0f);
            float rY = a * y * y + midR;
            // normal derivation: dr/dy = 2*a*y
            // tangent = (dr/dy, 1, 0)
            // normal in X-Y plane = (1, -dr/dy, 0)
            float drdy = 2.0f * a * y;
            glm::vec2 n2 = glm::normalize(glm::vec2(1.0f, -drdy));
            
            for (int s = 0; s <= segs; ++s) {
                float omega = (2.0f * glm::pi<float>() * s / segs);
                float px = rY * std::cos(omega);
                float pz = rY * std::sin(omega);
                glm::vec3 norm = {n2.x * std::cos(omega), n2.y, n2.x * std::sin(omega)};
                vBuffer.push_back(CreateVertex({px, y, pz}, norm));
            }
        }
        for (int rIdx = 0; rIdx < rings; ++rIdx) {
            for (int s = 0; s < segs; ++s) {
                int a = startOut + rIdx * (segs + 1) + s;
                addTri(a, a + 1, a + segs + 1);
                addTri(a + segs + 1, a + 1, a + segs + 2);
            }
        }
        
        // Top Cap
        int topStart = vBuffer.size();
        for (int s = 0; s < segs; ++s) {
            glm::vec3 p0 = toGlm(vBuffer[startOut + rings * (segs + 1) + s].position);
            glm::vec3 p1 = toGlm(vBuffer[startOut + rings * (segs + 1) + s + 1].position);
            int st = vBuffer.size();
            vBuffer.push_back(CreateVertex({0, h/2.0f, 0}, {0, 1, 0}));
            vBuffer.push_back(CreateVertex(p1, {0, 1, 0}));
            vBuffer.push_back(CreateVertex(p0, {0, 1, 0}));
            addTri(st, st+1, st+2);
        }
        
        // Bottom Cap
        int botStart = vBuffer.size();
        for (int s = 0; s < segs; ++s) {
            glm::vec3 p0 = toGlm(vBuffer[startOut + s].position);
            glm::vec3 p1 = toGlm(vBuffer[startOut + s + 1].position);
            int st = vBuffer.size();
            vBuffer.push_back(CreateVertex({0, -h/2.0f, 0}, {0, -1, 0}));
            vBuffer.push_back(CreateVertex(p0, {0, -1, 0}));
            vBuffer.push_back(CreateVertex(p1, {0, -1, 0}));
            addTri(st, st+1, st+2);
        }
    }
    else if (definition.type == ShapeType::TriangleMesh) {
        const auto* p = std::get_if<ShapeParamsTriangleMesh>(&definition.parameters);
        if (p && !p->vertices.empty() && p->indices.size() >= 3) {
            for (size_t i = 0; i + 2 < p->indices.size(); i += 3) {
                uint32_t i0 = p->indices[i];
                uint32_t i1 = p->indices[i + 1];
                uint32_t i2 = p->indices[i + 2];
                if (i0 < p->vertices.size() && i1 < p->vertices.size() && i2 < p->vertices.size()) {
                    glm::vec3 v0 = p->vertices[i0];
                    glm::vec3 v1 = p->vertices[i1];
                    glm::vec3 v2 = p->vertices[i2];
                    glm::vec3 normal = glm::cross(v1 - v0, v2 - v0);
                    float len = glm::length(normal);
                    if (len > 1e-6f) normal /= len; else normal = glm::vec3(0, 1, 0);

                    uint32_t cur = (uint32_t)vBuffer.size();
                    vBuffer.push_back(CreateVertex(v0, normal));
                    vBuffer.push_back(CreateVertex(v1, normal));
                    vBuffer.push_back(CreateVertex(v2, normal));
                    addTri(cur, cur + 1, cur + 2);
                }
            }
        } else {
            result.success = false;
            result.errorMessage = "TriangleMesh parameters empty or invalid.";
            return result;
        }
    }
    else if (definition.type == ShapeType::Compound) {
        const auto* p = std::get_if<ShapeParamsCompound>(&definition.parameters);
        if (p && options.shapeRegistry && options.recursionDepth < 8) {
            GeometryCompileOptions childOptions = options;
            childOptions.recursionDepth++;
            for (const auto& child : p->children) {
                const auto* childDef = options.shapeRegistry->GetShapeDef(child.handle);
                if (childDef) {
                    auto childRes = GeometryCompiler::Compile(*childDef, childOptions);
                    if (childRes.success) {
                        glm::mat4 childMat = glm::translate(glm::mat4(1.0f), child.position) *
                                             glm::mat4_cast(child.rotation) *
                                             glm::scale(glm::mat4(1.0f), child.scale);
                        glm::mat3 normMat = glm::transpose(glm::inverse(glm::mat3(childMat)));
                        uint32_t baseIdx = (uint32_t)vBuffer.size();
                        for (const auto& cv : childRes.mesh.vertices) {
                            glm::vec4 tp = childMat * glm::vec4(cv.position.x, cv.position.y, cv.position.z, 1.0f);
                            glm::vec3 tn = glm::normalize(normMat * glm::vec3(cv.normal.x, cv.normal.y, cv.normal.z));
                            vBuffer.push_back(CreateVertex(glm::vec3(tp), tn));
                        }
                        for (size_t k = 0; k < childRes.mesh.vertices.size(); ++k) {
                            iBuffer.push_back(baseIdx + (uint32_t)k);
                        }
                    }
                }
            }
        } else {
            result.success = false;
            result.errorMessage = "Compound compilation requires valid ShapeRegistry context.";
            return result;
        }
    }
    else {
        result.success = false;
        result.errorMessage = "ShapeType compilation not implemented.";
        return result;
    }

    // Unroll index buffer into pure vertex buffer for BlockMakerRenderer which doesn't use indexed rendering
    for (uint32_t idx : iBuffer) {
        if (idx < vBuffer.size()) {
            result.mesh.vertices.push_back(vBuffer[idx]);
        }
    }
    
    // Explicitly set the face list if semantic faces were not recorded
    if (result.mesh.faces.empty()) {
        Face f;
        f.indices = std::vector<int>(iBuffer.begin(), iBuffer.end());
        result.mesh.faces.push_back(f);
    }

    result.success = true;
    return result;
}

GeometryCompileResult GeometryCompiler::CompileEditableMesh(const EditableMesh& mesh, const GeometryCompileOptions& options) {
    GeometryCompileResult result;
    result.mesh.name = "EditableMeshCompiled";
    result.mesh.type = MeshType::Editor;

    if (mesh.IsEmpty() || mesh.GetFaceCount() == 0) {
        result.success = false;
        result.errorMessage = "EditableMesh e' vuota o priva di facce attive.";
        return result;
    }

    auto activeFaces = mesh.GetActiveFaceIDs();
    if (activeFaces.empty()) {
        result.success = false;
        result.errorMessage = "Nessuna faccia attiva trovata nella EditableMesh.";
        return result;
    }

    for (FaceID fId : activeFaces) {
        const auto* face = mesh.GetFace(fId);
        if (!face || face->isDeleted) continue;

        auto faceVerts = mesh.GetFaceVertices(fId);
        auto faceCorners = mesh.GetFaceCorners(fId);
        size_t vCount = faceVerts.size();
        if (vCount < 3) continue;

        // Triangolazione deterministica (Ear Clipping / fan fallback)
        std::vector<uint32_t> localTriIndices;
        if (!mesh.TriangulateFace(fId, localTriIndices) || localTriIndices.empty()) {
            localTriIndices.clear();
            for (uint32_t i = 1; i + 1 < static_cast<uint32_t>(vCount); ++i) {
                localTriIndices.push_back(0);
                localTriIndices.push_back(i);
                localTriIndices.push_back(i + 1);
            }
        }

        // Risoluzione SurfaceOverride (BM-C / BM-S)
        uint32_t resolvedMatId = (face->materialId != 0) ? face->materialId : options.defaultMaterialId;
        const SurfaceOverride* matchedOverride = nullptr;
        if (options.surfaceOverrides) {
            for (const auto& so : *options.surfaceOverrides) {
                if (so.surfaceId == face->surfaceId) {
                    matchedOverride = &so;
                    if (so.useExplicitMaterial) {
                        resolvedMatId = so.materialId;
                    }
                    break;
                }
            }
        }

        uint32_t baseVertexIdx = static_cast<uint32_t>(result.mesh.vertices.size());

        // Genera vertici dedicati per preservare UV seams e discontinuità
        for (size_t i = 0; i < vCount; ++i) {
            const auto* v = mesh.GetVertex(faceVerts[i]);
            Vertex vert{};
            if (v) {
                vert.position = { v->position.x, v->position.y, v->position.z };
                vert.normal = { face->faceNormal.x, face->faceNormal.y, face->faceNormal.z };
            }

            glm::vec2 cornerUv = (i < faceCorners.size()) ? faceCorners[i].uv : glm::vec2(0.0f);
            if (matchedOverride && !matchedOverride->uvTransform.IsIdentity()) {
                cornerUv = matchedOverride->uvTransform.Transform(cornerUv);
            } else if (options.defaultUVTransform) {
                cornerUv = options.defaultUVTransform->Transform(cornerUv);
            }

            if (matchedOverride && matchedOverride->overridePbr) {
                vert.color = { matchedOverride->baseColorFallback.x, matchedOverride->baseColorFallback.y, matchedOverride->baseColorFallback.z, 1.0f };
                vert.roughMetal = { matchedOverride->roughness, matchedOverride->metallic };
                vert.emissive = matchedOverride->emissive;
            } else {
                if (i < faceCorners.size()) {
                    vert.color = { faceCorners[i].color.r, faceCorners[i].color.g, faceCorners[i].color.b, faceCorners[i].color.a };
                } else {
                    vert.color = { 1.0f, 1.0f, 1.0f, 1.0f };
                }
                // Bridge dati: coordinate UV in roughMetal per preservare l'ABI Vulkan a 64 byte
                vert.roughMetal = { cornerUv.x, cornerUv.y };
                vert.emissive = 0.0f;
            }

            vert.materialID = resolvedMatId;
            vert.ao = 1.0f;
            vert.light = 1.0f;

            result.mesh.vertices.push_back(vert);
        }

        // Aggiunge le facce triangolate al MeshComponent
        for (size_t t = 0; t + 2 < localTriIndices.size(); t += 3) {
            Face triFace;
            triFace.indices = {
                static_cast<int>(baseVertexIdx + localTriIndices[t]),
                static_cast<int>(baseVertexIdx + localTriIndices[t + 1]),
                static_cast<int>(baseVertexIdx + localTriIndices[t + 2])
            };
            triFace.faceNormal = { face->faceNormal.x, face->faceNormal.y, face->faceNormal.z };
            triFace.matIndex = static_cast<int>(resolvedMatId);
            result.mesh.faces.push_back(triFace);
        }
    }

    result.mesh.cachedBounds = mesh.ComputeAABB();
    result.mesh.boundsCalculated = true;
    result.success = !result.mesh.vertices.empty();
    return result;
}

} // namespace fw
