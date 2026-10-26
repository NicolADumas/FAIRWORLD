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

    auto getSurfaceCfg = resolveSurface;

    auto createSurfVertex = [&](const glm::vec3& pos, const glm::vec3& norm, uint32_t surfaceId = 0, float paramHeight = 1.0f) -> Vertex {
        SurfaceContext sc = resolveSurface(surfaceId);
        
        glm::vec2 rawUv(0.0f);
        if (sc.projection == UVProjectionMode::Cylindrical) {
            rawUv = CalculateCylindricalUV(pos, paramHeight, surfaceId);
        } else if (sc.projection == UVProjectionMode::Spherical) {
            rawUv = CalculateSphericalUV(norm);
        } else if ((definition.type == ShapeType::Cube || definition.type == ShapeType::Cuboid) && surfaceId < 6) {
            float col = static_cast<float>(surfaceId % 3);
            float row = static_cast<float>(surfaceId / 3);
            float tileW = 1.0f / 3.0f;
            float tileH = 1.0f / 2.0f;
            const float margin = 0.004f;
            const float cellW = tileW - 2.0f * margin;
            const float cellH = tileH - 2.0f * margin;
            float u0 = col * tileW + margin;
            float v0 = row * tileH + margin;
            float uLoc = 0.0f;
            float vLoc = 0.0f;
            float hx = 0.5f, hy = 0.5f, hz = 0.5f;
            if (definition.type == ShapeType::Cuboid) {
                const auto* pCub = std::get_if<ShapeParamsCuboid>(&definition.parameters);
                if (pCub) { hx = pCub->size.x * 0.5f; hy = pCub->size.y * 0.5f; hz = pCub->size.z * 0.5f; }
            }
            float bSx = hx * 2.0f > 1e-4f ? hx * 2.0f : 1.0f;
            float bSy = hy * 2.0f > 1e-4f ? hy * 2.0f : 1.0f;
            float bSz = hz * 2.0f > 1e-4f ? hz * 2.0f : 1.0f;
            switch (surfaceId) {
                case 0: uLoc = 1.0f - ((pos.x + hx) / bSx); vLoc = (pos.y + hy) / bSy; break; // Back (-Z)
                case 1: uLoc = (pos.x + hx) / bSx;          vLoc = (pos.y + hy) / bSy; break; // Front (+Z)
                case 2: uLoc = (pos.z + hz) / bSz;          vLoc = (pos.y + hy) / bSy; break; // Left (-X)
                case 3: uLoc = 1.0f - ((pos.z + hz) / bSz); vLoc = (pos.y + hy) / bSy; break; // Right (+X)
                case 4: uLoc = (pos.x + hx) / bSx;          vLoc = 1.0f - ((pos.z + hz) / bSz); break; // Bottom (-Y)
                case 5: uLoc = (pos.x + hx) / bSx;          vLoc = (pos.z + hz) / bSz; break; // Top (+Y)
            }
            uLoc = glm::clamp(uLoc, 0.0f, 1.0f);
            vLoc = glm::clamp(vLoc, 0.0f, 1.0f);
            rawUv = glm::vec2(u0 + uLoc * cellW, v0 + vLoc * cellH);
        } else if (definition.type == ShapeType::Wedge && surfaceId < 5) {
            float col = static_cast<float>(surfaceId % 3);
            float row = static_cast<float>(surfaceId / 3);
            float tileW = 1.0f / 3.0f;
            float tileH = 1.0f / 2.0f;
            const float margin = 0.004f;
            const float cellW = tileW - 2.0f * margin;
            const float cellH = tileH - 2.0f * margin;
            float u0 = col * tileW + margin;
            float v0 = row * tileH + margin;
            float uLoc = 0.0f;
            float vLoc = 0.0f;
            float hw = 0.5f, hh = 0.5f, hd = 0.5f;
            const auto* pW = std::get_if<ShapeParamsWedge>(&definition.parameters);
            if (pW) { hw = pW->size.x * 0.5f; hh = pW->size.y * 0.5f; hd = pW->size.z * 0.5f; }
            float bSx = hw * 2.0f > 1e-4f ? hw * 2.0f : 1.0f;
            float bSy = hh * 2.0f > 1e-4f ? hh * 2.0f : 1.0f;
            float bSz = hd * 2.0f > 1e-4f ? hd * 2.0f : 1.0f;
            switch (surfaceId) {
                case 0: uLoc = (pos.x + hw) / bSx;          vLoc = 1.0f - ((pos.y + hh) / bSy); break; // Slope (+Z/+Y)
                case 1: uLoc = (pos.x + hw) / bSx;          vLoc = 1.0f - ((pos.z + hd) / bSz); break; // Bottom (-Y)
                case 2: uLoc = 1.0f - ((pos.x + hw) / bSx); vLoc = 1.0f - ((pos.y + hh) / bSy); break; // Back (-Z)
                case 3: uLoc = (pos.z + hd) / bSz;          vLoc = 1.0f - ((pos.y + hh) / bSy); break; // Left (-X)
                case 4: uLoc = 1.0f - ((pos.z + hd) / bSz); vLoc = 1.0f - ((pos.y + hh) / bSy); break; // Right (+X)
            }
            uLoc = glm::clamp(uLoc, 0.0f, 1.0f);
            vLoc = glm::clamp(vLoc, 0.0f, 1.0f);
            rawUv = glm::vec2(u0 + uLoc * cellW, v0 + vLoc * cellH);
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
            v.roughMetal = {finalUv.x, finalUv.y};
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
        f.surfaceId = surfaceId;
        result.mesh.faces.push_back(f);
    };

    auto addQuadFace = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t surfaceId, const glm::vec3& faceNorm) {
        addTri(a, b, c);
        addTri(a, c, d);
        Face f;
        f.indices = {(int)a, (int)b, (int)c, (int)d};
        f.faceNormal = {faceNorm.x, faceNorm.y, faceNorm.z};
        f.matIndex = (int)surfaceId;
        f.surfaceId = surfaceId;
        result.mesh.faces.push_back(f);
    };

    auto buildBoxTopology = [&](const glm::vec3 pts[8], const glm::vec3 norms[6]) -> CompiledTopology {
        CompiledTopology topo;
        topo.geometricPositions.assign(pts, pts + 8);
        topo.faces.reserve(6);

        // Definizione standard delle 6 facce quad (AssetCubeFace 0..5) con winding CCW
        struct QuadDef {
            uint32_t v[4];
            uint32_t surfaceId;
        };
        const QuadDef kQuads[6] = {
            { { 1, 0, 3, 2 }, 0 }, // Back (-Z)
            { { 4, 5, 6, 7 }, 1 }, // Front (+Z)
            { { 0, 4, 7, 3 }, 2 }, // Left (-X)
            { { 5, 1, 2, 6 }, 3 }, // Right (+X)
            { { 0, 1, 5, 4 }, 4 }, // Bottom (-Y)
            { { 3, 7, 6, 2 }, 5 }  // Top (+Y)
        };

        for (int f = 0; f < 6; ++f) {
            CompiledFace cFace;
            cFace.surfaceId = kQuads[f].surfaceId;
            SurfaceContext sc = resolveSurface(cFace.surfaceId);
            cFace.materialId = sc.materialId;
            cFace.faceNormal = norms[f];
            cFace.corners.reserve(4);

            for (int i = 0; i < 4; ++i) {
                uint32_t vIdx = kQuads[f].v[i];
                const glm::vec3& pos = pts[vIdx];

                glm::vec2 rawUv(0.0f);
                if (sc.projection == UVProjectionMode::Cylindrical) {
                    rawUv = CalculateCylindricalUV(pos, 1.0f, cFace.surfaceId);
                } else if (sc.projection == UVProjectionMode::Spherical) {
                    rawUv = CalculateSphericalUV(cFace.faceNormal);
                } else {
                    float col = static_cast<float>(f % 3);
                    float row = static_cast<float>(f / 3);
                    float tileW = 1.0f / 3.0f;
                    float tileH = 1.0f / 2.0f;
                    const float margin = 0.004f;
                    const float cellW = tileW - 2.0f * margin;
                    const float cellH = tileH - 2.0f * margin;
                    float u0 = col * tileW + margin;
                    float v0 = row * tileH + margin;
                    float uLoc = 0.0f;
                    float vLoc = 0.0f;
                    glm::vec3 bMin = pts[0];
                    glm::vec3 bMax = pts[6];
                    glm::vec3 bSize = bMax - bMin;
                    if (bSize.x < 1e-4f) bSize.x = 1.0f;
                    if (bSize.y < 1e-4f) bSize.y = 1.0f;
                    if (bSize.z < 1e-4f) bSize.z = 1.0f;
                    switch (f) {
                        case 0: uLoc = 1.0f - ((pos.x - bMin.x) / bSize.x); vLoc = (pos.y - bMin.y) / bSize.y; break; // Back (-Z)
                        case 1: uLoc = (pos.x - bMin.x) / bSize.x;          vLoc = (pos.y - bMin.y) / bSize.y; break; // Front (+Z)
                        case 2: uLoc = (pos.z - bMin.z) / bSize.z;          vLoc = (pos.y - bMin.y) / bSize.y; break; // Left (-X)
                        case 3: uLoc = 1.0f - ((pos.z - bMin.z) / bSize.z); vLoc = (pos.y - bMin.y) / bSize.y; break; // Right (+X)
                        case 4: uLoc = (pos.x - bMin.x) / bSize.x;          vLoc = 1.0f - ((pos.z - bMin.z) / bSize.z); break; // Bottom (-Y)
                        case 5: uLoc = (pos.x - bMin.x) / bSize.x;          vLoc = (pos.z - bMin.z) / bSize.z; break; // Top (+Y)
                    }
                    uLoc = glm::clamp(uLoc, 0.0f, 1.0f);
                    vLoc = glm::clamp(vLoc, 0.0f, 1.0f);
                    rawUv = glm::vec2(u0 + uLoc * cellW, v0 + vLoc * cellH);
                }
                glm::vec2 finalUv = sc.uvTransform.Transform(rawUv);

                CompiledCorner corner;
                corner.geometricVertexIndex = vIdx;
                corner.uv = finalUv;
                corner.normal = cFace.faceNormal;
                corner.color = sc.hasPbrOverride ? glm::vec4(sc.baseColor, 1.0f) : glm::vec4(1.0f);

                cFace.corners.push_back(corner);
            }
            topo.faces.push_back(std::move(cFace));
        }

        return topo;
    };

    auto buildCylinderTopology = [&](float radius, float height, int segs) -> CompiledTopology {
        CompiledTopology topo;
        float halfH = height * 0.5f;
        int N = std::max(3, segs);

        // 1. Posizioni Geometriche Uniche: V = 2N + 2
        // Indice 0: Centro Top
        // Indice 1: Centro Bottom
        // Indici 2 .. 2+N-1: Anello Top
        // Indici 2+N .. 2+2N-1: Anello Bottom
        topo.geometricPositions.reserve(2 * N + 2);
        topo.geometricPositions.push_back(glm::vec3(0.0f,  halfH, 0.0f)); // 0: Top Center
        topo.geometricPositions.push_back(glm::vec3(0.0f, -halfH, 0.0f)); // 1: Bottom Center

        for (int i = 0; i < N; ++i) {
            float angle = (float)i / (float)N * glm::two_pi<float>();
            float x = radius * std::cos(angle);
            float z = radius * std::sin(angle);
            topo.geometricPositions.push_back(glm::vec3(x,  halfH, z)); // Top ring: 2 + i
        }
        for (int i = 0; i < N; ++i) {
            float angle = (float)i / (float)N * glm::two_pi<float>();
            float x = radius * std::cos(angle);
            float z = radius * std::sin(angle);
            topo.geometricPositions.push_back(glm::vec3(x, -halfH, z)); // Bottom ring: 2 + N + i
        }

        topo.faces.reserve(3 * N);

        SurfaceContext scBody = resolveSurface(0); // Mantle
        SurfaceContext scTop  = resolveSurface(1); // Top Cap
        SurfaceContext scBot  = resolveSurface(2); // Bottom Cap

        // 2. Mantle Quads (N facce quad, SurfaceID = 0)
        // Winding CCW visto dall'esterno: B_i -> T_i -> T_{i+1} -> B_{i+1}
        for (int i = 0; i < N; ++i) {
            int nextI = (i + 1) % N;
            uint32_t vBi = 2 + N + i;
            uint32_t vTi = 2 + i;
            uint32_t vTnext = 2 + nextI;
            uint32_t vBnext = 2 + N + nextI;

            CompiledFace f;
            f.surfaceId = 0;
            f.materialId = scBody.materialId;

            float midAngle = ((float)i + 0.5f) / (float)N * glm::two_pi<float>();
            f.faceNormal = glm::vec3(std::cos(midAngle), 0.0f, std::sin(midAngle));

            float u0 = (float)i / (float)N;
            float u1 = (i + 1 == N) ? 1.0f : ((float)(i + 1) / (float)N);

            float a0 = (float)i / (float)N * glm::two_pi<float>();
            float a1 = (float)(i + 1) / (float)N * glm::two_pi<float>();
            glm::vec3 n0 = glm::vec3(std::cos(a0), 0.0f, std::sin(a0));
            glm::vec3 n1 = glm::vec3(std::cos(a1), 0.0f, std::sin(a1));

            // Corner 0: Bi (u0, 0.0)
            CompiledCorner c0;
            c0.geometricVertexIndex = vBi;
            c0.uv = scBody.uvTransform.Transform(glm::vec2(u0, 0.0f));
            c0.normal = n0;
            c0.color = scBody.hasPbrOverride ? glm::vec4(scBody.baseColor, 1.0f) : glm::vec4(1.0f);
            f.corners.push_back(c0);

            // Corner 1: Ti (u0, 1.0)
            CompiledCorner c1;
            c1.geometricVertexIndex = vTi;
            c1.uv = scBody.uvTransform.Transform(glm::vec2(u0, 1.0f));
            c1.normal = n0;
            c1.color = scBody.hasPbrOverride ? glm::vec4(scBody.baseColor, 1.0f) : glm::vec4(1.0f);
            f.corners.push_back(c1);

            // Corner 2: Tnext (u1, 1.0)
            CompiledCorner c2;
            c2.geometricVertexIndex = vTnext;
            c2.uv = scBody.uvTransform.Transform(glm::vec2(u1, 1.0f));
            c2.normal = n1;
            c2.color = scBody.hasPbrOverride ? glm::vec4(scBody.baseColor, 1.0f) : glm::vec4(1.0f);
            f.corners.push_back(c2);

            // Corner 3: Bnext (u1, 0.0)
            CompiledCorner c3;
            c3.geometricVertexIndex = vBnext;
            c3.uv = scBody.uvTransform.Transform(glm::vec2(u1, 0.0f));
            c3.normal = n1;
            c3.color = scBody.hasPbrOverride ? glm::vec4(scBody.baseColor, 1.0f) : glm::vec4(1.0f);
            f.corners.push_back(c3);

            topo.faces.push_back(std::move(f));
        }

        // 3. Top Cap Triangles (N triangoli, SurfaceID = 1)
        // Winding CCW visto dall'esterno (+Y): Center -> T_{i+1} -> T_i
        for (int i = 0; i < N; ++i) {
            int nextI = (i + 1) % N;
            uint32_t vCenter = 0;
            uint32_t vTnext = 2 + nextI;
            uint32_t vTi = 2 + i;

            CompiledFace f;
            f.surfaceId = 1;
            f.materialId = scTop.materialId;
            f.faceNormal = glm::vec3(0.0f, 1.0f, 0.0f);

            auto makeTopCapCorner = [&](uint32_t vIdx) {
                CompiledCorner c;
                c.geometricVertexIndex = vIdx;
                const glm::vec3& pos = topo.geometricPositions[vIdx];
                glm::vec2 rawUv(pos.x / (2.0f * radius) + 0.5f, pos.z / (2.0f * radius) + 0.5f);
                c.uv = scTop.uvTransform.Transform(rawUv);
                c.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                c.color = scTop.hasPbrOverride ? glm::vec4(scTop.baseColor, 1.0f) : glm::vec4(1.0f);
                return c;
            };

            f.corners.push_back(makeTopCapCorner(vCenter));
            f.corners.push_back(makeTopCapCorner(vTnext));
            f.corners.push_back(makeTopCapCorner(vTi));
            topo.faces.push_back(std::move(f));
        }

        // 4. Bottom Cap Triangles (N triangoli, SurfaceID = 2)
        // Winding CCW visto dall'esterno (-Y): Center -> B_i -> B_{i+1}
        for (int i = 0; i < N; ++i) {
            int nextI = (i + 1) % N;
            uint32_t vCenter = 1;
            uint32_t vBi = 2 + N + i;
            uint32_t vBnext = 2 + N + nextI;

            CompiledFace f;
            f.surfaceId = 2;
            f.materialId = scBot.materialId;
            f.faceNormal = glm::vec3(0.0f, -1.0f, 0.0f);

            auto makeBotCapCorner = [&](uint32_t vIdx) {
                CompiledCorner c;
                c.geometricVertexIndex = vIdx;
                const glm::vec3& pos = topo.geometricPositions[vIdx];
                glm::vec2 rawUv(pos.x / (2.0f * radius) + 0.5f, -pos.z / (2.0f * radius) + 0.5f);
                c.uv = scBot.uvTransform.Transform(rawUv);
                c.normal = glm::vec3(0.0f, -1.0f, 0.0f);
                c.color = scBot.hasPbrOverride ? glm::vec4(scBot.baseColor, 1.0f) : glm::vec4(1.0f);
                return c;
            };

            f.corners.push_back(makeBotCapCorner(vCenter));
            f.corners.push_back(makeBotCapCorner(vBi));
            f.corners.push_back(makeBotCapCorner(vBnext));
            topo.faces.push_back(std::move(f));
        }

        return topo;
    };

    auto buildConeTopology = [&](float radius, float height, int segs) -> CompiledTopology {
        CompiledTopology topo;
        float halfH = height * 0.5f;
        int N = std::max(3, segs);

        // 1. Posizioni Geometriche Uniche: V = N + 2
        // Indice 0: Apex (0, +halfH, 0)
        // Indice 1: Bottom Center (0, -halfH, 0)
        // Indici 2 .. N+1: Anello Bottom (r * cos(a), -halfH, r * sin(a))
        topo.geometricPositions.reserve(N + 2);
        topo.geometricPositions.push_back(glm::vec3(0.0f,  halfH, 0.0f)); // 0: Apex
        topo.geometricPositions.push_back(glm::vec3(0.0f, -halfH, 0.0f)); // 1: Bottom Center

        for (int i = 0; i < N; ++i) {
            float angle = (float)i / (float)N * glm::two_pi<float>();
            float x = radius * std::cos(angle);
            float z = radius * std::sin(angle);
            topo.geometricPositions.push_back(glm::vec3(x, -halfH, z)); // 2 + i: Bottom ring
        }

        topo.faces.reserve(2 * N);

        SurfaceContext scMantle = resolveSurface(0); // Mantle
        SurfaceContext scBot    = resolveSurface(2); // Bottom Cap

        float slope = radius / height;

        // 2. Mantle Triangles (N triangoli, SurfaceID = 0)
        // Winding CCW visto dall'esterno: B_i -> Apex (0) -> B_{i+1}
        for (int i = 0; i < N; ++i) {
            int nextI = (i + 1) % N;
            uint32_t vBi = 2 + i;
            uint32_t vApex = 0;
            uint32_t vBnext = 2 + nextI;

            CompiledFace f;
            f.surfaceId = 0;
            f.materialId = scMantle.materialId;

            float midAngle = ((float)i + 0.5f) / (float)N * glm::two_pi<float>();
            glm::vec3 midNormRad(std::cos(midAngle), 0.0f, std::sin(midAngle));
            f.faceNormal = glm::normalize(glm::vec3(midNormRad.x, slope, midNormRad.z));

            float u0 = (float)i / (float)N;
            float u1 = (i + 1 == N) ? 1.0f : ((float)(i + 1) / (float)N);
            float uMid = ((float)i + 0.5f) / (float)N;

            float a0 = (float)i / (float)N * glm::two_pi<float>();
            float a1 = (float)(i + 1) / (float)N * glm::two_pi<float>();
            glm::vec3 n0 = glm::normalize(glm::vec3(std::cos(a0), slope, std::sin(a0)));
            glm::vec3 n1 = glm::normalize(glm::vec3(std::cos(a1), slope, std::sin(a1)));
            glm::vec3 nMid = f.faceNormal;

            // Corner 0: Bi (u0, 0.0)
            CompiledCorner c0;
            c0.geometricVertexIndex = vBi;
            c0.uv = scMantle.uvTransform.Transform(glm::vec2(u0, 0.0f));
            c0.normal = n0;
            c0.color = scMantle.hasPbrOverride ? glm::vec4(scMantle.baseColor, 1.0f) : glm::vec4(1.0f);
            f.corners.push_back(c0);

            // Corner 1: Apex (uMid, 1.0)
            CompiledCorner c1;
            c1.geometricVertexIndex = vApex;
            c1.uv = scMantle.uvTransform.Transform(glm::vec2(uMid, 1.0f));
            c1.normal = nMid;
            c1.color = scMantle.hasPbrOverride ? glm::vec4(scMantle.baseColor, 1.0f) : glm::vec4(1.0f);
            f.corners.push_back(c1);

            // Corner 2: Bnext (u1, 0.0)
            CompiledCorner c2;
            c2.geometricVertexIndex = vBnext;
            c2.uv = scMantle.uvTransform.Transform(glm::vec2(u1, 0.0f));
            c2.normal = n1;
            c2.color = scMantle.hasPbrOverride ? glm::vec4(scMantle.baseColor, 1.0f) : glm::vec4(1.0f);
            f.corners.push_back(c2);

            topo.faces.push_back(std::move(f));
        }

        // 3. Bottom Cap Triangles (N triangoli, SurfaceID = 2)
        // Winding CCW visto dall'esterno (-Y): Center (1) -> B_i -> B_{i+1}
        for (int i = 0; i < N; ++i) {
            int nextI = (i + 1) % N;
            uint32_t vCenter = 1;
            uint32_t vBi = 2 + i;
            uint32_t vBnext = 2 + nextI;

            CompiledFace f;
            f.surfaceId = 2;
            f.materialId = scBot.materialId;
            f.faceNormal = glm::vec3(0.0f, -1.0f, 0.0f);

            auto makeBotCapCorner = [&](uint32_t vIdx) {
                CompiledCorner c;
                c.geometricVertexIndex = vIdx;
                const glm::vec3& pos = topo.geometricPositions[vIdx];
                glm::vec2 rawUv(pos.x / (2.0f * radius) + 0.5f, -pos.z / (2.0f * radius) + 0.5f);
                c.uv = scBot.uvTransform.Transform(rawUv);
                c.normal = glm::vec3(0.0f, -1.0f, 0.0f);
                c.color = scBot.hasPbrOverride ? glm::vec4(scBot.baseColor, 1.0f) : glm::vec4(1.0f);
                return c;
            };

            f.corners.push_back(makeBotCapCorner(vCenter));
            f.corners.push_back(makeBotCapCorner(vBi));
            f.corners.push_back(makeBotCapCorner(vBnext));
            topo.faces.push_back(std::move(f));
        }

        return topo;
    };

    auto buildConeFrustumTopology = [&](float rBot, float rTop, float height, int segs) -> CompiledTopology {
        if (rTop <= 1e-6f && rBot > 1e-6f) {
            return buildConeTopology(rBot, height, segs);
        }
        if (rBot <= 1e-6f && rTop > 1e-6f) {
            CompiledTopology topo;
            float halfH = height * 0.5f;
            int N = std::max(3, segs);

            topo.geometricPositions.reserve(N + 2);
            topo.geometricPositions.push_back(glm::vec3(0.0f,  halfH, 0.0f)); // 0: Top Center
            topo.geometricPositions.push_back(glm::vec3(0.0f, -halfH, 0.0f)); // 1: Bottom Apex

            for (int i = 0; i < N; ++i) {
                float angle = (float)i / (float)N * glm::two_pi<float>();
                float x = rTop * std::cos(angle);
                float z = rTop * std::sin(angle);
                topo.geometricPositions.push_back(glm::vec3(x, halfH, z)); // 2 + i: Top ring
            }

            topo.faces.reserve(2 * N);

            SurfaceContext scMantle = resolveSurface(0); // Mantle
            SurfaceContext scTop    = resolveSurface(1); // Top Cap

            float slope = -rTop / height;

            for (int i = 0; i < N; ++i) {
                int nextI = (i + 1) % N;
                uint32_t vApex = 1;
                uint32_t vTi = 2 + i;
                uint32_t vTnext = 2 + nextI;

                CompiledFace f;
                f.surfaceId = 0;
                f.materialId = scMantle.materialId;

                float midAngle = ((float)i + 0.5f) / (float)N * glm::two_pi<float>();
                glm::vec3 midNormRad(std::cos(midAngle), 0.0f, std::sin(midAngle));
                f.faceNormal = glm::normalize(glm::vec3(midNormRad.x, slope, midNormRad.z));

                float u0 = (float)i / (float)N;
                float u1 = (i + 1 == N) ? 1.0f : ((float)(i + 1) / (float)N);
                float uMid = ((float)i + 0.5f) / (float)N;

                float a0 = (float)i / (float)N * glm::two_pi<float>();
                float a1 = (float)(i + 1) / (float)N * glm::two_pi<float>();
                glm::vec3 n0 = glm::normalize(glm::vec3(std::cos(a0), slope, std::sin(a0)));
                glm::vec3 n1 = glm::normalize(glm::vec3(std::cos(a1), slope, std::sin(a1)));
                glm::vec3 nMid = f.faceNormal;

                CompiledCorner c0;
                c0.geometricVertexIndex = vApex;
                c0.uv = scMantle.uvTransform.Transform(glm::vec2(uMid, 0.0f));
                c0.normal = nMid;
                c0.color = scMantle.hasPbrOverride ? glm::vec4(scMantle.baseColor, 1.0f) : glm::vec4(1.0f);
                f.corners.push_back(c0);

                CompiledCorner c1;
                c1.geometricVertexIndex = vTi;
                c1.uv = scMantle.uvTransform.Transform(glm::vec2(u0, 1.0f));
                c1.normal = n0;
                c1.color = scMantle.hasPbrOverride ? glm::vec4(scMantle.baseColor, 1.0f) : glm::vec4(1.0f);
                f.corners.push_back(c1);

                CompiledCorner c2;
                c2.geometricVertexIndex = vTnext;
                c2.uv = scMantle.uvTransform.Transform(glm::vec2(u1, 1.0f));
                c2.normal = n1;
                c2.color = scMantle.hasPbrOverride ? glm::vec4(scMantle.baseColor, 1.0f) : glm::vec4(1.0f);
                f.corners.push_back(c2);

                topo.faces.push_back(std::move(f));
            }

            for (int i = 0; i < N; ++i) {
                int nextI = (i + 1) % N;
                uint32_t vCenter = 0;
                uint32_t vTnext = 2 + nextI;
                uint32_t vTi = 2 + i;

                CompiledFace f;
                f.surfaceId = 1;
                f.materialId = scTop.materialId;
                f.faceNormal = glm::vec3(0.0f, 1.0f, 0.0f);

                auto makeTopCapCorner = [&](uint32_t vIdx) {
                    CompiledCorner c;
                    c.geometricVertexIndex = vIdx;
                    const glm::vec3& pos = topo.geometricPositions[vIdx];
                    glm::vec2 rawUv(pos.x / (2.0f * rTop) + 0.5f, pos.z / (2.0f * rTop) + 0.5f);
                    c.uv = scTop.uvTransform.Transform(rawUv);
                    c.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                    c.color = scTop.hasPbrOverride ? glm::vec4(scTop.baseColor, 1.0f) : glm::vec4(1.0f);
                    return c;
                };

                f.corners.push_back(makeTopCapCorner(vCenter));
                f.corners.push_back(makeTopCapCorner(vTnext));
                f.corners.push_back(makeTopCapCorner(vTi));
                topo.faces.push_back(std::move(f));
            }

            return topo;
        }

        CompiledTopology topo;
        float halfH = height * 0.5f;
        int N = std::max(3, segs);

        topo.geometricPositions.reserve(2 * N + 2);
        topo.geometricPositions.push_back(glm::vec3(0.0f,  halfH, 0.0f)); // 0: Top Center
        topo.geometricPositions.push_back(glm::vec3(0.0f, -halfH, 0.0f)); // 1: Bottom Center

        for (int i = 0; i < N; ++i) {
            float angle = (float)i / (float)N * glm::two_pi<float>();
            float x = rTop * std::cos(angle);
            float z = rTop * std::sin(angle);
            topo.geometricPositions.push_back(glm::vec3(x,  halfH, z)); // 2 + i: Top ring
        }
        for (int i = 0; i < N; ++i) {
            float angle = (float)i / (float)N * glm::two_pi<float>();
            float x = rBot * std::cos(angle);
            float z = rBot * std::sin(angle);
            topo.geometricPositions.push_back(glm::vec3(x, -halfH, z)); // 2 + N + i: Bottom ring
        }

        topo.faces.reserve(3 * N);

        SurfaceContext scBody = resolveSurface(0); // Mantle
        SurfaceContext scTop  = resolveSurface(1); // Top Cap
        SurfaceContext scBot  = resolveSurface(2); // Bottom Cap

        float slope = (rBot - rTop) / height;

        for (int i = 0; i < N; ++i) {
            int nextI = (i + 1) % N;
            uint32_t vBi = 2 + N + i;
            uint32_t vTi = 2 + i;
            uint32_t vTnext = 2 + nextI;
            uint32_t vBnext = 2 + N + nextI;

            CompiledFace f;
            f.surfaceId = 0;
            f.materialId = scBody.materialId;

            float midAngle = ((float)i + 0.5f) / (float)N * glm::two_pi<float>();
            glm::vec3 midNormRad(std::cos(midAngle), 0.0f, std::sin(midAngle));
            f.faceNormal = glm::normalize(glm::vec3(midNormRad.x, slope, midNormRad.z));

            float u0 = (float)i / (float)N;
            float u1 = (i + 1 == N) ? 1.0f : ((float)(i + 1) / (float)N);

            float a0 = (float)i / (float)N * glm::two_pi<float>();
            float a1 = (float)(i + 1) / (float)N * glm::two_pi<float>();
            glm::vec3 n0 = glm::normalize(glm::vec3(std::cos(a0), slope, std::sin(a0)));
            glm::vec3 n1 = glm::normalize(glm::vec3(std::cos(a1), slope, std::sin(a1)));

            CompiledCorner c0;
            c0.geometricVertexIndex = vBi;
            c0.uv = scBody.uvTransform.Transform(glm::vec2(u0, 0.0f));
            c0.normal = n0;
            c0.color = scBody.hasPbrOverride ? glm::vec4(scBody.baseColor, 1.0f) : glm::vec4(1.0f);
            f.corners.push_back(c0);

            CompiledCorner c1;
            c1.geometricVertexIndex = vTi;
            c1.uv = scBody.uvTransform.Transform(glm::vec2(u0, 1.0f));
            c1.normal = n0;
            c1.color = scBody.hasPbrOverride ? glm::vec4(scBody.baseColor, 1.0f) : glm::vec4(1.0f);
            f.corners.push_back(c1);

            CompiledCorner c2;
            c2.geometricVertexIndex = vTnext;
            c2.uv = scBody.uvTransform.Transform(glm::vec2(u1, 1.0f));
            c2.normal = n1;
            c2.color = scBody.hasPbrOverride ? glm::vec4(scBody.baseColor, 1.0f) : glm::vec4(1.0f);
            f.corners.push_back(c2);

            CompiledCorner c3;
            c3.geometricVertexIndex = vBnext;
            c3.uv = scBody.uvTransform.Transform(glm::vec2(u1, 0.0f));
            c3.normal = n1;
            c3.color = scBody.hasPbrOverride ? glm::vec4(scBody.baseColor, 1.0f) : glm::vec4(1.0f);
            f.corners.push_back(c3);

            topo.faces.push_back(std::move(f));
        }

        for (int i = 0; i < N; ++i) {
            int nextI = (i + 1) % N;
            uint32_t vCenter = 0;
            uint32_t vTnext = 2 + nextI;
            uint32_t vTi = 2 + i;

            CompiledFace f;
            f.surfaceId = 1;
            f.materialId = scTop.materialId;
            f.faceNormal = glm::vec3(0.0f, 1.0f, 0.0f);

            auto makeTopCapCorner = [&](uint32_t vIdx) {
                CompiledCorner c;
                c.geometricVertexIndex = vIdx;
                const glm::vec3& pos = topo.geometricPositions[vIdx];
                glm::vec2 rawUv(pos.x / (2.0f * rTop) + 0.5f, pos.z / (2.0f * rTop) + 0.5f);
                c.uv = scTop.uvTransform.Transform(rawUv);
                c.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                c.color = scTop.hasPbrOverride ? glm::vec4(scTop.baseColor, 1.0f) : glm::vec4(1.0f);
                return c;
            };

            f.corners.push_back(makeTopCapCorner(vCenter));
            f.corners.push_back(makeTopCapCorner(vTnext));
            f.corners.push_back(makeTopCapCorner(vTi));
            topo.faces.push_back(std::move(f));
        }

        for (int i = 0; i < N; ++i) {
            int nextI = (i + 1) % N;
            uint32_t vCenter = 1;
            uint32_t vBi = 2 + N + i;
            uint32_t vBnext = 2 + N + nextI;

            CompiledFace f;
            f.surfaceId = 2;
            f.materialId = scBot.materialId;
            f.faceNormal = glm::vec3(0.0f, -1.0f, 0.0f);

            auto makeBotCapCorner = [&](uint32_t vIdx) {
                CompiledCorner c;
                c.geometricVertexIndex = vIdx;
                const glm::vec3& pos = topo.geometricPositions[vIdx];
                glm::vec2 rawUv(pos.x / (2.0f * rBot) + 0.5f, -pos.z / (2.0f * rBot) + 0.5f);
                c.uv = scBot.uvTransform.Transform(rawUv);
                c.normal = glm::vec3(0.0f, -1.0f, 0.0f);
                c.color = scBot.hasPbrOverride ? glm::vec4(scBot.baseColor, 1.0f) : glm::vec4(1.0f);
                return c;
            };

            f.corners.push_back(makeBotCapCorner(vCenter));
            f.corners.push_back(makeBotCapCorner(vBi));
            f.corners.push_back(makeBotCapCorner(vBnext));
            topo.faces.push_back(std::move(f));
        }

        return topo;
    };

    auto buildSphereTopology = [&](float radius, int rings, int segs) -> CompiledTopology {
        CompiledTopology topo;
        int M = std::max(2, rings);
        int N = std::max(3, segs);

        // 1. Posizioni Geometriche Uniche: V = 2 + N * (M - 1)
        // Indice 0: South Pole (0, -radius, 0)
        // Indice 1: North Pole (0, +radius, 0)
        // Indici 2 .. 2 + N*(M-1) - 1: Vertici degli anelli intermedi
        // Per r in [1, M-1] e s in [0, N-1]:
        //   vIdx = 2 + (r - 1) * N + s
        topo.geometricPositions.reserve(2 + N * (M - 1));
        topo.geometricPositions.push_back(glm::vec3(0.0f, -radius, 0.0f)); // 0: South Pole
        topo.geometricPositions.push_back(glm::vec3(0.0f,  radius, 0.0f)); // 1: North Pole

        for (int r = 1; r < M; ++r) {
            float eta = -glm::half_pi<float>() + (glm::pi<float>() * (float)r / (float)M);
            float y = radius * std::sin(eta);
            float rCross = radius * std::cos(eta);
            for (int s = 0; s < N; ++s) {
                float omega = -glm::pi<float>() + (glm::two_pi<float>() * (float)s / (float)N);
                float x = rCross * std::cos(omega);
                float z = rCross * std::sin(omega);
                topo.geometricPositions.push_back(glm::vec3(x, y, z));
            }
        }

        topo.faces.reserve(N * M);

        SurfaceContext sc = resolveSurface(0); // SurfaceID = 0

        auto getVertexIndex = [&](int r, int s) -> uint32_t {
            if (r == 0) return 0; // South Pole
            if (r == M) return 1; // North Pole
            return 2 + (r - 1) * N + (s % N);
        };

        // 2. South Polar Cap (N triangoli, r = 0 -> r = 1)
        // Winding CCW visto dall'esterno: Center(0) -> V(1, s) -> V(1, s_{next})
        for (int s = 0; s < N; ++s) {
            int nextS = (s + 1) % N;
            uint32_t v0 = 0; // South Pole
            uint32_t v1 = getVertexIndex(1, s);
            uint32_t v2 = getVertexIndex(1, nextS);

            CompiledFace f;
            f.surfaceId = 0;
            f.materialId = sc.materialId;

            const glm::vec3& p0 = topo.geometricPositions[v0];
            const glm::vec3& p1 = topo.geometricPositions[v1];
            const glm::vec3& p2 = topo.geometricPositions[v2];
            f.faceNormal = glm::normalize(p0 + p1 + p2);

            float u0 = (float)s / (float)N;
            float u1 = (s + 1 == N) ? 1.0f : ((float)(s + 1) / (float)N);
            float uMid = ((float)s + 0.5f) / (float)N;
            float vLat1 = 1.0f / (float)M;

            // Corner 0: South Pole (uMid, 0.0)
            CompiledCorner c0;
            c0.geometricVertexIndex = v0;
            c0.uv = sc.uvTransform.Transform(glm::vec2(uMid, 0.0f));
            c0.normal = glm::vec3(0.0f, -1.0f, 0.0f);
            c0.color = sc.hasPbrOverride ? glm::vec4(sc.baseColor, 1.0f) : glm::vec4(1.0f);
            f.corners.push_back(c0);

            // Corner 1: V(1, s) (u0, vLat1)
            CompiledCorner c1;
            c1.geometricVertexIndex = v1;
            c1.uv = sc.uvTransform.Transform(glm::vec2(u0, vLat1));
            c1.normal = glm::normalize(p1);
            c1.color = sc.hasPbrOverride ? glm::vec4(sc.baseColor, 1.0f) : glm::vec4(1.0f);
            f.corners.push_back(c1);

            // Corner 2: V(1, nextS) (u1, vLat1)
            CompiledCorner c2;
            c2.geometricVertexIndex = v2;
            c2.uv = sc.uvTransform.Transform(glm::vec2(u1, vLat1));
            c2.normal = glm::normalize(p2);
            c2.color = sc.hasPbrOverride ? glm::vec4(sc.baseColor, 1.0f) : glm::vec4(1.0f);
            f.corners.push_back(c2);

            topo.faces.push_back(std::move(f));
        }

        // 3. Intermediate Quads (r = 1 .. M - 2, N quads per anello)
        // Winding CCW visto dall'esterno: V(r, nextS) -> V(r, s) -> V(r+1, s) -> V(r+1, nextS)
        for (int r = 1; r < M - 1; ++r) {
            float vLat0 = (float)r / (float)M;
            float vLat1 = (float)(r + 1) / (float)M;

            for (int s = 0; s < N; ++s) {
                int nextS = (s + 1) % N;
                uint32_t v0 = getVertexIndex(r, nextS);
                uint32_t v1 = getVertexIndex(r, s);
                uint32_t v2 = getVertexIndex(r + 1, s);
                uint32_t v3 = getVertexIndex(r + 1, nextS);

                CompiledFace f;
                f.surfaceId = 0;
                f.materialId = sc.materialId;

                const glm::vec3& p0 = topo.geometricPositions[v0];
                const glm::vec3& p1 = topo.geometricPositions[v1];
                const glm::vec3& p2 = topo.geometricPositions[v2];
                const glm::vec3& p3 = topo.geometricPositions[v3];
                f.faceNormal = glm::normalize(p0 + p1 + p2 + p3);

                float u0 = (float)s / (float)N;
                float u1 = (s + 1 == N) ? 1.0f : ((float)(s + 1) / (float)N);

                // Corner 0: V(r, nextS) (u1, vLat0)
                CompiledCorner c0;
                c0.geometricVertexIndex = v0;
                c0.uv = sc.uvTransform.Transform(glm::vec2(u1, vLat0));
                c0.normal = glm::normalize(p0);
                c0.color = sc.hasPbrOverride ? glm::vec4(sc.baseColor, 1.0f) : glm::vec4(1.0f);
                f.corners.push_back(c0);

                // Corner 1: V(r, s) (u0, vLat0)
                CompiledCorner c1;
                c1.geometricVertexIndex = v1;
                c1.uv = sc.uvTransform.Transform(glm::vec2(u0, vLat0));
                c1.normal = glm::normalize(p1);
                c1.color = sc.hasPbrOverride ? glm::vec4(sc.baseColor, 1.0f) : glm::vec4(1.0f);
                f.corners.push_back(c1);

                // Corner 2: V(r+1, s) (u0, vLat1)
                CompiledCorner c2;
                c2.geometricVertexIndex = v2;
                c2.uv = sc.uvTransform.Transform(glm::vec2(u0, vLat1));
                c2.normal = glm::normalize(p2);
                c2.color = sc.hasPbrOverride ? glm::vec4(sc.baseColor, 1.0f) : glm::vec4(1.0f);
                f.corners.push_back(c2);

                // Corner 3: V(r+1, nextS) (u1, vLat1)
                CompiledCorner c3;
                c3.geometricVertexIndex = v3;
                c3.uv = sc.uvTransform.Transform(glm::vec2(u1, vLat1));
                c3.normal = glm::normalize(p3);
                c3.color = sc.hasPbrOverride ? glm::vec4(sc.baseColor, 1.0f) : glm::vec4(1.0f);
                f.corners.push_back(c3);

                topo.faces.push_back(std::move(f));
            }
        }

        // 4. North Polar Cap (N triangoli, r = M - 1 -> r = M)
        // Winding CCW visto dall'esterno: V(M-1, nextS) -> V(M-1, s) -> Center(1)
        for (int s = 0; s < N; ++s) {
            int nextS = (s + 1) % N;
            uint32_t v0 = getVertexIndex(M - 1, nextS);
            uint32_t v1 = getVertexIndex(M - 1, s);
            uint32_t v2 = 1; // North Pole

            CompiledFace f;
            f.surfaceId = 0;
            f.materialId = sc.materialId;

            const glm::vec3& p0 = topo.geometricPositions[v0];
            const glm::vec3& p1 = topo.geometricPositions[v1];
            const glm::vec3& p2 = topo.geometricPositions[v2];
            f.faceNormal = glm::normalize(p0 + p1 + p2);

            float u0 = (float)s / (float)N;
            float u1 = (s + 1 == N) ? 1.0f : ((float)(s + 1) / (float)N);
            float uMid = ((float)s + 0.5f) / (float)N;
            float vLat = (float)(M - 1) / (float)M;

            // Corner 0: V(M-1, nextS) (u1, vLat)
            CompiledCorner c0;
            c0.geometricVertexIndex = v0;
            c0.uv = sc.uvTransform.Transform(glm::vec2(u1, vLat));
            c0.normal = glm::normalize(p0);
            c0.color = sc.hasPbrOverride ? glm::vec4(sc.baseColor, 1.0f) : glm::vec4(1.0f);
            f.corners.push_back(c0);

            // Corner 1: V(M-1, s) (u0, vLat)
            CompiledCorner c1;
            c1.geometricVertexIndex = v1;
            c1.uv = sc.uvTransform.Transform(glm::vec2(u0, vLat));
            c1.normal = glm::normalize(p1);
            c1.color = sc.hasPbrOverride ? glm::vec4(sc.baseColor, 1.0f) : glm::vec4(1.0f);
            f.corners.push_back(c1);

            // Corner 2: North Pole (uMid, 1.0)
            CompiledCorner c2;
            c2.geometricVertexIndex = v2;
            c2.uv = sc.uvTransform.Transform(glm::vec2(uMid, 1.0f));
            c2.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            c2.color = sc.hasPbrOverride ? glm::vec4(sc.baseColor, 1.0f) : glm::vec4(1.0f);
            f.corners.push_back(c2);

            topo.faces.push_back(std::move(f));
        }

        return topo;
    };

    auto buildHemisphereTopology = [&](float radius, float rimHeight, int rings, int segs) -> CompiledTopology {
        CompiledTopology topo;
        int M = std::max(2, rings);
        int N = std::max(3, segs);
        bool hasRim = (rimHeight > 1e-4f);

        float yBase = 0.0f;
        float yDomeBase = yBase + (hasRim ? rimHeight : 0.0f);

        size_t totalVerts = 2 + N * M + (hasRim ? N : 0);
        topo.geometricPositions.reserve(totalVerts);

        topo.geometricPositions.push_back(glm::vec3(0.0f, yDomeBase + radius, 0.0f)); // 0: North Pole
        topo.geometricPositions.push_back(glm::vec3(0.0f, yBase, 0.0f));             // 1: Bottom Center

        // Anelli cupola: r = 0 .. M - 1
        for (int r = 0; r < M; ++r) {
            float phi = glm::half_pi<float>() * (float)r / (float)M;
            float y = yDomeBase + radius * std::sin(phi);
            float rCross = radius * std::cos(phi);
            for (int s = 0; s < N; ++s) {
                float omega = -glm::pi<float>() + (glm::two_pi<float>() * (float)s / (float)N);
                topo.geometricPositions.push_back(glm::vec3(rCross * std::cos(omega), y, rCross * std::sin(omega)));
            }
        }

        // Anello inferiore del rim (se hasRim): s = 0 .. N - 1 a yBase
        if (hasRim) {
            for (int s = 0; s < N; ++s) {
                float omega = -glm::pi<float>() + (glm::two_pi<float>() * (float)s / (float)N);
                topo.geometricPositions.push_back(glm::vec3(radius * std::cos(omega), yBase, radius * std::sin(omega)));
            }
        }

        auto getDomeVIdx = [&](int r, int s) -> uint32_t {
            if (r == M) return 0; // North Pole
            return 2 + r * N + (s % N);
        };

        auto getRimBotVIdx = [&](int s) -> uint32_t {
            if (hasRim) {
                return 2 + M * N + (s % N);
            }
            return getDomeVIdx(0, s); // Se no rim, la base coincide con ring 0 della cupola
        };

        SurfaceContext scDome = resolveSurface(0);
        SurfaceContext scRim  = resolveSurface(1);
        SurfaceContext scBot  = resolveSurface(2);

        // 2. Upper Dome (SurfaceID = 0)
        // 2a. Quads intermedi: r = 0 .. M - 2
        for (int r = 0; r < M - 1; ++r) {
            float vLat0 = (float)r / (float)M;
            float vLat1 = (float)(r + 1) / (float)M;
            for (int s = 0; s < N; ++s) {
                int nextS = (s + 1) % N;
                uint32_t v0 = getDomeVIdx(r, nextS);
                uint32_t v1 = getDomeVIdx(r, s);
                uint32_t v2 = getDomeVIdx(r + 1, s);
                uint32_t v3 = getDomeVIdx(r + 1, nextS);

                CompiledFace f;
                f.surfaceId = 0;
                f.materialId = scDome.materialId;

                const glm::vec3& p0 = topo.geometricPositions[v0];
                const glm::vec3& p1 = topo.geometricPositions[v1];
                const glm::vec3& p2 = topo.geometricPositions[v2];
                const glm::vec3& p3 = topo.geometricPositions[v3];
                f.faceNormal = glm::normalize((p0 + p1 + p2 + p3) * 0.25f - glm::vec3(0.0f, yDomeBase, 0.0f));

                float u0 = (float)s / (float)N;
                float u1 = (s + 1 == N) ? 1.0f : ((float)(s + 1) / (float)N);

                auto makeDomeCorner = [&](uint32_t vIdx, float u, float vLat) {
                    CompiledCorner c;
                    c.geometricVertexIndex = vIdx;
                    c.uv = scDome.uvTransform.Transform(glm::vec2(u, vLat));
                    glm::vec3 relPos = topo.geometricPositions[vIdx] - glm::vec3(0.0f, yDomeBase, 0.0f);
                    c.normal = (glm::length(relPos) > 1e-4f) ? glm::normalize(relPos) : glm::vec3(0.0f, 1.0f, 0.0f);
                    c.color = scDome.hasPbrOverride ? glm::vec4(scDome.baseColor, 1.0f) : glm::vec4(1.0f);
                    return c;
                };

                f.corners.push_back(makeDomeCorner(v0, u1, vLat0));
                f.corners.push_back(makeDomeCorner(v1, u0, vLat0));
                f.corners.push_back(makeDomeCorner(v2, u0, vLat1));
                f.corners.push_back(makeDomeCorner(v3, u1, vLat1));
                topo.faces.push_back(std::move(f));
            }
        }

        // 2b. North Polar Triangles: r = M - 1 -> Pole (0)
        for (int s = 0; s < N; ++s) {
            int nextS = (s + 1) % N;
            uint32_t v0 = getDomeVIdx(M - 1, nextS);
            uint32_t v1 = getDomeVIdx(M - 1, s);
            uint32_t v2 = 0; // North Pole

            CompiledFace f;
            f.surfaceId = 0;
            f.materialId = scDome.materialId;

            const glm::vec3& p0 = topo.geometricPositions[v0];
            const glm::vec3& p1 = topo.geometricPositions[v1];
            const glm::vec3& p2 = topo.geometricPositions[v2];
            f.faceNormal = glm::normalize((p0 + p1 + p2) / 3.0f - glm::vec3(0.0f, yDomeBase, 0.0f));

            float u0 = (float)s / (float)N;
            float u1 = (s + 1 == N) ? 1.0f : ((float)(s + 1) / (float)N);
            float uMid = ((float)s + 0.5f) / (float)N;
            float vLat = (float)(M - 1) / (float)M;

            auto makeDomeCorner = [&](uint32_t vIdx, float u, float v) {
                CompiledCorner c;
                c.geometricVertexIndex = vIdx;
                c.uv = scDome.uvTransform.Transform(glm::vec2(u, v));
                glm::vec3 relPos = topo.geometricPositions[vIdx] - glm::vec3(0.0f, yDomeBase, 0.0f);
                c.normal = (glm::length(relPos) > 1e-4f) ? glm::normalize(relPos) : glm::vec3(0.0f, 1.0f, 0.0f);
                c.color = scDome.hasPbrOverride ? glm::vec4(scDome.baseColor, 1.0f) : glm::vec4(1.0f);
                return c;
            };

            f.corners.push_back(makeDomeCorner(v0, u1, vLat));
            f.corners.push_back(makeDomeCorner(v1, u0, vLat));
            f.corners.push_back(makeDomeCorner(v2, uMid, 1.0f));
            topo.faces.push_back(std::move(f));
        }

        // 3. Rim Quads (SurfaceID = 1, se hasRim)
        if (hasRim) {
            for (int s = 0; s < N; ++s) {
                int nextS = (s + 1) % N;
                uint32_t vBnext = getRimBotVIdx(nextS);
                uint32_t vBcurr = getRimBotVIdx(s);
                uint32_t vTcurr = getDomeVIdx(0, s);
                uint32_t vTnext = getDomeVIdx(0, nextS);

                CompiledFace f;
                f.surfaceId = 1;
                f.materialId = scRim.materialId;

                float a0 = (float)s / (float)N * glm::two_pi<float>();
                float a1 = (float)(s + 1) / (float)N * glm::two_pi<float>();
                float aMid = ((float)s + 0.5f) / (float)N * glm::two_pi<float>();
                f.faceNormal = glm::vec3(std::cos(aMid), 0.0f, std::sin(aMid));

                float u0 = (float)s / (float)N;
                float u1 = (s + 1 == N) ? 1.0f : ((float)(s + 1) / (float)N);

                auto makeRimCorner = [&](uint32_t vIdx, float u, float v, float angle) {
                    CompiledCorner c;
                    c.geometricVertexIndex = vIdx;
                    c.uv = scRim.uvTransform.Transform(glm::vec2(u, v));
                    c.normal = glm::vec3(std::cos(angle), 0.0f, std::sin(angle));
                    c.color = scRim.hasPbrOverride ? glm::vec4(scRim.baseColor, 1.0f) : glm::vec4(1.0f);
                    return c;
                };

                f.corners.push_back(makeRimCorner(vBnext, u1, 0.0f, a1));
                f.corners.push_back(makeRimCorner(vBcurr, u0, 0.0f, a0));
                f.corners.push_back(makeRimCorner(vTcurr, u0, 1.0f, a0));
                f.corners.push_back(makeRimCorner(vTnext, u1, 1.0f, a1));
                topo.faces.push_back(std::move(f));
            }
        }

        // 4. Bottom Cap Triangles (SurfaceID = 2)
        // Winding CCW visto dall'esterno (-Y): Center (1) -> B_s -> B_{nextS}
        for (int s = 0; s < N; ++s) {
            int nextS = (s + 1) % N;
            uint32_t vCenter = 1;
            uint32_t vBs = getRimBotVIdx(s);
            uint32_t vBnext = getRimBotVIdx(nextS);

            CompiledFace f;
            f.surfaceId = 2;
            f.materialId = scBot.materialId;
            f.faceNormal = glm::vec3(0.0f, -1.0f, 0.0f);

            auto makeBotCapCorner = [&](uint32_t vIdx) {
                CompiledCorner c;
                c.geometricVertexIndex = vIdx;
                const glm::vec3& pos = topo.geometricPositions[vIdx];
                glm::vec2 rawUv(pos.x / (2.0f * radius) + 0.5f, -pos.z / (2.0f * radius) + 0.5f);
                c.uv = scBot.uvTransform.Transform(rawUv);
                c.normal = glm::vec3(0.0f, -1.0f, 0.0f);
                c.color = scBot.hasPbrOverride ? glm::vec4(scBot.baseColor, 1.0f) : glm::vec4(1.0f);
                return c;
            };

            f.corners.push_back(makeBotCapCorner(vCenter));
            f.corners.push_back(makeBotCapCorner(vBs));
            f.corners.push_back(makeBotCapCorner(vBnext));
            topo.faces.push_back(std::move(f));
        }

        return topo;
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

        result.topology = buildBoxTopology(p, n);
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

        if (definition.type == ShapeType::Cuboid) {
            float hx = vx.x * 0.5f; float hy = vy.y * 0.5f; float hz = vz.z * 0.5f;
            glm::vec3 pts[8] = {
                {-hx, -hy, -hz}, { hx, -hy, -hz}, { hx,  hy, -hz}, {-hx,  hy, -hz},
                {-hx, -hy,  hz}, { hx, -hy,  hz}, { hx,  hy,  hz}, {-hx,  hy,  hz}
            };
            glm::vec3 norms[6] = {
                {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}
            };
            result.topology = buildBoxTopology(pts, norms);
        } else {
            float det = glm::dot(vx, glm::cross(vy, vz));
            if (std::abs(det) > 1e-6f) {
                glm::vec3 pts[8] = {
                    (-vx - vy - vz) * 0.5f,
                    ( vx - vy - vz) * 0.5f,
                    ( vx + vy - vz) * 0.5f,
                    (-vx + vy - vz) * 0.5f,
                    (-vx - vy + vz) * 0.5f,
                    ( vx - vy + vz) * 0.5f,
                    ( vx + vy + vz) * 0.5f,
                    (-vx + vy + vz) * 0.5f
                };
                glm::vec3 norms[6] = {
                    glm::normalize(glm::cross(vy, vx)), // -Z (Back)
                    glm::normalize(glm::cross(vx, vy)), // +Z (Front)
                    glm::normalize(glm::cross(vz, vy)), // -X (Left)
                    glm::normalize(glm::cross(vy, vz)), // +X (Right)
                    glm::normalize(glm::cross(vx, vz)), // -Y (Bottom)
                    glm::normalize(glm::cross(vz, vx))  // +Y (Top)
                };
                if (det < 0.0f) {
                    for (int f = 0; f < 6; ++f) norms[f] = -norms[f];
                }
                result.topology = buildBoxTopology(pts, norms);
            }
        }
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

        if (definition.type == ShapeType::Cylinder && rBot > 1e-6f && h > 1e-6f && segments >= 3) {
            result.topology = buildCylinderTopology(rBot, h, segments);
        } else if (definition.type == ShapeType::Cone && rBot > 1e-6f && h > 1e-6f && segments >= 3) {
            result.topology = buildConeTopology(rBot, h, segments);
        } else if (definition.type == ShapeType::ConeFrustum && h > 1e-6f && segments >= 3 && (rBot > 1e-6f || rTop > 1e-6f)) {
            result.topology = buildConeFrustumTopology(rBot, rTop, h, segments);
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
            addQuadFace(oIdx, oIdx+1, oIdx+2, oIdx+3, 0, n1);

            // Inner wall (inverted normals)
            uint32_t iIdx = vBuffer.size();
            vBuffer.push_back(CreateVertex(glm::vec3(n2.x*rIn, -halfH, n2.z*rIn), -n2));
            vBuffer.push_back(CreateVertex(glm::vec3(n1.x*rIn, -halfH, n1.z*rIn), -n1));
            vBuffer.push_back(CreateVertex(glm::vec3(n1.x*rIn,  halfH, n1.z*rIn), -n1));
            vBuffer.push_back(CreateVertex(glm::vec3(n2.x*rIn,  halfH, n2.z*rIn), -n2));
            addQuadFace(iIdx, iIdx+1, iIdx+2, iIdx+3, 1, -n2);

            // Top Cap
            uint32_t tIdx = vBuffer.size();
            vBuffer.push_back(CreateVertex(glm::vec3(n1.x*rIn,  halfH, n1.z*rIn), glm::vec3(0,1,0)));
            vBuffer.push_back(CreateVertex(glm::vec3(n2.x*rIn,  halfH, n2.z*rIn), glm::vec3(0,1,0)));
            vBuffer.push_back(CreateVertex(glm::vec3(n2.x*rOut, halfH, n2.z*rOut), glm::vec3(0,1,0)));
            vBuffer.push_back(CreateVertex(glm::vec3(n1.x*rOut, halfH, n1.z*rOut), glm::vec3(0,1,0)));
            addQuadFace(tIdx, tIdx+1, tIdx+2, tIdx+3, 2, glm::vec3(0,1,0));

            // Bottom Cap
            uint32_t bIdx = vBuffer.size();
            vBuffer.push_back(CreateVertex(glm::vec3(n2.x*rIn,  -halfH, n2.z*rIn), glm::vec3(0,-1,0)));
            vBuffer.push_back(CreateVertex(glm::vec3(n1.x*rIn,  -halfH, n1.z*rIn), glm::vec3(0,-1,0)));
            vBuffer.push_back(CreateVertex(glm::vec3(n1.x*rOut, -halfH, n1.z*rOut), glm::vec3(0,-1,0)));
            vBuffer.push_back(CreateVertex(glm::vec3(n2.x*rOut, -halfH, n2.z*rOut), glm::vec3(0,-1,0)));
            addQuadFace(bIdx, bIdx+1, bIdx+2, bIdx+3, 3, glm::vec3(0,-1,0));
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

        auto buildFace = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, uint32_t surfId = 0) {
            glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
            uint32_t idx = (uint32_t)vBuffer.size();
            vBuffer.push_back(createSurfVertex(a, n, surfId, h)); vBuffer.push_back(createSurfVertex(b, n, surfId, h));
            vBuffer.push_back(createSurfVertex(c, n, surfId, h)); vBuffer.push_back(createSurfVertex(d, n, surfId, h));
            addQuadFace(idx, idx+1, idx+2, idx+3, surfId, n);
        };
        auto buildTriFace = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, uint32_t surfId = 0) {
            glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
            uint32_t idx = (uint32_t)vBuffer.size();
            vBuffer.push_back(createSurfVertex(a, n, surfId, h)); vBuffer.push_back(createSurfVertex(b, n, surfId, h)); vBuffer.push_back(createSurfVertex(c, n, surfId, h));
            addTriFace(idx, idx+1, idx+2, surfId, n);
        };

        if (wT == 0 && dT == 0) { // true pyramid
            buildTriFace(p1, p0, t0, 0); // Back (-Z)
            buildTriFace(p3, p2, t0, 1); // Front (+Z)
            buildTriFace(p0, p3, t0, 2); // Left (-X)
            buildTriFace(p2, p1, t0, 3); // Right (+X)
        } else {
            buildFace(p1, p0, t0, t1, 0); // Back (-Z)
            buildFace(p3, p2, t2, t3, 1); // Front (+Z)
            buildFace(p0, p3, t3, t0, 2); // Left (-X)
            buildFace(p2, p1, t1, t2, 3); // Right (+X)
            buildFace(t0, t3, t2, t1, 5); // Top (+Y)
        }
        buildFace(p0, p1, p2, p3, 4); // Bottom (-Y)

        if (definition.type == ShapeType::Pyramid && wB > 1e-4f && dB > 1e-4f && h > 1e-4f) {
            CompiledTopology topo;
            topo.geometricPositions = { p0, p1, p2, p3, t0 };
            topo.faces.reserve(5);

            struct TriDef { uint32_t v[3]; uint32_t surfId; glm::vec3 normal; };
            TriDef triDefs[4] = {
                { { 1, 0, 4 }, 0, glm::normalize(glm::cross(p0 - p1, t0 - p1)) }, // Back (-Z)
                { { 3, 2, 4 }, 1, glm::normalize(glm::cross(p2 - p3, t0 - p3)) }, // Front (+Z)
                { { 0, 3, 4 }, 2, glm::normalize(glm::cross(p3 - p0, t0 - p0)) }, // Left (-X)
                { { 2, 1, 4 }, 3, glm::normalize(glm::cross(p1 - p2, t0 - p2)) }  // Right (+X)
            };

            for (int f = 0; f < 4; ++f) {
                CompiledFace cFace;
                cFace.surfaceId = triDefs[f].surfId;
                SurfaceContext sc = resolveSurface(cFace.surfaceId);
                cFace.materialId = sc.materialId;
                cFace.faceNormal = triDefs[f].normal;
                cFace.corners.reserve(3);
                for (int i = 0; i < 3; ++i) {
                    uint32_t vIdx = triDefs[f].v[i];
                    CompiledCorner corner;
                    corner.geometricVertexIndex = vIdx;
                    corner.normal = cFace.faceNormal;
                    corner.color = sc.hasPbrOverride ? glm::vec4(sc.baseColor, 1.0f) : glm::vec4(1.0f);
                    corner.uv = (i == 2) ? glm::vec2(0.5f, 1.0f) : (i == 0 ? glm::vec2(1.0f, 0.0f) : glm::vec2(0.0f, 0.0f));
                    cFace.corners.push_back(corner);
                }
                topo.faces.push_back(std::move(cFace));
            }

            // Bottom base (quad)
            CompiledFace bFace;
            bFace.surfaceId = 4;
            SurfaceContext scBot = resolveSurface(bFace.surfaceId);
            bFace.materialId = scBot.materialId;
            bFace.faceNormal = glm::vec3(0.0f, -1.0f, 0.0f);
            bFace.corners.reserve(4);
            uint32_t bVerts[4] = { 0, 1, 2, 3 };
            glm::vec2 bUvs[4] = { {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f} };
            for (int i = 0; i < 4; ++i) {
                CompiledCorner corner;
                corner.geometricVertexIndex = bVerts[i];
                corner.normal = bFace.faceNormal;
                corner.color = scBot.hasPbrOverride ? glm::vec4(scBot.baseColor, 1.0f) : glm::vec4(1.0f);
                corner.uv = bUvs[i];
                bFace.corners.push_back(corner);
            }
            topo.faces.push_back(std::move(bFace));

            result.topology = std::move(topo);
        } else if (definition.type == ShapeType::PyramidFrustum && wB > 1e-4f && dB > 1e-4f && wT > 1e-4f && dT > 1e-4f && h > 1e-4f) {
            glm::vec3 pts[8] = {
                p0, p1, t1, t0,
                p3, p2, t2, t3
            };
            glm::vec3 norms[6] = {
                glm::normalize(glm::cross(p0 - p1, t0 - p1)), // Back (-Z, Surface 0)
                glm::normalize(glm::cross(p2 - p3, t2 - p3)), // Front (+Z, Surface 1)
                glm::normalize(glm::cross(p3 - p0, t3 - p0)), // Left (-X, Surface 2)
                glm::normalize(glm::cross(p1 - p2, t1 - p2)), // Right (+X, Surface 3)
                glm::vec3(0.0f, -1.0f, 0.0f),                 // Bottom (-Y, Surface 4)
                glm::vec3(0.0f, 1.0f, 0.0f)                   // Top (+Y, Surface 5)
            };
            result.topology = buildBoxTopology(pts, norms);
        }
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

        if (definition.type == ShapeType::Sphere && radius > 1e-6f && rings >= 2 && segs >= 3) {
            result.topology = buildSphereTopology(radius, rings, segs);
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
        int rings = options.maxResolution;
        int segs = options.maxResolution * 2;
        if (definition.type == ShapeType::SphericalZone) {
            const auto* p = std::get_if<ShapeParamsSphericalZone>(&definition.parameters);
            if(p) { radius = p->radius; topY = p->topY; bottomY = p->bottomY; }
        } else if (definition.type == ShapeType::SphericalSegment) {
            const auto* p = std::get_if<ShapeParamsSphericalSegment>(&definition.parameters);
            if(p) { radius = p->radius; bottomY = p->baseY; topY = p->radius; } // Assumes top segment
        }
        if (bottomY > topY) std::swap(bottomY, topY);
        bottomY = glm::clamp(bottomY, -radius, radius);
        topY = glm::clamp(topY, -radius, radius);
        
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
    else if (definition.type == ShapeType::Hemisphere) {
        float radius = 0.5f;
        int rings = 16;
        int segs = 32;
        float rimHeight = 0.05f;
        if (const auto* p = std::get_if<ShapeParamsHemisphere>(&definition.parameters)) {
            radius = p->radius;
            rings = std::max(2, p->rings);
            segs = std::max(3, p->segments);
            rimHeight = std::max(0.0f, p->rimHeight);
        }

        float yBase = 0.0f;
        float yDomeBase = yBase + rimHeight;

        // 1. Upper Dome (SurfaceID = 0)
        int domeStartVert = vBuffer.size();
        for (int r = 0; r <= rings; ++r) {
            float phi = glm::half_pi<float>() * static_cast<float>(r) / static_cast<float>(rings);
            float y = yDomeBase + radius * std::sin(phi);
            float rCross = radius * std::cos(phi);
            for (int s = 0; s <= segs; ++s) {
                float omega = -glm::pi<float>() + (2.0f * glm::pi<float>() * s / segs);
                float nx = std::cos(phi) * std::cos(omega);
                float ny = std::sin(phi);
                float nz = std::cos(phi) * std::sin(omega);
                glm::vec3 norm = glm::normalize(glm::vec3(nx, ny, nz));
                glm::vec3 pos{ rCross * std::cos(omega), y, rCross * std::sin(omega) };
                vBuffer.push_back(createSurfVertex(pos, norm, 0));
            }
        }
        for (int r = 0; r < rings; ++r) {
            for (int s = 0; s < segs; ++s) {
                int a = domeStartVert + r * (segs + 1) + s;
                int b = a + 1;
                int c = domeStartVert + (r + 1) * (segs + 1) + s;
                int d = c + 1;
                addTri(a, b, c);
                addTri(c, b, d);
            }
        }

        // 2. Rim (SurfaceID = 1, se rimHeight > 0)
        if (rimHeight > 1e-4f) {
            int rimStartVert = vBuffer.size();
            for (int s = 0; s <= segs; ++s) {
                float omega = -glm::pi<float>() + (2.0f * glm::pi<float>() * s / segs);
                float nx = std::cos(omega);
                float nz = std::sin(omega);
                glm::vec3 norm{ nx, 0.0f, nz };
                vBuffer.push_back(createSurfVertex(glm::vec3(radius * nx, yBase, radius * nz), norm, 1));
                vBuffer.push_back(createSurfVertex(glm::vec3(radius * nx, yDomeBase, radius * nz), norm, 1));
            }
            for (int s = 0; s < segs; ++s) {
                int bl = rimStartVert + s * 2;
                int tl = bl + 1;
                int br = rimStartVert + (s + 1) * 2;
                int tr = br + 1;
                addTri(bl, br, tl);
                addTri(tl, br, tr);
            }
        }

        // 3. Bottom Cap (SurfaceID = 2)
        int botCenter = vBuffer.size();
        glm::vec3 botNorm{ 0.0f, -1.0f, 0.0f };
        vBuffer.push_back(createSurfVertex(glm::vec3(0.0f, yBase, 0.0f), botNorm, 2));
        int capRingStart = vBuffer.size();
        for (int s = 0; s <= segs; ++s) {
            float omega = -glm::pi<float>() + (2.0f * glm::pi<float>() * s / segs);
            vBuffer.push_back(createSurfVertex(glm::vec3(radius * std::cos(omega), yBase, radius * std::sin(omega)), botNorm, 2));
        }
        for (int s = 0; s < segs; ++s) {
            addTri(botCenter, capRingStart + s, capRingStart + s + 1);
        }

        if (radius > 1e-4f && rings >= 2 && segs >= 3) {
            result.topology = buildHemisphereTopology(radius, rimHeight, rings, segs);
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
    else if (definition.type == ShapeType::Wedge) {
        glm::vec3 sz(1.0f);
        if (const auto* p = std::get_if<ShapeParamsWedge>(&definition.parameters)) sz = p->size;
        float hw = sz.x * 0.5f, hh = sz.y * 0.5f, hd = sz.z * 0.5f;

        // 6 vertici canonici
        glm::vec3 p[6] = {
            {-hw, -hh, -hd}, // 0: bottom-back-left
            { hw, -hh, -hd}, // 1: bottom-back-right
            { hw,  hh, -hd}, // 2: top-back-right
            {-hw,  hh, -hd}, // 3: top-back-left
            {-hw, -hh,  hd}, // 4: bottom-front-left
            { hw, -hh,  hd}  // 5: bottom-front-right
        };

        // Normali delle 5 facce
        glm::vec3 slopeNorm = glm::normalize(glm::cross(p[5] - p[4], p[2] - p[4]));
        glm::vec3 n[5] = {
            slopeNorm,             // 0: Slope (+Z/+Y)
            {0.0f, -1.0f, 0.0f},   // 1: Bottom (-Y)
            {0.0f, 0.0f, -1.0f},   // 2: Back (-Z)
            {-1.0f, 0.0f, 0.0f},   // 3: Left (-X)
            {1.0f, 0.0f, 0.0f}     // 4: Right (+X)
        };

        uint32_t idx = 0;
        // Face 0: Sloped Quad (p4, p5, p2, p3)
        vBuffer.push_back(createSurfVertex(p[4], n[0], 0));
        vBuffer.push_back(createSurfVertex(p[5], n[0], 0));
        vBuffer.push_back(createSurfVertex(p[2], n[0], 0));
        vBuffer.push_back(createSurfVertex(p[3], n[0], 0));
        addQuadFace(idx, idx+1, idx+2, idx+3, 0, n[0]); idx += 4;

        // Face 1: Bottom Quad (p4, p0, p1, p5)
        vBuffer.push_back(createSurfVertex(p[4], n[1], 1));
        vBuffer.push_back(createSurfVertex(p[0], n[1], 1));
        vBuffer.push_back(createSurfVertex(p[1], n[1], 1));
        vBuffer.push_back(createSurfVertex(p[5], n[1], 1));
        addQuadFace(idx, idx+1, idx+2, idx+3, 1, n[1]); idx += 4;

        // Face 2: Back Quad (p1, p0, p3, p2)
        vBuffer.push_back(createSurfVertex(p[1], n[2], 2));
        vBuffer.push_back(createSurfVertex(p[0], n[2], 2));
        vBuffer.push_back(createSurfVertex(p[3], n[2], 2));
        vBuffer.push_back(createSurfVertex(p[2], n[2], 2));
        addQuadFace(idx, idx+1, idx+2, idx+3, 2, n[2]); idx += 4;

        // Face 3: Left Triangle (p4, p3, p0)
        vBuffer.push_back(createSurfVertex(p[4], n[3], 3));
        vBuffer.push_back(createSurfVertex(p[3], n[3], 3));
        vBuffer.push_back(createSurfVertex(p[0], n[3], 3));
        addTriFace(idx, idx+1, idx+2, 3, n[3]); idx += 3;

        // Face 4: Right Triangle (p1, p2, p5)
        vBuffer.push_back(createSurfVertex(p[1], n[4], 4));
        vBuffer.push_back(createSurfVertex(p[2], n[4], 4));
        vBuffer.push_back(createSurfVertex(p[5], n[4], 4));
        addTriFace(idx, idx+1, idx+2, 4, n[4]); idx += 3;

        // CompiledTopology autoritativa
        CompiledTopology topo;
        for (int i = 0; i < 6; ++i) topo.geometricPositions.push_back(p[i]);

        auto makeWedgeFace = [&](const std::vector<uint32_t>& vertIdxs, uint32_t surfId, const glm::vec3& norm) {
            CompiledFace f;
            f.surfaceId = surfId;
            f.faceNormal = norm;
            SurfaceContext sc = resolveSurface(surfId);
            f.materialId = sc.materialId;
            float col = static_cast<float>(surfId % 3);
            float row = static_cast<float>(surfId / 3);
            float tileW = 1.0f / 3.0f;
            float tileH = 1.0f / 2.0f;
            const float margin = 0.004f;
            const float cellW = tileW - 2.0f * margin;
            const float cellH = tileH - 2.0f * margin;
            float u0 = col * tileW + margin;
            float v0 = row * tileH + margin;
            for (size_t ci = 0; ci < vertIdxs.size(); ++ci) {
                CompiledCorner c;
                c.geometricVertexIndex = vertIdxs[ci];
                c.normal = norm;
                const glm::vec3& cp = p[vertIdxs[ci]];
                float uLoc = 0.5f, vLoc = 0.5f;
                switch (surfId) {
                    case 0: uLoc = (cp.x / sz.x) + 0.5f;          vLoc = 1.0f - ((cp.y / sz.y) + 0.5f); break; // Slope (+Z/+Y)
                    case 1: uLoc = (cp.x / sz.x) + 0.5f;          vLoc = 1.0f - ((cp.z / sz.z) + 0.5f); break; // Bottom (-Y)
                    case 2: uLoc = 1.0f - ((cp.x / sz.x) + 0.5f); vLoc = 1.0f - ((cp.y / sz.y) + 0.5f); break; // Back (-Z)
                    case 3: uLoc = (cp.z / sz.z) + 0.5f;          vLoc = 1.0f - ((cp.y / sz.y) + 0.5f); break; // Left (-X)
                    case 4: uLoc = 1.0f - ((cp.z / sz.z) + 0.5f); vLoc = 1.0f - ((cp.y / sz.y) + 0.5f); break; // Right (+X)
                }
                uLoc = glm::clamp(uLoc, 0.0f, 1.0f);
                vLoc = glm::clamp(vLoc, 0.0f, 1.0f);
                glm::vec2 rawUv(u0 + uLoc * cellW, v0 + vLoc * cellH);
                c.uv = rawUv;
                c.color = sc.hasPbrOverride ? glm::vec4(sc.baseColor, 1.0f) : glm::vec4(1.0f);
                f.corners.push_back(c);
            }
            return f;
        };

        topo.faces.push_back(makeWedgeFace({4, 5, 2, 3}, 0, n[0]));
        topo.faces.push_back(makeWedgeFace({4, 0, 1, 5}, 1, n[1]));
        topo.faces.push_back(makeWedgeFace({1, 0, 3, 2}, 2, n[2]));
        topo.faces.push_back(makeWedgeFace({4, 3, 0}, 3, n[3]));
        topo.faces.push_back(makeWedgeFace({1, 2, 5}, 4, n[4]));

        result.topology = std::move(topo);
    }
    else if (definition.type == ShapeType::RoundedBox) {
        glm::vec3 sz(1.0f);
        if (const auto* p = std::get_if<ShapeParamsRoundedBox>(&definition.parameters)) sz = p->size;

        float hx = sz.x * 0.5f, hy = sz.y * 0.5f, hz = sz.z * 0.5f;
        glm::vec3 pts[8] = {
            {-hx, -hy, -hz}, { hx, -hy, -hz}, { hx,  hy, -hz}, {-hx,  hy, -hz},
            {-hx, -hy,  hz}, { hx, -hy,  hz}, { hx,  hy,  hz}, {-hx,  hy,  hz}
        };
        glm::vec3 norms[6] = {
            {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}
        };

        uint32_t idx = 0;
        // -Z (Surface 0)
        vBuffer.push_back(createSurfVertex(pts[0], norms[0], 0)); vBuffer.push_back(createSurfVertex(pts[1], norms[0], 0)); vBuffer.push_back(createSurfVertex(pts[2], norms[0], 0)); vBuffer.push_back(createSurfVertex(pts[3], norms[0], 0)); addQuadFace(idx, idx+1, idx+2, idx+3, 0, norms[0]); idx+=4;
        // +Z (Surface 1)
        vBuffer.push_back(createSurfVertex(pts[5], norms[1], 1)); vBuffer.push_back(createSurfVertex(pts[4], norms[1], 1)); vBuffer.push_back(createSurfVertex(pts[7], norms[1], 1)); vBuffer.push_back(createSurfVertex(pts[6], norms[1], 1)); addQuadFace(idx, idx+1, idx+2, idx+3, 1, norms[1]); idx+=4;
        // -X (Surface 2)
        vBuffer.push_back(createSurfVertex(pts[4], norms[2], 2)); vBuffer.push_back(createSurfVertex(pts[0], norms[2], 2)); vBuffer.push_back(createSurfVertex(pts[3], norms[2], 2)); vBuffer.push_back(createSurfVertex(pts[7], norms[2], 2)); addQuadFace(idx, idx+1, idx+2, idx+3, 2, norms[2]); idx+=4;
        // +X (Surface 3)
        vBuffer.push_back(createSurfVertex(pts[1], norms[3], 3)); vBuffer.push_back(createSurfVertex(pts[5], norms[3], 3)); vBuffer.push_back(createSurfVertex(pts[6], norms[3], 3)); vBuffer.push_back(createSurfVertex(pts[2], norms[3], 3)); addQuadFace(idx, idx+1, idx+2, idx+3, 3, norms[3]); idx+=4;
        // -Y (Surface 4)
        vBuffer.push_back(createSurfVertex(pts[4], norms[4], 4)); vBuffer.push_back(createSurfVertex(pts[5], norms[4], 4)); vBuffer.push_back(createSurfVertex(pts[1], norms[4], 4)); vBuffer.push_back(createSurfVertex(pts[0], norms[4], 4)); addQuadFace(idx, idx+1, idx+2, idx+3, 4, norms[4]); idx+=4;
        // +Y (Surface 5)
        vBuffer.push_back(createSurfVertex(pts[3], norms[5], 5)); vBuffer.push_back(createSurfVertex(pts[2], norms[5], 5)); vBuffer.push_back(createSurfVertex(pts[6], norms[5], 5)); vBuffer.push_back(createSurfVertex(pts[7], norms[5], 5)); addQuadFace(idx, idx+1, idx+2, idx+3, 5, norms[5]); idx+=4;

        result.topology = buildBoxTopology(pts, norms);
    }
    else if (definition.type == ShapeType::Torus) {
        float R = 0.75f, r = 0.25f;
        int N = 24, M = 12;
        if (const auto* p = std::get_if<ShapeParamsTorus>(&definition.parameters)) {
            R = p->majorRadius; r = p->minorRadius; N = p->majorSegments; M = p->minorSegments;
        }
        N = std::clamp(N, 6, 64);
        M = std::clamp(M, 4, 32);

        CompiledTopology topo;
        topo.geometricPositions.reserve(N * M);

        for (int i = 0; i < N; ++i) {
            float theta = static_cast<float>(i) * glm::two_pi<float>() / static_cast<float>(N);
            float cosTheta = std::cos(theta), sinTheta = std::sin(theta);
            glm::vec3 center(R * cosTheta, 0.0f, R * sinTheta);
            glm::vec3 radDir(cosTheta, 0.0f, sinTheta);

            for (int j = 0; j < M; ++j) {
                float phi = static_cast<float>(j) * glm::two_pi<float>() / static_cast<float>(M);
                float cosPhi = std::cos(phi), sinPhi = std::sin(phi);
                glm::vec3 pos = center + r * (cosPhi * radDir + glm::vec3(0.0f, sinPhi, 0.0f));
                topo.geometricPositions.push_back(pos);
            }
        }

        SurfaceContext sc = resolveSurface(0);
        uint32_t idx = 0;

        for (int i = 0; i < N; ++i) {
            int nextI = (i + 1) % N;
            for (int j = 0; j < M; ++j) {
                int nextJ = (j + 1) % M;

                uint32_t v0 = i * M + j;
                uint32_t v1 = nextI * M + j;
                uint32_t v2 = nextI * M + nextJ;
                uint32_t v3 = i * M + nextJ;

                const glm::vec3& p0 = topo.geometricPositions[v0];
                const glm::vec3& p1 = topo.geometricPositions[v1];
                const glm::vec3& p2 = topo.geometricPositions[v2];
                const glm::vec3& p3 = topo.geometricPositions[v3];

                glm::vec3 fn = glm::normalize(glm::cross(p1 - p0, p3 - p0));

                vBuffer.push_back(createSurfVertex(p0, fn, 0));
                vBuffer.push_back(createSurfVertex(p1, fn, 0));
                vBuffer.push_back(createSurfVertex(p2, fn, 0));
                vBuffer.push_back(createSurfVertex(p3, fn, 0));
                addQuadFace(idx, idx+1, idx+2, idx+3, 0, fn); idx += 4;

                CompiledFace f;
                f.surfaceId = 0;
                f.faceNormal = fn;
                f.materialId = sc.materialId;

                uint32_t qVerts[4] = {v0, v1, v2, v3};
                for (int ci = 0; ci < 4; ++ci) {
                    CompiledCorner c;
                    c.geometricVertexIndex = qVerts[ci];
                    c.normal = fn;
                    c.uv = glm::vec2(0.0f);
                    c.color = sc.hasPbrOverride ? glm::vec4(sc.baseColor, 1.0f) : glm::vec4(1.0f);
                    f.corners.push_back(c);
                }
                topo.faces.push_back(std::move(f));
            }
        }

        result.topology = std::move(topo);
    }
    else if (definition.type == ShapeType::Tetrahedron) {
        float s = 1.0f;
        if (const auto* p = std::get_if<ShapeParamsTetrahedron>(&definition.parameters)) s = p->sideLength;
        float a = s / (2.0f * std::sqrt(2.0f));

        glm::vec3 p[4] = {
            { a,  a,  a}, // 0
            { a, -a, -a}, // 1
            {-a,  a, -a}, // 2
            {-a, -a,  a}  // 3
        };

        // 4 facce triangolari con normali verso l'esterno
        glm::vec3 n[4] = {
            glm::normalize(glm::cross(p[2] - p[1], p[0] - p[1])), // Face 0: (1, 2, 0)
            glm::normalize(glm::cross(p[3] - p[0], p[1] - p[0])), // Face 1: (0, 3, 1)
            glm::normalize(glm::cross(p[2] - p[0], p[3] - p[0])), // Face 2: (0, 2, 3)
            glm::normalize(glm::cross(p[3] - p[1], p[2] - p[1]))  // Face 3: (1, 3, 2)
        };

        uint32_t idx = 0;
        // Face 0: (1, 2, 0)
        vBuffer.push_back(createSurfVertex(p[1], n[0], 0)); vBuffer.push_back(createSurfVertex(p[2], n[0], 0)); vBuffer.push_back(createSurfVertex(p[0], n[0], 0));
        addTriFace(idx, idx+1, idx+2, 0, n[0]); idx += 3;
        // Face 1: (0, 3, 1)
        vBuffer.push_back(createSurfVertex(p[0], n[1], 1)); vBuffer.push_back(createSurfVertex(p[3], n[1], 1)); vBuffer.push_back(createSurfVertex(p[1], n[1], 1));
        addTriFace(idx, idx+1, idx+2, 1, n[1]); idx += 3;
        // Face 2: (0, 2, 3)
        vBuffer.push_back(createSurfVertex(p[0], n[2], 2)); vBuffer.push_back(createSurfVertex(p[2], n[2], 2)); vBuffer.push_back(createSurfVertex(p[3], n[2], 2));
        addTriFace(idx, idx+1, idx+2, 2, n[2]); idx += 3;
        // Face 3: (1, 3, 2)
        vBuffer.push_back(createSurfVertex(p[1], n[3], 3)); vBuffer.push_back(createSurfVertex(p[3], n[3], 3)); vBuffer.push_back(createSurfVertex(p[2], n[3], 3));
        addTriFace(idx, idx+1, idx+2, 3, n[3]); idx += 3;

        CompiledTopology topo;
        for (int i = 0; i < 4; ++i) topo.geometricPositions.push_back(p[i]);

        auto makeTriFace = [&](uint32_t v0, uint32_t v1, uint32_t v2, uint32_t surfId, const glm::vec3& norm) {
            CompiledFace f;
            f.surfaceId = surfId;
            f.faceNormal = norm;
            SurfaceContext sc = resolveSurface(surfId);
            f.materialId = sc.materialId;
            uint32_t vIdxs[3] = {v0, v1, v2};
            for (int ci = 0; ci < 3; ++ci) {
                CompiledCorner c;
                c.geometricVertexIndex = vIdxs[ci];
                c.normal = norm;
                c.uv = glm::vec2(0.0f);
                c.color = sc.hasPbrOverride ? glm::vec4(sc.baseColor, 1.0f) : glm::vec4(1.0f);
                f.corners.push_back(c);
            }
            return f;
        };

        topo.faces.push_back(makeTriFace(1, 2, 0, 0, n[0]));
        topo.faces.push_back(makeTriFace(0, 3, 1, 1, n[1]));
        topo.faces.push_back(makeTriFace(0, 2, 3, 2, n[2]));
        topo.faces.push_back(makeTriFace(1, 3, 2, 3, n[3]));

        result.topology = std::move(topo);
    }
    else if (definition.type == ShapeType::Ellipsoid) {
        glm::vec3 r(1.0f, 0.7f, 0.5f);
        int rings = 16, segs = 24;
        if (const auto* p = std::get_if<ShapeParamsEllipsoid>(&definition.parameters)) {
            r = p->radius; rings = p->rings; segs = p->segments;
        }
        rings = std::clamp(rings, 3, 64);
        segs = std::clamp(segs, 4, 64);

        CompiledTopology topo;
        // South pole (0) & North pole (1)
        topo.geometricPositions.push_back(glm::vec3(0.0f, -r.y, 0.0f));
        topo.geometricPositions.push_back(glm::vec3(0.0f,  r.y, 0.0f));

        for (int ri = 1; ri < rings; ++ri) {
            float phi = -glm::half_pi<float>() + static_cast<float>(ri) * glm::pi<float>() / static_cast<float>(rings);
            float cosPhi = std::cos(phi), sinPhi = std::sin(phi);
            for (int s = 0; s < segs; ++s) {
                float theta = static_cast<float>(s) * glm::two_pi<float>() / static_cast<float>(segs);
                glm::vec3 pos(r.x * cosPhi * std::cos(theta), r.y * sinPhi, r.z * cosPhi * std::sin(theta));
                topo.geometricPositions.push_back(pos);
            }
        }

        auto ellipNorm = [&](const glm::vec3& pt) {
            glm::vec3 nm(pt.x / (r.x * r.x), pt.y / (r.y * r.y), pt.z / (r.z * r.z));
            return glm::length(nm) > 1e-6f ? glm::normalize(nm) : glm::vec3(0, 1, 0);
        };

        SurfaceContext sc = resolveSurface(0);
        uint32_t idx = 0;

        auto getVIdx = [&](int ri, int s) -> uint32_t {
            if (ri == 0) return 0;
            if (ri == rings) return 1;
            return 2 + (ri - 1) * segs + (s % segs);
        };

        // South Pole fan
        for (int s = 0; s < segs; ++s) {
            uint32_t v0 = 0;
            uint32_t v1 = getVIdx(1, (s + 1) % segs);
            uint32_t v2 = getVIdx(1, s);

            const glm::vec3& p0 = topo.geometricPositions[v0];
            const glm::vec3& p1 = topo.geometricPositions[v1];
            const glm::vec3& p2 = topo.geometricPositions[v2];
            glm::vec3 fn = ellipNorm((p0 + p1 + p2) / 3.0f);

            vBuffer.push_back(createSurfVertex(p0, fn, 0));
            vBuffer.push_back(createSurfVertex(p1, fn, 0));
            vBuffer.push_back(createSurfVertex(p2, fn, 0));
            addTriFace(idx, idx+1, idx+2, 0, fn); idx += 3;

            CompiledFace f;
            f.surfaceId = 0;
            f.faceNormal = fn;
            f.materialId = sc.materialId;
            uint32_t tVerts[3] = {v0, v1, v2};
            for (int ci = 0; ci < 3; ++ci) {
                CompiledCorner c;
                c.geometricVertexIndex = tVerts[ci];
                c.normal = ellipNorm(topo.geometricPositions[tVerts[ci]]);
                c.uv = glm::vec2(0.0f);
                c.color = sc.hasPbrOverride ? glm::vec4(sc.baseColor, 1.0f) : glm::vec4(1.0f);
                f.corners.push_back(c);
            }
            topo.faces.push_back(std::move(f));
        }

        // Intermediate quads
        for (int ri = 1; ri < rings - 1; ++ri) {
            for (int s = 0; s < segs; ++s) {
                uint32_t v0 = getVIdx(ri, s);
                uint32_t v1 = getVIdx(ri, (s + 1) % segs);
                uint32_t v2 = getVIdx(ri + 1, (s + 1) % segs);
                uint32_t v3 = getVIdx(ri + 1, s);

                const glm::vec3& p0 = topo.geometricPositions[v0];
                const glm::vec3& p1 = topo.geometricPositions[v1];
                const glm::vec3& p2 = topo.geometricPositions[v2];
                const glm::vec3& p3 = topo.geometricPositions[v3];
                glm::vec3 fn = ellipNorm((p0 + p1 + p2 + p3) * 0.25f);

                vBuffer.push_back(createSurfVertex(p0, fn, 0));
                vBuffer.push_back(createSurfVertex(p1, fn, 0));
                vBuffer.push_back(createSurfVertex(p2, fn, 0));
                vBuffer.push_back(createSurfVertex(p3, fn, 0));
                addQuadFace(idx, idx+1, idx+2, idx+3, 0, fn); idx += 4;

                CompiledFace f;
                f.surfaceId = 0;
                f.faceNormal = fn;
                f.materialId = sc.materialId;
                uint32_t qVerts[4] = {v0, v1, v2, v3};
                for (int ci = 0; ci < 4; ++ci) {
                    CompiledCorner c;
                    c.geometricVertexIndex = qVerts[ci];
                    c.normal = ellipNorm(topo.geometricPositions[qVerts[ci]]);
                    c.uv = glm::vec2(0.0f);
                    c.color = sc.hasPbrOverride ? glm::vec4(sc.baseColor, 1.0f) : glm::vec4(1.0f);
                    f.corners.push_back(c);
                }
                topo.faces.push_back(std::move(f));
            }
        }

        // North Pole fan
        for (int s = 0; s < segs; ++s) {
            uint32_t v0 = 1;
            uint32_t v1 = getVIdx(rings - 1, s);
            uint32_t v2 = getVIdx(rings - 1, (s + 1) % segs);

            const glm::vec3& p0 = topo.geometricPositions[v0];
            const glm::vec3& p1 = topo.geometricPositions[v1];
            const glm::vec3& p2 = topo.geometricPositions[v2];
            glm::vec3 fn = ellipNorm((p0 + p1 + p2) / 3.0f);

            vBuffer.push_back(createSurfVertex(p0, fn, 0));
            vBuffer.push_back(createSurfVertex(p1, fn, 0));
            vBuffer.push_back(createSurfVertex(p2, fn, 0));
            addTriFace(idx, idx+1, idx+2, 0, fn); idx += 3;

            CompiledFace f;
            f.surfaceId = 0;
            f.faceNormal = fn;
            f.materialId = sc.materialId;
            uint32_t tVerts[3] = {v0, v1, v2};
            for (int ci = 0; ci < 3; ++ci) {
                CompiledCorner c;
                c.geometricVertexIndex = tVerts[ci];
                c.normal = ellipNorm(topo.geometricPositions[tVerts[ci]]);
                c.uv = glm::vec2(0.0f);
                c.color = sc.hasPbrOverride ? glm::vec4(sc.baseColor, 1.0f) : glm::vec4(1.0f);
                f.corners.push_back(c);
            }
            topo.faces.push_back(std::move(f));
        }

        result.topology = std::move(topo);
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
        for (size_t i = 0; i + 2 < result.mesh.vertices.size(); i += 3) {
            Face f;
            f.indices = { (int)i, (int)(i + 1), (int)(i + 2) };
            f.matIndex = 0;
            result.mesh.faces.push_back(f);
        }
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

        // Genera vertici dedicati per i corner del poligono
        std::vector<Vertex> cornerVerts(vCount);
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
            }

            if (matchedOverride && matchedOverride->overridePbr) {
                vert.color = { matchedOverride->baseColorFallback.x, matchedOverride->baseColorFallback.y, matchedOverride->baseColorFallback.z, 1.0f };
                vert.roughMetal = { cornerUv.x, cornerUv.y };
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

            cornerVerts[i] = vert;
        }

        // Unroll triangulated faces into pure vertex buffer for non-indexed Vulkan renderer (VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        for (size_t t = 0; t + 2 < localTriIndices.size(); t += 3) {
            uint32_t idx0 = localTriIndices[t];
            uint32_t idx1 = localTriIndices[t + 1];
            uint32_t idx2 = localTriIndices[t + 2];

            if (idx0 < cornerVerts.size() && idx1 < cornerVerts.size() && idx2 < cornerVerts.size()) {
                uint32_t triStart = static_cast<uint32_t>(result.mesh.vertices.size());
                result.mesh.vertices.push_back(cornerVerts[idx0]);
                result.mesh.vertices.push_back(cornerVerts[idx1]);
                result.mesh.vertices.push_back(cornerVerts[idx2]);

                Face triFace;
                triFace.indices = {
                    static_cast<int>(triStart),
                    static_cast<int>(triStart + 1),
                    static_cast<int>(triStart + 2)
                };
                triFace.faceNormal = { face->faceNormal.x, face->faceNormal.y, face->faceNormal.z };
                triFace.matIndex = static_cast<int>(resolvedMatId);
                triFace.surfaceId = face->surfaceId;
                result.mesh.faces.push_back(triFace);
            }
        }
    }

    result.mesh.cachedBounds = mesh.ComputeAABB();
    result.mesh.boundsCalculated = true;
    result.success = !result.mesh.vertices.empty();
    return result;
}

} // namespace fw
