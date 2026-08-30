#include "pch.h"
#include "MeshGenerators.h"
#include "SharedContext.h"
#include "MaterialRegistry.h"
#include <cmath>
#include <vector>
#include <iostream>

namespace fw {

static float sgn(float val) {
    if (val > 0.0f) return 1.0f;
    if (val < 0.0f) return -1.0f;
    return 0.0f;
}

static float ppow(float val, float p) {
    return sgn(val) * std::pow(std::abs(val), p);
}

MeshComponent MeshGenerators::MakeCube(float size) {
    MeshComponent m; 
    m.name = "Cube";
    m.type = MeshType::Editor;
    float h = size * 0.5f;
    fw::Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    
    auto addFace = [&](fw::Vec3 v0, fw::Vec3 v1, fw::Vec3 v2, fw::Vec3 v3, fw::Vec3 n) {
        m.vertices.push_back({v0, color, {0.5f, 0.0f}, 0, n, 1.0f, 1.0f, 0.0f});
        m.vertices.push_back({v1, color, {0.5f, 0.0f}, 0, n, 1.0f, 1.0f, 0.0f});
        m.vertices.push_back({v2, color, {0.5f, 0.0f}, 0, n, 1.0f, 1.0f, 0.0f});
        m.vertices.push_back({v0, color, {0.5f, 0.0f}, 0, n, 1.0f, 1.0f, 0.0f});
        m.vertices.push_back({v2, color, {0.5f, 0.0f}, 0, n, 1.0f, 1.0f, 0.0f});
        m.vertices.push_back({v3, color, {0.5f, 0.0f}, 0, n, 1.0f, 1.0f, 0.0f});
    };

    // Front (-Z)
    addFace({-h,-h,-h}, { h,-h,-h}, { h, h,-h}, {-h, h,-h}, {0,0,-1});
    // Back (+Z)
    addFace({ h,-h, h}, {-h,-h, h}, {-h, h, h}, { h, h, h}, {0,0, 1});
    // Left (-X)
    addFace({-h,-h, h}, {-h,-h,-h}, {-h, h,-h}, {-h, h, h}, {-1,0,0});
    // Right (+X)
    addFace({ h,-h,-h}, { h,-h, h}, { h, h, h}, { h, h,-h}, { 1,0,0});
    // Bottom (-Y)
    addFace({-h,-h, h}, { h,-h, h}, { h,-h,-h}, {-h,-h,-h}, {0,-1,0});
    // Top (+Y)
    addFace({-h, h,-h}, { h, h,-h}, { h, h, h}, {-h, h, h}, {0, 1,0});
    
    return m;
}

MeshComponent MeshGenerators::MakeVoxelPreview(int blockId, SharedContext* ctx) {
    MeshComponent m;
    m.name = "PreviewSphere";
    m.type = MeshType::Editor;
    
    const fw::PBRMaterialDef* mat = nullptr;
    if (ctx && ctx->materialRegistry) {
        mat = &ctx->materialRegistry->GetMaterial(blockId);
    }

    fw::Vec4 color = mat ? fw::Vec4{mat->baseColorFallback.x, mat->baseColorFallback.y, mat->baseColorFallback.z, 1.0f} : fw::Vec4{1,1,1,1};
    float rough = mat ? mat->roughnessFallback : 0.5f;
    float metal = mat ? mat->metallicFallback : 0.0f;
    float emissive = mat ? mat->emissiveStrength : 0.0f;

    auto addFace = [&](const fw::Vec3& p1, const fw::Vec3& p2, const fw::Vec3& p3, const fw::Vec3& p4, const fw::Vec3& norm) {
        float light = 1.0f;
        float ao = 1.0f;
        uint32_t materialID = blockId;
        m.vertices.push_back({p1, color, {rough, metal}, materialID, norm, ao, light, emissive});
        m.vertices.push_back({p2, color, {rough, metal}, materialID, norm, ao, light, emissive});
        m.vertices.push_back({p3, color, {rough, metal}, materialID, norm, ao, light, emissive});
        m.vertices.push_back({p1, color, {rough, metal}, materialID, norm, ao, light, emissive});
        m.vertices.push_back({p3, color, {rough, metal}, materialID, norm, ao, light, emissive});
        m.vertices.push_back({p4, color, {rough, metal}, materialID, norm, ao, light, emissive});
    };

    float h = 0.505f;
    // Front (+Z)
    addFace({-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h}, {0, 0, 1});
    // Back (-Z)
    addFace({ h,-h,-h}, {-h,-h,-h}, {-h, h,-h}, { h, h,-h}, {0, 0,-1});
    // Left (-X)
    addFace({-h,-h,-h}, {-h,-h, h}, {-h, h, h}, {-h, h,-h}, {-1,0, 0});
    // Right (+X)
    addFace({ h,-h, h}, { h,-h,-h}, { h, h,-h}, { h, h, h}, { 1,0, 0});
    // Bottom (-Y)
    addFace({-h,-h,-h}, { h,-h,-h}, { h,-h, h}, {-h,-h, h}, {0,-1,0});
    // Top (+Y)
    addFace({-h, h, h}, { h, h, h}, { h, h,-h}, {-h, h,-h}, {0, 1,0});
    
    return m;
}

MeshComponent MeshGenerators::MakeSphere(int segs, int rings, float r) {
    MeshComponent m; 
    m.name = "Sphere";
    m.type = MeshType::Editor;
    const float PI = 3.14159265f;
    
    std::vector<Vertex> tempVerts;
    for(int ri = 0; ri <= rings; ri++) {
        float phi = PI * ri / rings;
        for(int si = 0; si <= segs; si++) {
            float theta = 2 * PI * si / segs;
            Vertex v;
            v.position = {r * std::sin(phi) * std::cos(theta),
                          r * std::cos(phi),
                          r * std::sin(phi) * std::sin(theta)};
            v.color = {1.0f, 1.0f, 1.0f, 1.0f};
            v.materialID = 0;
            v.normal = v.position.norm();
            v.roughMetal = {0.5f, 0.0f};
            v.ao = 1.0f;
            v.light = 1.0f;
            v.emissive = 0.0f;
            tempVerts.push_back(v);
        }
    }
    
    for(int ri = 0; ri < rings; ri++) {
        for(int si = 0; si < segs; si++) {
            int a = ri * (segs + 1) + si;
            int b = a + 1;
            int c = a + (segs + 1);
            int d = c + 1;
            
            m.vertices.push_back(tempVerts[a]);
            m.vertices.push_back(tempVerts[b]);
            m.vertices.push_back(tempVerts[c]);
            
            m.vertices.push_back(tempVerts[c]);
            m.vertices.push_back(tempVerts[b]);
            m.vertices.push_back(tempVerts[d]);
        }
    }
    
    return m;
}

MeshComponent MeshGenerators::MakeSuperSphere(float n, float radius, int resolution) {
    MeshComponent m;
    m.name = "SuperSphere";
    m.type = MeshType::Editor;

    const float PI = 3.1415926535f;
    float exp = 2.0f / n;

    int rings = resolution;
    int segs = resolution * 2;

    std::vector<Vertex> grid;
    grid.reserve((rings + 1) * (segs + 1));

    for (int r = 0; r <= rings; ++r) {
        float eta = -PI / 2.0f + (PI * r / rings); // [-pi/2, pi/2]
        float cosEta = std::cos(eta);
        float sinEta = std::sin(eta);

        float y = radius * ppow(sinEta, exp);
        float rCross = radius * ppow(cosEta, exp);

        for (int s = 0; s <= segs; ++s) {
            float omega = -PI + (2.0f * PI * s / segs); // [-pi, pi]
            float cosOm = std::cos(omega);
            float sinOm = std::sin(omega);

            float x = rCross * ppow(cosOm, exp);
            float z = rCross * ppow(sinOm, exp);

            // Normale analitica per la superficie |x|^n + |y|^n + |z|^n = r^n
            float nx = ppow(x / radius, n - 1.0f);
            float ny = ppow(y / radius, n - 1.0f);
            float nz = ppow(z / radius, n - 1.0f);

            fw::Vec3 normVec = fw::Vec3{nx, ny, nz}.norm();

            Vertex v;
            v.position = {x, y, z};
            v.color = {1.0f, 1.0f, 1.0f, 1.0f};
            v.roughMetal = {0.4f, 0.0f};
            v.materialID = 0;
            v.normal = normVec;
            v.ao = 1.0f;
            v.light = 1.0f;
            v.emissive = 0.0f;

            grid.push_back(v);
        }
    }

    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segs; ++s) {
            int a = r * (segs + 1) + s;
            int b = a + 1;
            int c = (r + 1) * (segs + 1) + s;
            int d = c + 1;

            m.vertices.push_back(grid[a]);
            m.vertices.push_back(grid[b]);
            m.vertices.push_back(grid[c]);

            m.vertices.push_back(grid[c]);
            m.vertices.push_back(grid[b]);
            m.vertices.push_back(grid[d]);
        }
    }

    return m;
}

