#include "pch.h"
#include "MeshBridgeContractTests.h"
#include "blocks/sculpt/MeshBridgeService.h"
#include "blocks/sculpt/EditableMesh.h"
#include "world/GeometryCompiler.h"
#include "blocks/uv/UVIslandExtractor.h"
#include "blocks/uv/SeamGraph.h"
#include "blocks/uv/PrimitiveUVContractTests.h"
#include "blocks/uv/GeneralUVContractTests.h"
#include "blocks/uv/AtlasUVContractTests.h"
#include "blocks/uv/UVEditorContractTests.h"
#include "blocks/uv/TextureReprojectionContractTests.h"
#include "blocks/uv/UVFactoryIntegrationTests.h"
#include "blocks/uv/CompiledTopologyTests.h"
#include <cmath>
#include <set>
#include <map>
#include <algorithm>

namespace fw {

std::vector<MeshBridgeContractTestResult> MeshBridgeContractTests::RunAllTests() {
    std::vector<MeshBridgeContractTestResult> results;
    results.reserve(16);

    auto addResult = [&](const std::string& id, const std::string& name,
                         const std::string& input, const std::string& expected,
                         const std::string& observed, bool passed, const std::string& details) {
        MeshBridgeContractTestResult r;
        r.testId = id;
        r.testName = name;
        r.inputDesc = input;
        r.expectedDesc = expected;
        r.observedDesc = observed;
        r.passed = passed;
        r.details = details;
        results.push_back(r);
    };

    // -------------------------------------------------------------------------
    // MBR-1: Cube Authoritative Topology (8V, 6F, 12E, zero boundary)
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cube;
        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        auto val = mesh.ValidateTopology();

        size_t V = mesh.GetVertexCount();
        size_t F = mesh.GetFaceCount();
        size_t E = mesh.GetEdgeCount();
        int chi = static_cast<int>(V) - static_cast<int>(E) + static_cast<int>(F);

        bool passed = (V == 8) && (F == 6) && (E == 12) && (chi == 2) &&
                      val.isValid && (val.boundaryEdgeCount == 0) && val.isClosedManifold;

        std::string obs = "V=" + std::to_string(V) + ", F=" + std::to_string(F) +
                          ", E=" + std::to_string(E) + ", Boundary=" + std::to_string(val.boundaryEdgeCount) +
                          ", Valid=" + (val.isValid ? "true" : "false");

        addResult("MBR-1", "Cube Authoritative Topology",
                  "ShapeDefinition(Cube) -> MeshBridgeService::CreateMeshFromShape",
                  "V=8, F=6, E=12, chi=2, zero boundary, manifold chiuso",
                  obs, passed,
                  passed ? "Topologia autoritativa del Cubo validata con successo senza bypass."
                         : "Incoerenza nei conteggi o boundary aperti nel cubo.");
    }

    // -------------------------------------------------------------------------
    // MBR-2: Cylinder Shared Rings & Seam Preservation
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cylinder;
        ShapeParamsCylinder p;
        p.radius = 1.0f;
        p.height = 2.0f;
        sDef.parameters = p;

        GeometryCompileOptions opt;
        opt.maxResolution = 16;
        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        auto val = mesh.ValidateTopology();

        size_t V = mesh.GetVertexCount();
        size_t F = mesh.GetFaceCount();
        size_t E = mesh.GetEdgeCount();
        int chi = static_cast<int>(V) - static_cast<int>(E) + static_cast<int>(F);

        // N=16 => V=34 (2N+2), F=48 (3N), E=80 (5N), chi=2
        bool countsOk = (V == 34) && (F == 48) && (E == 80) && (chi == 2) &&
                        val.isValid && (val.boundaryEdgeCount == 0);

        // Verifica che le facce di confine alla cucitura (seam) condividano gli stessi vertici topologici
        // mantenendo half-edge gemelli (twin reciproco) con attributi UV distinti
        bool seamConnected = false;
        auto faceIds = mesh.GetActiveFaceIDs();
        for (FaceID fId : faceIds) {
            std::vector<HalfEdgeID> hes = mesh.GetFaceHalfEdges(fId);
            for (HalfEdgeID heId : hes) {
                const auto* he = mesh.GetHalfEdge(heId);
                if (he && he->twin.IsValid()) {
                    const auto* twinHe = mesh.GetHalfEdge(he->twin);
                    if (twinHe && twinHe->twin == heId) {
                        // Verifica salto UV attraverso seam (u ~ 0.0 vs u ~ 1.0)
                        if (std::abs(he->corner.uv.x - twinHe->corner.uv.x) > 0.8f) {
                            seamConnected = true;
                        }
                    }
                }
            }
        }

        bool passed = countsOk && seamConnected;
        std::string obs = "V=" + std::to_string(V) + ", F=" + std::to_string(F) +
                          ", E=" + std::to_string(E) + ", Boundary=" + std::to_string(val.boundaryEdgeCount) +
                          ", SeamConnected=" + std::string(seamConnected ? "true" : "false");

