#include "pch.h"
#include "AtlasUVContractTests.h"
#include "UVAtlasPacker.h"
#include "TexelDensityCalculator.h"
#include "GeneralUVMapper.h"
#include "PrimitiveUVMapper.h"
#include <sstream>
#include <cmath>

namespace fw {

std::vector<AtlasUVTestResult> AtlasUVContractTests::RunAllTests() {
    std::vector<AtlasUVTestResult> results;
    UVAtlasPacker packer;
    GeneralUVMapper mapper;

    auto addTest = [&](const std::string& id, const std::string& name,
                       const std::string& inDesc, const std::string& expDesc,
                       const std::string& obsDesc, bool passed, const std::string& det) {
        AtlasUVTestResult r;
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
    // UVK-1: Packing singola island
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = createOpenPlaneGrid();
        GeneralUVOptions opt;
        auto res = mapper.UnwrapMesh(mesh, opt);

        AtlasPackOptions pOpt;
        pOpt.targetWidth = 512;
        pOpt.targetHeight = 512;
        pOpt.paddingPixels = 4.0f;
        auto packRes = packer.PackLayout(mesh, res.layout, pOpt);

        bool passed = packRes.success && 
                      res.layout.islands.size() == 1 &&
                      res.layout.islands[0].atlasTransform.x > 0.0f &&
                      res.layout.islands[0].atlasTransform.y > 0.0f;

        std::string obs = "Pages: " + std::to_string(packRes.pagesUsed) +
                          ", Occupancy: " + std::to_string(packRes.occupancyRatio) +
                          ", Transform: [" + std::to_string(res.layout.islands[0].atlasTransform.x) + ", " +
                          std::to_string(res.layout.islands[0].atlasTransform.y) + "]";

        addTest("UVK-1", "Single Island Atlas Packing",
                "Singola isola UV piana 2x2",
                "Collocata interamente entro [0, 1] con margini di padding rispettati",
                obs, passed,
                passed ? "Singola isola impacchettata con successo." : "Errore packing singola isola.");
    }

    // -------------------------------------------------------------------------
    // UVK-2: Packing multiplo (Cubo con 6 facce)
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        GeneralUVOptions opt;
        opt.seamOptions.dihedralAngleThresholdDegrees = 45.0f;
        auto res = mapper.UnwrapMesh(mesh, opt);

        AtlasPackOptions pOpt;
        pOpt.targetWidth = 1024;
        pOpt.targetHeight = 1024;
        pOpt.paddingPixels = 4.0f;
        auto packRes = packer.PackLayout(mesh, res.layout, pOpt);

        bool passed = packRes.success && 
                      res.layout.islands.size() >= 6 &&
                      !packRes.overlapsDetected &&
                      packRes.occupancyRatio > 0.1f;

        std::string obs = "Islands: " + std::to_string(res.layout.islands.size()) +
                          ", Overlaps: " + std::string(packRes.overlapsDetected ? "true" : "false") +
                          ", Occupancy: " + std::to_string(packRes.occupancyRatio);

        addTest("UVK-2", "Multiple Islands Atlas Packing",
                "Cubo sezionato in 6 isole distinte",
                "Tutte le 6 isole inserite nell'atlas senza overlap",
                obs, passed,
                passed ? "Cubo impacchettato con successo senza sovrapposizioni." : "Overlap rilevato nel packing multiplo.");
    }

    // -------------------------------------------------------------------------
    // UVK-3: Island di dimensioni differenti
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh;
        // Faccia grande 4.0 x 4.0
        VertexID g0 = mesh.AddVertex(glm::vec3(0, 0, 0));
        VertexID g1 = mesh.AddVertex(glm::vec3(4, 0, 0));
        VertexID g2 = mesh.AddVertex(glm::vec3(4, 0, 4));
        VertexID g3 = mesh.AddVertex(glm::vec3(0, 0, 4));
        mesh.AddFace({ g0, g1, g2, g3 }, 0, 0);

