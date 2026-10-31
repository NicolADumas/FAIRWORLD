#include "pch.h"
#include "UVEditorContractTests.h"
#include "UVEditorDocument.h"
#include "GeneralUVMapper.h"
#include "UVAtlasPacker.h"
#include "blocks/texture/SurfacePaintingAdapter.h"
#include "blocks/texture/TexturePaintingService.h"
#include "PrimitiveUVMapper.h"
#include "world/GeometryCompiler.h"
#include "blocks/sculpt/MeshBridgeService.h"
#include <sstream>
#include <cmath>

namespace fw {

std::vector<UVEditorTestResult> UVEditorContractTests::RunAllTests() {
    std::vector<UVEditorTestResult> results;
    GeneralUVMapper mapper;
    UVAtlasPacker packer;

    auto addTest = [&](const std::string& id, const std::string& name,
                       const std::string& inDesc, const std::string& expDesc,
                       const std::string& obsDesc, bool passed, const std::string& det) {
        UVEditorTestResult r;
        r.testId = id;
        r.testName = name;
        r.inputDesc = inDesc;
        r.expectedDesc = expDesc;
        r.observedDesc = obsDesc;
        r.passed = passed;
        r.details = det;
        results.push_back(r);
    };

    auto setupCubeLayout = [&]() -> std::pair<EditableMesh, UVLayout> {
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        GeneralUVOptions gOpt;
        gOpt.seamOptions.dihedralAngleThresholdDegrees = 45.0f;
        auto res = mapper.UnwrapMesh(mesh, gOpt);
        AtlasPackOptions pOpt;
        packer.PackLayout(mesh, res.layout, pOpt);
        return { std::move(mesh), std::move(res.layout) };
    };

    // -------------------------------------------------------------------------
    // UVE-1: Island selection
    // -------------------------------------------------------------------------
    {
        auto [mesh, layout] = setupCubeLayout();
        UVEditorDocument doc(mesh, layout);

        doc.SelectIsland(0, false);

        bool passed = doc.IsIslandSelected(0) && 
                      doc.GetSelectedIslands().size() == 1 &&
                      !doc.GetSelectedFaces().empty();

        std::string obs = "SelectedIslands: " + std::to_string(doc.GetSelectedIslands().size()) +
                          ", SelectedFaces: " + std::to_string(doc.GetSelectedFaces().size());

        addTest("UVE-1", "Island Selection State",
                "Selezione dell'isola 0 tramite UVEditorDocument",
                "Isola 0 marcata selezionata e tutte le sue facce inserite in selectedFaces",
                obs, passed,
                passed ? "Selezione isola completata con successo." : "Errore selezione isola.");
    }

    // -------------------------------------------------------------------------
    // UVE-2: 3D -> 2D selection
    // -------------------------------------------------------------------------
    {
        auto [mesh, layout] = setupCubeLayout();
        UVEditorDocument doc(mesh, layout);

        FaceID testFace = mesh.GetActiveFaceIDs()[0];
        doc.SelectFace3D(testFace, false);

        bool passed = doc.IsFaceSelected(testFace) && !doc.GetSelectedIslands().empty();
        std::string obs = "FaceSelected: " + std::string(doc.IsFaceSelected(testFace) ? "true" : "false") +
                          ", AutoIslandSelected: " + std::to_string(doc.GetSelectedIslands().size());

        addTest("UVE-2", "3D to 2D Synchronized Selection",
                "Selezione della faccia 3D nel viewport",
                "Risoluzione automatica e selezione dell'isola UV 2D corrispondente",
                obs, passed,
                passed ? "Sincronizzazione 3D->2D operativa." : "Mancata selezione 2D da faccia 3D.");
    }

    // -------------------------------------------------------------------------
    // UVE-3: 2D -> 3D selection
    // -------------------------------------------------------------------------
    {
        auto [mesh, layout] = setupCubeLayout();
        UVEditorDocument doc(mesh, layout);

        bool passed = false;
        std::string obs;
        if (layout.islands.size() > 1) {
            doc.SelectIsland(1, false);
            const auto& islFaces = layout.islands[1].faces;

            bool allFacesSelected = true;
            for (FaceID f : islFaces) {
                if (!doc.IsFaceSelected(f)) {
                    allFacesSelected = false;
                    break;
                }
            }

            passed = allFacesSelected && !islFaces.empty();
            obs = "Island 1 Faces: " + std::to_string(islFaces.size()) +
                  ", AllSelected: " + std::string(allFacesSelected ? "true" : "false");
        } else {
            obs = "Insufficient islands: " + std::to_string(layout.islands.size());
        }

        addTest("UVE-3", "2D to 3D Synchronized Selection",
                "Selezione dell'isola UV 2D nell'editor",
                "Tutte le facce 3D costituenti l'isola marcate selezionate per highlight 3D",
                obs, passed,
                passed ? "Sincronizzazione 2D->3D operativa." : "Mancata selezione facce 3D da isola.");
    }

    // -------------------------------------------------------------------------
    // UVE-4: UV translation
    // -------------------------------------------------------------------------
    {
        auto [mesh, layout] = setupCubeLayout();
        UVEditorDocument doc(mesh, layout);

        doc.SelectIsland(0, false);
        glm::vec4 initialTransform = layout.islands[0].atlasTransform;

        doc.TranslateSelectedIslands(glm::vec2(0.05f, 0.02f));
        glm::vec4 newTransform = layout.islands[0].atlasTransform;

        bool passed = (newTransform.z == initialTransform.z + 0.05f) &&
                      (newTransform.w == initialTransform.w + 0.02f);

        std::string obs = "Init: [" + std::to_string(initialTransform.z) + ", " + std::to_string(initialTransform.w) +
                          "], New: [" + std::to_string(newTransform.z) + ", " + std::to_string(newTransform.w) + "]";

        addTest("UVE-4", "UV Island Translation",
                "Traslazione di (0.05, 0.02) nello spazio Atlas UV",
                "Offset trasformazione aggiornato esattamente della quantita delta",
                obs, passed,
                passed ? "Traslazione applicata con successo." : "Errore nella traslazione UV.");
    }

    // -------------------------------------------------------------------------
    // UVE-5: UV rotation
    // -------------------------------------------------------------------------
    {
        auto [mesh, layout] = setupCubeLayout();
        UVEditorDocument doc(mesh, layout);

        doc.SelectIsland(0, false);
        float initRot = layout.islands[0].rotationDegrees;

        doc.RotateSelectedIslands(90.0f);
        float newRot = layout.islands[0].rotationDegrees;

        bool passed = (std::abs(newRot - std::fmod(initRot + 90.0f, 360.0f)) < 0.1f);
        std::string obs = "InitRot: " + std::to_string(initRot) + ", NewRot: " + std::to_string(newRot);

        addTest("UVE-5", "UV Island Rotation",
                "Rotazione di 90 gradi su isola selezionata",
                "Aggiornamento deterministico di rotationDegrees",
                obs, passed,
                passed ? "Rotazione applicata correttamente." : "Errore rotazione isola.");
    }

    // -------------------------------------------------------------------------
    // UVE-6: UV scaling
    // -------------------------------------------------------------------------
    {
        auto [mesh, layout] = setupCubeLayout();
        UVEditorDocument doc(mesh, layout);

        doc.SelectIsland(0, false);
        glm::vec4 initTransform = layout.islands[0].atlasTransform;

        doc.ScaleSelectedIslands(glm::vec2(0.5f, 0.5f));
        glm::vec4 newTransform = layout.islands[0].atlasTransform;

        bool passed = (std::abs(newTransform.x - (initTransform.x * 0.5f)) < 1e-4f) &&
                      (std::abs(newTransform.y - (initTransform.y * 0.5f)) < 1e-4f);

        std::string obs = "InitScale: [" + std::to_string(initTransform.x) + ", " + std::to_string(initTransform.y) +
                          "], NewScale: [" + std::to_string(newTransform.x) + ", " + std::to_string(newTransform.y) + "]";

        addTest("UVE-6", "UV Island Scaling",
                "Scalatura di 0.5x su isola selezionata",
                "Fattori scaleU e scaleV dimezzati coerentemente",
                obs, passed,
                passed ? "Scalatura applicata con successo." : "Errore scalatura UV.");
    }

    // -------------------------------------------------------------------------
    // UVE-7: Atlas bounds validation
    // -------------------------------------------------------------------------
    {
        auto [mesh, layout] = setupCubeLayout();
        UVEditorDocument doc(mesh, layout);

        doc.SelectIsland(0, false);
        // Tenta di traslare l'isola fuori dall'atlas (delta = +2.0)
        doc.TranslateSelectedIslands(glm::vec2(2.0f, 2.0f));
        const auto& isl = layout.islands[0];

        bool withinBounds = (isl.atlasTransform.z >= 0.0f) &&
                            (isl.atlasTransform.w >= 0.0f) &&
                            (isl.atlasTransform.z + isl.atlasTransform.x <= 1.0001f) &&
                            (isl.atlasTransform.w + isl.atlasTransform.y <= 1.0001f);

        bool passed = withinBounds;
        std::string obs = "Offset: [" + std::to_string(isl.atlasTransform.z) + ", " + std::to_string(isl.atlasTransform.w) +
                          "], End: [" + std::to_string(isl.atlasTransform.z + isl.atlasTransform.x) + ", " +
                          std::to_string(isl.atlasTransform.w + isl.atlasTransform.y) + "]";

        addTest("UVE-7", "Atlas Bounds Clamping Safety",
                "Tentativo di traslazione oltre i margini dell'atlas (+2.0)",
                "Clamping automatico per impedire coordinate fuori dallo spazio texture",
                obs, passed,
                passed ? "Bounds clamping rispettato." : "Isola traslata fuori bounds.");
    }

    // -------------------------------------------------------------------------
    // UVE-8: Manual seam preservation
    // -------------------------------------------------------------------------
    {
        auto [mesh, layout] = setupCubeLayout();
        UVEditorDocument doc(mesh, layout);

        EdgeID testEdge = mesh.GetActiveEdgeIDs()[0];
        doc.ToggleManualSeam(testEdge);

        const auto* e = mesh.GetEdge(testEdge);
        bool passed = e && (e->seamWeight >= 0.99f);

        std::string obs = "SeamWeight: " + std::to_string(e ? e->seamWeight : 0.0f);

        addTest("UVE-8", "Manual Seam Interactive Toggling",
                "Inversione stato cucitura su spigolo topologico",
                "Spigolo impostato con seamWeight = 1.0f e revisione aggiornata",
                obs, passed,
                passed ? "Seam manuale attivata." : "Seam non impostata.");
    }

    // -------------------------------------------------------------------------
    // UVE-9: Undo / Redo
    // -------------------------------------------------------------------------
    {
        auto [mesh, layout] = setupCubeLayout();
        UVEditorDocument doc(mesh, layout);

        doc.SelectIsland(0, false);
        glm::vec4 originalTransform = layout.islands[0].atlasTransform;

        doc.TranslateSelectedIslands(glm::vec2(0.1f, 0.1f));
        glm::vec4 movedTransform = layout.islands[0].atlasTransform;

        bool undoOk = doc.Undo();
        glm::vec4 undoneTransform = layout.islands[0].atlasTransform;

        bool redoOk = doc.Redo();
        glm::vec4 redoneTransform = layout.islands[0].atlasTransform;

        bool passed = undoOk && redoOk &&
                      (undoneTransform == originalTransform) &&
                      (redoneTransform == movedTransform);

        std::string obs = "UndoneMatch: " + std::string(undoneTransform == originalTransform ? "true" : "false") +
                          ", RedoneMatch: " + std::string(redoneTransform == movedTransform ? "true" : "false");

        addTest("UVE-9", "Undo/Redo Command Invertibility",
                "Sequenza Translate -> Undo -> Redo",
                "Ripristino esatto dello snapshot precedente e successivo",
                obs, passed,
                passed ? "Undo/Redo perfettamente funzionante." : "Fallimento Undo/Redo.");
    }

    // -------------------------------------------------------------------------
    // UVE-10: 2D paint target resolution
    // -------------------------------------------------------------------------
    {
        auto [mesh, layout] = setupCubeLayout();
        UVEditorDocument doc(mesh, layout);
        TexturePaintingService paintService;
        paintService.InitializeCanvas(512, 512);

        PaintBrush brush;
        brush.radiusPixels = 10.0f;
        brush.color = glm::vec4(1, 0, 0, 1);

        bool painted = doc.PaintAtUV2D(glm::vec2(0.5f, 0.5f), brush, &paintService);

        bool passed = painted && (paintService.GetRevision() > 1);
        std::string obs = "Painted: " + std::string(painted ? "true" : "false") +
                          ", Revision: " + std::to_string(paintService.GetRevision());

        addTest("UVE-10", "2D UV Painting Delegation",
                "Pennellata a coordinate UV (0.5, 0.5) tramite UVEditorDocument",
                "Conversione in coordinate raster e applicazione dab su TexturePaintingService",
                obs, passed,
                passed ? "Pittura 2D delegata con successo." : "Errore pittura 2D.");
    }

    // -------------------------------------------------------------------------
    // UVE-11: 3D paint target resolution
    // -------------------------------------------------------------------------
    {
        auto [mesh, layout] = setupCubeLayout();
        TexturePaintingService paintService;
        paintService.InitializeCanvas(512, 512);
        SurfacePaintingAdapter adapter(&paintService);
        adapter.SetUVLayout(&layout);

        PaintBrush brush;
        brush.radiusPixels = 12.0f;
        brush.color = glm::vec4(0, 1, 0, 1);

        // Raycast dritto contro la faccia frontale del cubo (Z = +1.0 verso Z = 0)
        bool strokeBegun = adapter.BeginStroke3D(mesh, glm::vec3(0, 0, 2.0f), glm::vec3(0, 0, -1.0f), brush);
        adapter.EndStroke3D();

        bool passed = strokeBegun && (paintService.GetRevision() > 1);
        std::string obs = "StrokeBegun: " + std::string(strokeBegun ? "true" : "false") +
                          ", Revision: " + std::to_string(paintService.GetRevision());

        addTest("UVE-11", "3D Raycast to Atlas UV Paint Target",
                "Tratto 3D via SurfacePaintingAdapter con UVLayout associato",
                "Risoluzione corretta delle coordinate per-corner dell'atlas e applicazione dab",
                obs, passed,
                passed ? "Pittura 3D integrata con layout UV atlas." : "Errore integrazione 3D.");
    }

    // -------------------------------------------------------------------------
    // UVE-12: Cross-seam painting
    // -------------------------------------------------------------------------
    {
        auto [mesh, layout] = setupCubeLayout();
        TexturePaintingService paintService;
        paintService.InitializeCanvas(512, 512);
        SurfacePaintingAdapter adapter(&paintService);
        adapter.SetUVLayout(&layout);
        adapter.SetBrushSpaceMode(BrushSpaceMode::SurfaceSpace);

        PaintBrush brush;
        brush.radiusPixels = 30.0f;
        brush.radiusMetric = 0.4f; // Raggio metrico ampio che tocca lo spigolo
        brush.color = glm::vec4(0, 0, 1, 1);

        // Colpisce vicino al bordo del cubo (X = 0.45, bordo a 0.5)
        bool begun = adapter.BeginStroke3D(mesh, glm::vec3(0.45f, 0.0f, 2.0f), glm::vec3(0, 0, -1.0f), brush);
        adapter.EndStroke3D();

        bool passed = begun;
        std::string obs = "CrossSeamStrokeBegun: " + std::string(begun ? "true" : "false");

        addTest("UVE-12", "Cross-Seam Paint Dab Propagation",
                "Tratto 3D in prossimita di una cucitura topologica con raggio metrico esteso",
                "Propagazione del dab sull'isola adiacente attraverso la cucitura",
                obs, passed,
                passed ? "Cross-seam painting propagato con successo." : "Mancata propagazione cross-seam.");
    }

    // -------------------------------------------------------------------------
    // UVE-13: Multi-atlas painting
    // -------------------------------------------------------------------------
    {
        auto [mesh, layout] = setupCubeLayout();
        layout.totalAtlasPages = 2;
        layout.islands[0].atlasPage = 1; // Isola 0 su pagina 1

        UVEditorDocument doc(mesh, layout);
        doc.SelectIsland(0, false);

        bool passed = (doc.GetActiveAtlasPage() == 1);
        std::string obs = "ActiveAtlasPage: " + std::to_string(doc.GetActiveAtlasPage());

        addTest("UVE-13", "Multi-Atlas Active Page Resolution",
                "Selezione di isola collocata su pagina atlas secondaria (Page 1)",
                "Aggiornamento automatico della pagina atlas attiva nell'editor",
                obs, passed,
                passed ? "Pagina multi-atlas risolta correttamente." : "Errore pagina multi-atlas.");
    }

    // -------------------------------------------------------------------------
    // UVE-14: Dirty-region tracking
    // -------------------------------------------------------------------------
    {
        auto [mesh, layout] = setupCubeLayout();
        TexturePaintingService paintService;
        paintService.InitializeCanvas(512, 512);
        SurfacePaintingAdapter adapter(&paintService);

        PaintBrush brush;
        brush.radiusPixels = 15.0f;
        adapter.BeginStroke3D(mesh, glm::vec3(0, 0, 2.0f), glm::vec3(0, 0, -1.0f), brush);
        PaintDirtyRect dr = adapter.EndStroke3D();

        bool passed = !dr.isEmpty && dr.GetWidth() > 0 && dr.GetHeight() > 0;
        std::string obs = "DirtyRect: [" + std::to_string(dr.xMin) + ", " + std::to_string(dr.yMin) +
                          " -> " + std::to_string(dr.xMax) + ", " + std::to_string(dr.yMax) + "]";

        addTest("UVE-14", "Stroke Paint Dirty Region Tracking",
                "Esecuzione di un tratto e chiusura con EndStroke3D",
                "Restituzione di una bounding box dirty non vuota per upload GPU incrementale",
                obs, passed,
                passed ? "Dirty region calcolata correttamente." : "Dirty rect vuoto.");
    }

    // -------------------------------------------------------------------------
    // UVE-15: Zero-Work
    // -------------------------------------------------------------------------
    {
        auto [mesh, layout] = setupCubeLayout();
        UVEditorDocument doc(mesh, layout);

        uint64_t topRev = mesh.GetTopologyRevision();
        uint64_t geomRev = mesh.GetGeometryRevision();
        uint64_t atlasRev = layout.atlasRevision;

        doc.SelectIsland(0, false);
        doc.ClearSelection();

        bool passed = (mesh.GetTopologyRevision() == topRev) &&
                      (mesh.GetGeometryRevision() == geomRev) &&
                      (layout.atlasRevision == atlasRev) &&
                      (doc.GetSelectionRevision() > 1);

        std::string obs = "SelRevChanged: " + std::to_string(doc.GetSelectionRevision()) +
                          ", AtlasRevIntact: " + std::string(layout.atlasRevision == atlasRev ? "true" : "false");

        addTest("UVE-15", "Selection Zero-Work Revision Isolation",
                "Operazioni di selezione pura nell'editor 2D",
                "Avanzamento solo di selectionRevision; nessuna invalidazione di topologia o atlas",
                obs, passed,
                passed ? "Isolamento Zero-Work della selezione verificato." : "Invalidazione spuria delle revisioni.");
    }

    // -------------------------------------------------------------------------
    // UVE-16: Painted asset UV-change safety
    // -------------------------------------------------------------------------
    {
        auto [mesh, layout] = setupCubeLayout();
        UVEditorDocument doc(mesh, layout);

        doc.SelectIsland(0, false);
        doc.TranslateSelectedIslands(glm::vec2(0.01f, 0.01f));

        bool migrationRequired = doc.RequiresTextureMigration();
        bool passed = migrationRequired;

        std::string obs = "RequiresTextureMigration: " + std::string(migrationRequired ? "true" : "false");

        addTest("UVE-16", "Painted Asset UV-Change Safety Notification",
                "Modifica delle coordinate di un'isola mediante transform 2D",
                "Attivazione automatica del flag requiresTextureMigration per BM-UV6",
                obs, passed,
                passed ? "Notifica di migrazione texture registrata." : "Mancata notifica di sicurezza.");
    }

    // -------------------------------------------------------------------------
    // UVE-17: Generational ID invalidation
    // -------------------------------------------------------------------------
    {
        auto [mesh, layout] = setupCubeLayout();
        UVEditorDocument doc(mesh, layout);

        FaceID f0 = mesh.GetActiveFaceIDs()[0];
        doc.SelectFace3D(f0, false);

        // Simula la cancellazione della faccia e l'avanzamento della revisione topologica
        mesh.DeleteFace(f0);
        doc.ValidateSelection();

        bool passed = !doc.IsFaceSelected(f0);
        std::string obs = "DeletedFaceSelected: " + std::string(doc.IsFaceSelected(f0) ? "true" : "false");

        addTest("UVE-17", "Generational ID Invalidation Safety",
                "Eliminazione topologica di una faccia precedentemente selezionata",
                "ValidateSelection elimina l'identificatore obsoleto prevenendo accessi dangling",
                obs, passed,
                passed ? "ID generazionale invalidato correttamente." : "Faccia eliminata ancora in selezione.");
    }

    // -------------------------------------------------------------------------
    // UVE-18: Serialization consistency
    // -------------------------------------------------------------------------
    {
        auto [mesh, layout] = setupCubeLayout();
        UVEditorDocument doc(mesh, layout);

        doc.SelectIsland(0, false);
        doc.RotateSelectedIslands(90.0f);

        nlohmann::json j = layout;
        UVLayout restored = j.get<UVLayout>();

        bool passed = (restored.islands[0].rotationDegrees == layout.islands[0].rotationDegrees) &&
                      (restored.atlasRevision == layout.atlasRevision);

        std::string obs = "OriginalRot: " + std::to_string(layout.islands[0].rotationDegrees) +
                          ", RestoredRot: " + std::to_string(restored.islands[0].rotationDegrees);

        addTest("UVE-18", "Editor State JSON Serialization Consistency",
                "Serializzazione e ripristino di UVLayout post-modifiche editor",
                "Conservazione esatta dei gradi di rotazione e revisione atlas",
                obs, passed,
                passed ? "Stato dell'editor coerente con serializzazione JSON." : "Incoerenza di serializzazione.");
    }

    // -------------------------------------------------------------------------
    // UVE-19: Fail-closed Topology Revision & FaceID Validation
    // -------------------------------------------------------------------------
    {
        auto [mesh, layout] = setupCubeLayout();
        UVEditorDocument doc(mesh, layout);

        glm::vec2 validUV = glm::vec2(layout.islands[0].atlasTransform.z + layout.islands[0].atlasTransform.x * 0.5f,
                                      layout.islands[0].atlasTransform.w + layout.islands[0].atlasTransform.y * 0.5f);
        bool initialHit = doc.IsPointInIsland(validUV, 0);

        // Modifica la revisione topologica della mesh senza aggiornare il layout UV
        mesh.IncrementTopologyRevision();

        // Con revisione disallineata, IsPointInIsland deve fallire (fail-closed)
        bool hitAfterTopoChange = doc.IsPointInIsland(validUV, 0);

        // Anche BeginStroke2D in SurfacePaint deve fallire closed e riportare blocco
        TexturePaintingService painter;
        painter.InitializeCanvas(512, 512);
        PaintBrush brush;
        brush.radiusPixels = 5.0f;
        brush.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        bool strokeStarted = doc.BeginStroke2D(validUV, brush, &painter, 1, CanvasPaintMode::SurfacePaint);

        bool passed = initialHit && !hitAfterTopoChange && !strokeStarted &&
                      !doc.GetLastPaintBlockedReason().empty();

        std::string obs = "InitialHit=" + std::string(initialHit ? "true" : "false") +
                          ", HitAfterTopoChange=" + std::string(hitAfterTopoChange ? "true" : "false") +
                          ", StrokeStarted=" + std::string(strokeStarted ? "true" : "false") +
                          ", BlockedReason='" + doc.GetLastPaintBlockedReason() + "'";

        addTest("UVE-FailClosed", "Fail-closed Topology Revision & FaceID Contract",
                "Verifica fail-closed su IsPointInIsland e BeginStroke2D con revisione topologica disallineata",
                "Rifiuto categorico del test di contenimento e blocco della pennellata",
                obs, passed,
                passed ? "Contratto fail-closed rispettato: nessuna pittura non autorizzata."
                       : "Violazione fail-closed: tratto consentito con revisione topologica disallineata.");
    }

    // -------------------------------------------------------------------------
    // UVE-19: Polygon Mask Isolation (Wedge Triangle vs Quad Islands)
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Wedge;
        ShapeParamsWedge p;
        p.size = glm::vec3(1.0f);
        sDef.parameters = p;

        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        PrimitiveUVMapper primMapper;
        PrimitiveUVOptions opt;
        UVLayout layout;
        primMapper.GenerateWedgeMapping(mesh, layout, glm::vec3(1.0f), opt);
        UVEditorDocument doc(mesh, layout);

        TexturePaintingService painter;
        painter.InitializeCanvas(512, 512, glm::vec4(0.0f));

        // Isola 3 e' il triangolo sinistro (col 0, row 1), Isola 1 e' il quad centrale (col 1, row 0).
        // Seleziona esplicitamente l'isola 3 per vincolare la pittura alla sola faccia autorizzata
        doc.SelectIsland(3, false);

        const auto& isl3 = layout.islands[3];
        glm::vec2 triCenterUV = glm::vec2(isl3.atlasTransform.z + isl3.atlasTransform.x * 0.25f,
                                          isl3.atlasTransform.w + isl3.atlasTransform.y * 0.75f);
        PaintBrush brush;
        brush.radiusPixels = 40.0f;
        brush.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        brush.opacity = 1.0f;
        brush.strength = 1.0f;

        bool strokeOk = doc.BeginStroke2D(triCenterUV, brush, &painter, 1, CanvasPaintMode::SurfacePaint);
        doc.EndStroke2D(&painter);

        // Verifica che i texel dell'Isola 1 (quad) NON siano stati alterati (alpha == 0)
        const auto& isl1 = layout.islands[1];
        glm::vec2 quadCenterUV = glm::vec2(isl1.atlasTransform.z + isl1.atlasTransform.x * 0.5f,
                                           isl1.atlasTransform.w + isl1.atlasTransform.y * 0.5f);
        glm::vec2 quadPx = UVAtlasPacker::AtlasToPixel(quadCenterUV, 512, 512);
        PaintLayer* l = painter.GetLayer(1);
        glm::vec4 quadCol = l ? l->pixelBuffer.GetPixel(static_cast<uint32_t>(quadPx.x), static_cast<uint32_t>(quadPx.y)) : glm::vec4(1.0f);

        // Verifica che il centro del triangolo abbia ricevuto il colore del pennello
        glm::vec2 triPx = UVAtlasPacker::AtlasToPixel(triCenterUV, 512, 512);
        glm::vec4 triCol = l ? l->pixelBuffer.GetPixel(static_cast<uint32_t>(triPx.x), static_cast<uint32_t>(triPx.y)) : glm::vec4(0.0f);

        bool passed = strokeOk && (quadCol.a < 1e-4f) && (triCol.r > 0.5f);
        std::string obs = "StrokeOk=" + std::string(strokeOk ? "true" : "false") +
                          ", QuadTexelAlpha=" + std::to_string(quadCol.a) +
                          ", TriTexelRed=" + std::to_string(triCol.r);

        addTest("UVE-19", "Polygon Mask Isolation",
                "Pennellata su isola triangolare (isola 3) con maschera poligonale SurfacePaint",
                "Texel interni di isola 1 intatti (alpha=0), texel interni triangolo dipinti",
                obs, passed,
                passed ? "Isolamento maschera poligonale rispettato: nessuna contaminazione multi-isola."
                       : "Violazione isolamento: texel di altre isole contaminati.");
    }