        addResult("MBR-2", "Cylinder Shared Rings & Seam Preservation",
                  "ShapeDefinition(Cylinder, N=16) -> MeshBridgeService::CreateMeshFromShape",
                  "V=34, F=48, E=80, chi=2, anelli condivisi e connettivita periodica alla cucitura",
                  obs, passed,
                  passed ? "Cilindro autoritativo integrato con successo senza duplicazione topologica al seam."
                         : "Errore nei conteggi o rottura della continuita topologica del cilindro.");
    }

    // -------------------------------------------------------------------------
    // MBR-3: Cone Unique Topological Apex
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cone;
        ShapeParamsCone p;
        p.radius = 1.0f;
        p.height = 2.0f;
        sDef.parameters = p;

        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        auto val = mesh.ValidateTopology();

        size_t V = mesh.GetVertexCount();
        size_t F = mesh.GetFaceCount();
        size_t E = mesh.GetEdgeCount();
        int chi = static_cast<int>(V) - static_cast<int>(E) + static_cast<int>(F);

        // N=16 => V=18 (N+2), F=32 (2N), E=48 (3N), chi=2
        bool countsOk = (V == 18) && (F == 32) && (E == 48) && (chi == 2) &&
                        val.isValid && (val.boundaryEdgeCount == 0);

        // Ricerca del vertice apicale (y = 1.0) e verifica che sia unico
        VertexID apexId = INVALID_VERTEX_ID;
        int apexMatches = 0;
        for (VertexID vId : mesh.GetActiveVertexIDs()) {
            const auto* v = mesh.GetVertex(vId);
            if (v && std::abs(v->position.y - 1.0f) < 1e-4f &&
                std::abs(v->position.x) < 1e-4f && std::abs(v->position.z) < 1e-4f) {
                apexId = vId;
                apexMatches++;
            }
        }

        std::vector<FaceID> incFaces = mesh.GetVertexIncidentFaces(apexId);
        bool apexUnique = (apexMatches == 1) && (incFaces.size() == 16);

        bool passed = countsOk && apexUnique;
        std::string obs = "V=" + std::to_string(V) + ", F=" + std::to_string(F) +
                          ", E=" + std::to_string(E) + ", ApexMatches=" + std::to_string(apexMatches) +
                          ", ApexIncFaces=" + std::to_string(incFaces.size());

        addResult("MBR-3", "Cone Unique Topological Apex",
                  "ShapeDefinition(Cone, N=16) -> MeshBridgeService::CreateMeshFromShape",
                  "V=18, F=32, E=48, chi=2, singolo VertexID apicale condiviso da tutti i triangoli laterali",
                  obs, passed,
                  passed ? "Apice del cono rappresentato da una singola identita topologica autoritativa."
                         : "Duplicazione topologica dell'apice o conteggio facce incidenti errato.");
    }

    // -------------------------------------------------------------------------
    // MBR-4: ConeFrustum Connected Rings and Caps
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::ConeFrustum;
        ShapeParamsConeFrustum p;
        p.bottomRadius = 1.5f;
        p.topRadius = 0.5f;
        p.height = 2.0f;
        sDef.parameters = p;

        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        auto val = mesh.ValidateTopology();

        size_t V = mesh.GetVertexCount();
        size_t F = mesh.GetFaceCount();
        size_t E = mesh.GetEdgeCount();
        int chi = static_cast<int>(V) - static_cast<int>(E) + static_cast<int>(F);

        // N=16 => V=34 (2N+2), F=48 (3N), E=80 (5N), chi=2
        bool countsOk = (V == 34) && (F == 48) && (E == 80) && (chi == 2) &&
                        val.isValid && (val.boundaryEdgeCount == 0);

        bool passed = countsOk;
        std::string obs = "V=" + std::to_string(V) + ", F=" + std::to_string(F) +
                          ", E=" + std::to_string(E) + ", Boundary=" + std::to_string(val.boundaryEdgeCount);

        addResult("MBR-4", "ConeFrustum Connected Rings and Caps",
                  "ShapeDefinition(ConeFrustum, N=16) -> MeshBridgeService::CreateMeshFromShape",
                  "V=34, F=48, E=80, chi=2, basi superiore e inferiore connesse al mantello senza boundary",
                  obs, passed,
                  passed ? "Tronco di cono 2-manifold chiuso validato con connettivita continua anelli/basi."
                         : "Disconnessione tra anelli e basi del tronco di cono.");
    }

    // -------------------------------------------------------------------------
    // MBR-5: Sphere Unique Poles & Longitudinal Seam
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Sphere;
        ShapeParamsSphere p;
        p.radius = 1.0f;
        sDef.parameters = p;

        GeometryCompileOptions opt;
        opt.maxResolution = 16;
        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        auto val = mesh.ValidateTopology();

        size_t V = mesh.GetVertexCount();
        size_t F = mesh.GetFaceCount();
        size_t E = mesh.GetEdgeCount();
        int chi = static_cast<int>(V) - static_cast<int>(E) + static_cast<int>(F);

        // M=16, N=32 => V = 2 + 32*15 = 482, F = 64 + 32*14 = 512, E = 482+512-2 = 992
        bool countsOk = (V == 482) && (F == 512) && (E == 992) && (chi == 2) &&
                        val.isValid && (val.boundaryEdgeCount == 0);

        // Verifica unicità poli
        VertexID southPoleId = INVALID_VERTEX_ID;
        VertexID northPoleId = INVALID_VERTEX_ID;
        int southCount = 0;
        int northCount = 0;

        for (VertexID vId : mesh.GetActiveVertexIDs()) {
            const auto* v = mesh.GetVertex(vId);
            if (!v) continue;
            if (std::abs(v->position.y - (-1.0f)) < 1e-4f && std::abs(v->position.x) < 1e-4f && std::abs(v->position.z) < 1e-4f) {
                southPoleId = vId;
                southCount++;
            }
            if (std::abs(v->position.y - 1.0f) < 1e-4f && std::abs(v->position.x) < 1e-4f && std::abs(v->position.z) < 1e-4f) {
                northPoleId = vId;
                northCount++;
            }
        }

        bool polesUnique = (southCount == 1) && (northCount == 1) &&
                           (mesh.GetVertexIncidentFaces(southPoleId).size() == 32) &&
                           (mesh.GetVertexIncidentFaces(northPoleId).size() == 32);

        bool passed = countsOk && polesUnique;
        std::string obs = "V=" + std::to_string(V) + ", F=" + std::to_string(F) +
                          ", E=" + std::to_string(E) + ", SouthIncFaces=" + std::to_string(mesh.GetVertexIncidentFaces(southPoleId).size()) +
                          ", NorthIncFaces=" + std::to_string(mesh.GetVertexIncidentFaces(northPoleId).size());

        addResult("MBR-5", "Sphere Unique Poles & Longitudinal Seam",
                  "ShapeDefinition(Sphere, M=16, N=32) -> MeshBridgeService::CreateMeshFromShape",
                  "V=482, F=512, E=992, chi=2, poli unici con 32 facce incidenti e seam chiuso",
                  obs, passed,
                  passed ? "Sfera autoritativa verificata con poli unici e chiusura periodica longitudinale."
                         : "Poli duplicati o fessure longitudinali nella sfera.");
    }

    // -------------------------------------------------------------------------
    // MBR-6: Independent SurfaceID and MaterialID Preservation
    // -------------------------------------------------------------------------
    {
        CompiledTopology topo;
        topo.geometricPositions = {
            glm::vec3(-1.0f, 0.0f, 0.0f),
            glm::vec3( 0.0f, 1.0f, 0.0f),
            glm::vec3( 0.0f,-1.0f, 0.0f),
            glm::vec3( 1.0f, 0.0f, 0.0f)
        };

        CompiledFace f0;
        f0.surfaceId = 42;
        f0.materialId = 100;
        f0.faceNormal = glm::vec3(0.0f, 0.0f, 1.0f);
        f0.corners = {
            { 0, glm::vec2(0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec4(1.0f) },
            { 1, glm::vec2(0.0f, 1.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec4(1.0f) },
            { 2, glm::vec2(1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec4(1.0f) }
        };

        CompiledFace f1;
        f1.surfaceId = 7;
        f1.materialId = 200;
        f1.faceNormal = glm::vec3(0.0f, 0.0f, 1.0f);
        f1.corners = {
            { 1, glm::vec2(0.0f, 1.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec4(1.0f) },
            { 3, glm::vec2(1.0f, 1.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec4(1.0f) },
            { 2, glm::vec2(1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec4(1.0f) }
        };

        topo.faces = { f0, f1 };

        EditableMesh mesh = MeshBridgeService::CreateMeshFromCompiledTopology(topo);
        auto fIds = mesh.GetActiveFaceIDs();

        bool passed = false;
        if (fIds.size() == 2) {
            const auto* mf0 = mesh.GetFace(fIds[0]);
            const auto* mf1 = mesh.GetFace(fIds[1]);
            if (mf0 && mf1) {
                bool surfOk = (mf0->surfaceId == 42 && mf1->surfaceId == 7);
                bool matOk = (mf0->materialId == 100 && mf1->materialId == 200);
                passed = surfOk && matOk;
            }
        }

        std::string obs = passed ? "Face0: Surf=42, Mat=100; Face1: Surf=7, Mat=200" : "Disallineamento ID";
        addResult("MBR-6", "Independent SurfaceID and MaterialID Preservation",
                  "CompiledTopology con SurfaceID={42, 7} e MaterialID={100, 200}",
                  "Assegnazione indipendente e non sovrapposta di SurfaceID e MaterialID",
                  obs, passed,
                  passed ? "SurfaceID e MaterialID gestiti come campi ortogonali e indipendenti."
                         : "Accoppiamento spurio o perdita di SurfaceID/MaterialID.");
    }

    // -------------------------------------------------------------------------
    // MBR-7: Per-Corner UV, Normal & Color Attributes Preservation
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cube;
        GeometryCompileOptions opt;
        auto res = GeometryCompiler::Compile(sDef, opt);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
            const auto& topo = *res.topology;
            auto fIds = mesh.GetActiveFaceIDs();

            if (fIds.size() == topo.faces.size()) {
                bool allCornersMatch = true;
                for (size_t f = 0; f < topo.faces.size(); ++f) {
                    const auto& tf = topo.faces[f];
                    std::vector<HalfEdgeID> hes = mesh.GetFaceHalfEdges(fIds[f]);
                    if (hes.size() != tf.corners.size()) {
                        allCornersMatch = false;
                        break;
                    }

                    for (size_t c = 0; c < tf.corners.size(); ++c) {
                        const auto* he = mesh.GetHalfEdge(hes[c]);
                        if (!he) { allCornersMatch = false; break; }

                        if (glm::length(he->corner.uv - tf.corners[c].uv) > 1e-5f) {
                            allCornersMatch = false;
                            break;
                        }
                        if (glm::length(he->corner.normal - tf.corners[c].normal) > 1e-5f) {
                            allCornersMatch = false;
                            break;
                        }
                        if (glm::length(he->corner.color - tf.corners[c].color) > 1e-5f) {
                            allCornersMatch = false;
                            break;
                        }
                    }
                    if (!allCornersMatch) break;
                }
                passed = allCornersMatch;
                obs = passed ? "Tutti i corner (UV, normale, colore) coincidono al 100%" : "Discrepanza attributi corner";
            }
        }

        addResult("MBR-7", "Per-Corner Attributes Preservation",
                  "Cube compilato -> verifica di CornerAttributes::(uv, normal, color) su ogni half-edge",
                  "Preservazione 1:1 delle normali per-corner senza sovrascrittura da normali geometriche",
                  obs, passed,
                  passed ? "Attributi d'angolo compilati preservati senza degradazione o sovrascrittura."
                         : "Attributi d'angolo alterati o sovrascritti durante l'integrazione.");
    }

    // -------------------------------------------------------------------------
    // MBR-8: Invalid Topology Rejection (Zero Partial Mesh Published)
    // -------------------------------------------------------------------------
    {
        // 1. Topologia con indice vertice fuori scala
        CompiledTopology badTopo1;
        badTopo1.geometricPositions = { glm::vec3(0), glm::vec3(1,0,0), glm::vec3(0,1,0) };
        CompiledFace bf1;
        bf1.corners = {
            { 0, glm::vec2(0), glm::vec3(0,1,0), glm::vec4(1) },
            { 1, glm::vec2(0), glm::vec3(0,1,0), glm::vec4(1) },
            { 999, glm::vec2(0), glm::vec3(0,1,0), glm::vec4(1) } // Indice 999 invalido
        };
        badTopo1.faces = { bf1 };
        std::string err1;
        EditableMesh mesh1 = MeshBridgeService::CreateMeshFromCompiledTopology(badTopo1, &err1);
        bool reject1 = mesh1.IsEmpty() && (mesh1.GetVertexCount() == 0) && (mesh1.GetFaceCount() == 0) && !err1.empty();

        // 2. Topologia con spigolo non-manifold duplicato nello stesso verso
        CompiledTopology badTopo2;
        badTopo2.geometricPositions = { glm::vec3(0), glm::vec3(1,0,0), glm::vec3(0,1,0), glm::vec3(0,0,1) };
        CompiledFace bf2a; // 0 -> 1 -> 2
        bf2a.corners = {
            { 0, glm::vec2(0), glm::vec3(0,0,1), glm::vec4(1) },
            { 1, glm::vec2(0), glm::vec3(0,0,1), glm::vec4(1) },
            { 2, glm::vec2(0), glm::vec3(0,0,1), glm::vec4(1) }
        };
        CompiledFace bf2b; // 0 -> 1 -> 3 (stesso verso 0->1)
        bf2b.corners = {
            { 0, glm::vec2(0), glm::vec3(0,0,1), glm::vec4(1) },
            { 1, glm::vec2(0), glm::vec3(0,0,1), glm::vec4(1) },
            { 3, glm::vec2(0), glm::vec3(0,0,1), glm::vec4(1) }
        };
        badTopo2.faces = { bf2a, bf2b };
        std::string err2;
        EditableMesh mesh2 = MeshBridgeService::CreateMeshFromCompiledTopology(badTopo2, &err2);
        bool reject2 = mesh2.IsEmpty() && (mesh2.GetVertexCount() == 0) && (mesh2.GetFaceCount() == 0) && !err2.empty();

        // 3. TriangleMesh con parametri vuoti/corrotti
        ShapeDefinition badTriDef;
        badTriDef.type = ShapeType::TriangleMesh;
        badTriDef.parameters = ShapeParamsTriangleMesh{};
        std::string err3;
        EditableMesh mesh3 = MeshBridgeService::CreateMeshFromShape(badTriDef, &err3);
        bool reject3 = mesh3.IsEmpty() && (mesh3.GetFaceCount() == 0) && !err3.empty();

        bool passed = reject1 && reject2 && reject3;
        std::string obs = "OutOfBoundsReject=" + std::string(reject1 ? "true" : "false") +
                          ", NonManifoldReject=" + std::string(reject2 ? "true" : "false") +
                          ", CorruptTriangleReject=" + std::string(reject3 ? "true" : "false");

        addResult("MBR-8", "Invalid Topology Rejection",
                  "Topologie corrotte: indici fuori scala, edge non-manifold, parametri assenti",
                  "Rigetto esplicito: nessuna mesh parziale restituita, nessun fallback silenzioso a cubo",
                  obs, passed,
                  passed ? "Protezione attiva: geometrie corrotte respinte atomicamente senza leak di mesh parziali."
                         : "Fuga di mesh parziale o fallback non autorizzato.");
    }

    // -------------------------------------------------------------------------
    // MBR-9: Unmigrated Primitives Legacy Fallback with Diagnostics
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Pyramid;
        ShapeParamsPyramid p;
        p.baseWidth = 1.0f;
        p.baseDepth = 1.0f;
        p.height = 2.0f;
        sDef.parameters = p;

        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);
        bool notEmpty = !mesh.IsEmpty() && (mesh.GetFaceCount() > 0) && (mesh.GetVertexCount() > 0);

        // Verifica che sia effettivamente una piramide di altezza 2.0 e non un cubo fittizio
        auto aabb = mesh.ComputeAABB();
        float height = aabb.max.y - aabb.min.y;
        bool isActualPyramid = (std::abs(height - 2.0f) < 1e-4f);

        bool passed = notEmpty && isActualPyramid;
        std::string obs = "FaceCount=" + std::to_string(mesh.GetFaceCount()) +
                          ", VertexCount=" + std::to_string(mesh.GetVertexCount()) +
                          ", Height=" + std::to_string(height);

        addResult("MBR-9", "Unmigrated Primitives Legacy Fallback",
                  "ShapeDefinition(Pyramid, height=2.0) primitiva non ancora migrata a CompiledTopology",
                  "Fallback legacy diagnosticato operativo senza fallback automatico a CreateCube(1.0)",
                  obs, passed,
                  passed ? "Fallback legacy operativo e geometricamente corretto per primitive non migrate."
                         : "Fallback legacy fallito o sostituito da cubo hardcoded.");
    }

    // -------------------------------------------------------------------------
    // MBR-10: Pre-existing Test Suite Regression Shield (135/135 PASS)
    // -------------------------------------------------------------------------
    {
        int preDiscovered = 0;
        int prePassed = 0;
        int preFailed = 0;

        auto accumulate = [&](auto testResults) {
            for (const auto& tr : testResults) {
                preDiscovered++;
                if (tr.passed) prePassed++;
                else preFailed++;
            }
        };

        accumulate(PrimitiveUVContractTests::RunAllTests());
        accumulate(GeneralUVContractTests::RunAllTests());
        accumulate(AtlasUVContractTests::RunAllTests());
        accumulate(UVEditorContractTests::RunAllTests());
        accumulate(TextureReprojectionContractTests::RunAllTests());
        accumulate(UVFactoryIntegrationTests::RunAllTests());
        accumulate(CompiledTopologyTests::RunAllTests());

        bool passed = (preDiscovered == 135) && (prePassed == 135) && (preFailed == 0);
        std::string obs = "Discovered: " + std::to_string(preDiscovered) +
                          ", Passed: " + std::to_string(prePassed) +
                          ", Failed: " + std::to_string(preFailed);

        addResult("MBR-10", "Pre-existing 135 Tests Regression Shield",
                  "Esecuzione consolidata suites UVP(15), UVG(18), UVK(18), UVE(18), UVR(20), UVI(10), TOP(36)",
                  "135/135 test preesistenti PASS, 0 FAIL, 0 regressioni",
                  obs, passed,
                  passed ? "Scudo di regressione perfetto: tutti i 135 test preesistenti superati."
                         : "Rilevata regressione su uno o piu test preesistenti.");
    }

    // -------------------------------------------------------------------------
    // MBR-11: Programmatic Error Diagnostics (Task A)
    // -------------------------------------------------------------------------
    {
        // 1. Successo: outError azzerato e vuoto
        ShapeDefinition sDefCube;
        sDefCube.type = ShapeType::Cube;
        std::string errSuccess = "pre-existing";
        EditableMesh m1 = MeshBridgeService::CreateMeshFromShape(sDefCube, &errSuccess);
        bool ok1 = !m1.IsEmpty() && errSuccess.empty();

        // 2. Errore di compilazione: Compound senza registro forme valido
        ShapeDefinition sDefComp;
        sDefComp.type = ShapeType::Compound;
        std::string errComp;
        EditableMesh m2 = MeshBridgeService::CreateMeshFromShape(sDefComp, &errComp);
        bool ok2 = m2.IsEmpty() && (errComp.find("GeometryCompiler error") != std::string::npos);

        // 3. Topologia invalida (indice vertice fuori scala)
        CompiledTopology badTopo;
        badTopo.geometricPositions = { glm::vec3(0), glm::vec3(1,0,0), glm::vec3(0,1,0) };
        CompiledFace bf;
        bf.corners = { {0,{},{},{}}, {1,{},{},{}}, {999,{},{},{}} };
        badTopo.faces = { bf };
        std::string errTopo;
        EditableMesh m3 = MeshBridgeService::CreateMeshFromCompiledTopology(badTopo, &errTopo);
        bool ok3 = m3.IsEmpty() && (errTopo.find("out-of-bounds") != std::string::npos);

        // 4. Parametri non validi (TriangleMesh con array vuoti)
        ShapeDefinition sDefTri;
        sDefTri.type = ShapeType::TriangleMesh;
        sDefTri.parameters = ShapeParamsTriangleMesh{};
        std::string errTri;
        EditableMesh m4 = MeshBridgeService::CreateMeshFromShape(sDefTri, &errTri);
        bool ok4 = m4.IsEmpty() && (errTri.find("TriangleMesh parameters empty or invalid") != std::string::npos);

        bool passed = ok1 && ok2 && ok3 && ok4;
        std::string obs = "SuccessEmpty=" + std::string(ok1 ? "true" : "false") +
                          ", CompErr=" + std::string(ok2 ? "true" : "false") +
                          ", TopoErr=" + std::string(ok3 ? "true" : "false") +
                          ", ParamErr=" + std::string(ok4 ? "true" : "false");

        addResult("MBR-11", "Programmatic Error Diagnostics",
                  "CreateMeshFromShape/CreateMeshFromCompiledTopology con parametro std::string* outError",
                  "outError vuoto su successo; messaggi specifici e non vuoti per compilazione, topologia e parametri invalidi",
                  obs, passed,
                  passed ? "Diagnostica programmatica completa, non invasiva e non-breaking verificata."
                         : "Mancata o errata propagazione della diagnostica programmatica.");
    }

    // -------------------------------------------------------------------------
    // MBR-12: Direct UV Island Extraction — Cube Policy Verification (Task B.1)
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cube;
        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);

        // Scenario 1: Default SeamOptions (dihedralAngleThreshold = 45 deg, seamOnSurfaceBoundary = true)
        // Tutti i 12 spigoli del cubo (90 deg) sono feature seam -> esattamente 6 isole quad planari
        SeamOptions defOpt;
        SeamGraph seamGraphDef;
        seamGraphDef.BuildFromMesh(mesh, defOpt);

        std::unordered_map<FaceID, UVIslandID> mapDef;
        IslandExtractionReport repDef;
        auto islandsDef = UVIslandExtractor::ExtractIslands(mesh, seamGraphDef, mapDef, repDef);
        bool okDef = (islandsDef.size() == 6) && (repDef.closedIslandsDetected == 0);

        // Scenario 2: Zero-seams policy (soglia 180 deg, no surface seam, no auto cut)
        // Zero seam e zero boundary -> esattamente 1 isola topologicamente chiusa rilevata
        SeamOptions zeroOpt;
        zeroOpt.seamOnSharpEdges = false;
        zeroOpt.seamOnSurfaceBoundary = false;
        zeroOpt.dihedralAngleThresholdDegrees = 180.0f;
        zeroOpt.autoCutClosedComponents = false;
        SeamGraph seamGraphZero;
        seamGraphZero.BuildFromMesh(mesh, zeroOpt);

        std::unordered_map<FaceID, UVIslandID> mapZero;
        IslandExtractionReport repZero;
        auto islandsZero = UVIslandExtractor::ExtractIslands(mesh, seamGraphZero, mapZero, repZero);
        bool okZero = (islandsZero.size() == 1) && (repZero.closedIslandsDetected == 1);

        bool passed = okDef && okZero;
        std::string obs = "DefaultIslands=" + std::to_string(islandsDef.size()) +
                          ", ZeroSeamIslands=" + std::to_string(islandsZero.size()) +
                          ", ClosedDetected=" + std::to_string(repZero.closedIslandsDetected);

        addResult("MBR-12", "Direct UV Island Extraction — Cube Policy",
                  "Cube -> SeamGraph -> UVIslandExtractor: default policy vs zero-seam policy",
                  "Default: esattamente 6 isole planari; Zero-seam: esattamente 1 isola chiusa rilevata",
                  obs, passed,
                  passed ? "Estrazione isole UV del cubo coerente al 100% con la configurazione del SeamGraph."
                         : "Disallineamento nel numero di isole o nella propagazione twin del cubo.");
    }

    // -------------------------------------------------------------------------
    // MBR-13: Direct UV Island Extraction — Cube with Explicit Manual Seams (Task B.2)
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cube;
        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);

        // Disabilita seam automatiche per testare esclusivamente seam manuali
        SeamOptions manOpt;
        manOpt.seamOnSharpEdges = false;
        manOpt.seamOnSurfaceBoundary = false;
        manOpt.dihedralAngleThresholdDegrees = 180.0f;
        manOpt.autoCutClosedComponents = false;
        manOpt.preserveManualSeams = true;

        // Trova la faccia Top (+Y, surfaceId 5) e marca i suoi 4 half-edge come seam manuali
        auto fIds = mesh.GetActiveFaceIDs();
        FaceID topFaceId = INVALID_FACE_ID;
        for (FaceID fId : fIds) {
            const auto* f = mesh.GetFace(fId);
            if (f && f->surfaceId == 5) {
                topFaceId = fId;
                break;
            }
        }

        bool marked = false;
        if (topFaceId.IsValid()) {
            std::vector<HalfEdgeID> hes = mesh.GetFaceHalfEdges(topFaceId);
            for (HalfEdgeID heId : hes) {
                const auto* he = mesh.GetHalfEdge(heId);
                if (he) {
                    SeamGraph::SetManualSeam(mesh, he->edge, true);
                }
            }
            marked = (hes.size() == 4);
        }

        SeamGraph seamGraph;
        seamGraph.BuildFromMesh(mesh, manOpt);

        std::unordered_map<FaceID, UVIslandID> faceToIslandMap;
        IslandExtractionReport rep;
        auto islands = UVIslandExtractor::ExtractIslands(mesh, seamGraph, faceToIslandMap, rep);

        // Configurazione deterministica:
        // - Faccia 5 isolata dai 4 tagli perimetrali -> 1 isola (1 faccia)
        // - Le altre 5 facce rimangono connesse a conca aperta -> 1 isola (5 facce)
        // Totale = 2 isole distinte
        bool okCount = (islands.size() == 2);
        bool okFaces = false;
        if (okCount) {
            size_t c0 = islands[0].faces.size();
            size_t c1 = islands[1].faces.size();
            okFaces = (std::min(c0, c1) == 1 && std::max(c0, c1) == 5);
        }

        bool passed = marked && okCount && okFaces && (rep.closedIslandsDetected == 0);
        std::string obs = "Islands=" + std::to_string(islands.size()) +
                          ", FaceCounts=" + (okCount ? (std::to_string(islands[0].faces.size()) + "+" + std::to_string(islands[1].faces.size())) : "err");

        addResult("MBR-13", "Direct UV Island Extraction — Cube Explicit Seams",
                  "Cubo con 4 seam manuali perimetrali attorno alla faccia Top (+Y)",
                  "Esattamente 2 isole deterministiche: coperchio (1 faccia) e conca aperta (5 facce)",
                  obs, passed,
                  passed ? "Seam manuali rispettate e partizione deterministica delle isole verificata."
                         : "Fallimento nel partizionamento manuale delle isole UV.");
    }

    // -------------------------------------------------------------------------
    // MBR-14: Direct UV Island Extraction — Cylinder 3-Islands (Task B.3)
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cylinder;
        ShapeParamsCylinder p;
        p.radius = 1.0f;
        p.height = 2.0f;
        sDef.parameters = p;

        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);

        // Seleziona uno spigolo verticale del mantello (collegamento anello superiore-inferiore)
        EdgeID verticalSeamEdge = INVALID_EDGE_ID;
        for (EdgeID eId : mesh.GetActiveEdgeIDs()) {
            const auto* e = mesh.GetEdge(eId);
            if (!e || !e->halfEdge.IsValid() || !e->twin.IsValid()) continue;
            const auto* h0 = mesh.GetHalfEdge(e->halfEdge);
            const auto* h1 = mesh.GetHalfEdge(e->twin);
            if (!h0 || !h1) continue;
            const auto* v0 = mesh.GetVertex(h0->origin);
            const auto* v1 = mesh.GetVertex(h1->origin);
            if (!v0 || !v1) continue;
            // Se collega il vertice top (+1.0) al vertice bottom (-1.0)
            if (std::abs(std::abs(v0->position.y - v1->position.y) - 2.0f) < 1e-4f) {
                verticalSeamEdge = eId;
                break;
            }
        }

        bool seamSet = false;
        if (verticalSeamEdge.IsValid()) {
            SeamGraph::SetManualSeam(mesh, verticalSeamEdge, true);
            seamSet = true;
        }

        SeamOptions cylOpt;
        cylOpt.seamOnSurfaceBoundary = true; // Separa basi (surfaceId 1, 2) da mantello (surfaceId 0)
        cylOpt.preserveManualSeams = true;   // Include il taglio longitudinale

        SeamGraph seamGraph;
        seamGraph.BuildFromMesh(mesh, cylOpt);

        std::unordered_map<FaceID, UVIslandID> faceToIslandMap;
        IslandExtractionReport rep;
        auto islands = UVIslandExtractor::ExtractIslands(mesh, seamGraph, faceToIslandMap, rep);

        // Tre isole attese: Mantello (srotolato in striscia planare) + Top Cap + Bottom Cap
        bool passed = seamSet && (islands.size() == 3) && (rep.closedIslandsDetected == 0);
        std::string obs = "Islands=" + std::to_string(islands.size()) +
                          ", ClosedDetected=" + std::to_string(rep.closedIslandsDetected) +
                          ", SeamFound=" + std::string(seamSet ? "true" : "false");

        addResult("MBR-14", "Direct UV Island Extraction — Cylinder 3-Islands",
                  "Cilindro con taglio longitudinale del mantello e separazione delle 2 basi",
                  "Esattamente 3 isole planari (mantello srotolato + 2 basi circolari)",
                  obs, passed,
                  passed ? "Cilindro srotolato esattamente in 3 isole conformi senza residui di chiusura."
                         : "Numero di isole cilindriche non conforme o mantello non sezionato.");
    }

    // -------------------------------------------------------------------------
    // MBR-15: Direct UV Island Extraction — Sphere Pole-to-Pole Seam (Task B.4)
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Sphere;
        ShapeParamsSphere p;
        p.radius = 1.0f;
        sDef.parameters = p;

        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);

        // Verifica che la mesh geometrica non abbia confini geometrici aperti
        auto valMesh = mesh.ValidateTopology();
        bool zeroBndMesh = (valMesh.boundaryEdgeCount == 0);

        SeamOptions sphOpt;
        sphOpt.seamOnSharpEdges = false;
        sphOpt.seamOnSurfaceBoundary = false;
        sphOpt.dihedralAngleThresholdDegrees = 180.0f;
        sphOpt.autoCutClosedComponents = true; // Attiva taglio longitudinale automatico da polo a polo

        SeamGraph seamGraph;
        seamGraph.BuildFromMesh(mesh, sphOpt);

        std::unordered_map<FaceID, UVIslandID> faceToIslandMap;
        IslandExtractionReport rep;
        auto islands = UVIslandExtractor::ExtractIslands(mesh, seamGraph, faceToIslandMap, rep);

        // Con il taglio longitudinale polare, la sfera chiusa viene aperta in un unico disco topologico (1 isola)
        bool passed = zeroBndMesh && (islands.size() == 1) && (rep.closedIslandsDetected == 0);
        std::string obs = "Islands=" + std::to_string(islands.size()) +
                          ", ClosedDetected=" + std::to_string(rep.closedIslandsDetected) +
                          ", MeshGeometricBoundary=" + std::to_string(valMesh.boundaryEdgeCount);

        addResult("MBR-15", "Direct UV Island Extraction — Sphere Pole-to-Pole Seam",
                  "Sfera 2-manifold chiusa con taglio automatico longitudinale minimo polo-polo",
                  "Esattamente 1 isola planare aperta (disco topologico) con boundary geometrico nullo",
                  obs, passed,
                  passed ? "Sfera aperta in un'unica isola topologicamente conforme senza bordi geometrici aperti."
                         : "Sfera non aperta correttamente o isole multiple spurie generate.");
    }

    // -------------------------------------------------------------------------
    // MBR-16: Shared Geometric Edges Across UV Cuts (Task B.5)
    // -------------------------------------------------------------------------
    {
        // Verifica che uno spigolo marcato come seam UV rimanga geometricamente condiviso e manifold nella DCEL
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cube;
        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(sDef);

        auto eIds = mesh.GetActiveEdgeIDs();
        EdgeID testEdge = eIds.empty() ? INVALID_EDGE_ID : eIds[0];
        SeamGraph::SetManualSeam(mesh, testEdge, true);

        SeamOptions opt;
        opt.preserveManualSeams = true;
        SeamGraph seamGraph;
        seamGraph.BuildFromMesh(mesh, opt);

        bool isSeam = seamGraph.IsSeam(testEdge);

        // Verifica invarianti geometriche della DCEL sull'edge marcato come seam
        const auto* e = mesh.GetEdge(testEdge);
        bool edgeValid = e && e->halfEdge.IsValid() && e->twin.IsValid();

        const auto* h0 = edgeValid ? mesh.GetHalfEdge(e->halfEdge) : nullptr;
        const auto* h1 = edgeValid ? mesh.GetHalfEdge(e->twin) : nullptr;

        bool twinsReciprocal = h0 && h1 && (h0->twin == e->twin) && (h1->twin == e->halfEdge);
        bool verticesShared = h0 && h1 && (h0->origin.IsValid()) && (h1->origin.IsValid());

        // La validazione topologica complessiva della mesh deve rimanere manifold chiusa con zero boundary aperti
        auto val = mesh.ValidateTopology();
        bool closedManifold = val.isValid && (val.boundaryEdgeCount == 0);

        bool passed = isSeam && edgeValid && twinsReciprocal && verticesShared && closedManifold;
        std::string obs = "IsSeam=" + std::string(isSeam ? "true" : "false") +
                          ", TwinsReciprocal=" + std::string(twinsReciprocal ? "true" : "false") +
                          ", MeshBoundaryEdges=" + std::to_string(val.boundaryEdgeCount);

        addResult("MBR-16", "Shared Geometric Edges Across UV Cuts",
                  "Spigolo di EditableMesh marcato come cucitura UV in SeamGraph",
                  "Lo spigolo e seam UV ma preserva half-edge gemelli reciproci e zero confini aperti nella DCEL",
                  obs, passed,
                  passed ? "Invariante DCEL verificata: le cuciture UV non lacerano la continuita geometrica."
                         : "Lacerazione topologica o perdita di gemellaggio sullo spigolo seam.");
    }

    return results;
}

} // namespace fw
