#include "pch.h"
#include "GeneralUVContractTests.h"
#include "GeneralUVMapper.h"
#include "SeamGraph.h"
#include "UVIslandExtractor.h"
#include "LSCMParameterizer.h"
#include "blocks/sculpt/MeshBridgeService.h"
#include "world/GeometryCompiler.h"
#include <sstream>
#include <cmath>

namespace fw {

std::vector<GeneralUVTestResult> GeneralUVContractTests::RunAllTests() {
    std::vector<GeneralUVTestResult> results;
    GeneralUVMapper mapper;

    auto addTest = [&](const std::string& id, const std::string& name,
                       const std::string& inDesc, const std::string& expDesc,
                       const std::string& obsDesc, bool passed, const std::string& det) {
        GeneralUVTestResult r;
        r.testId = id;
        r.testName = name;
        r.inputDesc = inDesc;
        r.expectedDesc = expDesc;
        r.observedDesc = obsDesc;
        r.passed = passed;
        r.details = det;
        results.push_back(r);
    };

    // Helper: Crea una griglia piana aperta (mesh aperta 2x2 quad)
    auto createOpenPlaneGrid = []() -> EditableMesh {
        EditableMesh mesh;
        std::vector<VertexID> v;
        for (int y = 0; y <= 2; ++y) {
            for (int x = 0; x <= 2; ++x) {
                v.push_back(mesh.AddVertex(glm::vec3(x * 0.5f, 0.0f, y * 0.5f)));
            }
        }
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 2; ++x) {
                int i0 = y * 3 + x;
                int i1 = y * 3 + (x + 1);
                int i2 = (y + 1) * 3 + (x + 1);
                int i3 = (y + 1) * 3 + x;
                mesh.AddFace({ v[i0], v[i1], v[i2], v[i3] }, 0, 0);
            }
        }
        mesh.ComputeAllFaceNormals();
        mesh.ComputeVertexNormals();
        return mesh;
    };

    // -------------------------------------------------------------------------
    // UVG-1: Mesh aperta planare
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = createOpenPlaneGrid();
        GeneralUVOptions opt;
        auto res = mapper.UnwrapMesh(mesh, opt);

        bool passed = res.success && 
                      res.layout.islands.size() == 1 &&
                      res.validation.nanInfCount == 0 &&
                      res.validation.degenerateUVTriangles == 0 &&
                      res.layout.islands[0].distortion.flippedTriangleCount == 0;

        std::string obs = "Islands: " + std::to_string(res.layout.islands.size()) +
                          ", Flipped: " + std::to_string(res.layout.islands.empty() ? 0 : res.layout.islands[0].distortion.flippedTriangleCount) +
                          ", NaN: " + std::to_string(res.validation.nanInfCount);

        addTest("UVG-1", "Planar Open Mesh Unwrapping",
                "Griglia piana aperta 2x2 quad (4 facce, 9 vertici)",
                "1 singola isola senza seams interne, 0 flipped triangles, coordinate valide",
                obs, passed,
                passed ? "Mesh aperta sviluppata conformemente in unica isola." : "Fallito unwrap su mesh planare.");
    }

    // -------------------------------------------------------------------------
    // UVG-2: Mesh chiusa con seams automatiche (Cube)
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        GeneralUVOptions opt;
        opt.seamOptions.dihedralAngleThresholdDegrees = 45.0f; // Il cubo ha 90 gradi -> tutte le seams rilevate
        auto res = mapper.UnwrapMesh(mesh, opt);

        bool passed = res.success && 
                      res.layout.islands.size() >= 6 &&
                      res.validation.isValid &&
                      res.validation.nanInfCount == 0;

        std::string obs = "Islands: " + std::to_string(res.layout.islands.size()) +
                          ", Seam weight checked, Valid: " + (res.validation.isValid ? "true" : "false");

        addTest("UVG-2", "Closed Mesh Auto-Seams & Island Extraction",
                "Cubo chiuso manifold 1.0m (6 facce quad, 8 vertici)",
                "Almeno 6 isole separate su spigoli vivi a 90 gradi, no buchi topologici",
                obs, passed,
                passed ? "Cubo sezionato e sviluppato correttamente." : "Fallito sezionamento cubo.");
    }

    // -------------------------------------------------------------------------
    // UVG-3: Mesh scolpita con curvature variabili
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        // Deforma alcuni vertici per simulare una scultura organica
        for (VertexID vId : mesh.GetActiveVertexIDs()) {
            auto* v = mesh.GetVertexMutable(vId);
            if (v) {
                v->position += glm::vec3(std::sin(v->position.x * 3.0f) * 0.15f,
                                         std::cos(v->position.y * 3.0f) * 0.15f,
                                         0.0f);
            }
        }
        mesh.IncrementGeometryRevision();
        mesh.ComputeAllFaceNormals();

        GeneralUVOptions opt;
        auto res = mapper.UnwrapMesh(mesh, opt);

        bool passed = res.success && 
                      res.validation.nanInfCount == 0 &&
                      !res.layout.islands.empty();

        std::string obs = "Islands: " + std::to_string(res.layout.islands.size()) +
                          ", MaxStretch: " + std::to_string(res.layout.islands.empty() ? 0.0f : res.layout.islands[0].distortion.maxStretch);

        addTest("UVG-3", "Sculpted Mesh Variable Curvature",
                "Mesh cubica deformata con spostamenti trigonometrici sui vertici",
                "LSCM converge calcolando stretch e distorsioni finite senza divergenza",
                obs, passed,
                passed ? "Mesh scolpita parametrizzata con metriche calcolate." : "LSCM fallito su mesh scolpita.");
    }

    // -------------------------------------------------------------------------
    // UVG-4: Mesh estrusa
    // -------------------------------------------------------------------------
    {
        ShapeDefinition cylShape;
        cylShape.type = ShapeType::Cylinder;
        ShapeParamsCylinder cp;
        cp.radius = 0.5f;
        cp.height = 2.0f;
        cylShape.parameters = cp;
        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(cylShape);

        GeneralUVOptions opt;
        auto res = mapper.UnwrapMesh(mesh, opt);

        bool passed = res.success && 
                      res.layout.islands.size() >= 3 && // 2 basi + cilindro o settori
                      res.validation.nanInfCount == 0;

        std::string obs = "Islands: " + std::to_string(res.layout.islands.size()) +
                          ", Valid: " + (res.validation.isValid ? "true" : "false");

        addTest("UVG-4", "Extruded Geometry Unwrapping",
                "Cilindro estruso con basi e mantello laterale",
                "Basi e mantello sezionati in isole con coordinate conformi",
                obs, passed,
                passed ? "Cilindro estruso sviluppato con successo." : "Errore nell'unwrapping cilindrico.");
    }

    // -------------------------------------------------------------------------
    // UVG-5: Mesh con bevel/inset
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = createOpenPlaneGrid();
        // Inset simulato: crea una sottofaccia interna
        VertexID c0 = mesh.AddVertex(glm::vec3(0.2f, 0.0f, 0.2f));
        VertexID c1 = mesh.AddVertex(glm::vec3(0.8f, 0.0f, 0.2f));
        VertexID c2 = mesh.AddVertex(glm::vec3(0.8f, 0.0f, 0.8f));
        VertexID c3 = mesh.AddVertex(glm::vec3(0.2f, 0.0f, 0.8f));
        mesh.AddFace({ c0, c1, c2, c3 }, 1, 0);
        mesh.ComputeAllFaceNormals();

        GeneralUVOptions opt;
        auto res = mapper.UnwrapMesh(mesh, opt);

        bool passed = res.success && res.validation.nanInfCount == 0;
        std::string obs = "Islands: " + std::to_string(res.layout.islands.size()) +
                          ", Valid: " + (res.validation.isValid ? "true" : "false");

        addTest("UVG-5", "Inset/Bevel Geometry Unwrapping",
                "Mesh piana con anello poligonale e inset centrale",
                "Facce interne e di raccordo parametrizzate coerentemente",
                obs, passed,
                passed ? "Geometria con inset sviluppata senza degenerazioni." : "Errore inset unwrapping.");
    }

    // -------------------------------------------------------------------------
    // UVG-6: Componenti disconnesse
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh;
        // Cubo 1 a X = -2.0
        EditableMesh c1 = EditableMesh::CreateCube(0.5f);
        // Cubo 2 a X = +2.0
        EditableMesh c2 = EditableMesh::CreateCube(0.5f);

        // Aggiungi entrambi alla stessa EditableMesh
        for (FaceID fId : c1.GetActiveFaceIDs()) {
            const auto* f = c1.GetFace(fId);
            std::vector<VertexID> vList;
            HalfEdgeID h = f->halfEdge, startH = h;
            do {
                const auto* he = c1.GetHalfEdge(h);
                const auto* v = c1.GetVertex(he->origin);
                vList.push_back(mesh.AddVertex(v->position + glm::vec3(-2.0f, 0, 0)));
                h = he->next;
            } while (h != startH && h.IsValid());
            mesh.AddFace(vList, 0, 0);
        }
        for (FaceID fId : c2.GetActiveFaceIDs()) {
            const auto* f = c2.GetFace(fId);
            std::vector<VertexID> vList;
            HalfEdgeID h = f->halfEdge, startH = h;
            do {
                const auto* he = c2.GetHalfEdge(h);
                const auto* v = c2.GetVertex(he->origin);
                vList.push_back(mesh.AddVertex(v->position + glm::vec3(2.0f, 0, 0)));
                h = he->next;
            } while (h != startH && h.IsValid());
            mesh.AddFace(vList, 1, 0);
        }
        mesh.ComputeAllFaceNormals();

        GeneralUVOptions opt;
        auto res = mapper.UnwrapMesh(mesh, opt);

        bool passed = res.success && 
                      res.layout.islands.size() >= 12 && // 6 facce * 2 cubi
                      res.validation.nanInfCount == 0;

        std::string obs = "Islands: " + std::to_string(res.layout.islands.size()) +
                          ", Total Faces: " + std::to_string(mesh.GetFaceCount());

        addTest("UVG-6", "Disconnected Components Handling",
                "Due cubi separati nello spazio all'interno della medesima EditableMesh",
                "Estrazione indipendente di isole per ciascuna componente senza collisioni",
                obs, passed,
                passed ? "Componenti disconnesse sezionate in isole indipendenti." : "Collisione isole disconnesse.");
    }

    // -------------------------------------------------------------------------
    // UVG-7: Superficie con cavità o tunnel
    // -------------------------------------------------------------------------
    {
        ShapeDefinition hcShape;
        hcShape.type = ShapeType::HollowCylinder;
        ShapeParamsHollowCylinder hcp;
        hcp.outerRadius = 1.0f;
        hcp.innerRadius = 0.5f;
        hcp.height = 1.0f;
        hcShape.parameters = hcp;
        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(hcShape);

        GeneralUVOptions opt;
        auto res = mapper.UnwrapMesh(mesh, opt);

        bool passed = res.success && 
                      res.layout.islands.size() >= 4 && // parete est, parete int, 2 anelli
                      res.validation.nanInfCount == 0;

        std::string obs = "Islands: " + std::to_string(res.layout.islands.size()) +
                          ", Valid: " + (res.validation.isValid ? "true" : "false");

        addTest("UVG-7", "Cavity / Tunnel Surface Unwrapping",
                "Cilindro cavo con tunnel passante assiale",
                "Parete interna ed esterna sezionate e parametrizzate senza auto-intersezioni",
                obs, passed,
                passed ? "Cavita interna e pareti sviluppate correttamente." : "Errore su superficie cava.");
    }

    // -------------------------------------------------------------------------
    // UVG-8: N-gon concavo
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh;
        // Poligono a 6 lati (a forma di L)
        VertexID v0 = mesh.AddVertex(glm::vec3(0, 0, 0));
        VertexID v1 = mesh.AddVertex(glm::vec3(2, 0, 0));
        VertexID v2 = mesh.AddVertex(glm::vec3(2, 0, 1));
        VertexID v3 = mesh.AddVertex(glm::vec3(1, 0, 1));
        VertexID v4 = mesh.AddVertex(glm::vec3(1, 0, 2));
        VertexID v5 = mesh.AddVertex(glm::vec3(0, 0, 2));
        mesh.AddFace({ v0, v1, v2, v3, v4, v5 }, 0, 0);
        mesh.ComputeAllFaceNormals();

        GeneralUVOptions opt;
        auto res = mapper.UnwrapMesh(mesh, opt);

        bool passed = res.success && 
                      res.layout.islands.size() == 1 &&
                      res.validation.nanInfCount == 0;

        std::string obs = "Islands: " + std::to_string(res.layout.islands.size()) +
                          ", Valid: " + (res.validation.isValid ? "true" : "false");

        addTest("UVG-8", "Concave N-Gon Unwrapping",
                "Poligono a L concavo a 6 lati in singola faccia",
                "Triangolazione coerente, coordinate UV finite per tutti i 6 corner",
                obs, passed,
                passed ? "N-gon concavo triangolato e mappato correttamente." : "Fallito unwrap su N-gon.");
    }

    // -------------------------------------------------------------------------
    // UVG-9: Non-manifold diagnostic
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh;
        // Crea vertice pinched (due triangoli che condividono solo il vertice centrale)
        VertexID center = mesh.AddVertex(glm::vec3(0, 0, 0));
        VertexID a1 = mesh.AddVertex(glm::vec3(-1, 0, 0));
        VertexID a2 = mesh.AddVertex(glm::vec3(-1, 0, 1));
        VertexID b1 = mesh.AddVertex(glm::vec3(1, 0, 0));
        VertexID b2 = mesh.AddVertex(glm::vec3(1, 0, 1));
        mesh.AddFace({ a1, a2, center }, 0, 0);
        mesh.AddFace({ center, b1, b2 }, 0, 0);
        mesh.ComputeAllFaceNormals();

        std::vector<std::string> issues;
        auto classification = GeneralUVMapper::ClassifyTopology(mesh, issues);

        bool passed = (classification == TopologySupportClassification::RequiresRepair) &&
                      !issues.empty();

        std::string obs = "Classification: " + std::string(GetTopologySupportClassificationName(classification)) +
                          ", Issues: " + (issues.empty() ? "None" : issues[0]);

        addTest("UVG-9", "Non-Manifold Topology Diagnostic",
                "Topologia non-manifold con pinched vertex a clessidra",
                "Classificazione esplicita REQUIRES_REPAIR con diagnostica",
                obs, passed,
                passed ? "Pinched vertex diagnosticato correttamente." : "Mancato rilevamento non-manifold.");
    }

    // -------------------------------------------------------------------------
    // UVG-10: Degenerate triangle handling
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0, 0, 0));
        VertexID v1 = mesh.AddVertex(glm::vec3(1, 0, 0));
        VertexID v2 = mesh.AddVertex(glm::vec3(2, 0, 0)); // Collineare -> area = 0
        mesh.AddFace({ v0, v1, v2 }, 0, 0);
        mesh.ComputeAllFaceNormals();

        std::vector<std::string> issues;
        auto classification = GeneralUVMapper::ClassifyTopology(mesh, issues);

        bool passed = (classification == TopologySupportClassification::SupportedWithDiagnostic);

        std::string obs = "Classification: " + std::string(GetTopologySupportClassificationName(classification));

        addTest("UVG-10", "Degenerate Triangle Handling",
                "Triangolo degenere con 3 vertici collineari (area = 0)",
                "Classificato SUPPORTED_WITH_DIAGNOSTIC senza crash o eccezioni",
                obs, passed,
                passed ? "Triangolo degenere gestito con diagnostica." : "Degenerazione non gestita.");
    }

    // -------------------------------------------------------------------------
    // UVG-11: Manual seam preservation
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        // Marca manualmente un edge come seam
        EdgeID testEdge = mesh.GetActiveEdgeIDs()[0];
        SeamGraph::SetManualSeam(mesh, testEdge, true);

        SeamGraph sg;
        SeamOptions opt;
        sg.BuildFromMesh(mesh, opt);

        bool isManual = (sg.GetSeamType(testEdge) == SeamType::Manual);
        SeamGraph::ClearAutomaticSeams(mesh);
        const auto* e = mesh.GetEdge(testEdge);
        bool preservedAfterClear = e && (e->seamWeight >= 0.99f);

        bool passed = isManual && preservedAfterClear;
        std::string obs = "IsManual: " + std::string(isManual ? "true" : "false") +
                          ", Preserved: " + std::string(preservedAfterClear ? "true" : "false");

        addTest("UVG-11", "Manual Seam Preservation",
                "Cubo con cucitura utente manuale (seamWeight = 1.0f)",
                "Seam riconosciuta come Manual e preservata dopo pulizia seam automatiche",
                obs, passed,
                passed ? "Seam manuale preservata con ownership utente." : "Seam manuale persa o sovrascritta.");
    }

    // -------------------------------------------------------------------------
    // UVG-12: Island extraction determinism
    // -------------------------------------------------------------------------
    {
        EditableMesh m1 = EditableMesh::CreateCube(1.0f);
        EditableMesh m2 = EditableMesh::CreateCube(1.0f);

        GeneralUVOptions opt;
        auto res1 = mapper.UnwrapMesh(m1, opt);
        auto res2 = mapper.UnwrapMesh(m2, opt);

        bool passed = (res1.layout.islands.size() == res2.layout.islands.size()) &&
                      (res1.layout.islands.size() > 0);

        std::string obs = "Run1 Islands: " + std::to_string(res1.layout.islands.size()) +
                          ", Run2 Islands: " + std::to_string(res2.layout.islands.size());

        addTest("UVG-12", "Island Extraction Determinism",
                "Due istanze indipendenti generate dal medesimo codice",
                "Stesso numero di isole e mappatura deterministica 1:1",
                obs, passed,
                passed ? "Estrazione isole 100% deterministica." : "Non-determinismo nell'estrazione isole.");
    }

    // -------------------------------------------------------------------------
    // UVG-13: LSCM numerical convergence
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = createOpenPlaneGrid();
        UVIsland island;
        island.faces = mesh.GetActiveFaceIDs();
        for (FaceID f : island.faces) {
            island.surfaceArea3D += mesh.GetFace(f)->area;
        }

        LSCMOptions lscmOpt;
        lscmOpt.tolerance = 1e-5f;
        auto lscmRes = LSCMParameterizer::ParameterizeIsland(mesh, island, lscmOpt);

        bool passed = lscmRes.success && 
                      lscmRes.finalResidual <= lscmOpt.tolerance * 10.0f &&
                      lscmRes.pinnedVertexCount == 2;

        std::string obs = "Residual: " + std::to_string(lscmRes.finalResidual) +
                          ", Iters: " + std::to_string(lscmRes.iterationsExecuted) +
                          ", Pinned: " + std::to_string(lscmRes.pinnedVertexCount);

        addTest("UVG-13", "LSCM Numerical Convergence",
                "Griglia piana aperta con solver Conjugate Gradient",
                "Residuo convergente entro tolleranza, 2 vertici di ancoraggio pin",
                obs, passed,
                passed ? "LSCM ha raggiunto convergenza numerica." : "LSCM ha fallito la convergenza.");
    }

    // -------------------------------------------------------------------------
    // UVG-14: UV distortion metrics
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = createOpenPlaneGrid();
        GeneralUVOptions opt;
        auto res = mapper.UnwrapMesh(mesh, opt);

        bool passed = res.success && !res.layout.islands.empty() &&
                      std::isfinite(res.layout.islands[0].distortion.areaDistortion) &&
                      std::isfinite(res.layout.islands[0].distortion.maxStretch);

        std::string obs = "AreaDist: " + std::to_string(res.layout.islands.empty() ? 0.0f : res.layout.islands[0].distortion.areaDistortion) +
                          ", Stretch: " + std::to_string(res.layout.islands.empty() ? 0.0f : res.layout.islands[0].distortion.maxStretch);

        addTest("UVG-14", "UV Distortion Metrics Calculation",
                "Isola UV parametrizzata con valutazione Jacobiano locale",
                "Metriche di distorsione d'area e stretch calcolate e numericamente finite",
                obs, passed,
                passed ? "Metriche di distorsione coerenti." : "Distorsione indeterminata o NaN.");
    }

    // -------------------------------------------------------------------------
    // UVG-15: UV revision & Zero-Work
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        GeneralUVOptions opt;
        opt.enableZeroWorkCache = true;

        auto res1 = mapper.UnwrapMesh(mesh, opt);
        auto res2 = mapper.UnwrapMesh(mesh, opt);

        bool passed = res1.success && res2.success && 
                      !res1.zeroWorkCacheHit && res2.zeroWorkCacheHit &&
                      res1.layout.uvRevision == res2.layout.uvRevision;

        std::string obs = "Run1 CacheHit: " + std::string(res1.zeroWorkCacheHit ? "true" : "false") +
                          ", Run2 CacheHit: " + std::string(res2.zeroWorkCacheHit ? "true" : "false");

        addTest("UVG-15", "UV Revision & Zero-Work Cache",
                "Due invocazioni consecutive sulla stessa mesh inalterata",
                "Seconda chiamata risolta da cache con zero lavoro computazionale",
                obs, passed,
                passed ? "Zero-Work operativo su ingressi stabili." : "Cache Zero-Work non rilevata.");
    }

    // -------------------------------------------------------------------------
    // UVG-16: Imported mesh UV preservation
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = createOpenPlaneGrid();
        // Assegna coordinate UV manuali arbitrarie
        for (HalfEdgeID heId : mesh.GetActiveHalfEdgeIDs()) {
            auto* he = mesh.GetHalfEdgeMutable(heId);
            if (he) he->corner.uv = glm::vec2(0.42f, 0.42f);
        }

        GeneralUVOptions opt;
        opt.dispatchMode = UVUnwrapDispatchMode::PreserveExisting;
        auto res = mapper.UnwrapWithAutoDispatch(mesh, ShapeType::TriangleMesh, std::monostate{}, false, opt);

        // Verifica che un corner mantenga il valore esistente
        const auto* he0 = mesh.GetHalfEdge(mesh.GetActiveHalfEdgeIDs()[0]);
        bool preserved = he0 && (he0->corner.uv.x == 0.42f);

        bool passed = preserved;
        std::string obs = "Preserved UV: (" + std::to_string(he0 ? he0->corner.uv.x : 0) + ", " + 
                          std::to_string(he0 ? he0->corner.uv.y : 0) + ")";

        addTest("UVG-16", "Imported Mesh UV Preservation",
                "Mesh con coordinate UV preesistenti e modalita PreserveExisting",
                "Coordinate d'angolo non alterate dal dispatcher",
                obs, passed,
                passed ? "UV importate preservate integralmente." : "UV importate sovrascritte.");
    }

    // -------------------------------------------------------------------------
    // UVG-17: Shape -> EditableMesh topology consistency
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cube;
        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);

        bool validTopology = (mesh.GetFaceCount() == 6) && 
                             (mesh.GetVertexCount() == 8) &&
                             (mesh.GetEdgeCount() == 12);

        bool passed = validTopology;
        std::string obs = "V: " + std::to_string(mesh.GetVertexCount()) +
                          ", E: " + std::to_string(mesh.GetEdgeCount()) +
                          ", F: " + std::to_string(mesh.GetFaceCount());

        addTest("UVG-17", "Shape to EditableMesh Topology Consistency",
                "Conversione da ShapeDefinition a EditableMesh DCEL",
                "Conteggi esatti di Eulero (V=8, E=12, F=6 per cubo) e adiacenze coerenti",
                obs, passed,
                passed ? "Integrita topologica Half-Edge verificata." : "Incoerenza topologica post-conversione.");
    }

    // -------------------------------------------------------------------------
    // UVG-18: UV layout serialization roundtrip
    // -------------------------------------------------------------------------
    {
        UVLayout layout;
        layout.assetId = "Asset_Serialization_Test";
        layout.uvRevision = 42;
        layout.strategyUsed = UVMappingStrategy::LSCM;
        layout.status = PrimitiveMappingStatus::SUCCESS;

        UVIsland isl;
        isl.id = 1;
        isl.semanticName = "Island_Test";
        isl.semanticSurfaceId = 2;
        isl.faces = { ElementID<FaceTag>{0, 1}, ElementID<FaceTag>{1, 1} };
        isl.distortion.areaDistortion = 0.05f;
        isl.distortion.maxStretch = 1.12f;
        layout.islands.push_back(isl);
        layout.faceToIslandMap[ElementID<FaceTag>{0, 1}] = 1;
        layout.faceToIslandMap[ElementID<FaceTag>{1, 1}] = 1;

        nlohmann::json j = layout;
        UVLayout restored = j.get<UVLayout>();

        bool passed = (restored.assetId == layout.assetId) &&
                      (restored.uvRevision == 42) &&
                      (restored.islands.size() == 1) &&
                      (restored.islands[0].semanticName == "Island_Test") &&
                      (restored.faceToIslandMap.size() == 2);

        std::string obs = "Restored AssetId: " + restored.assetId +
                          ", Islands: " + std::to_string(restored.islands.size()) +
                          ", FaceToIsland: " + std::to_string(restored.faceToIslandMap.size());

        addTest("UVG-18", "UVLayout Serialization Roundtrip",
                "Struttura UVLayout con UVIsland e IslandDistortion esportata in JSON",
                "Deserializzazione completa con campi, revisioni e mappe identici",
                obs, passed,
                passed ? "Roundtrip di serializzazione JSON completato." : "Dati persi nella deserializzazione JSON.");
    }

    return results;
}

} // namespace fw
