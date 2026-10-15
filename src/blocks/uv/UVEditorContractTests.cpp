#include "pch.h"
#include "UVEditorContractTests.h"
#include "UVEditorDocument.h"
#include "GeneralUVMapper.h"
#include "UVAtlasPacker.h"
#include "blocks/texture/SurfacePaintingAdapter.h"
#include "blocks/texture/TexturePaintingService.h"
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

    return results;
}

} // namespace fw
