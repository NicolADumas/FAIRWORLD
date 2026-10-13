#include "SurfacePaintContractTests.h"
#include "SurfacePaintingAdapter.h"
#include "blocks/sculpt/EditableMesh.h"
#include <iostream>
#include <cmath>

namespace fw {

std::vector<SurfacePaintTestResult> SurfacePaintContractTests::RunContractTests() {
    std::vector<SurfacePaintTestResult> results;
    results.reserve(15);

    // =========================================================================
    // BP-1: Raycast su Triangolo Singolo
    // =========================================================================
    {
        SurfacePaintTestResult res;
        res.testId = "BP-1";
        res.testName = "BP-1 — Raycast su Triangolo Singolo";
        res.inputDesc = "Ray da (0.25, 0.25, 2.0) verso -Z su triangolo unitario xy a Z=0";
        res.expectedDesc = "Hit=true, dist=2.0, barycentric coords w+u+v==1.0, hitPoint=(0.25, 0.25, 0.0)";

        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
        VertexID v1 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
        VertexID v2 = mesh.AddVertex(glm::vec3(0.0f, 1.0f, 0.0f));
        mesh.AddFace({v0, v1, v2}, 0, 0);

        SurfacePaintHit hit = SurfacePaintingAdapter::RaycastMesh(
            mesh, glm::vec3(0.25f, 0.25f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f)
        );

        float barySum = hit.barycentricCoords.x + hit.barycentricCoords.y + hit.barycentricCoords.z;
        bool distOk = std::abs(hit.distance - 2.0f) < 1e-4f;
        bool baryOk = std::abs(barySum - 1.0f) < 1e-4f;
        bool ptOk = (std::abs(hit.localPosition.x - 0.25f) < 1e-4f && 
                     std::abs(hit.localPosition.y - 0.25f) < 1e-4f && 
                     std::abs(hit.localPosition.z) < 1e-4f);

        bool passed = hit.valid && distOk && baryOk && ptOk;
        res.passed = passed;
        res.observedDesc = "Hit=" + std::string(hit.valid ? "YES" : "NO") + 
                           ", Dist=" + std::to_string(hit.distance) + 
                           ", BarySum=" + std::to_string(barySum);
        res.details = passed ? "PASS: Intersezione Möller-Trumbore conforme ed esatta" : "FAIL: Anomalia raycast";
        results.push_back(res);
    }

    // =========================================================================
    // BP-2: Selezione Superficie Piu Vicina
    // =========================================================================
    {
        SurfacePaintTestResult res;
        res.testId = "BP-2";
        res.testName = "BP-2 — Selezione Superficie Piu Vicina";
        res.inputDesc = "Due triangoli paralleli allineati a Z=1.0 (anteriore) e Z=0.0 (posteriore)";
        res.expectedDesc = "Hit su triangolo anteriore a Z=1.0 (distanza 1.0 rispetto a ray origin Z=2.0)";

        EditableMesh mesh;
        VertexID a0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
        VertexID a1 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
        VertexID a2 = mesh.AddVertex(glm::vec3(0.0f, 1.0f, 0.0f));
        FaceID fBack = mesh.AddFace({a0, a1, a2}, 0, 0);

        VertexID b0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 1.0f));
        VertexID b1 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, 1.0f));
        VertexID b2 = mesh.AddVertex(glm::vec3(0.0f, 1.0f, 1.0f));
        FaceID fFront = mesh.AddFace({b0, b1, b2}, 1, 0);

        SurfacePaintHit hit = SurfacePaintingAdapter::RaycastMesh(
            mesh, glm::vec3(0.2f, 0.2f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f)
        );

        bool passed = hit.valid && (hit.faceId == fFront) && (std::abs(hit.distance - 1.0f) < 1e-4f);
        res.passed = passed;
        res.observedDesc = "HitFace=" + std::string(hit.faceId == fFront ? "FRONT" : (hit.faceId == fBack ? "BACK" : "NONE")) + 
                           ", Dist=" + std::to_string(hit.distance);
        res.details = passed ? "PASS: Algoritmo z-buffering object-space corretto" : "FAIL: Selezione triangolo errato";
        results.push_back(res);
    }

    // =========================================================================
    // BP-3: Interpolazione UV Baricentrica
    // =========================================================================
    {
        SurfacePaintTestResult res;
        res.testId = "BP-3";
        res.testName = "BP-3 — Interpolazione UV Baricentrica";
        res.inputDesc = "Triangolo con UV (0,0), (1,0), (0,1); ray centrale a u=0.25, v=0.25, w=0.50";
        res.expectedDesc = "UV interpolato == (0.25, 0.25)";

        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
        VertexID v1 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
        VertexID v2 = mesh.AddVertex(glm::vec3(0.0f, 1.0f, 0.0f));

        std::vector<CornerAttributes> corners(3);
        corners[0].uv = glm::vec2(0.0f, 0.0f);
        corners[1].uv = glm::vec2(1.0f, 0.0f);
        corners[2].uv = glm::vec2(0.0f, 1.0f);

        mesh.AddFace({v0, v1, v2}, 0, 0, &corners);

        SurfacePaintHit hit = SurfacePaintingAdapter::RaycastMesh(
            mesh, glm::vec3(0.25f, 0.25f, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f)
        );

        bool passed = hit.valid && (std::abs(hit.interpolatedUV.x - 0.25f) < 1e-4f) && 
                                   (std::abs(hit.interpolatedUV.y - 0.25f) < 1e-4f);

        res.passed = passed;
        res.observedDesc = "InterpolatedUV=(" + std::to_string(hit.interpolatedUV.x) + ", " + 
                           std::to_string(hit.interpolatedUV.y) + ")";
        res.details = passed ? "PASS: Interpolazione baricentrica esatta dei corner attributes" : "FAIL: UV errate";
        results.push_back(res);
    }

    // =========================================================================
    // BP-4: UV Seam Preservation
    // =========================================================================
    {
        SurfacePaintTestResult res;
        res.testId = "BP-4";
        res.testName = "BP-4 — UV Seam Preservation";
        res.inputDesc = "Due facce adiacenti con vertici 3D condivisi ma UV disgiunti (cucitura / seam)";
        res.expectedDesc = "Ray su Faccia 1 restituisce UV Faccia 1; Ray su Faccia 2 restituisce UV Faccia 2";

        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
        VertexID v1 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
        VertexID v2 = mesh.AddVertex(glm::vec3(1.0f, 1.0f, 0.0f));
        VertexID v3 = mesh.AddVertex(glm::vec3(0.0f, 1.0f, 0.0f));
        VertexID v4 = mesh.AddVertex(glm::vec3(2.0f, 0.0f, 0.0f));
        VertexID v5 = mesh.AddVertex(glm::vec3(2.0f, 1.0f, 0.0f));

        std::vector<CornerAttributes> c1(4);
        c1[0].uv = {0.0f, 0.0f}; c1[1].uv = {0.2f, 0.0f};
        c1[2].uv = {0.2f, 0.2f}; c1[3].uv = {0.0f, 0.2f};
        mesh.AddFace({v0, v1, v2, v3}, 0, 0, &c1);

        std::vector<CornerAttributes> c2(4);
        c2[0].uv = {0.8f, 0.0f}; c2[1].uv = {1.0f, 0.0f};
        c2[2].uv = {1.0f, 0.2f}; c2[3].uv = {0.8f, 0.2f};
        mesh.AddFace({v1, v4, v5, v2}, 1, 0, &c2);

        SurfacePaintHit hitLeft = SurfacePaintingAdapter::RaycastMesh(
            mesh, glm::vec3(0.5f, 0.5f, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f)
        );
        SurfacePaintHit hitRight = SurfacePaintingAdapter::RaycastMesh(
            mesh, glm::vec3(1.5f, 0.5f, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f)
        );

        bool leftOk = hitLeft.valid && (hitLeft.interpolatedUV.x <= 0.25f);
        bool rightOk = hitRight.valid && (hitRight.interpolatedUV.x >= 0.75f);

        bool passed = leftOk && rightOk;
        res.passed = passed;
        res.observedDesc = "UV_Left.x=" + std::to_string(hitLeft.interpolatedUV.x) + 
                           ", UV_Right.x=" + std::to_string(hitRight.interpolatedUV.x);
        res.details = passed ? "PASS: CornerAttributes per-corner isolati; zero bleeding su cuciture" : "FAIL: Bleeding su seam";
        results.push_back(res);
    }

    // =========================================================================
    // BP-5: UV to Pixel Mapping (Canvas Non Quadrato)
    // =========================================================================
    {
        SurfacePaintTestResult res;
        res.testId = "BP-5";
        res.testName = "BP-5 — UV to Pixel Mapping (Canvas Non Quadrato)";
        res.inputDesc = "Conversione UV (0.5, 0.5) su canvas rettangolare 512x256";
        res.expectedDesc = "Pixel = (256, 128)";

        UVToPixelConfig cfg;
        cfg.textureWidth = 512;
        cfg.textureHeight = 256;
        cfg.wrapMode = TextureWrapMode::ClampToEdge;
        cfg.flipV = false;

        glm::vec2 px = MapUVToPixelCoords(glm::vec2(0.5f, 0.5f), cfg);

        bool passed = (std::abs(px.x - 256.0f) < 1.0f && std::abs(px.y - 128.0f) < 1.0f);
        res.passed = passed;
        res.observedDesc = "PixelCoords=(" + std::to_string(px.x) + ", " + std::to_string(px.y) + ")";
        res.details = passed ? "PASS: Mappatura corretta per texture con aspect ratio non unitario" : "FAIL: Coordinate pixel errate";
        results.push_back(res);
    }

    // =========================================================================
    // BP-6: Wrap Modes (Repeat / ClampToEdge / MirroredRepeat)
    // =========================================================================
    {
        SurfacePaintTestResult res;
        res.testId = "BP-6";
        res.testName = "BP-6 — Wrap Modes (Repeat / Clamp / Mirror)";
        res.inputDesc = "Coordinate UV fuori range: u=1.25, v=-0.30 con wrap modes Repeat, Clamp, MirroredRepeat";
        res.expectedDesc = "Repeat: (0.25, 0.70); Clamp: (1.00, 0.00); MirroredRepeat: (0.75, 0.30)";

        float repU = ApplyWrapModeScalar(1.25f, TextureWrapMode::Repeat);
        float repV = ApplyWrapModeScalar(-0.30f, TextureWrapMode::Repeat);
        float clmU = ApplyWrapModeScalar(1.25f, TextureWrapMode::ClampToEdge);
        float clmV = ApplyWrapModeScalar(-0.30f, TextureWrapMode::ClampToEdge);
        float mirU = ApplyWrapModeScalar(1.25f, TextureWrapMode::MirroredRepeat);
        float mirV = ApplyWrapModeScalar(-0.30f, TextureWrapMode::MirroredRepeat);

        bool repOk = (std::abs(repU - 0.25f) < 1e-4f && std::abs(repV - 0.70f) < 1e-4f);
        bool clmOk = (std::abs(clmU - 1.00f) < 1e-4f && std::abs(clmV - 0.00f) < 1e-4f);
        bool mirOk = (std::abs(mirU - 0.75f) < 1e-4f && std::abs(mirV - 0.30f) < 1e-4f);

        bool passed = repOk && clmOk && mirOk;
        res.passed = passed;
        res.observedDesc = "Rep=(" + std::to_string(repU) + "," + std::to_string(repV) + 
                           "), Clm=(" + std::to_string(clmU) + "," + std::to_string(clmV) + 
                           "), Mir=(" + std::to_string(mirU) + "," + std::to_string(mirV) + ")";
        res.details = passed ? "PASS: Wrap modes scalari Repeat, Clamp e Mirror verificati e deterministici" : "FAIL: Wrap mode errato";
        results.push_back(res);
    }

    // =========================================================================
    // BP-7: Pittura Locale su Faccia Singola del Cubo (Top Face)
    // =========================================================================
    {
        SurfacePaintTestResult res;
        res.testId = "BP-7";
        res.testName = "BP-7 — Pittura Locale su Faccia Singola del Cubo";
        res.inputDesc = "Cubo con 6 facce; stroke rosso al centro della faccia superiore (+Y, surfaceId=5)";
        res.expectedDesc = "Solo centro faccia Top rosso; bordi Top bianchi; altre 5 facce intatte; mesh inalterata";

        EditableMesh cubeMesh = EditableMesh::CreateCube(1.0f);
        uint64_t geomRevBefore = cubeMesh.GetGeometryRevision();
        uint64_t topoRevBefore = cubeMesh.GetTopologyRevision();

        TextureTargetResolver resolver;
        auto sharedMaterialService = std::make_shared<TexturePaintingService>();
        sharedMaterialService->InitializeCanvas(64, 64, glm::vec4(1.0f)); // Canvas bianco di default
        resolver.RegisterMaterialTarget(0, PaintChannel::BaseColor, sharedMaterialService);

        SurfacePaintingAdapter adapter;
        adapter.SetTargetResolver(&resolver);
        adapter.SetIsolateBySurfaceId(true);

        PaintBrush redBrush;
        redBrush.radiusPixels = 6.0f;
        redBrush.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

        // Raycast dal vertice superiore (0, 2, 0) verso il basso (0, -1, 0) colpisce Top (+Y)
        bool startOk = adapter.BeginStroke3D(cubeMesh, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), redBrush);
        PaintDirtyRect dr = adapter.EndStroke3D();

        auto topCanvas = resolver.GetSurfaceTarget(5, PaintChannel::BaseColor);
        bool topIsolatedExists = (topCanvas != nullptr);

        ImageBitmap topBmp;
        if (topCanvas) topCanvas->CompositeLayers(topBmp);
        glm::vec4 centerTopPx = topBmp.GetPixel(32, 32);
        glm::vec4 cornerTopPx = topBmp.GetPixel(2, 2);

        ImageBitmap sharedBmp;
        sharedMaterialService->CompositeLayers(sharedBmp);
        glm::vec4 sharedCenterPx = sharedBmp.GetPixel(32, 32);

        bool topCenterRed = (centerTopPx.r > 0.8f && centerTopPx.g < 0.2f && centerTopPx.b < 0.2f);
        bool topCornerWhite = (cornerTopPx.r > 0.9f && cornerTopPx.g > 0.9f && cornerTopPx.b > 0.9f);
        bool sharedUntouched = (sharedCenterPx.r > 0.9f && sharedCenterPx.g > 0.9f && sharedCenterPx.b > 0.9f);
        bool meshUntouched = (geomRevBefore == cubeMesh.GetGeometryRevision()) && 
                             (topoRevBefore == cubeMesh.GetTopologyRevision());

        bool passed = startOk && topIsolatedExists && topCenterRed && topCornerWhite && sharedUntouched && meshUntouched;
        res.passed = passed;
        res.observedDesc = "TopCenterRed=" + std::string(topCenterRed ? "YES" : "NO") + 
                           ", TopCornerWhite=" + (topCornerWhite ? "YES" : "NO") + 
                           ", SharedUntouched=" + (sharedUntouched ? "YES" : "NO") + 
                           ", MeshUntouched=" + (meshUntouched ? "YES" : "NO");
        res.details = passed ? "PASS: Requisito fondamentale pittura localizzata e isolamento completato" : "FAIL: Contaminazione o fallimento pittura locale";
        results.push_back(res);
    }

    // =========================================================================
    // BP-8: Stesso MaterialID su Piu Facce Senza Contaminazione (Copy-On-Write)
    // =========================================================================
    {
        SurfacePaintTestResult res;
        res.testId = "BP-8";
        res.testName = "BP-8 — Stesso MaterialID su Piu Facce Senza Contaminazione (COW)";
        res.inputDesc = "Face 0 e Face 1 condividono MaterialID=0; pittura su Face 0 con COW attivo";
        res.expectedDesc = "Face 0 riceve pixel rossi; target Face 1 rimane completamente bianco (zero contaminazione)";

        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
        VertexID v1 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
        VertexID v2 = mesh.AddVertex(glm::vec3(1.0f, 1.0f, 0.0f));
        VertexID v3 = mesh.AddVertex(glm::vec3(0.0f, 1.0f, 0.0f));
        VertexID v4 = mesh.AddVertex(glm::vec3(2.0f, 0.0f, 0.0f));
        VertexID v5 = mesh.AddVertex(glm::vec3(2.0f, 1.0f, 0.0f));

        mesh.AddFace({v0, v1, v2, v3}, 0, 0); // surfaceId=0, materialId=0
        mesh.AddFace({v1, v4, v5, v2}, 1, 0); // surfaceId=1, materialId=0

        TextureTargetResolver resolver;
        auto sharedService = std::make_shared<TexturePaintingService>();
        sharedService->InitializeCanvas(64, 64, glm::vec4(1.0f));
        resolver.RegisterMaterialTarget(0, PaintChannel::BaseColor, sharedService);

        SurfacePaintingAdapter adapter;
        adapter.SetTargetResolver(&resolver);
        adapter.SetIsolateBySurfaceId(true);

        PaintBrush brush;
        brush.radiusPixels = 6.0f;
        brush.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

        adapter.BeginStroke3D(mesh, glm::vec3(0.5f, 0.5f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f), brush);
        adapter.EndStroke3D();

        auto surf0 = resolver.GetSurfaceTarget(0, PaintChannel::BaseColor);
        auto surf1Resolved = resolver.ResolveTarget(SurfacePaintHit{true, "", 0, INVALID_FACE_ID, 0, 1, 0}, PaintChannel::BaseColor, false);

        ImageBitmap bmp0, bmp1;
        if (surf0) surf0->CompositeLayers(bmp0);
        if (surf1Resolved.paintingService) surf1Resolved.paintingService->CompositeLayers(bmp1);

        glm::vec4 col0 = bmp0.GetPixel(32, 32);
        glm::vec4 col1 = bmp1.GetPixel(32, 32);

        bool surf0Painted = (col0.r > 0.8f && col0.g < 0.2f);
        bool surf1Untouched = (col1.r > 0.9f && col1.g > 0.9f && col1.b > 0.9f);

        bool passed = surf0Painted && surf1Untouched;
        res.passed = passed;
        res.observedDesc = "Surf0Painted=" + std::string(surf0Painted ? "YES" : "NO") + 
                           ", Surf1Untouched=" + (surf1Untouched ? "YES" : "NO");
        res.details = passed ? "PASS: Copy-On-Write isola perfettamente superfici con lo stesso MaterialID" : "FAIL: Contaminazione tra superfici con stesso MaterialID";
        results.push_back(res);
    }

    // =========================================================================
    // BP-9: UV Sovrapposte e Isolamento Esplicito
    // =========================================================================
    {
        SurfacePaintTestResult res;
        res.testId = "BP-9";
        res.testName = "BP-9 — UV Sovrapposte e Isolamento Esplicito";
        res.inputDesc = "Due facce geometriche differenti aventi UV coincidenti [0,1]x[0,1]; pittura su Faccia A con isolamento";
        res.expectedDesc = "Canvas di Faccia A modificato; canvas di Faccia B inalterato (zero pixel sporcati)";

        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
        VertexID v1 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
        VertexID v2 = mesh.AddVertex(glm::vec3(1.0f, 1.0f, 0.0f));
        VertexID v3 = mesh.AddVertex(glm::vec3(0.0f, 1.0f, 0.0f));

        VertexID v4 = mesh.AddVertex(glm::vec3(3.0f, 0.0f, 0.0f));
        VertexID v5 = mesh.AddVertex(glm::vec3(4.0f, 0.0f, 0.0f));
        VertexID v6 = mesh.AddVertex(glm::vec3(4.0f, 1.0f, 0.0f));
        VertexID v7 = mesh.AddVertex(glm::vec3(3.0f, 1.0f, 0.0f));

        std::vector<CornerAttributes> uvShared(4);
        uvShared[0].uv = {0.0f, 0.0f}; uvShared[1].uv = {1.0f, 0.0f};
        uvShared[2].uv = {1.0f, 1.0f}; uvShared[3].uv = {0.0f, 1.0f};

        mesh.AddFace({v0, v1, v2, v3}, 0, 0, &uvShared);
        mesh.AddFace({v4, v5, v6, v7}, 1, 0, &uvShared);

        TextureTargetResolver resolver;
        auto s0 = std::make_shared<TexturePaintingService>();
        auto s1 = std::make_shared<TexturePaintingService>();
        s0->InitializeCanvas(64, 64, glm::vec4(1.0f));
        s1->InitializeCanvas(64, 64, glm::vec4(1.0f));
        resolver.RegisterSurfaceTarget(0, PaintChannel::BaseColor, s0);
        resolver.RegisterSurfaceTarget(1, PaintChannel::BaseColor, s1);

        SurfacePaintingAdapter adapter;
        adapter.SetTargetResolver(&resolver);
        adapter.SetIsolateBySurfaceId(true);

        PaintBrush greenBrush;
        greenBrush.radiusPixels = 6.0f;
        greenBrush.color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);

        // Raycast su Face 0 (centro x=0.5)
        adapter.BeginStroke3D(mesh, glm::vec3(0.5f, 0.5f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f), greenBrush);
        adapter.EndStroke3D();

        ImageBitmap b0, b1;
        s0->CompositeLayers(b0);
        s1->CompositeLayers(b1);

        glm::vec4 pxA = b0.GetPixel(32, 32);
        glm::vec4 pxB = b1.GetPixel(32, 32);

        bool faceAPainted = (pxA.g > 0.8f && pxA.r < 0.2f);
        bool faceBBlank = (pxB.r > 0.9f && pxB.g > 0.9f && pxB.b > 0.9f);

        bool passed = faceAPainted && faceBBlank;
        res.passed = passed;
        res.observedDesc = "FaceAPainted=" + std::string(faceAPainted ? "YES" : "NO") + 
                           ", FaceBBlank=" + (faceBBlank ? "YES" : "NO");
        res.details = passed ? "PASS: Isolamento per superficie garantito anche con UV coincidenti" : "FAIL: Bleeding su UV sovrapposte";
        results.push_back(res);
    }

    // =========================================================================
    // BP-10: Stroke Continuo su Triangoli Adiacenti
    // =========================================================================
    {
        SurfacePaintTestResult res;
        res.testId = "BP-10";
        res.testName = "BP-10 — Stroke Continuo su Triangoli Adiacenti";
        res.inputDesc = "Transizione cursore attraverso la diagonale interna di un quad triangolato";
        res.expectedDesc = "Stroke continuo lungo lo spigolo comune; UpdateStroke ritorna true; dirty rect non vuoto";

        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
        VertexID v1 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
        VertexID v2 = mesh.AddVertex(glm::vec3(1.0f, 1.0f, 0.0f));
        VertexID v3 = mesh.AddVertex(glm::vec3(0.0f, 1.0f, 0.0f));
        mesh.AddFace({v0, v1, v2, v3}, 0, 0);

        TexturePaintingService ps;
        ps.InitializeCanvas(64, 64, glm::vec4(0.0f));
        SurfacePaintingAdapter adapter(&ps);

        PaintBrush b;
        b.radiusPixels = 4.0f;
        b.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

        bool sOk = adapter.BeginStroke3D(mesh, glm::vec3(0.2f, 0.2f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f), b);
        bool uOk = adapter.UpdateStroke3D(mesh, glm::vec3(0.8f, 0.8f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f), b);
        PaintDirtyRect dr = adapter.EndStroke3D();

        bool passed = sOk && uOk && !dr.isEmpty;
        res.passed = passed;
        res.observedDesc = "Start=" + std::string(sOk ? "OK" : "ERR") + 
                           ", Update=" + (uOk ? "OK" : "ERR") + 
                           ", DirtyRectArea=" + std::to_string(dr.GetWidth() * dr.GetHeight());
        res.details = passed ? "PASS: Continuita regolare dello stroke attraverso triangoli adiacenti" : "FAIL: Stroke interrotto";
        results.push_back(res);
    }

    // =========================================================================
    // BP-11: Stroke su Superficie Curva con Normal Culling
    // =========================================================================
    {
        SurfacePaintTestResult res;
        res.testId = "BP-11";
        res.testName = "BP-11 — Stroke su Superficie Curva con Normal Culling";
        res.inputDesc = "Mesh a cuneo: faccia frontale (normale +Z) e faccia posteriore/opposta (normale -Z). Stroke avviato su +Z";
        res.expectedDesc = "UpdateStroke su faccia posteriore con deviazione normale > 65 gradi rigettato (ritorna false); zero paint sul retro";

        EditableMesh mesh;
        // Faccia Anteriore (+Z): vertici a Z=0.1
        VertexID f0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 0.1f));
        VertexID f1 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, 0.1f));
        VertexID f2 = mesh.AddVertex(glm::vec3(1.0f, 1.0f, 0.1f));
        VertexID f3 = mesh.AddVertex(glm::vec3(0.0f, 1.0f, 0.1f));
        mesh.AddFace({f0, f1, f2, f3}, 0, 0); // normale (0, 0, 1)

        // Faccia Posteriore (-Z): vertici a Z=-0.1 (parete sottile opposta)
        VertexID b0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, -0.1f));
        VertexID b1 = mesh.AddVertex(glm::vec3(0.0f, 1.0f, -0.1f));
        VertexID b2 = mesh.AddVertex(glm::vec3(1.0f, 1.0f, -0.1f));
        VertexID b3 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, -0.1f));
        mesh.AddFace({b0, b1, b2, b3}, 0, 0); // normale (0, 0, -1)

        TexturePaintingService ps;
        ps.InitializeCanvas(64, 64, glm::vec4(0.0f));
        SurfacePaintingAdapter adapter(&ps);
        adapter.SetMaxNormalDeviationDegrees(65.0f); // Soglia di culling

        PaintBrush brush;
        brush.radiusPixels = 4.0f;
        brush.color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);

        // Stroke inizia su faccia anteriore (+Z)
        bool startFront = adapter.BeginStroke3D(mesh, glm::vec3(0.5f, 0.5f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f), brush);

        // Tentativo di update indirizzato verso la faccia posteriore (ray da dietro, normale opposta)
        bool updateOpposite = adapter.UpdateStroke3D(mesh, glm::vec3(0.5f, 0.5f, -2.0f), glm::vec3(0.0f, 0.0f, 1.0f), brush);

        adapter.EndStroke3D();

        bool passed = startFront && (!updateOpposite);
        res.passed = passed;
        res.observedDesc = "StartFront=" + std::string(startFront ? "OK" : "ERR") + 
                           ", BleedOppositeWallBlocked=" + (!updateOpposite ? "YES" : "NO");
        res.details = passed ? "PASS: Normal culling protegge da bleeding attraverso pareti sottili e retri di superfici curve" : "FAIL: Pennello ha travalicato su faccia opposta";
        results.push_back(res);
    }

    // =========================================================================
    // BP-12: Attraversamento UV Seam con Segmentazione
    // =========================================================================
    {
        SurfacePaintTestResult res;
        res.testId = "BP-12";
        res.testName = "BP-12 — Attraversamento UV Seam con Segmentazione";
        res.inputDesc = "Movimento del cursore attraverso cucitura con salto UV delta > 0.35";
        res.expectedDesc = "Segmentazione dello stroke: dab isolato senza striscia continua attraverso il centro dell'atlas (pixel 50,50 pulito)";

        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
        VertexID v1 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
        VertexID v2 = mesh.AddVertex(glm::vec3(1.0f, 1.0f, 0.0f));
        VertexID v3 = mesh.AddVertex(glm::vec3(0.0f, 1.0f, 0.0f));
        VertexID v4 = mesh.AddVertex(glm::vec3(2.0f, 0.0f, 0.0f));
        VertexID v5 = mesh.AddVertex(glm::vec3(2.0f, 1.0f, 0.0f));

        std::vector<CornerAttributes> c1(4);
        c1[0].uv = {0.05f, 0.0f}; c1[1].uv = {0.15f, 0.0f};
        c1[2].uv = {0.15f, 1.0f}; c1[3].uv = {0.05f, 1.0f};
        mesh.AddFace({v0, v1, v2, v3}, 0, 0, &c1);

        std::vector<CornerAttributes> c2(4);
        c2[0].uv = {0.85f, 0.0f}; c2[1].uv = {0.95f, 0.0f};
        c2[2].uv = {0.95f, 1.0f}; c2[3].uv = {0.85f, 1.0f};
        mesh.AddFace({v1, v4, v5, v2}, 0, 0, &c2);

        TexturePaintingService ps;
        ps.InitializeCanvas(100, 100, glm::vec4(0.0f));
        SurfacePaintingAdapter adapter(&ps);
        adapter.SetIsolateBySurfaceId(false);
        adapter.SetSeamMode(SeamMode::ContinueAcrossSeam);
        adapter.SetSeamDiscontinuityThreshold(0.35f);

        PaintBrush b;
        b.radiusPixels = 2.0f;
        b.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

        adapter.BeginStroke3D(mesh, glm::vec3(0.5f, 0.5f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f), b);
        adapter.UpdateStroke3D(mesh, glm::vec3(1.5f, 0.5f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f), b);
        adapter.EndStroke3D();

        ImageBitmap comp;
        ps.CompositeLayers(comp);
        glm::vec4 centerPx = comp.GetPixel(50, 50);

        bool noSpuriousLine = (centerPx.r == 0.0f && centerPx.a == 0.0f);
        bool passed = noSpuriousLine;
        res.passed = passed;
        res.observedDesc = "CenterCanvasCol=(" + std::to_string(centerPx.r) + "), SpuriousLineAvoided=" + 
                           (noSpuriousLine ? "YES" : "NO");
        res.details = passed ? "PASS: Segmentazione dello stroke su seam UV verificata con successo" : "FAIL: Linea spuria generata attraverso l'atlas";
        results.push_back(res);
    }

    // =========================================================================
    // BP-13: Raycast Perso Durante lo Stroke (Gestione Uscita/Rientro)
    // =========================================================================
    {
        SurfacePaintTestResult res;
        res.testId = "BP-13";
        res.testName = "BP-13 — Raycast Perso Durante lo Stroke";
        res.inputDesc = "Stroke avviato a (0.1, 0.1); cursore esce dalla mesh nello spazio vuoto (raycast perso); cursore rientra a (0.9, 0.9)";
        res.expectedDesc = "Nessuna linea tracciata attraverso il vuoto; pixel intermedio (0.5, 0.5) rimane intatto";

        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
        VertexID v1 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
        VertexID v2 = mesh.AddVertex(glm::vec3(1.0f, 1.0f, 0.0f));
        VertexID v3 = mesh.AddVertex(glm::vec3(0.0f, 1.0f, 0.0f));
        mesh.AddFace({v0, v1, v2, v3}, 0, 0);

        TexturePaintingService ps;
        ps.InitializeCanvas(100, 100, glm::vec4(0.0f));
        SurfacePaintingAdapter adapter(&ps);

        PaintBrush b;
        b.radiusPixels = 2.0f;
        b.color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);

        // 1. Inizio a (0.1, 0.1)
        bool sOk = adapter.BeginStroke3D(mesh, glm::vec3(0.1f, 0.1f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f), b);

        // 2. Cursore fuori dalla mesh (raycast perso)
        bool uMiss = adapter.UpdateStroke3D(mesh, glm::vec3(10.0f, 10.0f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f), b);
        bool lostFlag = adapter.IsRaycastLost();

        // 3. Rientro sulla mesh a (0.9, 0.9)
        bool uReenter = adapter.UpdateStroke3D(mesh, glm::vec3(0.9f, 0.9f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f), b);
        adapter.EndStroke3D();

        ImageBitmap comp;
        ps.CompositeLayers(comp);
        glm::vec4 midPx = comp.GetPixel(50, 50);

        bool midClean = (midPx.r == 0.0f && midPx.a == 0.0f);
        bool passed = sOk && (!uMiss) && lostFlag && uReenter && midClean;

        res.passed = passed;
        res.observedDesc = "MissHandled=" + std::string(!uMiss ? "YES" : "NO") + 
                           ", LostFlag=" + (lostFlag ? "YES" : "NO") + 
                           ", MidVoidClean=" + (midClean ? "YES" : "NO");
        res.details = passed ? "PASS: Gestione corretta della perdita raycast; zero righe spurie nel vuoto" : "FAIL: Cursore ha tracciato linea attraverso il vuoto";
        results.push_back(res);
    }

    // =========================================================================
    // BP-14: CancelStroke, Undo/Redo e Revisioni Monotone
    // =========================================================================
    {
        SurfacePaintTestResult res;
        res.testId = "BP-14";
        res.testName = "BP-14 — CancelStroke, Undo/Redo e Revisioni Monotone";
        res.inputDesc = "Esecuzione stroke, poi CancelStroke3D; secondo stroke confermato con EndStroke3D, poi Undo sul painting service";
        res.expectedDesc = "CancelStroke ripristina pixel iniziali; EndStroke incrementa monotonicamente revisione; Undo ripristina stato";

        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
        VertexID v1 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
        VertexID v2 = mesh.AddVertex(glm::vec3(1.0f, 1.0f, 0.0f));
        VertexID v3 = mesh.AddVertex(glm::vec3(0.0f, 1.0f, 0.0f));
        mesh.AddFace({v0, v1, v2, v3}, 0, 0);

        TexturePaintingService ps;
        ps.InitializeCanvas(64, 64, glm::vec4(1.0f)); // Bianco di partenza
        uint64_t rev0 = ps.GetRevision();

        SurfacePaintingAdapter adapter(&ps);
        PaintBrush redB;
        redB.radiusPixels = 6.0f;
        redB.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

        // 1. Inizia stroke e cancella
        adapter.BeginStroke3D(mesh, glm::vec3(0.5f, 0.5f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f), redB);
        bool cancelOk = adapter.CancelStroke3D();

        ImageBitmap bCancel;
        ps.CompositeLayers(bCancel);
        glm::vec4 colAfterCancel = bCancel.GetPixel(32, 32);
        bool cancelRestored = (colAfterCancel.r > 0.9f && colAfterCancel.g > 0.9f);

        // 2. Nuovo stroke completato regolarmente
        PaintBrush blueB;
        blueB.radiusPixels = 6.0f;
        blueB.color = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);

        adapter.BeginStroke3D(mesh, glm::vec3(0.5f, 0.5f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f), blueB);
        adapter.EndStroke3D();
        uint64_t rev1 = ps.GetRevision();

        ImageBitmap bDone;
        ps.CompositeLayers(bDone);
        glm::vec4 colDone = bDone.GetPixel(32, 32);
        bool blueApplied = (colDone.b > 0.8f && colDone.r < 0.2f);
        bool revMonotonic = (rev1 > rev0);

        // 3. Undo
        bool undoOk = ps.Undo();
        ImageBitmap bUndo;
        ps.CompositeLayers(bUndo);
        glm::vec4 colUndo = bUndo.GetPixel(32, 32);
        bool undoRestored = (colUndo.r > 0.9f && colUndo.b > 0.9f);

        bool passed = cancelOk && cancelRestored && blueApplied && revMonotonic && undoOk && undoRestored;
        res.passed = passed;
        res.observedDesc = "CancelRestored=" + std::string(cancelRestored ? "YES" : "NO") + 
                           ", RevMonotonic=" + (revMonotonic ? "YES" : "NO") + 
                           ", UndoRestored=" + (undoRestored ? "YES" : "NO");
        res.details = passed ? "PASS: Transazioni stroke, annullamento e cronologia undo conformi" : "FAIL: Errore in CancelStroke o Undo";
        results.push_back(res);
    }

    // =========================================================================
    // BP-15: Zero-Work e Determinismo Geometrico
    // =========================================================================
    {
        SurfacePaintTestResult res;
        res.testId = "BP-15";
        res.testName = "BP-15 — Zero-Work e Determinismo Geometrico";
        res.inputDesc = "Esecuzione completa di stroke 3D su mesh; verifica invarianza di geometria e topologia DCEL";
        res.expectedDesc = "Mesh revisioni intatte, zero modifiche a vertici/facce, texture aggiornata";

        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        uint64_t topoRevBefore = mesh.GetTopologyRevision();
        uint64_t geomRevBefore = mesh.GetGeometryRevision();
        uint64_t attrRevBefore = mesh.GetAttributeRevision();
        size_t vCountBefore = mesh.GetVertexCount();
        size_t fCountBefore = mesh.GetFaceCount();

        TexturePaintingService ps;
        ps.InitializeCanvas(64, 64, glm::vec4(0.0f));
        SurfacePaintingAdapter adapter(&ps);

        PaintBrush b;
        b.radiusPixels = 6.0f;
        b.color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);

        adapter.BeginStroke3D(mesh, glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f), b);
        adapter.UpdateStroke3D(mesh, glm::vec3(0.2f, 0.2f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f), b);
        PaintDirtyRect dr = adapter.EndStroke3D();

        uint64_t topoRevAfter = mesh.GetTopologyRevision();
        uint64_t geomRevAfter = mesh.GetGeometryRevision();
        uint64_t attrRevAfter = mesh.GetAttributeRevision();
        size_t vCountAfter = mesh.GetVertexCount();
        size_t fCountAfter = mesh.GetFaceCount();

        bool meshUntouched = (topoRevBefore == topoRevAfter) && 
                             (geomRevBefore == geomRevAfter) && 
                             (attrRevBefore == attrRevAfter) && 
                             (vCountBefore == vCountAfter) && 
                             (fCountBefore == fCountAfter);
        bool paintUpdated = !dr.isEmpty;

        bool passed = meshUntouched && paintUpdated;
        res.passed = passed;
        res.observedDesc = "MeshUntouched=" + std::string(meshUntouched ? "YES" : "NO") + 
                           ", PaintDirtyArea=" + std::to_string(dr.GetWidth() * dr.GetHeight());
        res.details = passed ? "PASS: Disaccoppiamento assoluto tra pittura texture e topologia mesh" : "FAIL: Modifica indebita a EditableMesh";
        results.push_back(res);
    }

    return results;
}

} // namespace fw
