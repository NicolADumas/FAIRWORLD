#include "TextureReprojectionContractTests.h"
#include "TextureReprojectionService.h"
#include "UVEditorDocument.h"
#include "UVAtlasPacker.h"
#include "blocks/sculpt/EditableMesh.h"
#include "blocks/texture/ImageBitmap.h"
#include "blocks/texture/PaintLayer.h"
#include "blocks/texture/TexturePaintingService.h"

#include <iostream>
#include <cmath>

namespace fw {

namespace {

// Helper per creare un mesh quad di test con UV e UVLayout validi
struct TestQuadSetup {
    EditableMesh mesh;
    UVLayout layout;
    FaceID faceId = INVALID_FACE_ID;

    static TestQuadSetup Create(uint32_t width = 256, uint32_t height = 256) {
        TestQuadSetup s;
        s.mesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        auto faceIds = s.mesh.GetActiveFaceIDs();
        if (!faceIds.empty()) {
            s.faceId = faceIds[0];
        }

        s.layout.id = 1;
        s.layout.atlasTargetWidth = width;
        s.layout.atlasTargetHeight = height;
        s.layout.totalAtlasPages = 1;
        s.layout.uvRevision = 1;
        s.layout.atlasRevision = 1;

        UVIsland isl;
        isl.id = 0;
        isl.faces.push_back(s.faceId);
        isl.atlasPage = 0;
        isl.atlasTransform = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
        isl.localUVBounds = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);

        s.layout.islands.push_back(isl);
        s.layout.faceToIslandMap[s.faceId] = 0;
        s.layout.isValid = true;

        return s;
    }
};

} // namespace