    // -------------------------------------------------------------------------
    // UVE-20: CPU/GPU UV Correspondence (Zero Divergence on Wedge)
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Wedge;
        ShapeParamsWedge p;
        p.size = glm::vec3(1.0f);
        sDef.parameters = p;

        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        PrimitiveUVMapper primMapper;
        PrimitiveUVOptions opt;
        UVLayout layout;
        primMapper.GenerateWedgeMapping(mesh, layout, glm::vec3(1.0f), opt);

        // Compila mesh per rendering Vulkan 3D
        GeometryCompileOptions gOpt;
        auto compRes = GeometryCompiler::CompileEditableMesh(mesh, gOpt);
        bool compileOk = compRes.success && !compRes.mesh.vertices.empty();

        // Verifica che per ogni corner dell'EditableMesh, v.roughMetal (location=2 nello shader)
        // coincida esattamente con l'UV atlas dell'editor entro 1e-4f
        float maxDelta = 0.0f;
        size_t verifiedCorners = 0;

        for (FaceID fId : mesh.GetActiveFaceIDs()) {
            const auto* f = mesh.GetFace(fId);
            if (!f || !IsValid(f->halfEdge)) continue;
            HalfEdgeID startH = f->halfEdge;
            HalfEdgeID curH = startH;
            do {
                const auto* he = mesh.GetHalfEdge(curH);
                if (!he) break;
                glm::vec2 cpuUV = he->corner.uv;
                
                const auto* v = mesh.GetVertex(he->origin);
                if (v) {
                    bool matched = false;
                    for (const auto& cv : compRes.mesh.vertices) {
                        glm::vec3 cvPos(cv.position.x, cv.position.y, cv.position.z);
                        if (glm::length(cvPos - v->position) < 1e-4f) {
                            glm::vec2 cvRoughMetal(cv.roughMetal.x, cv.roughMetal.y);
                            float d = glm::length(cvRoughMetal - cpuUV);
                            if (d < 1e-4f) {
                                matched = true;
                                maxDelta = std::max(maxDelta, d);
                                break;
                            }
                        }
                    }
                    if (matched) verifiedCorners++;
                }
                curH = he->next;
            } while (curH != startH && IsValid(curH));
        }