MeshComponent MeshGenerators::MakeGridBox(int width, int height, int depth, float thickness) {
    MeshComponent m;
    m.name = "GridBox";
    m.type = MeshType::Editor;
    
    float thick = 0.15f; 

    auto addBar = [&](glm::vec3 pos, glm::vec3 size) {
        MeshComponent bar = MakeCube(1.0f);
        for (auto& v : bar.vertices) {
            v.position.x = v.position.x * size.x + pos.x;
            v.position.y = v.position.y * size.y + pos.y;
            v.position.z = v.position.z * size.z + pos.z;
            v.color = {0.8f, 0.8f, 0.0f, 1.0f};
            m.vertices.push_back(v);
        }
    };
    
    for (int y : {0, height}) {
        for (int x = 0; x <= width; ++x) {
            addBar({(float)x, (float)y, depth/2.0f}, {thick, thick, (float)depth});
        }
        for (int z = 0; z <= depth; ++z) {
            addBar({width/2.0f, (float)y, (float)z}, {(float)width, thick, thick});
        }
    }
    
    for (int x : {0, width}) {
        for (int z : {0, depth}) {
            addBar({(float)x, height/2.0f, (float)z}, {thick, (float)height, thick});
        }
    }
    
    return m;
}

MeshComponent MeshGenerators::MakeObelisk(float baseW, float baseD, float height) {
    MeshComponent m;
    m.name = "Obelisk";
    m.type = MeshType::Editor;
    fw::Vec4 white = {1.0f, 1.0f, 1.0f, 1.0f};

    float hw = baseW * 0.5f;
    float hd = baseD * 0.5f;

    // Helper: aggiunge un quad (2 triangoli) con normale esplicita
    auto addQuad = [&](fw::Vec3 v0, fw::Vec3 v1, fw::Vec3 v2, fw::Vec3 v3, fw::Vec3 n) {
        m.vertices.push_back({v0, white, {0.0f, 1.0f}, 0, n, 1.0f, 1.0f, 0.0f});
        m.vertices.push_back({v1, white, {1.0f, 1.0f}, 0, n, 1.0f, 1.0f, 0.0f});
        m.vertices.push_back({v2, white, {1.0f, 0.0f}, 0, n, 1.0f, 1.0f, 0.0f});
        m.vertices.push_back({v0, white, {0.0f, 1.0f}, 0, n, 1.0f, 1.0f, 0.0f});
        m.vertices.push_back({v2, white, {1.0f, 0.0f}, 0, n, 1.0f, 1.0f, 0.0f});
        m.vertices.push_back({v3, white, {0.0f, 0.0f}, 0, n, 1.0f, 1.0f, 0.0f});
    };

    // 4 lati verticali del corpo principale (base piena a +Y)
    // Faccia -Z
    addQuad({-hw, 0,    -hd}, { hw, 0,    -hd}, { hw, height, -hd}, {-hw, height, -hd}, { 0,  0, -1});
    // Faccia +Z
    addQuad({ hw, 0,     hd}, {-hw, 0,     hd}, {-hw, height,  hd}, { hw, height,  hd}, { 0,  0,  1});
    // Faccia -X
    addQuad({-hw, 0,     hd}, {-hw, 0,    -hd}, {-hw, height, -hd}, {-hw, height,  hd}, {-1,  0,  0});
    // Faccia +X
    addQuad({ hw, 0,    -hd}, { hw, 0,     hd}, { hw, height,  hd}, { hw, height, -hd}, { 1,  0,  0});
    // Base -Y
    addQuad({-hw, 0,    -hd}, {-hw, 0,     hd}, { hw, 0,      hd}, { hw, 0,     -hd}, { 0, -1,  0});

    // --- Cappella piramidale in cima ---
    float capH = height + baseW; // picco
    float cs   = hw * 0.5f;     // base del cappello (metà del corpo)
    // 4 triangoli della piramide
    auto addTri = [&](fw::Vec3 a, fw::Vec3 b, fw::Vec3 c, fw::Vec3 n) {
        m.vertices.push_back({a, white, {0.0f, 0.0f}, 0, n, 1.0f, 1.0f, 0.0f});
        m.vertices.push_back({b, white, {1.0f, 0.0f}, 0, n, 1.0f, 1.0f, 0.0f});
        m.vertices.push_back({c, white, {0.5f, 1.0f}, 0, n, 1.0f, 1.0f, 0.0f});
    };
    addTri({-cs, height, -cs}, { cs, height, -cs}, { 0, capH, 0}, { 0, 0.5f, -1});
    addTri({ cs, height, -cs}, { cs, height,  cs}, { 0, capH, 0}, { 1, 0.5f,  0});
    addTri({ cs, height,  cs}, {-cs, height,  cs}, { 0, capH, 0}, { 0, 0.5f,  1});
    addTri({-cs, height,  cs}, {-cs, height, -cs}, { 0, capH, 0}, {-1, 0.5f,  0});

    return m;
}

} // namespace fw