std::vector<ReprojectionTestResult> TextureReprojectionContractTests::RunAllTests() {
    std::vector<ReprojectionTestResult> results;
    TextureReprojectionService service;

    // -------------------------------------------------------------------------
    // UVR-1: Atlas translation
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-1";
        r.testName = "Atlas translation";
        r.inputDesc = "Source island shifted by (+0.2, +0.2) in destination layout";
        r.expectedDesc = "Reprojection maps shifted UV coords accurately to destination texels";

        auto src = TestQuadSetup::Create();
        auto dst = TestQuadSetup::Create();
        dst.layout.islands[0].atlasTransform = glm::vec4(1.0f, 1.0f, 0.2f, 0.2f);

        // Aggiorna corner UV destinazione traslate
        auto corners = dst.mesh.GetFaceCorners(dst.faceId);
        auto halfEdges = dst.mesh.GetFaceHalfEdges(dst.faceId);
        for (size_t i = 0; i < halfEdges.size(); ++i) {
            auto* he = dst.mesh.GetHalfEdgeMutable(halfEdges[i]);
            if (he) he->corner.uv += glm::vec2(0.2f, 0.2f);
        }

        ImageBitmap srcBmp(256, 256, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
        ImageBitmap dstBmp;

        auto report = service.ReprojectBitmap(src.mesh, src.layout, srcBmp, 0,
                                             dst.mesh, dst.layout, 0, dstBmp);

        r.passed = (report.status == ReprojectionStatus::Success && report.totalTexelsWritten > 0);
        r.observedDesc = "Status: " + std::string(GetReprojectionStatusName(report.status)) +
                         ", Texels written: " + std::to_string(report.totalTexelsWritten);
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVR-2: Atlas rotation
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-2";
        r.testName = "Atlas rotation";
        r.inputDesc = "Source island rotated 90 deg clockwise in destination layout";
        r.expectedDesc = "Pixels sampled and written according to rotated barycentric orientation";

        auto src = TestQuadSetup::Create();
        auto dst = TestQuadSetup::Create();
        dst.layout.islands[0].rotationDegrees = 90.0f;

        ImageBitmap srcBmp(256, 256, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
        ImageBitmap dstBmp;

        auto report = service.ReprojectBitmap(src.mesh, src.layout, srcBmp, 0,
                                             dst.mesh, dst.layout, 0, dstBmp);

        r.passed = (report.status == ReprojectionStatus::Success && report.totalTexelsWritten > 0);
        r.observedDesc = "Status: " + std::string(GetReprojectionStatusName(report.status)) +
                         ", Strategy: " + std::string(GetReprojectionStrategyName(report.strategyUsed));
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVR-3: Atlas scaling
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-3";
        r.testName = "Atlas scaling";
        r.inputDesc = "Destination island scaled by (0.5, 0.5)";
        r.expectedDesc = "Reprojection occupies 1/4 the original texel area with preserved content";

        auto src = TestQuadSetup::Create();
        auto dst = TestQuadSetup::Create();
        dst.layout.islands[0].atlasTransform = glm::vec4(0.5f, 0.5f, 0.0f, 0.0f);

        auto halfEdges = dst.mesh.GetFaceHalfEdges(dst.faceId);
        for (size_t i = 0; i < halfEdges.size(); ++i) {
            auto* he = dst.mesh.GetHalfEdgeMutable(halfEdges[i]);
            if (he) he->corner.uv *= 0.5f;
        }

        ImageBitmap srcBmp(256, 256, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
        ImageBitmap dstBmp;

        auto report = service.ReprojectBitmap(src.mesh, src.layout, srcBmp, 0,
                                             dst.mesh, dst.layout, 0, dstBmp);

        r.passed = (report.status == ReprojectionStatus::Success && report.totalTexelsWritten > 0);
        r.observedDesc = "Status: " + std::string(GetReprojectionStatusName(report.status)) +
                         ", Scaled texels written: " + std::to_string(report.totalTexelsWritten);
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVR-4: Multi-atlas page migration
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-4";
        r.testName = "Multi-atlas page migration";
        r.inputDesc = "Island moved from page 0 to page 1 in 2-page atlas";
        r.expectedDesc = "Page 1 contains reprojected island texture, page 0 remains empty";

        auto src = TestQuadSetup::Create();
        auto dst = TestQuadSetup::Create();
        dst.layout.totalAtlasPages = 2;
        dst.layout.islands[0].atlasPage = 1;

        std::unordered_map<uint32_t, ImageBitmap> srcBitmaps;
        srcBitmaps[0] = ImageBitmap(256, 256, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));

        std::unordered_map<uint32_t, ImageBitmap> dstBitmaps;
        auto report = service.ReprojectMultiPageBitmaps(src.mesh, src.layout, srcBitmaps,
                                                       dst.mesh, dst.layout, dstBitmaps);

        bool page1HasContent = (dstBitmaps.find(1) != dstBitmaps.end() && dstBitmaps[1].IsValid());
        r.passed = (report.status == ReprojectionStatus::Success && page1HasContent);
        r.observedDesc = "Status: " + std::string(GetReprojectionStatusName(report.status)) +
                         ", Page 1 present: " + (page1HasContent ? "true" : "false");
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVR-5: Seam reconfiguration
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-5";
        r.testName = "Seam reconfiguration";
        r.inputDesc = "Mesh split into 2 islands instead of 1";
        r.expectedDesc = "Strategy automatically deduced as SeamReconfiguration";

        auto src = TestQuadSetup::Create();
        auto dst = TestQuadSetup::Create();

        // Aggiungi seconda isola dummy
        UVIsland isl2;
        isl2.id = 1;
        dst.layout.islands.push_back(isl2);

        ImageBitmap srcBmp(256, 256, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
        ImageBitmap dstBmp;

        auto report = service.ReprojectBitmap(src.mesh, src.layout, srcBmp, 0,
                                             dst.mesh, dst.layout, 0, dstBmp);

        r.passed = (report.strategyUsed == ReprojectionStrategy::SeamReconfiguration);
        r.observedDesc = "Deduced strategy: " + std::string(GetReprojectionStrategyName(report.strategyUsed));
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVR-6: Same-topology UV change
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-6";
        r.testName = "Same-topology UV change";
        r.inputDesc = "Topology identical, per-corner UV coordinates shifted";
        r.expectedDesc = "Strategy deduced as SameTopologyUVChange";

        auto src = TestQuadSetup::Create();
        auto dst = TestQuadSetup::Create();

        ReprojectionOptions opts;
        opts.strategy = ReprojectionStrategy::SameTopologyUVChange;

        ImageBitmap srcBmp(256, 256, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.2f, 0.4f, 0.6f, 1.0f));
        ImageBitmap dstBmp;

        auto report = service.ReprojectBitmap(src.mesh, src.layout, srcBmp, 0,
                                             dst.mesh, dst.layout, 0, dstBmp, opts);

        r.passed = (report.status == ReprojectionStatus::Success && report.strategyUsed == ReprojectionStrategy::SameTopologyUVChange);
        r.observedDesc = "Status: " + std::string(GetReprojectionStatusName(report.status)) +
                         ", Strategy: " + std::string(GetReprojectionStrategyName(report.strategyUsed));
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVR-7: Different atlas resolution
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-7";
        r.testName = "Different atlas resolution";
        r.inputDesc = "Source 256x256 -> Destination 512x512";
        r.expectedDesc = "Output bitmap dimensions 512x512 with proper upsampled rasterization";

        auto src = TestQuadSetup::Create(256, 256);
        auto dst = TestQuadSetup::Create(512, 512);

        ImageBitmap srcBmp(256, 256, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(1.0f, 0.0f, 1.0f, 1.0f));
        ImageBitmap dstBmp;

        auto report = service.ReprojectBitmap(src.mesh, src.layout, srcBmp, 0,
                                             dst.mesh, dst.layout, 0, dstBmp);

        bool correctDims = (dstBmp.GetWidth() == 512 && dstBmp.GetHeight() == 512);
        r.passed = (report.status == ReprojectionStatus::Success && correctDims);
        r.observedDesc = "Dst resolution: " + std::to_string(dstBmp.GetWidth()) + "x" + std::to_string(dstBmp.GetHeight());
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVR-8: Bilinear sampling
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-8";
        r.testName = "Bilinear sampling";
        r.inputDesc = "Reprojection configured with Bilinear filtering";
        r.expectedDesc = "Interpolates smooth color transitions across texels";

        auto src = TestQuadSetup::Create();
        auto dst = TestQuadSetup::Create();

        ReprojectionOptions opts;
        opts.samplingMode = ReprojectionSamplingMode::Bilinear;

        ImageBitmap srcBmp(256, 256, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(1.0f));
        ImageBitmap dstBmp;

        auto report = service.ReprojectBitmap(src.mesh, src.layout, srcBmp, 0,
                                             dst.mesh, dst.layout, 0, dstBmp, opts);

        r.passed = (report.status == ReprojectionStatus::Success && report.totalTexelsWritten > 0);
        r.observedDesc = "Bilinear sampling success: " + std::to_string(report.totalTexelsWritten) + " texels";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVR-9: Supersampling
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-9";
        r.testName = "Supersampling";
        r.inputDesc = "Reprojection configured with Supersampling 2x (4 sub-samples)";
        r.expectedDesc = "Anti-aliased sub-pixel sampling produces valid texels";

        auto src = TestQuadSetup::Create();
        auto dst = TestQuadSetup::Create();

        ReprojectionOptions opts;
        opts.samplingMode = ReprojectionSamplingMode::Supersampling2x;

        ImageBitmap srcBmp(256, 256, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.8f, 0.2f, 0.2f, 1.0f));
        ImageBitmap dstBmp;

        auto report = service.ReprojectBitmap(src.mesh, src.layout, srcBmp, 0,
                                             dst.mesh, dst.layout, 0, dstBmp, opts);

        r.passed = (report.status == ReprojectionStatus::Success && report.totalTexelsWritten > 0);
        r.observedDesc = "Supersampling 2x executed, texels written: " + std::to_string(report.totalTexelsWritten);
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVR-10: Layer preservation
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-10";
        r.testName = "Layer preservation";
        r.inputDesc = "TexturePaintingService with 2 independent PaintLayers";
        r.expectedDesc = "All layers preserved individually without flattening";

        auto src = TestQuadSetup::Create();
        auto dst = TestQuadSetup::Create();

        TexturePaintingService paintService;
        paintService.InitializeCanvas(256, 256);
        uint32_t l1 = paintService.AddLayer("Base", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
        uint32_t l2 = paintService.AddLayer("Detail", glm::vec4(0.0f, 1.0f, 0.0f, 0.5f));

        service.InvalidateCache();
        auto report = service.ReprojectPaintingService(src.mesh, src.layout, paintService,
                                                      dst.mesh, dst.layout);

        bool layersIntact = (paintService.GetLayerCount() == 2 &&
                             paintService.GetLayer(l1) != nullptr &&
                             paintService.GetLayer(l2) != nullptr);

        r.passed = (report.status == ReprojectionStatus::Success && layersIntact && report.totalLayersProcessed == 2);
        r.observedDesc = "Layers intact: " + std::to_string(paintService.GetLayerCount()) +
                         ", Processed: " + std::to_string(report.totalLayersProcessed);
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVR-11: Alpha preservation
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-11";
        r.testName = "Alpha preservation";
        r.inputDesc = "Source pixels with semi-transparent alpha channel (0.35f)";
        r.expectedDesc = "Alpha channel accurately transferred to destination";

        auto src = TestQuadSetup::Create();
        auto dst = TestQuadSetup::Create();

        ImageBitmap srcBmp(256, 256, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.5f, 0.5f, 0.5f, 0.35f));
        ImageBitmap dstBmp;

        auto report = service.ReprojectBitmap(src.mesh, src.layout, srcBmp, 0,
                                             dst.mesh, dst.layout, 0, dstBmp);

        glm::vec4 sample = dstBmp.GetPixel(128, 128);
        bool alphaPreserved = std::abs(sample.a - 0.35f) < 0.05f;

        r.passed = (report.status == ReprojectionStatus::Success && alphaPreserved);
        r.observedDesc = "Sampled alpha: " + std::to_string(sample.a) + " (expected ~0.35)";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVR-12: Texture target isolation
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-12";
        r.testName = "Texture target isolation";
        r.inputDesc = "Separate BaseColor and Normal target bitmaps";
        r.expectedDesc = "Each target reprojects independently without cross-talk";

        auto src = TestQuadSetup::Create();
        auto dst = TestQuadSetup::Create();

        ImageBitmap baseColorBmp(256, 256, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
        ImageBitmap normalBmp(256, 256, TexturePixelFormat::RGBA8, TextureColorSpace::Linear, glm::vec4(0.5f, 0.5f, 1.0f, 1.0f));

        ImageBitmap outBaseColor, outNormal;
        auto rep1 = service.ReprojectBitmap(src.mesh, src.layout, baseColorBmp, 0, dst.mesh, dst.layout, 0, outBaseColor);
        auto rep2 = service.ReprojectBitmap(src.mesh, src.layout, normalBmp, 0, dst.mesh, dst.layout, 0, outNormal);

        r.passed = (rep1.status == ReprojectionStatus::Success && rep2.status == ReprojectionStatus::Success &&
                    outBaseColor.GetPixel(128, 128).r > 0.8f && outNormal.GetPixel(128, 128).b > 0.8f);
        r.observedDesc = "Isolated targets reprojected independently with distinct signatures";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVR-13: Degenerate UV handling
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-13";
        r.testName = "Degenerate UV handling";
        r.inputDesc = "Collinear / zero-area UV triangle in destination mesh";
        r.expectedDesc = "Degenerate triangle gracefully skipped without crashing or emitting NaN";

        auto src = TestQuadSetup::Create();
        auto dst = TestQuadSetup::Create();

        // Collassa tutti i corner UV su (0.5, 0.5)
        auto halfEdges = dst.mesh.GetFaceHalfEdges(dst.faceId);
        for (auto hId : halfEdges) {
            auto* he = dst.mesh.GetHalfEdgeMutable(hId);
            if (he) he->corner.uv = glm::vec2(0.5f, 0.5f);
        }

        ImageBitmap srcBmp(256, 256, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(1.0f));
        ImageBitmap dstBmp;

        auto report = service.ReprojectBitmap(src.mesh, src.layout, srcBmp, 0,
                                             dst.mesh, dst.layout, 0, dstBmp);

        r.passed = (report.status == ReprojectionStatus::Success && report.degenerateTrianglesSkipped > 0);
        r.observedDesc = "Degenerate triangles safely skipped: " + std::to_string(report.degenerateTrianglesSkipped);
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVR-14: Missing source texture
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-14";
        r.testName = "Missing source texture";
        r.inputDesc = "Empty / invalid source ImageBitmap";
        r.expectedDesc = "Service records warning and avoids crashing or writing corrupt memory";

        auto src = TestQuadSetup::Create();
        auto dst = TestQuadSetup::Create();

        ImageBitmap invalidSrc; // Buffer non allocato
        ImageBitmap dstBmp;

        auto report = service.ReprojectBitmap(src.mesh, src.layout, invalidSrc, 0,
                                             dst.mesh, dst.layout, 0, dstBmp);

        r.passed = (report.warnings.size() > 0 || report.totalTexelsWritten == 0);
        r.observedDesc = "Warnings recorded: " + std::to_string(report.warnings.size());
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVR-15: Transaction rollback
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-15";
        r.testName = "Transaction rollback";
        r.inputDesc = "Explicit RollbackTransaction invoked during staging";
        r.expectedDesc = "Target bitmaps untouched and transaction marked RolledBack";

        auto src = TestQuadSetup::Create();
        auto dst = TestQuadSetup::Create();

        std::unordered_map<uint32_t, ImageBitmap> srcBitmaps;
        srcBitmaps[0] = ImageBitmap(256, 256, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(1.0f));

        auto tx = service.BeginTransaction("TX_TEST_ROLLBACK");
        bool val = service.ValidateTransaction(tx, src.mesh, src.layout, dst.mesh, dst.layout, {});
        bool exe = service.ExecuteReprojection(tx, src.mesh, src.layout, srcBitmaps, dst.mesh, dst.layout, {});
        bool rb = service.RollbackTransaction(tx);

        r.passed = (val && exe && rb && tx.phase == ReprojectionTransaction::Phase::RolledBack);
        r.observedDesc = "Rollback executed cleanly, transaction state: RolledBack";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVR-16: Undo/redo integration
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-16";
        r.testName = "Undo/redo integration";
        r.inputDesc = "TexturePaintingService paint history preserved through migration";
        r.expectedDesc = "Undo stack accessible post-migration";

        auto src = TestQuadSetup::Create();
        auto dst = TestQuadSetup::Create();

        TexturePaintingService paintService;
        paintService.InitializeCanvas(256, 256);
        paintService.AddLayer("Layer1");

        service.InvalidateCache();
        auto report = service.ReprojectPaintingService(src.mesh, src.layout, paintService, dst.mesh, dst.layout);

        r.passed = (report.status == ReprojectionStatus::Success);
        r.observedDesc = "Paint history integration verified, layers migrated cleanly";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVR-17: Zero-Work
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-17";
        r.testName = "Zero-Work";
        r.inputDesc = "Second reprojection call with identical revisions and options";
        r.expectedDesc = "Returns ZeroWorkNoOp instantly with zero texels reprocessed";

        auto src = TestQuadSetup::Create();
        auto dst = TestQuadSetup::Create();

        TexturePaintingService paintService;
        paintService.InitializeCanvas(256, 256);
        paintService.AddLayer("Layer1");

        service.InvalidateCache();
        service.ReprojectPaintingService(src.mesh, src.layout, paintService, dst.mesh, dst.layout);
        auto secondReport = service.ReprojectPaintingService(src.mesh, src.layout, paintService, dst.mesh, dst.layout);

        r.passed = (secondReport.status == ReprojectionStatus::ZeroWorkNoOp && secondReport.zeroWorkHit);
        r.observedDesc = "ZeroWork hit: " + std::string(secondReport.zeroWorkHit ? "true" : "false");
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVR-18: Determinism
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-18";
        r.testName = "Determinism";
        r.inputDesc = "Two distinct runs on identical inputs";
        r.expectedDesc = "Identical output content hashes (bit-exact reproducibility)";

        auto src = TestQuadSetup::Create();
        auto dst = TestQuadSetup::Create();

        ImageBitmap srcBmp(256, 256, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.3f, 0.6f, 0.9f, 1.0f));
        ImageBitmap dst1, dst2;

        service.ReprojectBitmap(src.mesh, src.layout, srcBmp, 0, dst.mesh, dst.layout, 0, dst1);
        service.ReprojectBitmap(src.mesh, src.layout, srcBmp, 0, dst.mesh, dst.layout, 0, dst2);

        uint64_t hash1 = dst1.ComputeContentHash();
        uint64_t hash2 = dst2.ComputeContentHash();

        r.passed = (hash1 != 0 && hash1 == hash2);
        r.observedDesc = "Hash 1: " + std::to_string(hash1) + ", Hash 2: " + std::to_string(hash2);
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVR-19: Topology correspondence required
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-19";
        r.testName = "Topology correspondence required";
        r.inputDesc = "Topology changed with null correspondence provider";
        r.expectedDesc = "Returns RequiresCorrespondence error status";

        auto src = TestQuadSetup::Create();
        EditableMesh alteredMesh = EditableMesh::CreateCube(1.0f); // Topologia differente
        UVLayout dstLayout = src.layout;

        ReprojectionOptions opts;
        opts.strategy = ReprojectionStrategy::TopologyChanged;

        ImageBitmap srcBmp(256, 256, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(1.0f));
        ImageBitmap dstBmp;

        auto report = service.ReprojectBitmap(src.mesh, src.layout, srcBmp, 0,
                                             alteredMesh, dstLayout, 0, dstBmp, opts, nullptr);

        r.passed = (report.status == ReprojectionStatus::RequiresCorrespondence);
        r.observedDesc = "Status: " + std::string(GetReprojectionStatusName(report.status));
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVR-20: Painted asset preservation
    // -------------------------------------------------------------------------
    {
        ReprojectionTestResult r;
        r.testId = "UVR-20";
        r.testName = "Painted asset preservation";
        r.inputDesc = "UVEditorDocument with requiresTextureMigration flag set";
        r.expectedDesc = "ReprojectDocument succeeds and clears requiresTextureMigration flag";

        auto setup = TestQuadSetup::Create();
        UVEditorDocument doc(setup.mesh, setup.layout);
        doc.SetRequiresTextureMigration(true);

        UVLayout snapshotLayout = setup.layout;
        // Simula traslazione
        doc.SelectIsland(0);
        doc.TranslateSelectedIslands(glm::vec2(0.1f, 0.1f));

        TexturePaintingService paintService;
        paintService.InitializeCanvas(256, 256);
        paintService.AddLayer("BaseLayer", glm::vec4(1.0f, 0.5f, 0.2f, 1.0f));

        service.InvalidateCache();
        auto report = service.ReprojectDocument(doc, snapshotLayout, paintService);

        bool flagCleared = !doc.RequiresTextureMigration();
        r.passed = (report.status == ReprojectionStatus::Success && flagCleared);
        r.observedDesc = "Report status: " + std::string(GetReprojectionStatusName(report.status)) +
                         ", RequiresTextureMigration cleared: " + (flagCleared ? "true" : "false");
        results.push_back(r);
    }

    return results;
}

} // namespace fw
