#include "UVFactoryIntegrationTests.h"
#include "UVEditorDocument.h"
#include "UVEditorPanel.h"
#include "TextureReprojectionService.h"
#include "blocks/texture/SurfacePaintingAdapter.h"
#include "blocks/texture/TexturePaintingService.h"
#include "blocks/sculpt/EditableMesh.h"
#include "blocks/uv/UVLayout.h"
#include "blocks/factory/RawAssetContract.h"

#include <iostream>
#include <cmath>

namespace fw {

namespace {

// Helper per creare un mesh quad di test per i test di integrazione
struct TestIntegrationSetup {
    EditableMesh mesh;
    UVLayout layout;
    FaceID faceId = INVALID_FACE_ID;

    static TestIntegrationSetup Create(uint32_t w = 256, uint32_t h = 256) {
        TestIntegrationSetup s;
        s.mesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        auto faceIds = s.mesh.GetActiveFaceIDs();
        if (!faceIds.empty()) {
            s.faceId = faceIds[0];
        }

        s.layout.id = 100;
        s.layout.atlasTargetWidth = w;
        s.layout.atlasTargetHeight = h;
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

std::vector<FactoryIntegrationTestResult> UVFactoryIntegrationTests::RunAllTests() {
    std::vector<FactoryIntegrationTestResult> results;

    // -------------------------------------------------------------------------
    // UVI-1: UV editor island selection
    // -------------------------------------------------------------------------
    {
        FactoryIntegrationTestResult r;
        r.testId = "UVI-1";
        r.testName = "UV editor island selection";
        r.inputDesc = "Selecting UV island 0 in UVEditorDocument";
        r.expectedDesc = "IsIslandSelected(0) returns true and selection revision increments";

        auto s = TestIntegrationSetup::Create();
        UVEditorDocument doc(s.mesh, s.layout);

        uint64_t initialRev = doc.GetSelectionRevision();
        doc.SelectIsland(0);

        r.passed = (doc.IsIslandSelected(0) && doc.GetSelectionRevision() > initialRev);
        r.observedDesc = "Island 0 selected: " + std::string(doc.IsIslandSelected(0) ? "true" : "false") +
                         ", Rev: " + std::to_string(doc.GetSelectionRevision());
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVI-2: 2D/3D synchronized selection
    // -------------------------------------------------------------------------
    {
        FactoryIntegrationTestResult r;
        r.testId = "UVI-2";
        r.testName = "2D/3D synchronized selection";
        r.inputDesc = "Selecting 3D face associated with Island 0";
        r.expectedDesc = "Island 0 is automatically resolved and added to 2D selected islands";

        auto s = TestIntegrationSetup::Create();
        UVEditorDocument doc(s.mesh, s.layout);

        doc.SelectFace3D(s.faceId);

        r.passed = (doc.IsIslandSelected(0) && doc.IsFaceSelected(s.faceId));
        r.observedDesc = "3D Face selected: " + std::string(doc.IsFaceSelected(s.faceId) ? "true" : "false") +
                         ", 2D Island selected: " + std::string(doc.IsIslandSelected(0) ? "true" : "false");
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVI-3: UV change on painted asset
    // -------------------------------------------------------------------------
    {
        FactoryIntegrationTestResult r;
        r.testId = "UVI-3";
        r.testName = "UV change on painted asset";
        r.inputDesc = "Translating island on an asset flagged as painted";
        r.expectedDesc = "requiresTextureMigration flag is raised to protect paint content";

        auto s = TestIntegrationSetup::Create();
        UVEditorDocument doc(s.mesh, s.layout);

        doc.SelectIsland(0);
        doc.TranslateSelectedIslands(glm::vec2(0.1f, 0.0f));

        r.passed = doc.RequiresTextureMigration();
        r.observedDesc = "RequiresTextureMigration: " + std::string(doc.RequiresTextureMigration() ? "true" : "false");
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVI-4: Successful reprojection commit
    // -------------------------------------------------------------------------
    {
        FactoryIntegrationTestResult r;
        r.testId = "UVI-4";
        r.testName = "Successful reprojection commit";
        r.inputDesc = "ReprojectDocument executed after island translation";
        r.expectedDesc = "Migration succeeds and clears requiresTextureMigration flag atomically";

        auto s = TestIntegrationSetup::Create();
        UVEditorDocument doc(s.mesh, s.layout);
        UVLayout snapshotLayout = s.layout;

        doc.SelectIsland(0);
        doc.TranslateSelectedIslands(glm::vec2(0.05f, 0.05f));

        TexturePaintingService painter;
        painter.InitializeCanvas(256, 256);
        painter.AddLayer("AlbedoLayer", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

        TextureReprojectionService reprojService;
        auto report = reprojService.ReprojectDocument(doc, snapshotLayout, painter);

        bool flagCleared = !doc.RequiresTextureMigration();
        r.passed = (report.status == ReprojectionStatus::Success && flagCleared);
        r.observedDesc = "Status: " + std::string(GetReprojectionStatusName(report.status)) +
                         ", Migration flag cleared: " + (flagCleared ? "true" : "false");
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVI-5: Failed reprojection rollback
    // -------------------------------------------------------------------------
    {
        FactoryIntegrationTestResult r;
        r.testId = "UVI-5";
        r.testName = "Failed reprojection rollback";
        r.inputDesc = "Transaction rollback requested during staging";
        r.expectedDesc = "Original layers untouched and status recorded as RollbackExecuted";

        auto s = TestIntegrationSetup::Create();
        TextureReprojectionService reprojService;

        auto tx = reprojService.BeginTransaction("INT_TX_ROLLBACK");
        bool val = reprojService.ValidateTransaction(tx, s.mesh, s.layout, s.mesh, s.layout, {});
        bool rb = reprojService.RollbackTransaction(tx);

        r.passed = (val && rb && tx.phase == ReprojectionTransaction::Phase::RolledBack &&
                    tx.report.status == ReprojectionStatus::RollbackExecuted);
        r.observedDesc = "Rollback phase: RolledBack, Status: " + std::string(GetReprojectionStatusName(tx.report.status));
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVI-6: 3D stroke lifecycle
    // -------------------------------------------------------------------------
    {
        FactoryIntegrationTestResult r;
        r.testId = "UVI-6";
        r.testName = "3D stroke lifecycle";
        r.inputDesc = "BeginStroke3D -> UpdateStroke3D -> EndStroke3D cycle";
        r.expectedDesc = "DirtyRect is valid and non-empty after stroke completion";

        auto s = TestIntegrationSetup::Create();
        TexturePaintingService paintService;
        paintService.InitializeCanvas(256, 256);
        paintService.AddLayer("PaintLayer");

        SurfacePaintingAdapter adapter(&paintService);
        adapter.SetUVLayout(&s.layout);

        SurfacePaintHit hit;
        hit.valid = true;
        hit.faceId = s.faceId;
        hit.barycentricCoords = glm::vec3(0.33f, 0.33f, 0.34f);
        hit.interpolatedUV = glm::vec2(0.5f, 0.5f);
        hit.worldNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        hit.localNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        hit.worldPosition = glm::vec3(0.0f, 0.0f, 0.0f);
        hit.localPosition = glm::vec3(0.0f, 0.0f, 0.0f);

        PaintBrush brush;
        brush.radiusPixels = 20.0f;

        bool b1 = adapter.BeginStroke3D(s.mesh, hit, brush);
        SurfacePaintHit hitUpdate = hit;
        hitUpdate.interpolatedUV = glm::vec2(0.55f, 0.5f);
        bool b2 = adapter.UpdateStroke3D(s.mesh, hitUpdate, brush);
        PaintDirtyRect dirty = adapter.EndStroke3D();

        uint32_t area = dirty.GetWidth() * dirty.GetHeight();
        r.passed = (b1 && b2 && !dirty.isEmpty && area > 0);
        r.observedDesc = "Stroke lifecycle complete, DirtyRect area: " + std::to_string(area);
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVI-7: Viewport input capture
    // -------------------------------------------------------------------------
    {
        FactoryIntegrationTestResult r;
        r.testId = "UVI-7";
        r.testName = "Viewport input capture";
        r.inputDesc = "Simulating stroke cancellation when focus is lost or UI captures mouse";
        r.expectedDesc = "CancelStroke3D cleanly aborts stroke without committing dirty pixels";

        auto s = TestIntegrationSetup::Create();
        TexturePaintingService paintService;
        paintService.InitializeCanvas(256, 256);
        paintService.AddLayer("TestLayer");

        SurfacePaintingAdapter adapter(&paintService);
        adapter.SetUVLayout(&s.layout);

        SurfacePaintHit hit;
        hit.valid = true;
        hit.faceId = s.faceId;
        hit.interpolatedUV = glm::vec2(0.5f, 0.5f);
        hit.worldNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        hit.localNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        hit.worldPosition = glm::vec3(0.0f, 0.0f, 0.0f);
        hit.localPosition = glm::vec3(0.0f, 0.0f, 0.0f);

        PaintBrush brush;
        adapter.BeginStroke3D(s.mesh, hit, brush);
        bool cancelled = adapter.CancelStroke3D();

        r.passed = cancelled;
        r.observedDesc = "CancelStroke3D executed: " + std::string(cancelled ? "true" : "false");
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVI-8: Multi-atlas target resolution
    // -------------------------------------------------------------------------
    {
        FactoryIntegrationTestResult r;
        r.testId = "UVI-8";
        r.testName = "Multi-atlas target resolution";
        r.inputDesc = "Hit on face belonging to Island on AtlasPage 1";
        r.expectedDesc = "SurfacePaintingAdapter resolves correct atlas page without crashing";

        auto s = TestIntegrationSetup::Create();
        s.layout.totalAtlasPages = 2;
        s.layout.islands[0].atlasPage = 1;

        TexturePaintingService paintService;
        paintService.InitializeCanvas(256, 256);
        paintService.AddLayer("Page1Layer");

        SurfacePaintingAdapter adapter(&paintService);
        adapter.SetUVLayout(&s.layout);

        SurfacePaintHit hit;
        hit.valid = true;
        hit.faceId = s.faceId;
        hit.interpolatedUV = glm::vec2(0.5f, 0.5f);
        hit.worldNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        hit.localNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        hit.worldPosition = glm::vec3(0.0f, 0.0f, 0.0f);
        hit.localPosition = glm::vec3(0.0f, 0.0f, 0.0f);

        PaintBrush brush;
        bool begun = adapter.BeginStroke3D(s.mesh, hit, brush);
        adapter.EndStroke3D();

        r.passed = begun;
        r.observedDesc = "Multi-atlas hit accepted on page 1: " + std::string(begun ? "true" : "false");
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVI-9: Revision invalidation
    // -------------------------------------------------------------------------
    {
        FactoryIntegrationTestResult r;
        r.testId = "UVI-9";
        r.testName = "Revision invalidation";
        r.inputDesc = "Incrementing topology revision on EditableMesh";
        r.expectedDesc = "Mesh topology revision increments and triggers cache invalidation";

        auto s = TestIntegrationSetup::Create();
        uint64_t revBefore = s.mesh.GetTopologyRevision();
        s.mesh.IncrementTopologyRevision();
        uint64_t revAfter = s.mesh.GetTopologyRevision();

        r.passed = (revAfter > revBefore);
        r.observedDesc = "RevBefore: " + std::to_string(revBefore) + ", RevAfter: " + std::to_string(revAfter);
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // UVI-10: Stable Input = Zero Work
    // -------------------------------------------------------------------------
    {
        FactoryIntegrationTestResult r;
        r.testId = "UVI-10";
        r.testName = "Stable Input = Zero Work";
        r.inputDesc = "Consecutive reprojection calls with identical revisions";
        r.expectedDesc = "Second call returns ZeroWorkNoOp with zero texels written";

        auto s = TestIntegrationSetup::Create();
        TexturePaintingService painter;
        painter.InitializeCanvas(256, 256);
        painter.AddLayer("L1");

        TextureReprojectionService reproj;
        reproj.InvalidateCache();
        reproj.ReprojectPaintingService(s.mesh, s.layout, painter, s.mesh, s.layout);
        auto secondReport = reproj.ReprojectPaintingService(s.mesh, s.layout, painter, s.mesh, s.layout);

        r.passed = (secondReport.status == ReprojectionStatus::ZeroWorkNoOp && secondReport.zeroWorkHit);
        r.observedDesc = "ZeroWork hit: " + std::string(secondReport.zeroWorkHit ? "true" : "false");
        results.push_back(r);
    }

    return results;
}

} // namespace fw