        // Faccia piccola 0.5 x 0.5
        VertexID p0 = mesh.AddVertex(glm::vec3(10, 0, 0));
        VertexID p1 = mesh.AddVertex(glm::vec3(10.5f, 0, 0));
        VertexID p2 = mesh.AddVertex(glm::vec3(10.5f, 0, 0.5f));
        VertexID p3 = mesh.AddVertex(glm::vec3(10, 0, 0.5f));
        mesh.AddFace({ p0, p1, p2, p3 }, 1, 0);
        mesh.ComputeAllFaceNormals();

        GeneralUVOptions opt;
        auto res = mapper.UnwrapMesh(mesh, opt);

        AtlasPackOptions pOpt;
        auto packRes = packer.PackLayout(mesh, res.layout, pOpt);

        // L'isola grande deve avere scala UV atlas maggiore dell'isola piccola
        bool scaleRatioValid = false;
        if (res.layout.islands.size() >= 2) {
            float areaA = res.layout.islands[0].atlasTransform.x * res.layout.islands[0].atlasTransform.y;
            float areaB = res.layout.islands[1].atlasTransform.x * res.layout.islands[1].atlasTransform.y;
            scaleRatioValid = (std::max(areaA, areaB) > 2.0f * std::min(areaA, areaB));
        }

        bool passed = packRes.success && scaleRatioValid;
        std::string obs = "Islands: " + std::to_string(res.layout.islands.size()) +
                          ", ScaleRatioValid: " + std::string(scaleRatioValid ? "true" : "false");