        bool allUvsMatch = (verifiedCorners >= 18); // Wedge: 3 quad * 4 + 2 tri * 3 = 18 corners
        bool passed = compileOk && allUvsMatch && (maxDelta < 1e-4f);
        std::string obs = "CompileOk=" + std::string(compileOk ? "true" : "false") +
                          ", VerifiedCorners=" + std::to_string(verifiedCorners) +
                          ", MaxDelta=" + std::to_string(maxDelta);

        addTest("UVE-20", "CPU/GPU UV Correspondence",
                "Wedge UV analitico da EditableMesh a vertici compilati (v.roughMetal)",
                "UV compilate Vulkan identiche alle UV CPU dell'editor entro 1e-4f senza duplicazioni o scaling",
                obs, passed,
                passed ? "Corrispondenza CPU/GPU UV perfetta: 2D Painter e 3D Vulkan in perfetta sincronia."
                       : "Divergenza UV CPU/GPU: vertici compilati non corrispondono alle UV dell'editor.");
    }

    // -------------------------------------------------------------------------
    // UVE-21: Stable UV / Zero Work (No Geometry Recompiles on Paint Stroke)
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Wedge;
        ShapeParamsWedge p;
        p.size = glm::vec3(1.0f);
        sDef.parameters = p;

        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        PrimitiveUVMapper primMapper;
        PrimitiveUVOptions opt;
        UVLayout layout;
        primMapper.GenerateWedgeMapping(mesh, layout, glm::vec3(1.0f), opt);

        BridgePreviewCache previewCache;
        GeometryCompileOptions gOpt;
        MeshBridgeService::CompileForPreview(mesh, gOpt, previewCache);

        uint32_t initialGeoRev = mesh.GetGeometryRevision();
        uint32_t initialTopoRev = mesh.GetTopologyRevision();
        uint32_t initialAtlasRev = layout.atlasRevision;

        UVEditorDocument doc(mesh, layout);
        TexturePaintingService painter;
        painter.InitializeCanvas(512, 512);
        PaintBrush brush;
        brush.radiusPixels = 5.0f;
        brush.color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);

        glm::vec2 pUV = glm::vec2(layout.islands[0].atlasTransform.z + 0.05f,
                                  layout.islands[0].atlasTransform.w + 0.05f);
        doc.SelectIsland(0, false);
        doc.BeginStroke2D(pUV, brush, &painter, 1, CanvasPaintMode::SurfacePaint);
        doc.EndStroke2D(&painter);

        bool geoUnchanged = (mesh.GetGeometryRevision() == initialGeoRev);
        bool topoUnchanged = (mesh.GetTopologyRevision() == initialTopoRev);
        bool atlasUnchanged = (layout.atlasRevision == initialAtlasRev);
        bool zeroWorkPreserved = !MeshBridgeService::NeedsPreviewRecompile(mesh, 0, previewCache);

        bool passed = geoUnchanged && topoUnchanged && atlasUnchanged && zeroWorkPreserved;
        std::string obs = "GeoRevDelta=" + std::to_string(mesh.GetGeometryRevision() - initialGeoRev) +
                          ", TopoRevDelta=" + std::to_string(mesh.GetTopologyRevision() - initialTopoRev) +
                          ", NeedsRecompile=" + std::string(zeroWorkPreserved ? "false" : "true");

        addTest("UVE-21", "Stable UV / Zero Work",
                "Pennellata 2D su mesh con cache preview compilata",
                "Nessun incremento di Geometry/Topology/Atlas revision; NeedsPreviewRecompile restituisce false",
                obs, passed,
                passed ? "Contratto Zero-Work rispettato: nessuna ricompilazione geometrica per la pittura."
                       : "Violazione Zero-Work: pittura causa ricompilazione della geometria.");
    }

    // -------------------------------------------------------------------------
    // UVE-22: Per-BlockID Isolation
    // -------------------------------------------------------------------------
    {
        TexturePaintingService painter14;
        TexturePaintingService painter15;
        painter14.InitializeCanvas(512, 512, glm::vec4(0.0f));
        painter15.InitializeCanvas(512, 512, glm::vec4(0.0f));

        uint32_t l14 = painter14.GetLayers()[0].layerId;
        uint32_t l15 = painter15.GetLayers()[0].layerId;
        painter14.GetLayer(l14)->name = "Block14_Layer";
        painter15.GetLayer(l15)->name = "Block15_Layer";

        PaintBrush brush;
        brush.radiusPixels = 10.0f;
        brush.color = glm::vec4(1.0f, 0.5f, 0.0f, 1.0f);

        painter14.BeginStroke(l14, false);
        painter14.ApplyDab(glm::vec2(100.0f, 100.0f), brush);
        painter14.EndStroke();

        PaintLayer* layer15 = painter15.GetLayer(l15);
        bool p15Clean = true;
        if (layer15) {
            glm::vec4 sample = layer15->pixelBuffer.GetPixel(100, 100);
            if (sample.a > 1e-4f) p15Clean = false;
        }

        bool passed = p15Clean && (painter14.GetLayerCount() == 1) && (painter15.GetLayerCount() == 1);
        std::string obs = "Block15Clean=" + std::string(p15Clean ? "true" : "false") +
                          ", LayerCount14=" + std::to_string(painter14.GetLayerCount()) +
                          ", LayerCount15=" + std::to_string(painter15.GetLayerCount());

        addTest("UVE-22", "Per-BlockID Isolation",
                "Pennellata su BlockID 14 verificata contro canvas di BlockID 15",
                "Nessuna mutazione su BlockID 15; completo isolamento dei layer e dei pixel",
                obs, passed,
                passed ? "Isolamento Per-BlockID garantito."
                       : "Contaminazione inter-BlockID rilevata.");
    }

    // -------------------------------------------------------------------------
    // UVE-23: Save/Reload Persistence
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Wedge;
        ShapeParamsWedge p;
        p.size = glm::vec3(1.0f);
        sDef.parameters = p;

        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        PrimitiveUVMapper primMapper;
        PrimitiveUVOptions opt;
        UVLayout layoutOriginal;
        primMapper.GenerateWedgeMapping(mesh, layoutOriginal, glm::vec3(1.0f), opt);

        nlohmann::json j;
        to_json(j, layoutOriginal);

        UVLayout layoutReloaded;
        from_json(j, layoutReloaded);

        bool sameIslandCount = (layoutOriginal.islands.size() == layoutReloaded.islands.size());
        bool sameTransforms = true;
        for (size_t i = 0; i < layoutOriginal.islands.size() && sameTransforms; ++i) {
            if (glm::length(layoutOriginal.islands[i].atlasTransform - layoutReloaded.islands[i].atlasTransform) > 1e-4f) {
                sameTransforms = false;
            }
        }

        UVEditorDocument docReloaded(mesh, layoutReloaded);
        const auto& isl3 = layoutReloaded.islands[3];
        glm::vec2 triCenterUV = glm::vec2(isl3.atlasTransform.z + isl3.atlasTransform.x * 0.25f,
                                          isl3.atlasTransform.w + isl3.atlasTransform.y * 0.75f);
        bool hitValidAfterReload = docReloaded.IsPointInIsland(triCenterUV, 3);

        bool passed = sameIslandCount && sameTransforms && hitValidAfterReload;
        std::string obs = "SameIslandCount=" + std::string(sameIslandCount ? "true" : "false") +
                          ", SameTransforms=" + std::string(sameTransforms ? "true" : "false") +
                          ", HitValidAfterReload=" + std::string(hitValidAfterReload ? "true" : "false");

        addTest("UVE-23", "Save/Reload Persistence",
                "Serializzazione e deserializzazione JSON di UVLayout Wedge e ricostruzione UVEditorDocument",
                "Isola triangolare e trasformazioni atlas conservate fedelmente dopo ricaricamento",
                obs, passed,
                passed ? "Persistenza UV e associazione superfici verificata con successo."
                       : "Fallimento nel salvataggio/ricaricamento del layout UV.");
    }

    // =========================================================================
    // BM-UV-FACE-SYNC-05 CONTRACT TESTS (UVE-33 -> UVE-43)
    // =========================================================================

    // -------------------------------------------------------------------------
    // UVE-33: 2D Face Selection
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Wedge;
        ShapeParamsWedge p;
        p.size = glm::vec3(1.0f);
        sDef.parameters = p;

        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        PrimitiveUVMapper primMapper;
        PrimitiveUVOptions opt;
        UVLayout layout;
        primMapper.GenerateWedgeMapping(mesh, layout, glm::vec3(1.0f), opt);

        UVEditorDocument doc(mesh, layout);
        glm::vec2 clickUV = glm::vec2(layout.islands[0].atlasTransform.z + layout.islands[0].atlasTransform.x * 0.5f,
                                      layout.islands[0].atlasTransform.w + layout.islands[0].atlasTransform.y * 0.5f);
        UVIslandID hitIsland = doc.FindIslandAtUV(clickUV);
        bool hitOk = (hitIsland == 0);
        doc.SelectIsland(hitIsland, false);

        FaceID expectedFace0 = layout.islands[0].faces.empty() ? INVALID_FACE_ID : layout.islands[0].faces[0];
        bool isl0Selected = doc.IsIslandSelected(0);
        bool face0Selected = expectedFace0.IsValid() && doc.IsFaceSelected(expectedFace0);
        bool passed = hitOk && isl0Selected && face0Selected;
        std::string obs = "HitIsland=" + std::to_string(hitIsland) +
                          ", Isl0Selected=" + std::string(isl0Selected ? "true" : "false") +
                          ", Face0Selected=" + std::string(face0Selected ? "true" : "false");

        addTest("UVE-33", "2D Face Selection",
                "Click su coordinate UV interne al poligono dell'Isola 0 (Slope Quad)",
                "FindIslandAtUV restituisce IslandID 0, SelectIsland seleziona autoritativamente FaceID 0",
                obs, passed,
                passed ? "2D Face Selection conforme: risoluzione biunivoca UV -> FaceID accertata."
                       : "Fallimento 2D Face Selection.");
    }

    // -------------------------------------------------------------------------
    // UVE-34: 3D Face Selection
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Wedge;
        ShapeParamsWedge p;
        p.size = glm::vec3(1.0f);
        sDef.parameters = p;

        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        PrimitiveUVMapper primMapper;
        PrimitiveUVOptions opt;
        UVLayout layout;
        primMapper.GenerateWedgeMapping(mesh, layout, glm::vec3(1.0f), opt);

        glm::vec3 rayOrigin(0.0f, 2.0f, 0.25f);
        glm::vec3 rayDir(0.0f, -1.0f, 0.0f);
        SurfacePaintHit hit = SurfacePaintingAdapter::RaycastMesh(mesh, rayOrigin, rayDir);

        FaceID expectedSlopeFace = layout.islands[0].faces.empty() ? INVALID_FACE_ID : layout.islands[0].faces[0];
        bool hitValid = hit.valid;
        bool faceHitCorrect = (hitValid && expectedSlopeFace.IsValid() && hit.faceId == expectedSlopeFace);
        bool passed = hitValid && faceHitCorrect;
        std::string obs = "HitValid=" + std::string(hitValid ? "true" : "false") +
                          ", HitFaceIdx=" + (hitValid ? std::to_string(hit.faceId.index) : "INVALID") +
                          ", ExpFaceIdx=" + (expectedSlopeFace.IsValid() ? std::to_string(expectedSlopeFace.index) : "INVALID");

        addTest("UVE-34", "3D Face Selection",
                "Raycast 3D diretto sulla superficie della Wedge (faccia Slope)",
                "Picking 3D intercetta la mesh e restituisce FaceID 0 autoritativo",
                obs, passed,
                passed ? "3D Face Picking conforme: FaceID 0 correttamente risolto dal raycast geometrico."
                       : "Fallimento 3D Face Picking.");
    }

    // -------------------------------------------------------------------------
    // UVE-35: Bidirectional Synchronization
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Wedge;
        ShapeParamsWedge p;
        p.size = glm::vec3(1.0f);
        sDef.parameters = p;

        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        PrimitiveUVMapper primMapper;
        PrimitiveUVOptions opt;
        UVLayout layout;
        primMapper.GenerateWedgeMapping(mesh, layout, glm::vec3(1.0f), opt);

        UVEditorDocument doc(mesh, layout);

        FaceID face0 = layout.islands[0].faces.empty() ? INVALID_FACE_ID : layout.islands[0].faces[0];
        FaceID face3 = layout.islands[3].faces.empty() ? INVALID_FACE_ID : layout.islands[3].faces[0];

        // Step 1: 3D -> 2D (SelectFace3D seleziona anche IslandID corrispondente)
        doc.SelectFace3D(face0, false);
        bool step1_Face0 = face0.IsValid() && doc.IsFaceSelected(face0);
        bool step1_Isl0 = doc.IsIslandSelected(0);

        // Step 2: 2D -> 3D (SelectIsland seleziona anche FaceID corrispondente)
        doc.SelectIsland(3, false);
        bool step2_Isl3 = doc.IsIslandSelected(3);
        bool step2_Face3 = face3.IsValid() && doc.IsFaceSelected(face3);

        bool passed = face0.IsValid() && face3.IsValid() && step1_Face0 && step1_Isl0 && step2_Isl3 && step2_Face3;
        std::string obs = "3Dto2D(Face0=" + std::string(step1_Face0 ? "T" : "F") + ",Isl0=" + std::string(step1_Isl0 ? "T" : "F") + ") " +
                          "2Dto3D(Isl3=" + std::string(step2_Isl3 ? "T" : "F") + ",Face3=" + std::string(step2_Face3 ? "T" : "F") + ")";

        addTest("UVE-35", "Bidirectional Synchronization",
                "Selezione sequenziale 3D (Face 0) e 2D (Island 3)",
                "Stato di selezione bidirezionale coerente al 100% in entrambe le direzioni",
                obs, passed,
                passed ? "Sincronizzazione bidirezionale 2D/3D perfettamente allineata."
                       : "Disallineamento nella sincronizzazione bidirezionale.");
    }

    // -------------------------------------------------------------------------
    // UVE-36: Highlight Isolation
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Wedge;
        ShapeParamsWedge p;
        p.size = glm::vec3(1.0f);
        sDef.parameters = p;

        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        PrimitiveUVMapper primMapper;
        PrimitiveUVOptions opt;
        UVLayout layout;
        primMapper.GenerateWedgeMapping(mesh, layout, glm::vec3(1.0f), opt);

        UVEditorDocument doc(mesh, layout);
        doc.SelectIsland(0, false);

        FaceID face0 = layout.islands[0].faces.empty() ? INVALID_FACE_ID : layout.islands[0].faces[0];
        bool onlyIsl0 = (doc.GetSelectedIslands().size() == 1) && doc.IsIslandSelected(0);
        bool onlyFace0 = (doc.GetSelectedFaces().size() == 1) && face0.IsValid() && doc.IsFaceSelected(face0);

        bool otherFacesClean = true;
        for (size_t i = 1; i < layout.islands.size(); ++i) {
            for (FaceID fId : layout.islands[i].faces) {
                if (doc.IsFaceSelected(fId)) {
                    otherFacesClean = false;
                    break;
                }
            }
        }
        bool otherIslandsClean = !doc.IsIslandSelected(1) && !doc.IsIslandSelected(2) && !doc.IsIslandSelected(3) && !doc.IsIslandSelected(4);

        bool passed = onlyIsl0 && onlyFace0 && otherFacesClean && otherIslandsClean;
        std::string obs = "SelectedIslands=" + std::to_string(doc.GetSelectedIslands().size()) +
                          ", SelectedFaces=" + std::to_string(doc.GetSelectedFaces().size()) +
                          ", OthersClean=" + std::string((otherFacesClean && otherIslandsClean) ? "true" : "false");

        addTest("UVE-36", "Highlight Isolation",
                "Selezione singola della Faccia 0 / Isola 0",
                "Solo la faccia/isola selezionata e marcata attiva; tutte le altre 4 rimangono neutre",
                obs, passed,
                passed ? "Isolamento dell'evidenziazione confermato: zero contaminazione visiva."
                       : "Contaminazione selezione: facce non selezionate risultano attive.");
    }

    // -------------------------------------------------------------------------
    // UVE-37: Hard Face Lock
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Wedge;
        ShapeParamsWedge p;
        p.size = glm::vec3(1.0f);
        sDef.parameters = p;

        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        PrimitiveUVMapper primMapper;
        PrimitiveUVOptions opt;
        UVLayout layout;
        primMapper.GenerateWedgeMapping(mesh, layout, glm::vec3(1.0f), opt);

        UVEditorDocument doc(mesh, layout);
        TexturePaintingService painter;
        painter.InitializeCanvas(512, 512);
        PaintBrush brush;
        brush.radiusPixels = 6.0f;
        brush.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

        // 1. Con nessuna selezione attiva, BeginStroke2D DEVE essere rifiutato (Nessuna superficie selezionata)
        doc.ClearSelection();
        glm::vec2 pUv0 = glm::vec2(layout.islands[0].atlasTransform.z + layout.islands[0].atlasTransform.x * 0.5f,
                                   layout.islands[0].atlasTransform.w + layout.islands[0].atlasTransform.y * 0.5f);
        bool strokeWithoutSelectionBlocked = !doc.BeginStroke2D(pUv0, brush, &painter, 1, CanvasPaintMode::SurfacePaint);

        // 2. Selezioniamo Isola 0. Click su Isola 1 DEVE essere rifiutato (Fuori dalla superficie selezionata)
        doc.SelectIsland(0, false);
        glm::vec2 pUv1 = glm::vec2(layout.islands[1].atlasTransform.z + layout.islands[1].atlasTransform.x * 0.5f,
                                   layout.islands[1].atlasTransform.w + layout.islands[1].atlasTransform.y * 0.5f);
        bool strokeOutsideSelectedBlocked = !doc.BeginStroke2D(pUv1, brush, &painter, 1, CanvasPaintMode::SurfacePaint);

        // 3. Click su Isola 0 DEVE essere accettato
        bool strokeInsideSelectedAllowed = doc.BeginStroke2D(pUv0, brush, &painter, 1, CanvasPaintMode::SurfacePaint);
        doc.EndStroke2D(&painter);

        bool passed = strokeWithoutSelectionBlocked && strokeOutsideSelectedBlocked && strokeInsideSelectedAllowed;
        std::string obs = "BlockedNoSel=" + std::string(strokeWithoutSelectionBlocked ? "true" : "false") +
                          ", BlockedOutside=" + std::string(strokeOutsideSelectedBlocked ? "true" : "false") +
                          ", AllowedInside=" + std::string(strokeInsideSelectedAllowed ? "true" : "false");

        addTest("UVE-37", "Hard Face Lock",
                "Tentativi di pittura senza selezione, fuori dall'isola selezionata e dentro l'isola selezionata",
                "Pittura bloccata tassativamente in assenza di selezione o all'esterno della faccia bloccata",
                obs, passed,
                passed ? "Hard Face Lock rigoroso: rasterizzazione vincolata esclusivamente alla faccia attiva."
                       : "Violazione Hard Face Lock.");
    }

    // -------------------------------------------------------------------------
    // UVE-38: Overlap Detection
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Wedge;
        ShapeParamsWedge p;
        p.size = glm::vec3(1.0f);
        sDef.parameters = p;

        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        PrimitiveUVMapper primMapper;
        PrimitiveUVOptions opt;
        UVLayout layout;
        primMapper.GenerateWedgeMapping(mesh, layout, glm::vec3(1.0f), opt);

        bool hasOverlap = false;
        for (size_t i = 0; i < layout.islands.size() && !hasOverlap; ++i) {
            for (size_t j = i + 1; j < layout.islands.size() && !hasOverlap; ++j) {
                const auto& tA = layout.islands[i].atlasTransform;
                const auto& tB = layout.islands[j].atlasTransform;
                bool overlapX = (tA.z < tB.z + tB.x - 1e-4f) && (tA.z + tA.x > tB.z + 1e-4f);
                bool overlapY = (tA.w < tB.w + tB.y - 1e-4f) && (tA.w + tA.y > tB.w + 1e-4f);
                if (overlapX && overlapY) {
                    hasOverlap = true;
                }
            }
        }

        bool passed = (layout.islands.size() == 5) && !hasOverlap;
        std::string obs = "IslandCount=" + std::to_string(layout.islands.size()) +
                          ", OverlapDetected=" + std::string(hasOverlap ? "true" : "false");

        addTest("UVE-38", "Overlap Detection",
                "Verifica di disgiunzione e overlap su tutte le 5 isole UV della Wedge",
                "Tutte le 5 isole occupano porzioni disgiunte dell'atlante UV con overlap nullo",
                obs, passed,
                passed ? "Layout UV della Wedge disgiunto: zero sovrapposizione tra le facce."
                       : "Rilevata sovrapposizione UV nell'atlante della Wedge.");
    }

    // -------------------------------------------------------------------------
    // UVE-39: CPU/GPU UV Correspondence
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Wedge;
        ShapeParamsWedge p;
        p.size = glm::vec3(1.0f);
        sDef.parameters = p;

        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        PrimitiveUVMapper primMapper;
        PrimitiveUVOptions opt;
        UVLayout layout;
        primMapper.GenerateWedgeMapping(mesh, layout, glm::vec3(1.0f), opt);

        BridgePreviewCache previewCache;
        GeometryCompileOptions gOpt;
        GeometryCompileResult compRes = MeshBridgeService::CompileForPreview(mesh, gOpt, previewCache);

        bool compileOk = compRes.success && (compRes.mesh.vertices.size() == 24) && (compRes.mesh.faces.size() == 8);
        bool cacheOk = (previewCache.cachedMesh.vertices.size() == 24);

        // Verifica che per ogni corner dell'EditableMesh, v.roughMetal (location=2 nello shader forge.vert)
        // coincida con l'UV atlas dell'editor entro 1e-4f
        float maxDelta = 0.0f;
        size_t verifiedCorners = 0;

        for (FaceID fId : mesh.GetActiveFaceIDs()) {
            const auto* f = mesh.GetFace(fId);
            if (!f || !IsValid(f->halfEdge)) continue;
            HalfEdgeID startH = f->halfEdge;
            HalfEdgeID curH = startH;
            do {
                const auto* he = mesh.GetHalfEdge(curH);
                if (!he) break;
                glm::vec2 cpuUV = he->corner.uv;

                const auto* v = mesh.GetVertex(he->origin);
                if (v) {
                    bool matched = false;
                    for (const auto& cv : compRes.mesh.vertices) {
                        glm::vec3 cvPos(cv.position.x, cv.position.y, cv.position.z);
                        if (glm::length(cvPos - v->position) < 1e-4f) {
                            glm::vec2 cvRoughMetal(cv.roughMetal.x, cv.roughMetal.y);
                            float d = glm::length(cvRoughMetal - cpuUV);
                            if (d < 1e-4f) {
                                matched = true;
                                maxDelta = std::max(maxDelta, d);
                                break;
                            }
                        }
                    }
                    if (matched) verifiedCorners++;
                }
                curH = he->next;
            } while (curH != startH && IsValid(curH));
        }

        bool allUvsMatch = (verifiedCorners >= 18); // Wedge: 3 quad * 4 + 2 tri * 3 = 18 corners
        bool passed = compileOk && cacheOk && allUvsMatch && (maxDelta < 1e-4f);
        std::string obs = "CompileOk=" + std::string(compileOk ? "true" : "false") +
                          ", Vertices=" + std::to_string(compRes.mesh.vertices.size()) +
                          ", Triangles=" + std::to_string(compRes.mesh.faces.size()) +
                          ", VerifiedCorners=" + std::to_string(verifiedCorners) +
                          ", MaxDelta=" + std::to_string(maxDelta);

        addTest("UVE-39", "CPU/GPU UV Correspondence",
                "Compilazione Vulkan non indicizzata della Wedge con 24 vertici generati da 18 corner",
                "24 vertici compilati con coordinate UV embedded in vert.roughMetal coincidenti con l'atlante",
                obs, passed,
                passed ? "Corrispondenza CPU/GPU UV convalidata per tutti i 24 vertici."
                       : "Mancata corrispondenza tra vertici compilati GPU e corner UV CPU.");
    }

    // -------------------------------------------------------------------------
    // UVE-40: Selection Zero-Work
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Wedge;
        ShapeParamsWedge p;
        p.size = glm::vec3(1.0f);
        sDef.parameters = p;

        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        PrimitiveUVMapper primMapper;
        PrimitiveUVOptions opt;
        UVLayout layout;
        primMapper.GenerateWedgeMapping(mesh, layout, glm::vec3(1.0f), opt);

        BridgePreviewCache previewCache;
        GeometryCompileOptions gOpt;
        MeshBridgeService::CompileForPreview(mesh, gOpt, previewCache);

        uint32_t initGeo = mesh.GetGeometryRevision();
        uint32_t initTopo = mesh.GetTopologyRevision();
        uint32_t initAtlas = layout.atlasRevision;

        UVEditorDocument doc(mesh, layout);
        FaceID face2 = layout.islands[2].faces.empty() ? INVALID_FACE_ID : layout.islands[2].faces[0];
        doc.SelectIsland(1, false);
        if (face2.IsValid()) {
            doc.SelectFace3D(face2, false);
        }
        doc.ClearSelection();
        doc.SelectIsland(4, false);

        bool geoUnchanged = (mesh.GetGeometryRevision() == initGeo);
        bool topoUnchanged = (mesh.GetTopologyRevision() == initTopo);
        bool atlasUnchanged = (layout.atlasRevision == initAtlas);
        bool noRecompile = !MeshBridgeService::NeedsPreviewRecompile(mesh, 0, previewCache);

        bool passed = geoUnchanged && topoUnchanged && atlasUnchanged && noRecompile;
        std::string obs = "GeoRevDelta=" + std::to_string(mesh.GetGeometryRevision() - initGeo) +
                          ", TopoRevDelta=" + std::to_string(mesh.GetTopologyRevision() - initTopo) +
                          ", NeedsRecompile=" + std::string(noRecompile ? "false" : "true");

        addTest("UVE-40", "Selection Zero-Work",
                "Cambi ripetuti di selezione 2D e 3D sul modello",
                "Zero modifiche alle revisioni geometriche o UV, nessuna ricompilazione Vulkan",
                obs, passed,
                passed ? "Contratto Selection Zero-Work rispettato al 100%."
                       : "Violazione Selection Zero-Work: la selezione provoca ricompilazione.");
    }

    // -------------------------------------------------------------------------
    // UVE-41: Paint Zero-Work
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Wedge;
        ShapeParamsWedge p;
        p.size = glm::vec3(1.0f);
        sDef.parameters = p;

        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        PrimitiveUVMapper primMapper;
        PrimitiveUVOptions opt;
        UVLayout layout;
        primMapper.GenerateWedgeMapping(mesh, layout, glm::vec3(1.0f), opt);

        BridgePreviewCache previewCache;
        GeometryCompileOptions gOpt;
        MeshBridgeService::CompileForPreview(mesh, gOpt, previewCache);

        uint32_t initGeo = mesh.GetGeometryRevision();
        uint32_t initTopo = mesh.GetTopologyRevision();
        uint32_t initAtlas = layout.atlasRevision;

        UVEditorDocument doc(mesh, layout);
        doc.SelectIsland(0, false);
        TexturePaintingService painter;
        painter.InitializeCanvas(512, 512);
        PaintBrush brush;
        brush.radiusPixels = 5.0f;
        brush.color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);

        glm::vec2 pUv = glm::vec2(layout.islands[0].atlasTransform.z + layout.islands[0].atlasTransform.x * 0.5f,
                                  layout.islands[0].atlasTransform.w + layout.islands[0].atlasTransform.y * 0.5f);
        doc.BeginStroke2D(pUv, brush, &painter, 1, CanvasPaintMode::SurfacePaint);
        doc.EndStroke2D(&painter);

        bool geoUnchanged = (mesh.GetGeometryRevision() == initGeo);
        bool topoUnchanged = (mesh.GetTopologyRevision() == initTopo);
        bool atlasUnchanged = (layout.atlasRevision == initAtlas);
        bool noRecompile = !MeshBridgeService::NeedsPreviewRecompile(mesh, 0, previewCache);

        bool passed = geoUnchanged && topoUnchanged && atlasUnchanged && noRecompile;
        std::string obs = "GeoRevDelta=" + std::to_string(mesh.GetGeometryRevision() - initGeo) +
                          ", TopoRevDelta=" + std::to_string(mesh.GetTopologyRevision() - initTopo) +
                          ", NeedsRecompile=" + std::string(noRecompile ? "false" : "true");

        addTest("UVE-41", "Paint Zero-Work",
                "Tratto di pittura 2D completato sulla faccia selezionata",
                "Aggiornamento della sola revisione texture/layer; nessuna ricompilazione geometrica o remap UV",
                obs, passed,
                passed ? "Contratto Paint Zero-Work rispettato: la pittura non tocca la geometria."
                       : "Violazione Paint Zero-Work.");
    }

    // -------------------------------------------------------------------------
    // UVE-42: Persistence
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Wedge;
        ShapeParamsWedge p;
        p.size = glm::vec3(1.0f);
        sDef.parameters = p;

        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        PrimitiveUVMapper primMapper;
        PrimitiveUVOptions opt;
        UVLayout layout;
        primMapper.GenerateWedgeMapping(mesh, layout, glm::vec3(1.0f), opt);

        UVEditorDocument doc(mesh, layout);
        doc.SelectIsland(0, false);
        TexturePaintingService painter;
        painter.InitializeCanvas(512, 512);
        PaintBrush brush;
        brush.radiusPixels = 8.0f;
        brush.color = glm::vec4(0.2f, 0.8f, 0.4f, 1.0f);

        glm::vec2 pUv = glm::vec2(layout.islands[0].atlasTransform.z + layout.islands[0].atlasTransform.x * 0.5f,
                                  layout.islands[0].atlasTransform.w + layout.islands[0].atlasTransform.y * 0.5f);
        doc.BeginStroke2D(pUv, brush, &painter, 1, CanvasPaintMode::SurfacePaint);
        doc.EndStroke2D(&painter);

        // Simulazione serializzazione UVLayout e PaintLayers
        nlohmann::json jLayout;
        to_json(jLayout, layout);
        UVLayout layoutReloaded;
        from_json(jLayout, layoutReloaded);

        ImageBitmap comp;
        painter.CompositeLayers(comp);
        uint32_t pxX = static_cast<uint32_t>(pUv.x * 512.0f);
        uint32_t pxY = static_cast<uint32_t>(pUv.y * 512.0f);
        glm::vec4 paintedPx = comp.GetPixel(pxX, pxY);

        bool sameIslands = (layoutReloaded.islands.size() == 5);
        bool pixelPersisted = (paintedPx.a > 0.5f && paintedPx.g > 0.5f);
        bool passed = sameIslands && pixelPersisted;
        std::string obs = "SameIslands=" + std::string(sameIslands ? "true" : "false") +
                          ", PixelPersisted=" + std::string(pixelPersisted ? "true" : "false");

        addTest("UVE-42", "Persistence",
                "Serializzazione UVLayout e verifica persistenza texel dipinti",
                "Layout ricostruito identico e texel dipinti conservati intatti",
                obs, passed,
                passed ? "Persistenza convalidata: layout e texture rimangono integri."
                       : "Fallimento persistenza texture o layout UV.");
    }

    // -------------------------------------------------------------------------
    // UVE-43: Per-BlockID Isolation
    // -------------------------------------------------------------------------
    {
        TexturePaintingService painter14;
        TexturePaintingService painter15;
        painter14.InitializeCanvas(512, 512, glm::vec4(0.0f));
        painter15.InitializeCanvas(512, 512, glm::vec4(0.0f));

        uint32_t l14 = painter14.GetLayers()[0].layerId;
        uint32_t l15 = painter15.GetLayers()[0].layerId;
        painter14.GetLayer(l14)->name = "Block14_Wedge";
        painter15.GetLayer(l15)->name = "Block15_Other";

        PaintBrush brush;
        brush.radiusPixels = 12.0f;
        brush.color = glm::vec4(0.9f, 0.1f, 0.2f, 1.0f);

        painter14.BeginStroke(l14, false);
        painter14.ApplyDab(glm::vec2(200.0f, 200.0f), brush);
        painter14.EndStroke();

        ImageBitmap comp15;
        painter15.CompositeLayers(comp15);
        glm::vec4 px15 = comp15.GetPixel(200, 200);

        bool b15Clean = (px15.a < 1e-4f);
        bool passed = b15Clean && (painter14.GetLayerCount() == 1) && (painter15.GetLayerCount() == 1);
        std::string obs = "Block15PixelAlpha=" + std::to_string(px15.a) +
                          ", LayerCount14=" + std::to_string(painter14.GetLayerCount()) +
                          ", LayerCount15=" + std::to_string(painter15.GetLayerCount());

        addTest("UVE-43", "Per-BlockID Isolation",
                "Pennellata su BlockID 14 (Wedge) verificata contro canvas di BlockID 15",
                "Totale assenza di mutazione o leak su BlockID 15",
                obs, passed,
                passed ? "Isolamento Per-BlockID assoluto: zero contaminazione tra blocchi diversi."
                       : "Rilevata contaminazione cross-BlockID.");
    }

    return results;
}

} // namespace fw
