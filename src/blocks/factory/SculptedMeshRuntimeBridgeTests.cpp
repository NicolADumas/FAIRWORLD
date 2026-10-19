#include "pch.h"
#include "SculptedMeshRuntimeBridgeTests.h"
#include "FactoryOrchestrator.h"
#include "AssetPackagingService.h"
#include "TextureBakingService.h"
#include "blocks/BlockRegistry.h"
#include "blocks/MaterialRegistry.h"
#include "blocks/ShapeRegistry.h"
#include "blocks/sculpt/MeshBridgeService.h"
#include "blocks/sculpt/EditableMesh.h"
#include "world/GeometryCompiler.h"

// Suites pregresse per SBR-10
#include "blocks/uv/CompiledTopologyTests.h"
#include "PainterStationContractTests.h"
#include "TextureBakingContractTests.h"
#include "AssetPackagingContractTests.h"
#include "TransactionIntegrityAuditTests.h"
#include "TestRunContext.h"

#include <filesystem>
#include <fstream>
#include <cmath>

namespace fw {

std::vector<SculptedBridgeTestResult> SculptedMeshRuntimeBridgeTests::RunAllTests() {
    std::vector<SculptedBridgeTestResult> results;
    results.reserve(10);

    const std::string testRoot = (std::filesystem::temp_directory_path() / "fairworld_sbr_audit").string();
    std::error_code ec;
    std::filesystem::create_directories(testRoot, ec);

    // -------------------------------------------------------------------------
    // SBR-01: Mesh primitiva non scolpita invariata
    // -------------------------------------------------------------------------
    {
        SculptedBridgeTestResult r;
        r.testId = "SBR-01";
        r.testName = "Mesh primitiva non scolpita invariata";
        r.inputDesc = "Draft con ShapeType::Cube standard e nessun sculptMesh attivo";
        r.expectedDesc = "geometrySource == Primitive, shape.type == Cube, compilazione FG-01R identica";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:sbr_primitive", "Primitive Test Block");
        auto* draft = orch.GetCurrentDraft();
        draft->shape.type = ShapeType::Cube;
        draft->shape.supportsVisualCompilation = true;
        draft->geometrySource = GeometrySource::Primitive;
        draft->sculptMesh.reset();
        draft->completedStationsMask = 0x1F;

        auto pkgOpt = orch.BuildPackage(&sReg, &mReg, &bReg);
        bool buildOk = pkgOpt.has_value() &&
                       (pkgOpt->geometrySource == GeometrySource::Primitive) &&
                       (pkgOpt->shape.type == ShapeType::Cube) &&
                       pkgOpt->sculptTopologyJson.empty() &&
                       (pkgOpt->geometry.vertices.size() == 36);

        bool pubOk = false;
        bool shapeOk = false;
        if (buildOk) {
            pubOk = orch.PublishToRegistries(bReg, mReg, sReg);
            uint8_t vId = bReg.GetBlockIdByString("fairworld:sbr_primitive");
            if (vId != 0) {
                const auto& mat = mReg.GetMaterial(vId);
                const auto* sDef = sReg.GetShapeDef(mat.sharedShape);
                shapeOk = (sDef != nullptr) && (sDef->type == ShapeType::Cube);
            }
        }

        r.passed = buildOk && pubOk && shapeOk;
        r.observedDesc = "BuildOk: " + std::string(buildOk ? "true" : "false") +
                         ", Vertices: " + (pkgOpt ? std::to_string(pkgOpt->geometry.vertices.size()) : "0") + "/36" +
                         ", Source: " + (pkgOpt ? (pkgOpt->geometrySource == GeometrySource::Primitive ? "Primitive" : "Sculpted") : "None") +
                         ", Published: " + std::string(pubOk ? "true" : "false") +
                         ", SharedShapeType: " + (shapeOk ? "Cube" : "Non-Cube");
        r.details = r.passed ? "PASS" : "FAIL: Pipeline delle primitive alterata";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // SBR-02: Mesh scolpita compilata correttamente
    // -------------------------------------------------------------------------
    {
        SculptedBridgeTestResult r;
        r.testId = "SBR-02";
        r.testName = "Mesh scolpita compilata correttamente";
        r.inputDesc = "Draft con EditableMesh deformata (apice elevato a Y=1.75)";
        r.expectedDesc = "geometrySource == SculptedMesh, bounds riflettono la deformazione, shape.type == TriangleMesh";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:sbr_sculpted", "Sculpted Test Block");
        
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        auto vIds = mesh.GetActiveVertexIDs();
        // Deforma il vertice 0
        mesh.SetVertexPosition(vIds[0], glm::vec3(0.5f, 1.75f, 0.5f));
        orch.SetDraftSculptMesh(mesh);

        auto* draft = orch.GetCurrentDraft();
        draft->completedStationsMask = 0x1F;

        auto pkgOpt = orch.BuildPackage(&sReg, &mReg, &bReg);
        bool ok = pkgOpt.has_value() &&
                  (pkgOpt->geometrySource == GeometrySource::SculptedMesh) &&
                  (pkgOpt->shape.type == ShapeType::TriangleMesh) &&
                  !pkgOpt->sculptTopologyJson.empty() &&
                  (pkgOpt->geometry.vertices.size() > 0) &&
                  (pkgOpt->geometry.bounds.max.y >= 1.75f);

        r.passed = ok;
        r.observedDesc = "BuildOk: " + std::string(pkgOpt.has_value() ? "true" : "false") +
                         ", Source: " + (pkgOpt ? (pkgOpt->geometrySource == GeometrySource::SculptedMesh ? "SculptedMesh" : "Primitive") : "None") +
                         ", ShapeType: " + (pkgOpt ? (pkgOpt->shape.type == ShapeType::TriangleMesh ? "TriangleMesh" : "Other") : "None") +
                         ", BoundsMaxY: " + (pkgOpt ? std::to_string(pkgOpt->geometry.bounds.max.y) : "0");
        r.details = r.passed ? "PASS" : "FAIL: Compilazione geometrica della mesh scolpita fallita";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // SBR-03: Topologia e winding preservati
    // -------------------------------------------------------------------------
    {
        SculptedBridgeTestResult r;
        r.testId = "SBR-03";
        r.testName = "Topologia e winding preservati";
        r.inputDesc = "Quad planare unitario con normale orientata e winding CCW verificato";
        r.expectedDesc = "Tutti i triangoli compilati mantengono CCW coerente (dot product con normale attesa > 0.99)";

        EditableMesh mesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        GeometryCompileOptions opts;
        auto compRes = MeshBridgeService::CompileSculptedMesh(mesh, opts);

        bool ok = compRes.success && (compRes.mesh.faces.size() == 2) && (compRes.mesh.vertices.size() >= 4);
        bool windingOk = true;

        if (ok) {
            for (const auto& f : compRes.mesh.faces) {
                if (f.indices.size() < 3) { windingOk = false; break; }
                const auto& v0 = compRes.mesh.vertices[f.indices[0]].position;
                const auto& v1 = compRes.mesh.vertices[f.indices[1]].position;
                const auto& v2 = compRes.mesh.vertices[f.indices[2]].position;

                glm::vec3 edge1 = glm::vec3(v1.x - v0.x, v1.y - v0.y, v1.z - v0.z);
                glm::vec3 edge2 = glm::vec3(v2.x - v0.x, v2.y - v0.y, v2.z - v0.z);
                glm::vec3 n = glm::cross(edge1, edge2);
                float len = glm::length(n);
                if (len > 1e-5f) n /= len;

                glm::vec3 fn = glm::vec3(f.faceNormal.x, f.faceNormal.y, f.faceNormal.z);
                if (glm::dot(n, fn) < 0.99f) {
                    windingOk = false;
                }
            }
        }

        r.passed = ok && windingOk;
        r.observedDesc = "Success: " + std::string(compRes.success ? "true" : "false") +
                         ", Triangles: " + std::to_string(compRes.mesh.faces.size()) +
                         ", WindingCCWCoherent: " + std::string(windingOk ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Winding o orientamento triangoli invertito";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // SBR-04: UV per-corner preservate
    // -------------------------------------------------------------------------
    {
        SculptedBridgeTestResult r;
        r.testId = "SBR-04";
        r.testName = "UV per-corner preservate";
        r.inputDesc = "EditableMesh con UV per-corner esplicite e distinte (0.15, 0.85)";
        r.expectedDesc = "vert.roughMetal riceve le coordinate UV per-corner senza saldatura indiscriminata";

        EditableMesh mesh = EditableMesh::CreateQuad(1.0f, 1.0f);
        auto fIds = mesh.GetActiveFaceIDs();
        if (!fIds.empty()) {
            auto hes = mesh.GetFaceHalfEdges(fIds[0]);
            if (hes.size() >= 2) {
                if (auto* he0 = mesh.GetHalfEdgeMutable(hes[0])) {
                    he0->corner.uv = glm::vec2(0.15f, 0.85f);
                }
                if (auto* he1 = mesh.GetHalfEdgeMutable(hes[1])) {
                    he1->corner.uv = glm::vec2(0.65f, 0.85f);
                }
                mesh.IncrementAttributeRevision();
            }
        }

        GeometryCompileOptions opts;
        auto compRes = MeshBridgeService::CompileSculptedMesh(mesh, opts);

        bool ok = compRes.success && !compRes.mesh.vertices.empty();
        bool uvFound = false;
        if (ok) {
            for (const auto& v : compRes.mesh.vertices) {
                if (std::abs(v.roughMetal.x - 0.15f) < 1e-4f && std::abs(v.roughMetal.y - 0.85f) < 1e-4f) {
                    uvFound = true;
                    break;
                }
            }
        }

        r.passed = ok && uvFound;
        r.observedDesc = "Success: " + std::string(compRes.success ? "true" : "false") +
                         ", UVSeamCornerPreserved: " + std::string(uvFound ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Coordinate UV per-corner disperse o corrotte nella compilazione";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // SBR-05: SurfaceID e MaterialID preservati
    // -------------------------------------------------------------------------
    {
        SculptedBridgeTestResult r;
        r.testId = "SBR-05";
        r.testName = "SurfaceID e MaterialID preservati";
        r.inputDesc = "Facce con SurfaceID=4, MaterialID=9 configurate nella EditableMesh";
        r.expectedDesc = "Face::surfaceId == 4, Face::matIndex == 9 e Vertex::materialID == 9 nei dati compilati";

        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        auto fIds = mesh.GetActiveFaceIDs();
        if (!fIds.empty()) {
            auto* f0 = mesh.GetFaceMutable(fIds[0]);
            if (f0) {
                f0->surfaceId = 4;
                f0->materialId = 9;
            }
        }

        GeometryCompileOptions opts;
        opts.defaultMaterialId = 1;
        auto compRes = MeshBridgeService::CompileSculptedMesh(mesh, opts);

        bool ok = compRes.success && !compRes.mesh.faces.empty();
        bool idPreserved = false;
        if (ok) {
            for (const auto& f : compRes.mesh.faces) {
                if (f.surfaceId == 4 && f.matIndex == 9) {
                    idPreserved = true;
                    break;
                }
            }
        }

        r.passed = ok && idPreserved;
        r.observedDesc = "Success: " + std::string(compRes.success ? "true" : "false") +
                         ", SurfaceID_MaterialID_Preserved: " + std::string(idPreserved ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: SurfaceID o MaterialID non preservati";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // SBR-06: Runtime usa la mesh scolpita, non la primitiva
    // -------------------------------------------------------------------------
    {
        SculptedBridgeTestResult r;
        r.testId = "SBR-06";
        r.testName = "Runtime usa la mesh scolpita, non la primitiva";
        r.inputDesc = "Asset scolpito pubblicato transazionalmente nei registri runtime";
        r.expectedDesc = "ShapeRegistry riceve ShapeType::TriangleMesh; runtime compila triangoli scolpiti e NON un cubo";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:sbr_runtime_check", "Runtime Mesh Asset");
        
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        auto vIds = mesh.GetActiveVertexIDs();
        // Deforma visibilmente vertice 0
        mesh.SetVertexPosition(vIds[0], glm::vec3(0.5f, 2.5f, 0.5f));
        orch.SetDraftSculptMesh(mesh);

        auto* draft = orch.GetCurrentDraft();
        draft->completedStationsMask = 0x1F;

        bool pubOk = orch.PublishToRegistries(bReg, mReg, sReg);
        uint8_t vId = bReg.GetBlockIdByString("fairworld:sbr_runtime_check");
        
        bool typeIsTriangleMesh = false;
        bool runtimeCompileIsSculpted = false;
        if (pubOk && vId != 0) {
            const auto& mat = mReg.GetMaterial(vId);
            const auto* sDef = sReg.GetShapeDef(mat.sharedShape);
            if (sDef) {
                typeIsTriangleMesh = (sDef->type == ShapeType::TriangleMesh);
                // Compila attraverso il percorso runtime generico
                GeometryCompileOptions opts;
                auto comp = GeometryCompiler::Compile(*sDef, opts);
                runtimeCompileIsSculpted = comp.success && (comp.mesh.bounds().max.y >= 2.5f);
            }
        }

        r.passed = pubOk && typeIsTriangleMesh && runtimeCompileIsSculpted;
        r.observedDesc = "Published: " + std::string(pubOk ? "true" : "false") +
                         ", ShapeType: " + (typeIsTriangleMesh ? "TriangleMesh" : "Primitive/Cube") +
                         ", RuntimeCompileBoundsMaxY >= 2.5: " + std::string(runtimeCompileIsSculpted ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Runtime ricade silenziosamente sulla primitiva parametrica!";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // SBR-07: Save/Reload mantiene la geometria
    // -------------------------------------------------------------------------
    {
        SculptedBridgeTestResult r;
        r.testId = "SBR-07";
        r.testName = "Save/Reload mantiene la geometria";
        r.inputDesc = "Serializzazione RawAssetPackage su disco e deserializzazione post-riavvio simulato";
        r.expectedDesc = "Package caricato preserva geometrySource, vertici, bounds e ricostruisce EditableMesh identica";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:sbr_save_reload", "Save Reload Asset");
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        auto vIds = mesh.GetActiveVertexIDs();
        mesh.SetVertexPosition(vIds[0], glm::vec3(0.5f, 3.0f, 0.5f));
        orch.SetDraftSculptMesh(mesh);

        auto* draft = orch.GetCurrentDraft();
        draft->completedStationsMask = 0x1F;

        auto pkgOpt = orch.BuildPackage(&sReg, &mReg, &bReg);
        bool buildOk = pkgOpt.has_value();

        const std::string pkgFile = testRoot + "/sbr_saved_package.rawpkg";
        bool saveOk = buildOk && pkgOpt->SaveToFile(pkgFile);

        RawAssetPackage reloadedPkg;
        bool loadOk = saveOk && reloadedPkg.LoadFromFile(pkgFile);

        EditableMesh reconMesh;
        std::string reconErr;
        bool reconOk = loadOk && MeshBridgeService::ReconstructMeshFromPackage(reloadedPkg, reconMesh, &reconErr);

        bool geomMatch = reconOk && (reconMesh.GetVertexCount() == mesh.GetVertexCount()) &&
                         (reconMesh.GetFaceCount() == mesh.GetFaceCount()) &&
                         (reloadedPkg.geometrySource == GeometrySource::SculptedMesh) &&
                         (reloadedPkg.geometry.bounds.max.y >= 3.0f);

        r.passed = buildOk && saveOk && loadOk && reconOk && geomMatch;
        r.observedDesc = "SaveOk: " + std::string(saveOk ? "true" : "false") +
                         ", LoadOk: " + std::string(loadOk ? "true" : "false") +
                         ", ReconstructOk: " + std::string(reconOk ? "true" : "false") +
                         ", GeomMatch: " + std::string(geomMatch ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Perdita di geometria nel roundtrip di persistenza";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // SBR-08: Fallimento compilazione atomico, senza fallback silenzioso
    // -------------------------------------------------------------------------
    {
        SculptedBridgeTestResult r;
        r.testId = "SBR-08";
        r.testName = "Fallimento compilazione atomico, senza fallback silenzioso";
        r.inputDesc = "Draft impostato come SculptedMesh con EditableMesh vuota/corrotta";
        r.expectedDesc = "BuildPackage ritorna nullopt; PublishToRegistries fallisce; nessun fallback a cubo di default";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:sbr_fail_test", "Failing Asset");
        auto* draft = orch.GetCurrentDraft();
        draft->geometrySource = GeometrySource::SculptedMesh;
        draft->sculptMesh = EditableMesh{}; // Mesh vuota/corrotta (0 vertici, 0 facce)
        draft->shape.type = ShapeType::Cube; // Se ci fosse fallback silenzioso produrrebbe un cubo
        draft->completedStationsMask = 0x1F;

        uint32_t bCount0 = bReg.GetActiveBlockCount();
        auto pkgOpt = orch.BuildPackage(&sReg, &mReg, &bReg);
        bool buildRejected = !pkgOpt.has_value();

        bool pubRejected = !orch.PublishToRegistries(bReg, mReg, sReg);
        uint32_t bCount1 = bReg.GetActiveBlockCount();

        bool ok = buildRejected && pubRejected && (bCount0 == bCount1);

        r.passed = ok;
        r.observedDesc = "BuildRejected: " + std::string(buildRejected ? "true" : "false") +
                         ", PublishRejected: " + std::string(pubRejected ? "true" : "false") +
                         ", RegistriesUntouched: " + std::string(bCount0 == bCount1 ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Rilevato fallback silenzioso a primitiva in caso di errore!";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // SBR-09: Geometry invariata = nessuna ricompilazione
    // -------------------------------------------------------------------------
    {
        SculptedBridgeTestResult r;
        r.testId = "SBR-09";
        r.testName = "Geometry invariata = nessuna ricompilazione";
        r.inputDesc = "Due chiamate consecutive a BuildPackage su draft con geometria invariata";
        r.expectedDesc = "Seconda chiamata usa cache e conteggio ricompilazioni non incrementa (Zero-Work hit)";

        ShapeRegistry sReg; sReg.Initialize();
        BlockRegistry bReg; bReg.Initialize();
        MaterialRegistry mReg; mReg.Initialize();

        FactoryOrchestrator orch;
        orch.ResetGeometryCompileCallCount();
        orch.OpenDraft("fairworld:sbr_zero_work", "Zero Work Asset");
        orch.SetDraftSculptMesh(EditableMesh::CreateCube(1.0f));

        auto* draft = orch.GetCurrentDraft();
        draft->completedStationsMask = 0x1F;

        // Prima compilazione
        auto pkg1 = orch.BuildPackage(&sReg, &mReg, &bReg);
        uint64_t count1 = orch.GetGeometryCompileCallCount();
        uint64_t gRev1 = draft->sculptMesh ? draft->sculptMesh->GetGeometryRevision() : 0;
        uint64_t aRev1 = draft->sculptMesh ? draft->sculptMesh->GetAttributeRevision() : 0;

        // Seconda compilazione (input identico)
        auto pkg2 = orch.BuildPackage(&sReg, &mReg, &bReg);
        uint64_t count2 = orch.GetGeometryCompileCallCount();
        uint64_t gRev2 = draft->sculptMesh ? draft->sculptMesh->GetGeometryRevision() : 0;
        uint64_t aRev2 = draft->sculptMesh ? draft->sculptMesh->GetAttributeRevision() : 0;

        bool zeroWorkHit = (count1 == 1) && (count2 == 1) && pkg1.has_value() && pkg2.has_value();

        // Ora modifichiamo la geometria: deve invalidare e ricompilare
        EditableMesh modMesh = *orch.GetDraftSculptMesh();
        auto vIds = modMesh.GetActiveVertexIDs();
        modMesh.SetVertexPosition(vIds[0], glm::vec3(0.5f, 2.0f, 0.5f));
        orch.SetDraftSculptMesh(modMesh);

        auto pkg3 = orch.BuildPackage(&sReg, &mReg, &bReg);
        uint64_t count3 = orch.GetGeometryCompileCallCount();
        uint64_t gRev3 = draft->sculptMesh ? draft->sculptMesh->GetGeometryRevision() : 0;

        bool invalidationHit = (count3 == 2) && pkg3.has_value();

        r.passed = zeroWorkHit && invalidationHit;
        r.observedDesc = "Count1=" + std::to_string(count1) + " [gRev=" + std::to_string(gRev1) + ", aRev=" + std::to_string(aRev1) + "]" +
                         ", Count2_Unchanged=" + std::to_string(count2) + " [gRev=" + std::to_string(gRev2) + ", aRev=" + std::to_string(aRev2) + "]" +
                         ", Count3_Modified=" + std::to_string(count3) + " [gRev=" + std::to_string(gRev3) + "]";
        r.details = r.passed ? "PASS" : "FAIL: Zero-Work cache non operante per la geometria scolpita";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // SBR-10: Nessuna regressione della baseline precedente (191 test)
    // -------------------------------------------------------------------------
    {
        SculptedBridgeTestResult r;
        r.testId = "SBR-10";
        r.testName = "Nessuna regressione della baseline precedente (191 test)";
        r.inputDesc = "Esecuzione di TOP(36), PNT(10), TXB(10), PKG(10), TRX(10)";
        r.expectedDesc = "Tutti i 76 test delle suite sopra menzionate risultano PASS al 100%";

        if (TestRunContext::Instance().IsActive()) {
            static const std::vector<std::pair<std::string, size_t>> deps = {
                {"TOP", 36}, {"PNT", 10}, {"TXB", 10}, {"PKG", 10}, {"TRX", 10}
            };
            auto eval = TestRunContext::Instance().EvaluateRegressionGate(deps);
            size_t topPass = 0, pntPass = 0, txbPass = 0, pkgPass = 0, trxPass = 0;
            for (const auto& c : eval.checks) {
                if (c.suiteCode == "TOP") topPass = c.passedCount;
                if (c.suiteCode == "PNT") pntPass = c.passedCount;
                if (c.suiteCode == "TXB") txbPass = c.passedCount;
                if (c.suiteCode == "PKG") pkgPass = c.passedCount;
                if (c.suiteCode == "TRX") trxPass = c.passedCount;
            }
            r.passed = eval.allDependenciesPassed;
            r.observedDesc = "TOP: " + std::to_string(topPass) + "/36, PNT: " + std::to_string(pntPass) + "/10, TXB: " + std::to_string(txbPass) + "/10, PKG: " + std::to_string(pkgPass) + "/10, TRX: " + std::to_string(trxPass) + "/10";
            r.details = r.passed ? "PASS" : "FAIL: " + eval.failureDetails;
        } else {
            auto topRes = CompiledTopologyTests::RunAllTests();
            auto pntRes = PainterStationContractTests::RunAllTests();
            auto txbRes = TextureBakingContractTests::RunAllTests();
            auto pkgRes = AssetPackagingContractTests::RunAllTests();
            auto trxRes = TransactionIntegrityAuditTests::RunAllTests();

            size_t topPass = 0; for (const auto& tr : topRes) if (tr.passed) topPass++;
            size_t pntPass = 0; for (const auto& pr : pntRes) if (pr.passed) pntPass++;
            size_t txbPass = 0; for (const auto& xr : txbRes) if (xr.passed) txbPass++;
            size_t pkgPass = 0; for (const auto& kr : pkgRes) if (kr.passed) pkgPass++;
            size_t trxPass = 0; for (const auto& rr : trxRes) if (rr.passed) trxPass++;

            bool allPass = (topPass == topRes.size() && topRes.size() == 36) &&
                           (pntPass == pntRes.size() && pntRes.size() == 10) &&
                           (txbPass == txbRes.size() && txbRes.size() == 10) &&
                           (pkgPass == pkgRes.size() && pkgRes.size() == 10) &&
                           (trxPass == trxRes.size() && trxRes.size() == 10);

            r.passed = allPass;
            r.observedDesc = "TOP: " + std::to_string(topPass) + "/" + std::to_string(topRes.size()) +
                             ", PNT: " + std::to_string(pntPass) + "/" + std::to_string(pntRes.size()) +
                             ", TXB: " + std::to_string(txbPass) + "/" + std::to_string(txbRes.size()) +
                             ", PKG: " + std::to_string(pkgPass) + "/" + std::to_string(pkgRes.size()) +
                             ", TRX: " + std::to_string(trxPass) + "/" + std::to_string(trxRes.size());
            r.details = r.passed ? "PASS" : "FAIL: Regressione riscontrata nella baseline pregressa";
        }
        results.push_back(r);
    }

    return results;
}

} // namespace fw