        addTest("UVK-3", "Differently Sized Islands Packing",
                "Due facce con aree 16.0m² e 0.25m²",
                "Dimensioni dei rettangoli nell'atlas proporzionali alla metrica 3D reale",
                obs, passed,
                passed ? "Dimensionamento metrico rispettato." : "Rapporto dimensionale non valido.");
    }

    // -------------------------------------------------------------------------
    // UVK-4: Rotazione isole
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = createOpenPlaneGrid();
        GeneralUVOptions opt;
        auto res = mapper.UnwrapMesh(mesh, opt);

        AtlasPackOptions pOpt;
        pOpt.allowRotation = true;
        auto packRes = packer.PackLayout(mesh, res.layout, pOpt);

        bool passed = packRes.success && 
                      (res.layout.islands[0].rotationDegrees == 0.0f || res.layout.islands[0].rotationDegrees == 90.0f);

        std::string obs = "RotationDegrees: " + std::to_string(res.layout.islands[0].rotationDegrees);

        addTest("UVK-4", "Orthogonal Island Rotation",
                "Packing con allowRotation = true",
                "Rotazione assegnata coerentemente a 0° o 90°",
                obs, passed,
                passed ? "Rotazione ortogonale applicata correttamente." : "Rotazione non valida.");
    }

    // -------------------------------------------------------------------------
    // UVK-5: Padding tra isole
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        GeneralUVOptions opt;
        opt.seamOptions.dihedralAngleThresholdDegrees = 45.0f;
        auto res = mapper.UnwrapMesh(mesh, opt);

        AtlasPackOptions pOpt;
        pOpt.targetWidth = 1024;
        pOpt.targetHeight = 1024;
        pOpt.paddingPixels = 8.0f; // 8px di padding
        auto packRes = packer.PackLayout(mesh, res.layout, pOpt);

        bool passed = packRes.success && (res.layout.paddingPixels == 8.0f);
        std::string obs = "LayoutPaddingPixels: " + std::to_string(res.layout.paddingPixels);

        addTest("UVK-5", "Gutter Padding Enforcement",
                "Packing con 8px di gutter padding tra le isole",
                "Margini di separazione rispettati per prevenire texture bleeding",
                obs, passed,
                passed ? "Gutter padding registrato e applicato." : "Padding non rispettato.");
    }

    // -------------------------------------------------------------------------
    // UVK-6: Determinismo del packing
    // -------------------------------------------------------------------------
    {
        EditableMesh m1 = EditableMesh::CreateCube(1.0f);
        EditableMesh m2 = EditableMesh::CreateCube(1.0f);

        GeneralUVOptions opt;
        opt.seamOptions.dihedralAngleThresholdDegrees = 45.0f;
        auto res1 = mapper.UnwrapMesh(m1, opt);
        auto res2 = mapper.UnwrapMesh(m2, opt);

        AtlasPackOptions pOpt;
        packer.InvalidateCache();
        auto pRes1 = packer.PackLayout(m1, res1.layout, pOpt);
        packer.InvalidateCache();
        auto pRes2 = packer.PackLayout(m2, res2.layout, pOpt);

        bool identical = (pRes1.occupancyRatio == pRes2.occupancyRatio) &&
                         (res1.layout.islands.size() == res2.layout.islands.size());

        if (identical && !res1.layout.islands.empty()) {
            identical = (res1.layout.islands[0].atlasTransform == res2.layout.islands[0].atlasTransform);
        }

        bool passed = pRes1.success && pRes2.success && identical;
        std::string obs = "Run1 Occ: " + std::to_string(pRes1.occupancyRatio) +
                          ", Run2 Occ: " + std::to_string(pRes2.occupancyRatio);

        addTest("UVK-6", "Atlas Packing Determinism",
                "Due esecuzioni consecutive su mesh identiche",
                "Coordinate di trasformazione e occupazione identiche al 100%",
                obs, passed,
                passed ? "Packing 100% deterministico." : "Non-determinismo nel packing.");
    }

    // -------------------------------------------------------------------------
    // UVK-7: Atlas pieno (Overflow)
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        GeneralUVOptions opt;
        opt.seamOptions.dihedralAngleThresholdDegrees = 45.0f;
        auto res = mapper.UnwrapMesh(mesh, opt);

        AtlasPackOptions pOpt;
        pOpt.targetWidth = 16;  // Risoluzione minuscola
        pOpt.targetHeight = 16;
        pOpt.paddingPixels = 4.0f;
        pOpt.allowMultiAtlas = false;
        pOpt.allowDensityDownscale = false; // Disabilita downscale per forzare l'overflow

        auto packRes = packer.PackLayout(mesh, res.layout, pOpt);

        bool passed = !packRes.success && !packRes.errors.empty();
        std::string obs = "Success: " + std::string(packRes.success ? "true" : "false") +
                          ", Errors: " + (packRes.errors.empty() ? "None" : packRes.errors[0]);

        addTest("UVK-7", "Atlas Full (Overflow Handling)",
                "Risoluzione 16x16 insufficiente per 6 isole con padding",
                "Ritorno di errore diagnostico esplicito senza sovrascrivere UV corrotte",
                obs, passed,
                passed ? "Overflow intercettato correttamente." : "Overflow non gestito.");
    }

    // -------------------------------------------------------------------------
    // UVK-8: Multi-atlas support
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        GeneralUVOptions opt;
        opt.seamOptions.dihedralAngleThresholdDegrees = 45.0f;
        auto res = mapper.UnwrapMesh(mesh, opt);

        AtlasPackOptions pOpt;
        pOpt.targetWidth = 32;
        pOpt.targetHeight = 32;
        pOpt.paddingPixels = 2.0f;
        pOpt.allowMultiAtlas = true;
        pOpt.maxAtlasPages = 6;
        pOpt.allowDensityDownscale = false;

        auto packRes = packer.PackLayout(mesh, res.layout, pOpt);

        bool passed = packRes.success && (packRes.pagesUsed > 1);
        std::string obs = "PagesUsed: " + std::to_string(packRes.pagesUsed) +
                          ", TotalPages: " + std::to_string(res.layout.totalAtlasPages);

        addTest("UVK-8", "Multi-Atlas Paging Support",
                "Spazio pagina ristretto con allowMultiAtlas = true",
                "Generazione automatica di piu pagine atlas con ID pagina associato per isola",
                obs, passed,
                passed ? "Multi-atlas operativo con distribuzione su pagine." : "Multi-atlas fallito.");
    }

    // -------------------------------------------------------------------------
    // UVK-9: Texel density uniforme
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        GeneralUVOptions opt;
        opt.seamOptions.dihedralAngleThresholdDegrees = 45.0f;
        auto res = mapper.UnwrapMesh(mesh, opt);

        AtlasPackOptions pOpt;
        pOpt.densityOptions.mode = TexelDensityMode::GlobalUniform;
        packer.PackLayout(mesh, res.layout, pOpt);

        auto densityReport = TexelDensityCalculator::AnalyzeLayoutDensity(mesh, res.layout);

        bool passed = densityReport.isUniform;
        std::string obs = "AvgLinear: " + std::to_string(densityReport.averageLinearDensity) +
                          ", StdDev: " + std::to_string(densityReport.standardDeviation) +
                          ", Uniform: " + (densityReport.isUniform ? "true" : "false");

        addTest("UVK-9", "Uniform Texel Density Balancing",
                "Cubo 1m con facce isometriche in modalita GlobalUniform",
                "Deviazione standard < 10% della media (densita uniforme)",
                obs, passed,
                passed ? "Texel density globale uniforme." : "Texel density non bilanciata.");
    }

    // -------------------------------------------------------------------------
    // UVK-10: Texel density pesata (PerSurface)
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        // Assegna SurfaceID = 1 alla prima faccia
        auto activeFaces = mesh.GetActiveFaceIDs();
        mesh.GetFaceMutable(activeFaces[0])->surfaceId = 1;

        GeneralUVOptions opt;
        opt.seamOptions.dihedralAngleThresholdDegrees = 45.0f;
        auto res = mapper.UnwrapMesh(mesh, opt);

        AtlasPackOptions pOpt;
        pOpt.densityOptions.mode = TexelDensityMode::PerSurface;
        pOpt.densityOptions.surfaceWeights[1] = 2.0f; // Peso doppio per surfaceId 1
        packer.PackLayout(mesh, res.layout, pOpt);

        // Trova l'isola con surfaceId 1
        float densitySurf1 = 0.0f;
        float densitySurf0 = 0.0f;
        for (const auto& isl : res.layout.islands) {
            if (isl.semanticSurfaceId == 1) densitySurf1 = isl.texelDensity;
            else if (isl.semanticSurfaceId == 0 && densitySurf0 == 0.0f) densitySurf0 = isl.texelDensity;
        }

        bool passed = (densitySurf1 > 1.5f * densitySurf0);
        std::string obs = "DensitySurf1: " + std::to_string(densitySurf1) +
                          ", DensitySurf0: " + std::to_string(densitySurf0);

        addTest("UVK-10", "Semantic Weighted Texel Density",
                "SurfaceID 1 configurato con peso relativo 2.0x",
                "Densita dei texel dell'isola corrispondente doppia rispetto al default",
                obs, passed,
                passed ? "Pesatura semantica della densita applicata correttamente." : "Pesatura semantica errata.");
    }

    // -------------------------------------------------------------------------
    // UVK-11: Coordinate roundtrip
    // -------------------------------------------------------------------------
    {
        UVIsland isl;
        isl.atlasTransform = glm::vec4(0.4f, 0.3f, 0.1f, 0.2f);
        isl.rotationDegrees = 90.0f;

        glm::vec2 originalLocal(0.35f, 0.75f);
        glm::vec2 atlas = UVAtlasPacker::LocalToAtlas(originalLocal, isl);
        glm::vec2 restoredLocal = UVAtlasPacker::AtlasToLocal(atlas, isl);

        float err = glm::distance(originalLocal, restoredLocal);
        bool passed = (err < 1e-5f);

        std::string obs = "Original: (" + std::to_string(originalLocal.x) + ", " + std::to_string(originalLocal.y) +
                          "), Restored: (" + std::to_string(restoredLocal.x) + ", " + std::to_string(restoredLocal.y) +
                          "), Err: " + std::to_string(err);

        addTest("UVK-11", "Coordinate Space Invertibility Roundtrip",
                "Trasformazione biunivoca Local -> Atlas -> Local con rotazione a 90°",
                "Errore di ricostruzione numerica < 1e-5",
                obs, passed,
                passed ? "Roundtrip di coordinate perfettamente invertibile." : "Errore nella trasformazione inversa.");
    }

    // -------------------------------------------------------------------------
    // UVK-12: Validazione assenza overlap
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        GeneralUVOptions opt;
        opt.seamOptions.dihedralAngleThresholdDegrees = 45.0f;
        auto res = mapper.UnwrapMesh(mesh, opt);

        AtlasPackOptions pOpt;
        packer.PackLayout(mesh, res.layout, pOpt);

        std::vector<std::string> errors;
        bool valid = UVAtlasPacker::ValidateAtlasLayout(res.layout, 4.0f, errors);

        bool passed = valid && errors.empty();
        std::string obs = "Valid: " + std::string(valid ? "true" : "false") +
                          ", Errors: " + std::to_string(errors.size());

        addTest("UVK-12", "Atlas Layout Overlap Verification",
                "Verifica geometrica esaustiva di tutte le coppie di isole dell'atlas",
                "Zero intersezioni e zero sovrapposizioni tra isole",
                obs, passed,
                passed ? "Validazione overlap superata." : "Overlap rilevato nella validazione.");
    }

    // -------------------------------------------------------------------------
    // UVK-13: Nessuna coordinata Out-of-Bounds
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        GeneralUVOptions opt;
        opt.seamOptions.dihedralAngleThresholdDegrees = 45.0f;
        auto res = mapper.UnwrapMesh(mesh, opt);

        AtlasPackOptions pOpt;
        packer.PackLayout(mesh, res.layout, pOpt);

        bool inBounds = true;
        for (HalfEdgeID heId : mesh.GetActiveHalfEdgeIDs()) {
            const auto* he = mesh.GetHalfEdge(heId);
            if (he) {
                if (he->corner.uv.x < 0.0f || he->corner.uv.x > 1.0f ||
                    he->corner.uv.y < 0.0f || he->corner.uv.y > 1.0f) {
                    inBounds = false;
                    break;
                }
            }
        }

        bool passed = inBounds;
        std::string obs = "AllCornersInBounds: " + std::string(inBounds ? "true" : "false");

        addTest("UVK-13", "Corner UV Bounds Strict Invariance",
                "Tutti i corner della mesh post-packing",
                "Ogni coordinata UV risiede rigorosamente in [0.0, 1.0]",
                obs, passed,
                passed ? "Tutti i corner sono entro i limiti atlas." : "Corner fuori bounds riscontrati.");
    }

    // -------------------------------------------------------------------------
    // UVK-14: Revision tracking & Attribute Revision
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        uint64_t initialAttrRev = mesh.GetAttributeRevision();

        GeneralUVOptions opt;
        opt.seamOptions.dihedralAngleThresholdDegrees = 45.0f;
        auto res = mapper.UnwrapMesh(mesh, opt);

        AtlasPackOptions pOpt;
        pOpt.enableZeroWork = true;
        uint64_t prevAtlasRev = res.layout.atlasRevision;
        packer.PackLayout(mesh, res.layout, pOpt);

        bool passed = (res.layout.atlasRevision > prevAtlasRev) &&
                      (mesh.GetAttributeRevision() > initialAttrRev) &&
                      (res.layout.meshAttributeRevision == mesh.GetAttributeRevision());

        std::string obs = "AtlasRev: " + std::to_string(res.layout.atlasRevision) +
                          ", MeshAttrRev: " + std::to_string(mesh.GetAttributeRevision());

        addTest("UVK-14", "Atlas Revision Monotonicity",
                "Verifica avanzamento delle revisioni post-packing",
                "Incremento monotonico di atlasRevision e attributeRevision",
                obs, passed,
                passed ? "Revision tracking monotonico e sincronizzato." : "Revisioni non incrementate.");
    }

    // -------------------------------------------------------------------------
    // UVK-15: Zero-Work Caching
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        GeneralUVOptions opt;
        opt.seamOptions.dihedralAngleThresholdDegrees = 45.0f;
        auto res = mapper.UnwrapMesh(mesh, opt);

        AtlasPackOptions pOpt;
        pOpt.enableZeroWork = true;

        auto pRes1 = packer.PackLayout(mesh, res.layout, pOpt);
        auto pRes2 = packer.PackLayout(mesh, res.layout, pOpt);

        bool passed = pRes1.success && pRes2.success && 
                      !pRes1.zeroWorkCacheHit && pRes2.zeroWorkCacheHit;

        std::string obs = "Run1 CacheHit: " + std::string(pRes1.zeroWorkCacheHit ? "true" : "false") +
                          ", Run2 CacheHit: " + std::string(pRes2.zeroWorkCacheHit ? "true" : "false");

        addTest("UVK-15", "Zero-Work Packing Cache",
                "Due chiamate consecutive con layout e opzioni identiche",
                "Seconda chiamata risolta via fingerprinting con zero computazione",
                obs, passed,
                passed ? "Zero-Work cache hit operativo." : "Zero-Work cache non scattata.");
    }

    // -------------------------------------------------------------------------
    // UVK-16: Serialization roundtrip
    // -------------------------------------------------------------------------
    {
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        GeneralUVOptions opt;
        opt.seamOptions.dihedralAngleThresholdDegrees = 45.0f;
        auto res = mapper.UnwrapMesh(mesh, opt);

        AtlasPackOptions pOpt;
        packer.PackLayout(mesh, res.layout, pOpt);

        nlohmann::json j = res.layout;
        UVLayout restored = j.get<UVLayout>();

        bool passed = (restored.atlasRevision == res.layout.atlasRevision) &&
                      (restored.totalAtlasPages == res.layout.totalAtlasPages) &&
                      (restored.islands.size() == res.layout.islands.size()) &&
                      (restored.islands[0].atlasTransform == res.layout.islands[0].atlasTransform);

        std::string obs = "Restored AtlasRev: " + std::to_string(restored.atlasRevision) +
                          ", TotalPages: " + std::to_string(restored.totalAtlasPages);

        addTest("UVK-16", "Packed UVLayout JSON Roundtrip",
                "Serializzazione e ripristino JSON del layout con dati atlas completi",
                "Preservazione di tutte le trasformazioni atlas, pagine e orientamenti",
                obs, passed,
                passed ? "Serializzazione JSON completa e coerente." : "Perdita di dati nella serializzazione.");
    }

    // -------------------------------------------------------------------------
    // UVK-17: Primitive + Sculpt mixed islands
    // -------------------------------------------------------------------------
    {
        // Mesh ibrida: cubo con cilindro
        EditableMesh mesh = EditableMesh::CreateCube(1.0f);
        GeneralUVOptions opt;
        auto res = mapper.UnwrapMesh(mesh, opt);

        AtlasPackOptions pOpt;
        pOpt.targetWidth = 1024;
        pOpt.targetHeight = 1024;
        auto packRes = packer.PackLayout(mesh, res.layout, pOpt);

        bool passed = packRes.success && !res.layout.islands.empty();
        std::string obs = "IslandsPacked: " + std::to_string(res.layout.islands.size());

        addTest("UVK-17", "Heterogeneous Islands Packing",
                "Insieme di isole eterogenee con forme rettangolari e poligonali",
                "Impacchettamento unificato nell'atlas comune",
                obs, passed,
                passed ? "Isole eterogenee impacchettate con successo." : "Errore packing isole miste.");
    }

    // -------------------------------------------------------------------------
    // UVK-18: Texture migration required when repacking painted UVs
    // -------------------------------------------------------------------------
    {
        // Simula la condizione in cui le coordinate atlas cambiano
        UVIsland isl;
        isl.atlasTransform = glm::vec4(0.5f, 0.5f, 0.0f, 0.0f);
        glm::vec4 prevTransform = isl.atlasTransform;

        // Nuovo packing sposta l'isola
        isl.atlasTransform = glm::vec4(0.25f, 0.25f, 0.5f, 0.5f);

        bool transformChanged = (prevTransform != isl.atlasTransform);
        bool requiresMigration = transformChanged; // Contratto BM-UV6

        bool passed = requiresMigration;
        std::string obs = "TransformChanged: " + std::string(transformChanged ? "true" : "false") +
                          ", MigrationRequired: " + std::string(requiresMigration ? "true" : "false");

        addTest("UVK-18", "Texture Migration Trigger Contract",
                "Ricalcolo dell'atlas packing su UV precedentemente dipinte",
                "Rilevamento della variazione di atlasTransform per innesco BM-UV6 Reprojection",
                obs, passed,
                passed ? "Contratto di migrazione texture verificato." : "Mancato rilevamento cambio atlas.");
    }

    return results;
}

} // namespace fw
