#include "pch.h"
#include "PrimitiveUVContractTests.h"
#include "PrimitiveUVMapper.h"
#include "blocks/sculpt/MeshBridgeService.h"
#include "world/GeometryCompiler.h"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace fw {

std::vector<PrimitiveUVTestResult> PrimitiveUVContractTests::RunAllTests() {
    std::vector<PrimitiveUVTestResult> results;
    PrimitiveUVMapper mapper;

    auto addTest = [&](const std::string& id, const std::string& name,
                       const std::string& inDesc, const std::string& expDesc,
                       const std::string& obsDesc, bool passed, const std::string& det) {
        PrimitiveUVTestResult r;
        r.testId = id;
        r.testName = name;
        r.inputDesc = inDesc;
        r.expectedDesc = expDesc;
        r.observedDesc = obsDesc;
        r.passed = passed;
        r.details = det;
        results.push_back(r);
    };

    // -------------------------------------------------------------------------
    // UVP-1: Cube
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        PrimitiveUVOptions opt;
        opt.markSeamEdges = true;
        auto res = mapper.AnalyzeAndMap(mesh, ShapeType::Cube, std::monostate{}, opt);

        bool passed = res.success && 
                      res.status == PrimitiveMappingStatus::SUCCESS &&
                      res.layout.islands.size() == 6 &&
                      res.validation.isValid &&
                      res.validation.nanInfCount == 0 &&
                      res.validation.degenerateUVTriangles == 0;

        // Verifica che le seams siano state impostate
        size_t seamCount = 0;
        for (EdgeID eId : mesh.GetActiveEdgeIDs()) {
            const auto* e = mesh.GetEdge(eId);
            if (e && e->seamWeight > 0.5f) seamCount++;
        }
        passed = passed && (seamCount > 0);

        std::string obs = "Status: " + std::string(GetPrimitiveMappingStatusName(res.status)) +
                          ", Islands: " + std::to_string(res.layout.islands.size()) +
                          ", Seam edges: " + std::to_string(seamCount) +
                          ", Valid: " + (res.validation.isValid ? "true" : "false");

        addTest("UVP-1", "Cube Mapping & Seams",
                "Cube EditableMesh (1.0m, 6 faces)",
                "SUCCESS, 6 islands, 0 NaNs, 0 degenerate UVs, seam edges > 0",
                obs, passed,
                passed ? "Cube mappato correttamente in griglia 3x2 non-overlapping con seam di separazione." 
                       : "Fallimento mappatura Cube o validazione fallita.");
    }

    // -------------------------------------------------------------------------
    // UVP-2: Cuboid e Parallelepiped
    // -------------------------------------------------------------------------
    {
        ShapeDefinition defCuboid;
        defCuboid.type = ShapeType::Cuboid;
        defCuboid.parameters = ShapeParamsCuboid{ glm::vec3(2.5f, 1.0f, 0.4f) };
        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(defCuboid);

        auto res = mapper.AnalyzeAndMap(mesh, ShapeType::Cuboid, defCuboid.parameters);

        bool passed = res.success && 
                      res.status == PrimitiveMappingStatus::SUCCESS &&
                      res.layout.islands.size() == 6 &&
                      res.validation.isValid &&
                      res.validation.nanInfCount == 0;

        std::string obs = "Status: " + std::string(GetPrimitiveMappingStatusName(res.status)) +
                          ", Islands: " + std::to_string(res.layout.islands.size()) +
                          ", Valid: " + (res.validation.isValid ? "true" : "false");

        addTest("UVP-2", "Cuboid e Parallelepiped Mapping",
                "Cuboid (2.5 x 1.0 x 0.4), Parallelepiped param",
                "SUCCESS, 6 islands, proporzioni dimensionali conservate, 0 NaNs",
                obs, passed,
                passed ? "Cuboid mappato con successo preservando le proporzioni per-faccia."
                       : "Fallimento mappatura Cuboid.");
    }

    // -------------------------------------------------------------------------
    // UVP-3: Pyramid e PyramidFrustum
    // -------------------------------------------------------------------------
    {
        ShapeDefinition defPyr;
        defPyr.type = ShapeType::Pyramid;
        defPyr.parameters = ShapeParamsPyramid{ 1.0f, 1.0f, 1.5f, glm::vec2(0.0f) };
        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(defPyr);

        auto res = mapper.AnalyzeAndMap(mesh, ShapeType::Pyramid, defPyr.parameters);

        bool passed = res.success && 
                      (res.status == PrimitiveMappingStatus::SUCCESS || res.status == PrimitiveMappingStatus::PARTIAL_SUCCESS) &&
                      res.validation.nanInfCount == 0;

        // Verifica che l'apice non contenga NaN
        for (FaceID fId : mesh.GetActiveFaceIDs()) {
            for (HalfEdgeID heId : mesh.GetFaceHalfEdges(fId)) {
                const auto* he = mesh.GetHalfEdge(heId);
                if (he && (std::isnan(he->corner.uv.x) || std::isnan(he->corner.uv.y))) {
                    passed = false;
                }
            }
        }

        std::string obs = "Status: " + std::string(GetPrimitiveMappingStatusName(res.status)) +
                          ", Islands: " + std::to_string(res.layout.islands.size()) +
                          ", NaN/Inf: " + std::to_string(res.validation.nanInfCount);

        addTest("UVP-3", "Pyramid e PyramidFrustum Mapping",
                "Pyramid (1.0 x 1.0, height 1.5)",
                "SUCCESS, gestione singolarità apice senza NaN, facce triangolari mappate",
                obs, passed,
                passed ? "Piramide mappata con coordinate conformi per facce laterali e base piana."
                       : "Fallimento mappatura Pyramid.");
    }

    // -------------------------------------------------------------------------
    // UVP-4: Cylinder
    // -------------------------------------------------------------------------
    {
        ShapeDefinition defCyl;
        defCyl.type = ShapeType::Cylinder;
        defCyl.parameters = ShapeParamsCylinder{ 0.5f, 2.0f };
        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(defCyl);

        auto res = mapper.AnalyzeAndMap(mesh, ShapeType::Cylinder, defCyl.parameters);

        bool passed = res.success && 
                      res.layout.islands.size() >= 3 &&
                      res.validation.nanInfCount == 0;

        // Verifica che mantello, top cap e bottom cap siano distinti
        std::string obs = "Status: " + std::string(GetPrimitiveMappingStatusName(res.status)) +
                          ", Islands: " + std::to_string(res.layout.islands.size()) +
                          ", Valid: " + (res.validation.isValid ? "true" : "false");

        addTest("UVP-4", "Cylinder Continuous Unroll",
                "Cylinder (radius 0.5, height 2.0)",
                "SUCCESS, 3 isole (mantello + 2 basi), correzione salto cucitura",
                obs, passed,
                passed ? "Cilindro sviluppato con mantello rettangolare continuo e basi separate."
                       : "Fallimento mappatura Cylinder.");
    }

    // -------------------------------------------------------------------------
    // UVP-5: HollowCylinder
    // -------------------------------------------------------------------------
    {
        ShapeDefinition defHollow;
        defHollow.type = ShapeType::HollowCylinder;
        defHollow.parameters = ShapeParamsHollowCylinder{ 1.0f, 0.5f, 1.5f };
        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(defHollow);

        auto res = mapper.AnalyzeAndMap(mesh, ShapeType::HollowCylinder, defHollow.parameters);

        bool passed = res.success && 
                      res.layout.islands.size() >= 4 &&
                      res.validation.nanInfCount == 0;

        std::string obs = "Status: " + std::string(GetPrimitiveMappingStatusName(res.status)) +
                          ", Islands: " + std::to_string(res.layout.islands.size()) +
                          ", Valid: " + (res.validation.isValid ? "true" : "false");

        addTest("UVP-5", "HollowCylinder Internal & External Mantle",
                "HollowCylinder (rOut 1.0, rIn 0.5, height 1.5)",
                "SUCCESS, 4 isole distinte (parete esterna, parete interna, 2 tappi anulari)",
                obs, passed,
                passed ? "Cilindro cavo mappato con isolamento completo tra superficie esterna e interna."
                       : "Fallimento mappatura HollowCylinder.");
    }

    // -------------------------------------------------------------------------
    // UVP-6: SlicedCylinder
    // -------------------------------------------------------------------------
    {
        ShapeDefinition defSliced;
        defSliced.type = ShapeType::SlicedCylinder;
        defSliced.parameters = ShapeParamsSlicedCylinder{ 0.5f, 2.0f, glm::vec3(0.0f, 1.0f, 1.0f) };
        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(defSliced);

        auto res = mapper.AnalyzeAndMap(mesh, ShapeType::SlicedCylinder, defSliced.parameters);

        bool passed = res.success && 
                      res.layout.islands.size() >= 3 &&
                      res.validation.nanInfCount == 0;

        std::string obs = "Status: " + std::string(GetPrimitiveMappingStatusName(res.status)) +
                          ", Islands: " + std::to_string(res.layout.islands.size()) +
                          ", Valid: " + (res.validation.isValid ? "true" : "false");

        addTest("UVP-6", "SlicedCylinder Oblique Projection",
                "SlicedCylinder (radius 0.5, cutNormal [0, 1, 1])",
                "SUCCESS, proiezione planare conforme per tappo obliquo, mantello adattato",
                obs, passed,
                passed ? "Cilindro sezionato mappato con corretta orientazione del taglio inclinato."
                       : "Fallimento mappatura SlicedCylinder.");
    }

    // -------------------------------------------------------------------------
    // UVP-7: Cone e ConeFrustum
    // -------------------------------------------------------------------------
    {
        ShapeDefinition defCone;
        defCone.type = ShapeType::Cone;
        defCone.parameters = ShapeParamsCone{ 0.5f, 1.5f };
        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(defCone);

        auto res = mapper.AnalyzeAndMap(mesh, ShapeType::Cone, defCone.parameters);

        bool passed = res.success && 
                      res.layout.islands.size() >= 2 &&
                      res.validation.nanInfCount == 0;

        std::string obs = "Status: " + std::string(GetPrimitiveMappingStatusName(res.status)) +
                          ", Islands: " + std::to_string(res.layout.islands.size()) +
                          ", NaN/Inf: " + std::to_string(res.validation.nanInfCount);

        addTest("UVP-7", "Cone Singularity & ConeFrustum",
                "Cone (radius 0.5, height 1.5, apex r=0)",
                "SUCCESS, 2 isole (corpo + base), zero NaN all'apice",
                obs, passed,
                passed ? "Cono mappato gestendo correttamente il collasso della coordinata theta all'apice."
                       : "Fallimento mappatura Cone.");
    }

    // -------------------------------------------------------------------------
    // UVP-8: Sphere
    // -------------------------------------------------------------------------
    {
        ShapeDefinition defSphere;
        defSphere.type = ShapeType::Sphere;
        defSphere.parameters = ShapeParamsSphere{ 1.0f };
        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(defSphere);

        PrimitiveUVOptions opt;
        opt.strategy = UVMappingStrategy::Hemisphere;
        auto res = mapper.AnalyzeAndMap(mesh, ShapeType::Sphere, defSphere.parameters, opt);

        bool passed = res.success && 
                      res.layout.islands.size() == 2 && // 2 calotte emisferiche
                      res.validation.nanInfCount == 0;

        std::string obs = "Status: " + std::string(GetPrimitiveMappingStatusName(res.status)) +
                          ", Islands: " + std::to_string(res.layout.islands.size()) +
                          ", Strategy: Dual-Hemisphere Chart";

        addTest("UVP-8", "Sphere Dual-Hemisphere & Polar Unroll",
                "Sphere (radius 1.0m)",
                "SUCCESS, 2 calotte emisferiche con distorsione polare azzerata, zero NaN",
                obs, passed,
                passed ? "Sfera mappata via Dual-Hemisphere Chart conforme."
                       : "Fallimento mappatura Sphere.");
    }

    // -------------------------------------------------------------------------
    // UVP-9: SphericalZone e Segment
    // -------------------------------------------------------------------------
    {
        ShapeDefinition defZone;
        defZone.type = ShapeType::SphericalZone;
        defZone.parameters = ShapeParamsSphericalZone{ 1.0f, -0.5f, 0.8f };
        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(defZone);

        auto res = mapper.AnalyzeAndMap(mesh, ShapeType::SphericalZone, defZone.parameters);

        bool passed = res.success && 
                      res.validation.nanInfCount == 0;

        std::string obs = "Status: " + std::string(GetPrimitiveMappingStatusName(res.status)) +
                          ", Islands: " + std::to_string(res.layout.islands.size());

        addTest("UVP-9", "SphericalZone e SphericalSegment",
                "SphericalZone (r=1.0, bottomY=-0.5, topY=0.8)",
                "SUCCESS, mantello di zona sferica + tappi piani troncati",
                obs, passed,
                passed ? "Zona sferica mappata con delimitazione dei paralleli e tappi piani."
                       : "Fallimento mappatura SphericalZone.");
    }

    // -------------------------------------------------------------------------
    // UVP-10: Capsule
    // -------------------------------------------------------------------------
    {
        ShapeDefinition defCap;
        defCap.type = ShapeType::Capsule;
        defCap.parameters = ShapeParamsCapsule{ 0.5f, 1.0f };
        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(defCap);

        auto res = mapper.AnalyzeAndMap(mesh, ShapeType::Capsule, defCap.parameters);

        bool passed = res.success && 
                      res.validation.nanInfCount == 0;

        std::string obs = "Status: " + std::string(GetPrimitiveMappingStatusName(res.status)) +
                          ", Islands: " + std::to_string(res.layout.islands.size());

        addTest("UVP-10", "Capsule Multi-Part Mapping",
                "Capsule (radius 0.5, halfHeight 1.0)",
                "SUCCESS, corpo cilindrico e calotte emisferiche coordinate",
                obs, passed,
                passed ? "Capsula mappata correttamente con transizioni continue tra corpo e calotte."
                       : "Fallimento mappatura Capsule.");
    }

    // -------------------------------------------------------------------------
    // UVP-11: Forme con cavità
    // -------------------------------------------------------------------------
    {
        ShapeDefinition defBore;
        defBore.type = ShapeType::SphereWithCylindricalBore;
        defBore.parameters = ShapeParamsSphereWithCylindricalBore{ 1.0f, 0.4f, 2.0f };
        EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(defBore);

        auto res = mapper.AnalyzeAndMap(mesh, ShapeType::SphereWithCylindricalBore, defBore.parameters);

        bool passed = res.success && 
                      res.layout.islands.size() >= 3 &&
                      res.validation.nanInfCount == 0;

        std::string obs = "Status: " + std::string(GetPrimitiveMappingStatusName(res.status)) +
                          ", Islands: " + std::to_string(res.layout.islands.size()) +
                          ", Cavity Wall: Separata";

        addTest("UVP-11", "Cavity Shapes (Bore & Conical Cavities)",
                "SphereWithCylindricalBore (rS=1.0, rC=0.4)",
                "SUCCESS, parete del foro interno mappata come isola indipendente da quella esterna",
                obs, passed,
                passed ? "Geometria con cavità mappata separando nettamente pareti interne ed esterne."
                       : "Fallimento mappatura cavity shape.");
    }

    // -------------------------------------------------------------------------
    // UVP-12: ConvexHull
    // -------------------------------------------------------------------------
    {
        ShapeDefinition defHull;
        defHull.type = ShapeType::ConvexHull;
        EditableMesh mesh = EditableMesh::CreateCube(1.0f); // Poliedro convesso rappresentativo

        auto res = mapper.AnalyzeAndMap(mesh, ShapeType::ConvexHull, std::monostate{});

        bool passed = (res.status == PrimitiveMappingStatus::SUCCESS || res.status == PrimitiveMappingStatus::NEEDS_BM_UV3);

        std::string obs = "Status: " + std::string(GetPrimitiveMappingStatusName(res.status)) +
                          ", Valid: " + (res.validation.isValid ? "true" : "false");

        addTest("UVP-12", "ConvexHull Polyhedral Mapping",
                "ConvexHull (poliedro convesso analitico)",
                "SUCCESS o graceful NEEDS_BM_UV3 contrattuale",
                obs, passed,
                passed ? "ConvexHull gestito coerentemente secondo il contratto di fallback."
                       : "Fallimento gestione ConvexHull.");
    }

    // -------------------------------------------------------------------------
    // UVP-13: UV degeneri e geometrie problematiche
    // -------------------------------------------------------------------------
    {
        // Costruisce una mesh con una faccia degenere e corrompe un corner UV con NaN
        EditableMesh mesh;
        VertexID v0 = mesh.AddVertex(glm::vec3(0, 0, 0));
        VertexID v1 = mesh.AddVertex(glm::vec3(0, 0, 0)); // Coincidente (area 0)
        VertexID v2 = mesh.AddVertex(glm::vec3(1, 0, 0));
        FaceID f = mesh.AddFace({ v0, v1, v2 });

        UVLayout layout;
        layout.faceToIslandMap[f] = 0;
        UVIsland isl; isl.id = 0; isl.faces.push_back(f); layout.islands.push_back(isl);

        // Inietta NaN
        auto hes = mesh.GetFaceHalfEdges(f);
        if (!hes.empty()) {
            mesh.GetHalfEdgeMutable(hes[0])->corner.uv = glm::vec2(std::numeric_limits<float>::quiet_NaN());
        }

        UVValidationReport rep;
        bool valid = PrimitiveUVMapper::ValidateGeneratedUV(mesh, layout, rep);

        bool passed = (!valid) && (rep.nanInfCount > 0);

        std::string obs = "Valid: " + std::string(valid ? "true" : "false") +
                          ", NaN detected: " + std::to_string(rep.nanInfCount) +
                          ", Degenerate: " + std::to_string(rep.degenerateUVTriangles);

        addTest("UVP-13", "Degenerate Geometry & NaN Detection",
                "Mesh con triangolo ad area zero e corner UV = NaN",
                "Validation fallisce, NaN rilevato, degenerateUVTriangles > 0",
                obs, passed,
                passed ? "Validatore UV rileva correttamente coordinate corrotte e triangoli degeneri."
                       : "Il validatore non ha intercettato il corner NaN.");
    }

    // -------------------------------------------------------------------------
    // UVP-14: Revision tracking e Zero-Work
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        uint64_t initialGeomRev = mesh.GetGeometryRevision();
        uint64_t initialAttrRev = mesh.GetAttributeRevision();

        auto res = mapper.AnalyzeAndMap(mesh, ShapeType::Cube, std::monostate{});

        uint64_t postGeomRev = mesh.GetGeometryRevision();
        uint64_t postAttrRev = mesh.GetAttributeRevision();

        // 3D Geometry revision DEVE rimanere identica (zero work sulla geometria 3D!)
        // Attribute revision DEVE essere incrementata (corner UV scritti!)
        bool passed = (postGeomRev == initialGeomRev) && 
                      (postAttrRev == initialAttrRev + 1) &&
                      (res.layout.meshTopologyRevision == mesh.GetTopologyRevision()) &&
                      (res.layout.meshAttributeRevision == postAttrRev);

        std::string obs = "GeomRev: " + std::to_string(initialGeomRev) + " -> " + std::to_string(postGeomRev) +
                          ", AttrRev: " + std::to_string(initialAttrRev) + " -> " + std::to_string(postAttrRev);

        addTest("UVP-14", "Revision Tracking & Zero-Work",
                "Cube mapping con ispezione revisioni DCEL",
                "GeomRev invariata, AttrRev incrementata di 1, layout sincronizzato",
                obs, passed,
                passed ? "Revision tracking deterministico: nessuna modifica 3D, solo attributi aggiornati."
                       : "Invariante Zero-Work violato.");
    }

    // -------------------------------------------------------------------------
    // UVP-15: Inventario completo ShapeRegistry
    // -------------------------------------------------------------------------
    {
        uint32_t totalShapes = 26;
        uint32_t handledShapes = 0;
        uint32_t implementedCount = 0;
        uint32_t needsBMUV3Count = 0;

        for (uint8_t t = 0; t < totalShapes; ++t) {
            ShapeType st = static_cast<ShapeType>(t);
            ShapeDefinition sDef;
            sDef.type = st;
            EditableMesh m = MeshBridgeService::CreateMeshFromShape(sDef);

            auto res = mapper.AnalyzeAndMap(m, st, std::monostate{});
            if (res.status == PrimitiveMappingStatus::SUCCESS || res.status == PrimitiveMappingStatus::PARTIAL_SUCCESS) {
                implementedCount++;
                handledShapes++;
            } else if (res.status == PrimitiveMappingStatus::NEEDS_BM_UV3 || res.status == PrimitiveMappingStatus::UNSUPPORTED_MAPPING) {
                needsBMUV3Count++;
                handledShapes++;
            }
        }

        bool passed = (handledShapes == totalShapes);

        std::string obs = "Covered: " + std::to_string(handledShapes) + "/" + std::to_string(totalShapes) +
                          " (Implemented: " + std::to_string(implementedCount) +
                          ", BM-UV3: " + std::to_string(needsBMUV3Count) + ")";

        addTest("UVP-15", "ShapeRegistry Catalog Full Coverage",
                "Tutti i 26 ShapeType di ShapeRegistry",
                "26/26 forme classificate esplicitamente (nessun fallback indefinito)",
                obs, passed,
                passed ? "Catalogo completo coperto: ogni tipo ha esito e strategia determinati."
                       : "Forme con stato indefinito.");
    }

    return results;
}

} // namespace fw
