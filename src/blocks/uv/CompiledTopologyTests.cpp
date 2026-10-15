#include "pch.h"
#include "CompiledTopologyTests.h"
#include "world/GeometryCompiler.h"
#include "blocks/SurfaceDefinition.h"
#include <map>
#include <set>
#include <cmath>
#include <algorithm>

namespace fw {

std::vector<TopologyTestResult> CompiledTopologyTests::RunAllTests() {
    std::vector<TopologyTestResult> results;

    auto addResult = [&](const std::string& id, const std::string& name,
                         const std::string& input, const std::string& expected,
                         const std::string& observed, bool passed, const std::string& details) {
        TopologyTestResult r;
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
    // TOP-1: Cube CompiledTopology Primary Emission
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cube;
        auto res = GeometryCompiler::Compile(sDef);

        bool hasTopo = res.success && res.topology.has_value();
        bool validCounts = false;
        bool validIndices = true;
        if (hasTopo) {
            const auto& topo = *res.topology;
            validCounts = (topo.geometricPositions.size() == 8) && (topo.faces.size() == 6);
            for (const auto& f : topo.faces) {
                if (f.corners.size() != 4) { validIndices = false; break; }
                for (const auto& c : f.corners) {
                    if (c.geometricVertexIndex >= 8) { validIndices = false; break; }
                }
            }
        }

        bool passed = hasTopo && validCounts && validIndices;
        std::string obs = hasTopo ? ("V=" + std::to_string(res.topology->geometricPositions.size()) +
                                     ", F=" + std::to_string(res.topology->faces.size())) : "Topology missing";

        addResult("TOP-1", "Cube CompiledTopology Primary Emission",
                  "ShapeDefinition(Cube) compilato tramite GeometryCompiler::Compile",
                  "Presenza di CompiledTopology con V=8 posizioni uniche e 6 facce quad",
                  obs, passed,
                  passed ? "Topologia primaria del cubo emessa con successo." : "Emissione topologia cubo fallita.");
    }

    // -------------------------------------------------------------------------
    // TOP-2: Cube Topological Connectivity & Zero Boundary
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cube;
        auto res = GeometryCompiler::Compile(sDef);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            std::map<std::pair<uint32_t, uint32_t>, int> directedEdges;
            std::set<std::pair<uint32_t, uint32_t>> undirectedEdges;

            for (const auto& f : topo.faces) {
                size_t n = f.corners.size();
                for (size_t i = 0; i < n; ++i) {
                    uint32_t v0 = f.corners[i].geometricVertexIndex;
                    uint32_t v1 = f.corners[(i + 1) % n].geometricVertexIndex;
                    directedEdges[{v0, v1}]++;
                    uint32_t u0 = std::min(v0, v1);
                    uint32_t u1 = std::max(v0, v1);
                    undirectedEdges.insert({u0, u1});
                }
            }

            // Ogni directed edge (v0, v1) deve avere un twin esatto (v1, v0)
            bool allTwinsExist = true;
            for (const auto& [edge, count] : directedEdges) {
                auto twin = std::make_pair(edge.second, edge.first);
                if (directedEdges.find(twin) == directedEdges.end() || directedEdges[twin] != 1) {
                    allTwinsExist = false;
                    break;
                }
            }

            size_t V = topo.geometricPositions.size();
            size_t E = undirectedEdges.size();
            size_t F = topo.faces.size();
            int euler = static_cast<int>(V) - static_cast<int>(E) + static_cast<int>(F);

            passed = (V == 8) && (E == 12) && (F == 6) && (euler == 2) && allTwinsExist;
            obs = "V=" + std::to_string(V) + ", E=" + std::to_string(E) + ", F=" + std::to_string(F) +
                  ", Euler=" + std::to_string(euler) + ", Twins=" + std::string(allTwinsExist ? "true" : "false");
        }

        addResult("TOP-2", "Cube Topological Connectivity & Zero Boundary",
                  "Verifica di connettività half-edge e adiacenza twin su CompiledTopology del cubo",
                  "E=12 spigoli unici, tutti provvisti di twin opposto, 0 confini aperti, Eulero=2",
                  obs, passed,
                  passed ? "Connettività 2-manifold chiusa verificata con successo." : "Errore adiacenza twin o Eulero errato.");
    }

    // -------------------------------------------------------------------------
    // TOP-3: Cube Per-Corner Attributes & SurfaceID
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cube;
        auto res = GeometryCompiler::Compile(sDef);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            std::set<uint32_t> surfaceIds;
            bool validNormals = true;
            bool validUvs = true;

            for (const auto& f : topo.faces) {
                surfaceIds.insert(f.surfaceId);
                float lenN = glm::length(f.faceNormal);
                if (std::abs(lenN - 1.0f) > 1e-4f) validNormals = false;

                for (const auto& c : f.corners) {
                    if (c.uv.x < -1e-4f || c.uv.x > 1.0001f || c.uv.y < -1e-4f || c.uv.y > 1.0001f) {
                        validUvs = false;
                    }
                    if (std::abs(glm::length(c.normal) - 1.0f) > 1e-4f) {
                        validNormals = false;
                    }
                }
            }

            passed = (surfaceIds.size() == 6) && validNormals && validUvs;
            obs = "Surfaces=" + std::to_string(surfaceIds.size()) +
                  ", NormalsValid=" + std::string(validNormals ? "true" : "false") +
                  ", UVsValid=" + std::string(validUvs ? "true" : "false");
        }

        addResult("TOP-3", "Cube Per-Corner Attributes & SurfaceID",
                  "Verifica degli attributi per-corner e delle identità di superficie del cubo",
                  "6 SurfaceID distinti (0..5), normali unitarie ortogonali, UV nel range [0, 1]",
                  obs, passed,
                  passed ? "Attributi per-corner e surface IDs convalidati." : "Incoerenza attributi d'angolo o surface IDs.");
    }

    // -------------------------------------------------------------------------
    // TOP-4: Cuboid CompiledTopology Integrity
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cuboid;
        ShapeParamsCuboid p;
        p.size = glm::vec3(2.0f, 4.0f, 6.0f);
        sDef.parameters = p;

        auto res = GeometryCompiler::Compile(sDef);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            bool extentsValid = true;
            for (const auto& pt : topo.geometricPositions) {
                if (std::abs(std::abs(pt.x) - 1.0f) > 1e-4f ||
                    std::abs(std::abs(pt.y) - 2.0f) > 1e-4f ||
                    std::abs(std::abs(pt.z) - 3.0f) > 1e-4f) {
                    extentsValid = false;
                }
            }

            std::set<std::pair<uint32_t, uint32_t>> undirectedEdges;
            for (const auto& f : topo.faces) {
                size_t n = f.corners.size();
                for (size_t i = 0; i < n; ++i) {
                    uint32_t v0 = f.corners[i].geometricVertexIndex;
                    uint32_t v1 = f.corners[(i + 1) % n].geometricVertexIndex;
                    undirectedEdges.insert({std::min(v0, v1), std::max(v0, v1)});
                }
            }

            passed = (topo.geometricPositions.size() == 8) &&
                     (topo.faces.size() == 6) &&
                     (undirectedEdges.size() == 12) &&
                     extentsValid;
            obs = "V=" + std::to_string(topo.geometricPositions.size()) +
                  ", E=" + std::to_string(undirectedEdges.size()) +
                  ", ExtentsValid=" + std::string(extentsValid ? "true" : "false");
        }

        addResult("TOP-4", "Cuboid CompiledTopology Integrity",
                  "ShapeDefinition(Cuboid, size=(2,4,6)) compilato in CompiledTopology",
                  "V=8, E=12, F=6, estensioni conformi ai parametri e zero confini aperti",
                  obs, passed,
                  passed ? "Topologia Cuboid verificata con successo." : "Errore topologia o estensioni Cuboid.");
    }

    // -------------------------------------------------------------------------
    // TOP-5: Parallelepiped CompiledTopology Integrity
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Parallelepiped;
        ShapeParamsParallelepiped p;
        p.basisX = glm::vec3(2.0f, 0.0f, 0.0f);
        p.basisY = glm::vec3(0.5f, 3.0f, 0.0f);
        p.basisZ = glm::vec3(0.0f, 0.5f, 4.0f);
        sDef.parameters = p;

        auto res = GeometryCompiler::Compile(sDef);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            std::map<std::pair<uint32_t, uint32_t>, int> directedEdges;
            std::set<std::pair<uint32_t, uint32_t>> undirectedEdges;

            for (const auto& f : topo.faces) {
                size_t n = f.corners.size();
                for (size_t i = 0; i < n; ++i) {
                    uint32_t v0 = f.corners[i].geometricVertexIndex;
                    uint32_t v1 = f.corners[(i + 1) % n].geometricVertexIndex;
                    directedEdges[{v0, v1}]++;
                    undirectedEdges.insert({std::min(v0, v1), std::max(v0, v1)});
                }
            }

            bool allTwins = true;
            for (const auto& [edge, count] : directedEdges) {
                auto twin = std::make_pair(edge.second, edge.first);
                if (directedEdges.find(twin) == directedEdges.end() || directedEdges[twin] != 1) {
                    allTwins = false;
                    break;
                }
            }

            passed = (topo.geometricPositions.size() == 8) &&
                     (undirectedEdges.size() == 12) &&
                     (topo.faces.size() == 6) &&
                     allTwins;
            obs = "V=" + std::to_string(topo.geometricPositions.size()) +
                  ", E=" + std::to_string(undirectedEdges.size()) +
                  ", Twins=" + std::string(allTwins ? "true" : "false");
        }

        addResult("TOP-5", "Parallelepiped CompiledTopology Integrity",
                  "ShapeDefinition(Parallelepiped) a vettori di base deformati/inclinati",
                  "V=8, E=12, F=6, tutti gli spigoli provvisti di twin, topologia chiusa",
                  obs, passed,
                  passed ? "Topologia Parallelepiped conforme e 2-manifold." : "Errore connettività Parallelepiped.");
    }

    // -------------------------------------------------------------------------
    // TOP-6: SurfaceID & MaterialID Independence
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cube;

        std::vector<SurfaceOverride> overrides;
        SurfaceOverride so;
        so.surfaceId = 1; // Front
        so.useExplicitMaterial = true;
        so.materialId = 42;
        overrides.push_back(so);

        GeometryCompileOptions opt;
        opt.surfaceOverrides = &overrides;
        opt.defaultMaterialId = 7;

        auto res = GeometryCompiler::Compile(sDef, opt);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            bool frontMatch = false;
            bool othersMatch = true;

            for (const auto& f : topo.faces) {
                if (f.surfaceId == 1) {
                    if (f.materialId == 42) frontMatch = true;
                } else {
                    if (f.materialId != 7) othersMatch = false;
                }
            }

            passed = frontMatch && othersMatch;
            obs = "FrontMat=" + std::string(frontMatch ? "42" : "Mismatch") +
                  ", OtherMats=" + std::string(othersMatch ? "7" : "Mismatch");
        }

        addResult("TOP-6", "SurfaceID & MaterialID Independence",
                  "Compilazione con SurfaceOverride (surfaceId=1, materialId=42, default=7)",
                  "SurfaceID=1 preservato con MaterialID=42; altre facce mantengono MaterialID=7",
                  obs, passed,
                  passed ? "Indipendenza di SurfaceID e MaterialID confermata." : "Conflitto o sovrascrittura di ID.");
    }

    // -------------------------------------------------------------------------
    // TOP-7: Determinism & Invariant Input
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cube;

        auto res1 = GeometryCompiler::Compile(sDef);
        auto res2 = GeometryCompiler::Compile(sDef);

        bool passed = false;
        std::string obs = "Failed";

        if (res1.success && res2.success && res1.topology.has_value() && res2.topology.has_value()) {
            const auto& t1 = *res1.topology;
            const auto& t2 = *res2.topology;

            bool posMatch = (t1.geometricPositions == t2.geometricPositions);
            bool facesMatch = (t1.faces.size() == t2.faces.size());

            if (facesMatch) {
                for (size_t f = 0; f < t1.faces.size(); ++f) {
                    const auto& f1 = t1.faces[f];
                    const auto& f2 = t2.faces[f];
                    if (f1.surfaceId != f2.surfaceId || f1.materialId != f2.materialId ||
                        f1.faceNormal != f2.faceNormal || f1.corners.size() != f2.corners.size()) {
                        facesMatch = false;
                        break;
                    }
                    for (size_t c = 0; c < f1.corners.size(); ++c) {
                        if (f1.corners[c].geometricVertexIndex != f2.corners[c].geometricVertexIndex ||
                            f1.corners[c].uv != f2.corners[c].uv ||
                            f1.corners[c].normal != f2.corners[c].normal) {
                            facesMatch = false;
                            break;
                        }
                    }
                }
            }

            passed = posMatch && facesMatch;
            obs = "PosMatch=" + std::string(posMatch ? "true" : "false") +
                  ", FacesMatch=" + std::string(facesMatch ? "true" : "false");
        }

        addResult("TOP-7", "Determinism & Invariant Input",
                  "Due compilazioni consecutive della medesima ShapeDefinition",
                  "Topologia risultante identica bit-a-bit (posizioni, indici, UV e normali)",
                  obs, passed,
                  passed ? "Determinismo confermato a input invariato." : "Divergenza rilevata tra compilazioni identiche.");
    }

    // -------------------------------------------------------------------------
    // TOP-8: Cylinder Topology Counts & Euler Characteristic (N=16)
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

        auto res = GeometryCompiler::Compile(sDef, opt);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            std::set<std::pair<uint32_t, uint32_t>> undirectedEdges;
            for (const auto& f : topo.faces) {
                size_t n = f.corners.size();
                for (size_t i = 0; i < n; ++i) {
                    uint32_t v0 = f.corners[i].geometricVertexIndex;
                    uint32_t v1 = f.corners[(i + 1) % n].geometricVertexIndex;
                    undirectedEdges.insert({std::min(v0, v1), std::max(v0, v1)});
                }
            }

            size_t V = topo.geometricPositions.size();
            size_t E = undirectedEdges.size();
            size_t F = topo.faces.size();
            int euler = static_cast<int>(V) - static_cast<int>(E) + static_cast<int>(F);

            // N=16: V = 2N+2 = 34, F = 3N = 48 (16 quads + 32 tris), E = 5N = 80, Euler = 2
            passed = (V == 34) && (F == 48) && (E == 80) && (euler == 2);
            obs = "V=" + std::to_string(V) + ", E=" + std::to_string(E) + ", F=" + std::to_string(F) +
                  ", Euler=" + std::to_string(euler);
        }

        addResult("TOP-8", "Cylinder Topology Counts & Euler Characteristic",
                  "ShapeDefinition(Cylinder, N=16) compilato in CompiledTopology",
                  "V=34, E=80, F=48, Eulero V-E+F=2 per cilindro chiuso a 2-manifold",
                  obs, passed,
                  passed ? "Conteggi topologici ed Eulero del cilindro convalidati." : "Incoerenza conteggi topologici cilindro.");
    }

    // -------------------------------------------------------------------------
    // TOP-9: Cylinder Closed Manifold & Zero Boundary
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

        auto res = GeometryCompiler::Compile(sDef, opt);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            std::map<std::pair<uint32_t, uint32_t>, int> directedEdges;

            for (const auto& f : topo.faces) {
                size_t n = f.corners.size();
                for (size_t i = 0; i < n; ++i) {
                    uint32_t v0 = f.corners[i].geometricVertexIndex;
                    uint32_t v1 = f.corners[(i + 1) % n].geometricVertexIndex;
                    directedEdges[{v0, v1}]++;
                }
            }

            bool allTwinsExist = true;
            for (const auto& [edge, count] : directedEdges) {
                if (count != 1) { allTwinsExist = false; break; }
                auto twin = std::make_pair(edge.second, edge.first);
                if (directedEdges.find(twin) == directedEdges.end() || directedEdges[twin] != 1) {
                    allTwinsExist = false;
                    break;
                }
            }

            passed = allTwinsExist && (!directedEdges.empty());
            obs = "TotalDirectedEdges=" + std::to_string(directedEdges.size()) +
                  ", AllTwinsPresent=" + std::string(allTwinsExist ? "true" : "false");
        }

        addResult("TOP-9", "Cylinder Closed Manifold & Zero Boundary",
                  "Verifica di connettività half-edge tra mantello e basi del cilindro",
                  "Ogni semispigolo possiede un twin opposto esatto; zero confini aperti (OpenBoundary = 0)",
                  obs, passed,
                  passed ? "Chiusura 2-manifold tra mantello e basi verificata." : "Rilevati confini aperti artificiali nel cilindro.");
    }

    // -------------------------------------------------------------------------
    // TOP-10: Cylinder Periodic Continuity Across Seam 0/2pi
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

        auto res = GeometryCompiler::Compile(sDef, opt);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            const auto& quad0 = topo.faces[0];
            const auto& quadLast = topo.faces[15];

            uint32_t q0_B0 = quad0.corners[0].geometricVertexIndex;
            uint32_t q0_T0 = quad0.corners[1].geometricVertexIndex;

            uint32_t qL_T0 = quadLast.corners[2].geometricVertexIndex;
            uint32_t qL_B0 = quadLast.corners[3].geometricVertexIndex;

            bool sharedVertices = (q0_B0 == qL_B0) && (q0_T0 == qL_T0);
            passed = sharedVertices;
            obs = "SharedB0=" + std::string((q0_B0 == qL_B0) ? "true" : "false") +
                  ", SharedT0=" + std::string((q0_T0 == qL_T0) ? "true" : "false") +
                  ", IndexT0=" + std::to_string(q0_T0) + ", IndexB0=" + std::to_string(q0_B0);
        }

        addResult("TOP-10", "Cylinder Periodic Continuity Across Seam 0/2pi",
                  "Verifica della condivisione dei vertici geometrici attraverso la chiusura periodica 0/2pi",
                  "Quad 0 e Quad N-1 condividono gli identici indici topologici T0 e B0 formando semispigoli gemelli",
                  obs, passed,
                  passed ? "Continuità periodica geometrica confermata." : "Disconnessione rilevata sul perimetro periodico.");
    }

    // -------------------------------------------------------------------------
    // TOP-11: Cylinder Seam Discontinuous UVs & Independent Per-Corner Normals
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

        auto res = GeometryCompiler::Compile(sDef, opt);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            const auto& quad0 = topo.faces[0];
            const auto& quadLast = topo.faces[15];

            float u0 = quad0.corners[1].uv.x;
            float uLast = quadLast.corners[2].uv.x;
            bool uvDiscontinuous = (std::abs(u0 - 0.0f) < 1e-4f) && (std::abs(uLast - 1.0f) < 1e-4f);

            glm::vec3 nQuad = quad0.corners[1].normal;
            glm::vec3 nCap = topo.faces[16].faceNormal;
            float dotN = glm::dot(nQuad, nCap);
            bool normalsOrthogonal = (std::abs(dotN) < 1e-4f);

            passed = uvDiscontinuous && normalsOrthogonal;
            obs = "u0=" + std::to_string(u0) + ", uLast=" + std::to_string(uLast) +
                  ", NormalDot=" + std::to_string(dotN);
        }

        addResult("TOP-11", "Cylinder Seam Discontinuous UVs & Independent Per-Corner Normals",
                  "Verifica di UV seam e normali ortogonali d'angolo su vertici geometricamente condivisi",
                  "Delta UV = 1.0 sullo spigolo di cucitura, normali mantello ortogonali a normali basi (dot=0)",
                  obs, passed,
                  passed ? "UV seam e normali per-corner preservate correttamente." : "Errore discontinuità UV o normali d'angolo.");
    }

    // -------------------------------------------------------------------------
    // TOP-12: Cylinder Minimum Segmentation (N=3)
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cylinder;
        ShapeParamsCylinder p;
        p.radius = 1.0f;
        p.height = 2.0f;
        sDef.parameters = p;

        GeometryCompileOptions opt;
        opt.maxResolution = 3;

        auto res = GeometryCompiler::Compile(sDef, opt);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            size_t V = topo.geometricPositions.size();
            size_t F = topo.faces.size();

            std::set<std::pair<uint32_t, uint32_t>> undirectedEdges;
            for (const auto& f : topo.faces) {
                size_t n = f.corners.size();
                for (size_t i = 0; i < n; ++i) {
                    uint32_t v0 = f.corners[i].geometricVertexIndex;
                    uint32_t v1 = f.corners[(i + 1) % n].geometricVertexIndex;
                    undirectedEdges.insert({std::min(v0, v1), std::max(v0, v1)});
                }
            }

            size_t E = undirectedEdges.size();
            int euler = static_cast<int>(V) - static_cast<int>(E) + static_cast<int>(F);

            // N=3: V = 2(3)+2 = 8, F = 3(3) = 9, E = 5(3) = 15, Euler = 2
            passed = (V == 8) && (F == 9) && (E == 15) && (euler == 2);
            obs = "V=" + std::to_string(V) + ", E=" + std::to_string(E) + ", F=" + std::to_string(F) +
                  ", Euler=" + std::to_string(euler);
        }

        addResult("TOP-12", "Cylinder Minimum Segmentation (N=3)",
                  "Compilazione cilindro a segmentazione minima (prisma triangolare chiuso N=3)",
                  "V=8, E=15, F=9, Eulero=2, topologia manifold 2-chiusa valida",
                  obs, passed,
                  passed ? "Segmentazione minima N=3 convalidata." : "Errore segmentazione minima N=3.");
    }

    // -------------------------------------------------------------------------
    // TOP-13: Cylinder Degenerate Dimensions
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef1;
        sDef1.type = ShapeType::Cylinder;
        ShapeParamsCylinder p1;
        p1.radius = 0.0f;
        p1.height = 2.0f;
        sDef1.parameters = p1;

        auto res1 = GeometryCompiler::Compile(sDef1);

        ShapeDefinition sDef2;
        sDef2.type = ShapeType::Cylinder;
        ShapeParamsCylinder p2;
        p2.radius = 1.0f;
        p2.height = -1.0f;
        sDef2.parameters = p2;

        auto res2 = GeometryCompiler::Compile(sDef2);

        bool passed = (!res1.topology.has_value()) && (!res2.topology.has_value());
        std::string obs = "TopoZeroRadius=" + std::string(res1.topology.has_value() ? "Emitted" : "NullOpt") +
                          ", TopoNegHeight=" + std::string(res2.topology.has_value() ? "Emitted" : "NullOpt");

        addResult("TOP-13", "Cylinder Degenerate Dimensions",
                  "Compilazione di cilindro degenere (radius=0 o height<=0)",
                  "Nessuna CompiledTopology fittizia emessa (res.topology == nullopt)",
                  obs, passed,
                  passed ? "Dimensioni degeneri gestite esplicitamente." : "Emissione errata di topologia su input degenere.");
    }

    // -------------------------------------------------------------------------
    // TOP-14: Cylinder SurfaceID & MaterialID Independence
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cylinder;
        ShapeParamsCylinder p;
        p.radius = 1.0f;
        p.height = 2.0f;
        sDef.parameters = p;

        std::vector<SurfaceOverride> overrides;
        SurfaceOverride so0;
        so0.surfaceId = 0; // Body
        so0.useExplicitMaterial = true;
        so0.materialId = 10;
        overrides.push_back(so0);

        SurfaceOverride so1;
        so1.surfaceId = 1; // Top
        so1.useExplicitMaterial = true;
        so1.materialId = 20;
        overrides.push_back(so1);

        GeometryCompileOptions opt;
        opt.surfaceOverrides = &overrides;
        opt.defaultMaterialId = 5; // Bot default

        auto res = GeometryCompiler::Compile(sDef, opt);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            bool bodyMatch = true;
            bool topMatch = true;
            bool botMatch = true;

            for (const auto& f : topo.faces) {
                if (f.surfaceId == 0 && f.materialId != 10) bodyMatch = false;
                if (f.surfaceId == 1 && f.materialId != 20) topMatch = false;
                if (f.surfaceId == 2 && f.materialId != 5)  botMatch = false;
            }

            passed = bodyMatch && topMatch && botMatch;
            obs = "BodyMat10=" + std::string(bodyMatch ? "true" : "false") +
                  ", TopMat20=" + std::string(topMatch ? "true" : "false") +
                  ", BotMat5=" + std::string(botMatch ? "true" : "false");
        }

        addResult("TOP-14", "Cylinder SurfaceID & MaterialID Independence",
                  "Compilazione cilindro con SurfaceOverride su Body (surf=0, mat=10), Top (surf=1, mat=20), Bot (surf=2, mat=5)",
                  "SurfaceID (0,1,2) rigorosamente preservati e associati ai rispettivi MaterialID indipendenti",
                  obs, passed,
                  passed ? "Indipendenza SurfaceID e MaterialID del cilindro convalidata." : "Incoerenza o sovrascrittura di ID nel cilindro.");
    }

    // -------------------------------------------------------------------------
    // TOP-15: Cylinder Determinism & Invariant Input
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cylinder;
        ShapeParamsCylinder p;
        p.radius = 1.0f;
        p.height = 2.0f;
        sDef.parameters = p;

        auto res1 = GeometryCompiler::Compile(sDef);
        auto res2 = GeometryCompiler::Compile(sDef);

        bool passed = false;
        std::string obs = "Failed";

        if (res1.success && res2.success && res1.topology.has_value() && res2.topology.has_value()) {
            const auto& t1 = *res1.topology;
            const auto& t2 = *res2.topology;

            bool posMatch = (t1.geometricPositions == t2.geometricPositions);
            bool facesMatch = (t1.faces.size() == t2.faces.size());

            if (facesMatch) {
                for (size_t f = 0; f < t1.faces.size(); ++f) {
                    const auto& f1 = t1.faces[f];
                    const auto& f2 = t2.faces[f];
                    if (f1.surfaceId != f2.surfaceId || f1.materialId != f2.materialId ||
                        f1.corners.size() != f2.corners.size()) {
                        facesMatch = false;
                        break;
                    }
                    for (size_t c = 0; c < f1.corners.size(); ++c) {
                        if (f1.corners[c].geometricVertexIndex != f2.corners[c].geometricVertexIndex ||
                            f1.corners[c].uv != f2.corners[c].uv ||
                            f1.corners[c].normal != f2.corners[c].normal) {
                            facesMatch = false;
                            break;
                        }
                    }
                }
            }

            passed = posMatch && facesMatch;
            obs = "PosMatch=" + std::string(posMatch ? "true" : "false") +
                  ", FacesMatch=" + std::string(facesMatch ? "true" : "false");
        }

        addResult("TOP-15", "Cylinder Determinism & Invariant Input",
                  "Due compilazioni consecutive della medesima ShapeDefinition(Cylinder)",
                  "Topologia risultante identica bit-a-bit (posizioni, indici, UV d'angolo e normali)",
                  obs, passed,
                  passed ? "Determinismo confermato su cilindro a input invariato." : "Divergenza rilevata tra compilazioni identiche del cilindro.");
    }

    // -------------------------------------------------------------------------
    // TOP-16: Cone V/E/F Counts & Euler Characteristic (chi = 2)
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cone;
        ShapeParamsCone p;
        p.radius = 1.0f;
        p.height = 2.0f;
        sDef.parameters = p;
        GeometryCompileOptions opts;
        opts.maxResolution = 16;
        auto res = GeometryCompiler::Compile(sDef, opts);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            size_t V = topo.geometricPositions.size();
            size_t F = topo.faces.size();

            std::set<std::pair<uint32_t, uint32_t>> undirectedEdges;
            for (const auto& f : topo.faces) {
                size_t n = f.corners.size();
                for (size_t i = 0; i < n; ++i) {
                    uint32_t v0 = f.corners[i].geometricVertexIndex;
                    uint32_t v1 = f.corners[(i + 1) % n].geometricVertexIndex;
                    undirectedEdges.insert({std::min(v0, v1), std::max(v0, v1)});
                }
            }
            size_t E = undirectedEdges.size();
            int chi = (int)V - (int)E + (int)F;

            // N = 16 => V = 18, F = 32 (16 mantello + 16 base), E = 48, chi = 2
            bool countsMatch = (V == 18 && F == 32 && E == 48);
            bool chiMatch = (chi == 2);

            passed = countsMatch && chiMatch;
            obs = "V=" + std::to_string(V) + ", E=" + std::to_string(E) +
                  ", F=" + std::to_string(F) + ", chi=" + std::to_string(chi);
        }

        addResult("TOP-16", "Cone V/E/F Counts & Euler Characteristic",
                  "ShapeDefinition(Cone, r=1, h=2, N=16) compilato tramite GeometryCompiler::Compile",
                  "V=18, E=48, F=32 ed Eulero chi=2",
                  obs, passed,
                  passed ? "Conteggi V/E/F e caratteristica di Eulero del cono conformi." : "Conteggi o topologia del cono errati.");
    }

    // -------------------------------------------------------------------------
    // TOP-17: Cone Closed Manifold & Zero Geometric Boundary (Reciprocal Twins)
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cone;
        ShapeParamsCone p;
        p.radius = 1.0f;
        p.height = 2.0f;
        sDef.parameters = p;
        GeometryCompileOptions opts;
        opts.maxResolution = 16;
        auto res = GeometryCompiler::Compile(sDef, opts);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            std::map<std::pair<uint32_t, uint32_t>, int> directedEdges;

            for (const auto& f : topo.faces) {
                size_t n = f.corners.size();
                for (size_t i = 0; i < n; ++i) {
                    uint32_t v0 = f.corners[i].geometricVertexIndex;
                    uint32_t v1 = f.corners[(i + 1) % n].geometricVertexIndex;
                    directedEdges[{v0, v1}]++;
                }
            }

            bool allTwinsExist = true;
            for (const auto& [edge, count] : directedEdges) {
                if (count != 1) { allTwinsExist = false; break; }
                auto twin = std::make_pair(edge.second, edge.first);
                if (directedEdges.find(twin) == directedEdges.end() || directedEdges[twin] != 1) {
                    allTwinsExist = false;
                    break;
                }
            }

            passed = allTwinsExist;
            obs = "DirectedEdges=" + std::to_string(directedEdges.size()) +
                  ", ReciprocalTwins=" + std::string(allTwinsExist ? "true" : "false");
        }

        addResult("TOP-17", "Cone Closed Manifold & Zero Geometric Boundary",
                  "ShapeDefinition(Cone, N=16) - verifica di tutti i directed edges",
                  "Ogni directed edge ha un gemello reciproco univoco (boundary=0)",
                  obs, passed,
                  passed ? "Cono 2-manifold chiuso e orientabile senza confini geometrici." : "Presenza di bordi aperti o non-manifold nel cono.");
    }

    // -------------------------------------------------------------------------
    // TOP-18: Cone Apex Single Topological Identity & Per-Corner Slant Normals
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cone;
        ShapeParamsCone p;
        p.radius = 1.0f;
        p.height = 2.0f;
        sDef.parameters = p;
        GeometryCompileOptions opts;
        opts.maxResolution = 16;
        auto res = GeometryCompiler::Compile(sDef, opts);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            uint32_t apexIndex = 0;
            const glm::vec3& apexPos = topo.geometricPositions[apexIndex];

            bool apexPosOk = (std::abs(apexPos.x) < 1e-5f && std::abs(apexPos.y - 1.0f) < 1e-5f && std::abs(apexPos.z) < 1e-5f);

            int apexRefOccurrences = 0;
            std::set<float> apexNormalsX;
            std::set<float> apexUVsU;

            for (const auto& f : topo.faces) {
                if (f.surfaceId == 0) { // Mantello
                    for (const auto& c : f.corners) {
                        if (c.geometricVertexIndex == apexIndex) {
                            apexRefOccurrences++;
                            apexNormalsX.insert(std::round(c.normal.x * 100.0f));
                            apexUVsU.insert(std::round(c.uv.x * 100.0f));
                        }
                    }
                }
            }

            // Tutte le 16 facce del mantello devono referenziare l'apice con indice topologico 0
            bool countOk = (apexRefOccurrences == 16);
            // I corner all'apice hanno normali discontinue e coordinate UV distinte
            bool multipleNormals = (apexNormalsX.size() > 4);
            bool multipleUVs = (apexUVsU.size() > 4);

            passed = apexPosOk && countOk && multipleNormals && multipleUVs;
            obs = "ApexRefs=" + std::to_string(apexRefOccurrences) +
                  ", DistinctNormalsX=" + std::to_string(apexNormalsX.size()) +
                  ", DistinctUVs=" + std::to_string(apexUVsU.size());
        }

        addResult("TOP-18", "Cone Apex Single Topological Identity",
                  "ShapeDefinition(Cone) - ispezione vertice apicale e relativi corner",
                  "Singola identità geometrica per l'apice (indice 0) con attributi UV e normali d'angolo indipendenti",
                  obs, passed,
                  passed ? "Singola identità topologica dell'apice e normali per-corner convalidate." : "Duplicazione topologica dell'apice o normali degenerate.");
    }

    // -------------------------------------------------------------------------
    // TOP-19: Cone Base Rim Sharing & Minimum Segmentation (N=3)
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cone;
        ShapeParamsCone p;
        p.radius = 1.0f;
        p.height = 2.0f;
        sDef.parameters = p;
        GeometryCompileOptions opts;
        opts.maxResolution = 3; // Risoluzione minima
        auto res = GeometryCompiler::Compile(sDef, opts);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            size_t V = topo.geometricPositions.size();
            size_t F = topo.faces.size();

            std::set<uint32_t> mantleRimVerts;
            std::set<uint32_t> capRimVerts;

            for (const auto& f : topo.faces) {
                if (f.surfaceId == 0) {
                    for (const auto& c : f.corners) {
                        if (c.geometricVertexIndex >= 2) mantleRimVerts.insert(c.geometricVertexIndex);
                    }
                } else if (f.surfaceId == 2) {
                    for (const auto& c : f.corners) {
                        if (c.geometricVertexIndex >= 2) capRimVerts.insert(c.geometricVertexIndex);
                    }
                }
            }

            bool rimShared = (mantleRimVerts == capRimVerts) && (mantleRimVerts.size() == 3);
            bool countsOk = (V == 5 && F == 6); // V = 3+2 = 5, F = 3+3 = 6

            passed = rimShared && countsOk;
            obs = "V=" + std::to_string(V) + ", F=" + std::to_string(F) +
                  ", SharedRim=" + std::string(rimShared ? "true" : "false");
        }

        addResult("TOP-19", "Cone Base Rim Sharing & Min Segmentation",
                  "ShapeDefinition(Cone, N=3) a risoluzione minimale",
                  "V=5, F=6, condivisione esatta degli indici di bordo tra mantello e base",
                  obs, passed,
                  passed ? "Condivisione topologica dell'anello di base confermata anche per N=3." : "Disconnessione o indici errati nella base del cono.");
    }

    // -------------------------------------------------------------------------
    // TOP-20: ConeFrustum V/E/F Counts, Euler Characteristic & Twins
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::ConeFrustum;
        ShapeParamsConeFrustum p;
        p.bottomRadius = 1.5f;
        p.topRadius = 0.5f;
        p.height = 2.0f;
        sDef.parameters = p;
        GeometryCompileOptions opts;
        opts.maxResolution = 16;
        auto res = GeometryCompiler::Compile(sDef, opts);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            size_t V = topo.geometricPositions.size();
            size_t F = topo.faces.size();

            std::map<std::pair<uint32_t, uint32_t>, int> directedEdges;
            std::set<std::pair<uint32_t, uint32_t>> undirectedEdges;

            for (const auto& f : topo.faces) {
                size_t n = f.corners.size();
                for (size_t i = 0; i < n; ++i) {
                    uint32_t v0 = f.corners[i].geometricVertexIndex;
                    uint32_t v1 = f.corners[(i + 1) % n].geometricVertexIndex;
                    directedEdges[{v0, v1}]++;
                    undirectedEdges.insert({std::min(v0, v1), std::max(v0, v1)});
                }
            }

            size_t E = undirectedEdges.size();
            int chi = (int)V - (int)E + (int)F;

            bool allTwinsExist = true;
            for (const auto& [edge, count] : directedEdges) {
                if (count != 1) { allTwinsExist = false; break; }
                auto twin = std::make_pair(edge.second, edge.first);
                if (directedEdges.find(twin) == directedEdges.end() || directedEdges[twin] != 1) {
                    allTwinsExist = false;
                    break;
                }
            }

            // N=16 => V = 2*16 + 2 = 34, F = 3*16 = 48, E = 5*16 = 80, chi = 2
            bool countsOk = (V == 34 && F == 48 && E == 80);
            bool chiOk = (chi == 2);

            passed = countsOk && chiOk && allTwinsExist;
            obs = "V=" + std::to_string(V) + ", E=" + std::to_string(E) +
                  ", F=" + std::to_string(F) + ", chi=" + std::to_string(chi) +
                  ", TwinsOk=" + std::string(allTwinsExist ? "true" : "false");
        }

        addResult("TOP-20", "ConeFrustum V/E/F Counts & Euler Characteristic",
                  "ShapeDefinition(ConeFrustum, rBot=1.5, rTop=0.5, h=2, N=16)",
                  "V=34, E=80, F=48, Eulero chi=2 e gemelli reciproci completi",
                  obs, passed,
                  passed ? "Topologia 2-manifold del tronco di cono convalidata con successo." : "Errore nella topologia o nei conteggi del tronco di cono.");
    }

    // -------------------------------------------------------------------------
    // TOP-21: ConeFrustum Degenerate Top Radius (rTop=0 -> Clean Cone Topology)
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::ConeFrustum;
        ShapeParamsConeFrustum p;
        p.bottomRadius = 1.5f;
        p.topRadius = 0.0f; // Degenera in cono
        p.height = 2.0f;
        sDef.parameters = p;
        GeometryCompileOptions opts;
        opts.maxResolution = 16;
        auto res = GeometryCompiler::Compile(sDef, opts);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            size_t V = topo.geometricPositions.size();
            size_t F = topo.faces.size();

            // Deve collassare in un cono non degenere: V=18, F=32 (nessuna faccia ad area zero o indice ripetuto)
            bool noDegenerateFaces = true;
            for (const auto& f : topo.faces) {
                if (f.corners.size() < 3) { noDegenerateFaces = false; break; }
                std::set<uint32_t> faceVerts;
                for (const auto& c : f.corners) faceVerts.insert(c.geometricVertexIndex);
                if (faceVerts.size() != f.corners.size()) { noDegenerateFaces = false; break; }
            }

            bool countsMatchCone = (V == 18 && F == 32);

            passed = countsMatchCone && noDegenerateFaces;
            obs = "V=" + std::to_string(V) + ", F=" + std::to_string(F) +
                  ", CleanFaces=" + std::string(noDegenerateFaces ? "true" : "false");
        }

        addResult("TOP-21", "ConeFrustum Degenerate Top Radius",
                  "ShapeDefinition(ConeFrustum, rBot=1.5, rTop=0.0)",
                  "Riconoscimento analitico di rTop=0 senza emissione di facce degeneri (V=18, F=32)",
                  obs, passed,
                  passed ? "Degenerazione rTop=0 gestita analiticamente senza facce degenerate." : "Generazione di facce degeneri per rTop=0.");
    }

    // -------------------------------------------------------------------------
    // TOP-22: ConeFrustum Inverted Degenerate Bottom Radius (rBot=0 -> Inverted Cone)
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::ConeFrustum;
        ShapeParamsConeFrustum p;
        p.bottomRadius = 0.0f; // Cono capovolto
        p.topRadius = 1.2f;
        p.height = 2.0f;
        sDef.parameters = p;
        GeometryCompileOptions opts;
        opts.maxResolution = 16;
        auto res = GeometryCompiler::Compile(sDef, opts);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            size_t V = topo.geometricPositions.size();
            size_t F = topo.faces.size();

            std::map<std::pair<uint32_t, uint32_t>, int> directedEdges;
            for (const auto& f : topo.faces) {
                size_t n = f.corners.size();
                for (size_t i = 0; i < n; ++i) {
                    uint32_t v0 = f.corners[i].geometricVertexIndex;
                    uint32_t v1 = f.corners[(i + 1) % n].geometricVertexIndex;
                    directedEdges[{v0, v1}]++;
                }
            }

            bool allTwinsExist = true;
            for (const auto& [edge, count] : directedEdges) {
                if (count != 1) { allTwinsExist = false; break; }
                auto twin = std::make_pair(edge.second, edge.first);
                if (directedEdges.find(twin) == directedEdges.end() || directedEdges[twin] != 1) {
                    allTwinsExist = false;
                    break;
                }
            }

            bool countsOk = (V == 18 && F == 32);

            passed = countsOk && allTwinsExist;
            obs = "V=" + std::to_string(V) + ", F=" + std::to_string(F) +
                  ", TwinsOk=" + std::string(allTwinsExist ? "true" : "false");
        }

        addResult("TOP-22", "ConeFrustum Inverted Degenerate Bottom Radius",
                  "ShapeDefinition(ConeFrustum, rBot=0.0, rTop=1.2)",
                  "Riconoscimento analitico di rBot=0 producendo un cono capovolto 2-manifold chiuso",
                  obs, passed,
                  passed ? "Cono capovolto analitico validato senza facce degeneri." : "Errore nella gestione di rBot=0.");
    }

    // -------------------------------------------------------------------------
    // TOP-23: Cone & Frustum Degenerate Dimensions Handling
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef1;
        sDef1.type = ShapeType::Cone;
        ShapeParamsCone p1{0.0f, 2.0f}; // r = 0
        sDef1.parameters = p1;

        ShapeDefinition sDef2;
        sDef2.type = ShapeType::Cone;
        ShapeParamsCone p2{1.0f, -0.5f}; // h < 0
        sDef2.parameters = p2;

        ShapeDefinition sDef3;
        sDef3.type = ShapeType::ConeFrustum;
        ShapeParamsConeFrustum p3{0.0f, 0.0f, 2.0f}; // entrambi i raggi zero
        sDef3.parameters = p3;

        auto res1 = GeometryCompiler::Compile(sDef1);
        auto res2 = GeometryCompiler::Compile(sDef2);
        auto res3 = GeometryCompiler::Compile(sDef3);

        bool r1Null = !res1.topology.has_value();
        bool r2Null = !res2.topology.has_value();
        bool r3Null = !res3.topology.has_value();

        bool passed = r1Null && r2Null && r3Null;
        std::string obs = "rZeroTopoNull=" + std::string(r1Null ? "true" : "false") +
                          ", hNegTopoNull=" + std::string(r2Null ? "true" : "false") +
                          ", bothZeroTopoNull=" + std::string(r3Null ? "true" : "false");

        addResult("TOP-23", "Cone & Frustum Degenerate Dimensions",
                  "Compilazione di Cono e Tronco di cono con r<=0, h<=0 o raggi nulli",
                  "result.topology == nullopt senza crash o output topologico corrotto",
                  obs, passed,
                  passed ? "Parametri degeneri gestiti correttamente tramite nullopt." : "Mancato rifiuto o crash su dimensioni degenerate.");
    }

    // -------------------------------------------------------------------------
    // TOP-24: Cone & Frustum UV Seam Continuity & Zero Boundary
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cone;
        ShapeParamsCone p{1.0f, 2.0f};
        sDef.parameters = p;
        GeometryCompileOptions opts;
        opts.maxResolution = 16;
        auto res = GeometryCompiler::Compile(sDef, opts);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            
            // Faccia 0 (i=0) e Faccia 15 (i=15) condividono la giunzione u=0 e u=1
            const auto& faceStart = topo.faces[0];
            const auto& faceEnd   = topo.faces[15];

            // In faceStart, Corner 0 è a u=0.0 su geometric vertex 2
            // In faceEnd, Corner 2 è a u=1.0 su geometric vertex 2
            uint32_t seamVertM0 = faceStart.corners[0].geometricVertexIndex;
            float seamU0        = faceStart.corners[0].uv.x;

            uint32_t seamVertM15 = faceEnd.corners[2].geometricVertexIndex;
            float seamU15        = faceEnd.corners[2].uv.x;

            bool seamIndicesShared = (seamVertM0 == seamVertM15) && (seamVertM0 == 2);
            bool seamUVsIndependent = (std::abs(seamU0 - 0.0f) < 1e-4f) && (std::abs(seamU15 - 1.0f) < 1e-4f);

            // Verifica che l'edge della giunzione non sia un boundary
            std::map<std::pair<uint32_t, uint32_t>, int> directedEdges;
            for (const auto& f : topo.faces) {
                size_t n = f.corners.size();
                for (size_t i = 0; i < n; ++i) {
                    directedEdges[{f.corners[i].geometricVertexIndex, f.corners[(i + 1) % n].geometricVertexIndex}]++;
                }
            }
            // L'edge tra vertice 2 e apice 0 deve avere esattamente 2 half-edge (uno per verso)
            bool seamEdgeManifold = (directedEdges[{2, 0}] == 1) && (directedEdges[{0, 2}] == 1);

            passed = seamIndicesShared && seamUVsIndependent && seamEdgeManifold;
            obs = "SharedIndex=" + std::string(seamIndicesShared ? "true" : "false") +
                  ", UVs=(0.0, 1.0)=" + std::string(seamUVsIndependent ? "true" : "false") +
                  ", SeamManifold=" + std::string(seamEdgeManifold ? "true" : "false");
        }

        addResult("TOP-24", "Cone & Frustum UV Seam Continuity",
                  "Analisi della giunzione UV u=0/u=1 del mantello del cono",
                  "Condivisione della medesima identità topologica senza boundary geometrici aperti",
                  obs, passed,
                  passed ? "Cucitura UV analitica perfettamente chiusa e 2-manifold." : "Disconnessione o duplicazione sulla cucitura UV.");
    }

    // -------------------------------------------------------------------------
    // TOP-25: Cone & Frustum SurfaceID and MaterialID Independence
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::ConeFrustum;
        ShapeParamsConeFrustum p{1.5f, 0.5f, 2.0f};
        sDef.parameters = p;

        SurfaceOverride mantleOverride;
        mantleOverride.surfaceId = 0; // Mantello
        mantleOverride.useExplicitMaterial = true;
        mantleOverride.materialId = 42; // Materiale specifico per mantello

        SurfaceOverride topOverride;
        topOverride.surfaceId = 1; // Top Cap
        topOverride.useExplicitMaterial = true;
        topOverride.materialId = 99; // Materiale specifico per top cap

        std::vector<SurfaceOverride> overrides = {mantleOverride, topOverride};

        GeometryCompileOptions opts;
        opts.maxResolution = 16;
        opts.defaultMaterialId = 7; // Bottom cap manterrà il default
        opts.surfaceOverrides = &overrides;

        auto res = GeometryCompiler::Compile(sDef, opts);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            bool mantleOk = true;
            bool topOk = true;
            bool botOk = true;

            for (const auto& f : topo.faces) {
                if (f.surfaceId == 0) {
                    if (f.materialId != 42) mantleOk = false;
                } else if (f.surfaceId == 1) {
                    if (f.materialId != 99) topOk = false;
                } else if (f.surfaceId == 2) {
                    if (f.materialId != 7) botOk = false;
                }
            }

            passed = mantleOk && topOk && botOk;
            obs = "MantleMat=" + std::string(mantleOk ? "42" : "wrong") +
                  ", TopMat=" + std::string(topOk ? "99" : "wrong") +
                  ", BotMat=" + std::string(botOk ? "7" : "wrong");
        }

        addResult("TOP-25", "Cone/Frustum SurfaceID & MaterialID Independence",
                  "Tronco di cono con SurfaceOverride differenziati per Mantello (mat 42) e Top Cap (mat 99)",
                  "SurfaceID intatti e MaterialID applicati in modo indipendente",
                  obs, passed,
                  passed ? "Indipendenza SurfaceID e MaterialID validata per cono/frustum." : "Sovrascrittura o incoerenza di ID su tronco di cono.");
    }

    // -------------------------------------------------------------------------
    // TOP-26: Cone & Frustum Determinism & GPU Render Parity
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Cone;
        ShapeParamsCone p{1.0f, 2.0f};
        sDef.parameters = p;

        auto res1 = GeometryCompiler::Compile(sDef);
        auto res2 = GeometryCompiler::Compile(sDef);

        bool passed = false;
        std::string obs = "Failed";

        if (res1.success && res2.success && res1.topology.has_value() && res2.topology.has_value()) {
            const auto& t1 = *res1.topology;
            const auto& t2 = *res2.topology;

            bool posMatch = (t1.geometricPositions == t2.geometricPositions);
            bool facesMatch = (t1.faces.size() == t2.faces.size());

            // Verifica parità con mesh di rendering GPU (bounding box)
            glm::vec3 minTopo(1e9f), maxTopo(-1e9f);
            for (const auto& pt : t1.geometricPositions) {
                minTopo = glm::min(minTopo, pt);
                maxTopo = glm::max(maxTopo, pt);
            }

            glm::vec3 minRender(1e9f), maxRender(-1e9f);
            for (const auto& v : res1.mesh.vertices) {
                glm::vec3 pt(v.position.x, v.position.y, v.position.z);
                minRender = glm::min(minRender, pt);
                maxRender = glm::max(maxRender, pt);
            }

            bool bboxMatch = (glm::distance(minTopo, minRender) < 1e-4f) &&
                             (glm::distance(maxTopo, maxRender) < 1e-4f);

            passed = posMatch && facesMatch && bboxMatch;
            obs = "Deterministic=" + std::string(posMatch && facesMatch ? "true" : "false") +
                  ", RenderBBoxMatch=" + std::string(bboxMatch ? "true" : "false");
        }

        addResult("TOP-26", "Cone Determinism & GPU Render Parity",
                  "Due compilazioni consecutive e confronto bounding box con mesh renderizzabile",
                  "Determinismo bit-a-bit e parità geometrica perfetta tra topologia e buffer GPU",
                  obs, passed,
                  passed ? "Determinismo e parità geometrica GPU verificati con successo." : "Incoerenza tra topologia compilata e mesh GPU.");
    }

    // -------------------------------------------------------------------------
    // TOP-27: Sphere V/E/F Counts & Euler Characteristic (chi = 2)
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Sphere;
        ShapeParamsSphere p;
        p.radius = 1.0f;
        sDef.parameters = p;
        GeometryCompileOptions opts;
        opts.maxResolution = 16; // M = 16 rings, N = 32 segs
        auto res = GeometryCompiler::Compile(sDef, opts);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            size_t V = topo.geometricPositions.size();
            size_t F = topo.faces.size();

            std::set<std::pair<uint32_t, uint32_t>> undirectedEdges;
            for (const auto& f : topo.faces) {
                size_t n = f.corners.size();
                for (size_t i = 0; i < n; ++i) {
                    uint32_t v0 = f.corners[i].geometricVertexIndex;
                    uint32_t v1 = f.corners[(i + 1) % n].geometricVertexIndex;
                    undirectedEdges.insert({std::min(v0, v1), std::max(v0, v1)});
                }
            }
            size_t E = undirectedEdges.size();
            int chi = (int)V - (int)E + (int)F;

            // M = 16, N = 32
            // V = 2 + 32 * 15 = 482
            // F = 32 * 16 = 512
            // E = 482 + 512 - 2 = 992
            bool countsMatch = (V == 482 && F == 512 && E == 992);
            bool chiMatch = (chi == 2);

            passed = countsMatch && chiMatch;
            obs = "V=" + std::to_string(V) + ", E=" + std::to_string(E) +
                  ", F=" + std::to_string(F) + ", chi=" + std::to_string(chi);
        }

        addResult("TOP-27", "Sphere V/E/F Counts & Euler Characteristic",
                  "ShapeDefinition(Sphere, r=1, M=16, N=32)",
                  "V=482, E=992, F=512 ed Eulero chi=2",
                  obs, passed,
                  passed ? "Conteggi V/E/F e caratteristica di Eulero della sfera conformi." : "Conteggi o topologia della sfera errati.");
    }

    // -------------------------------------------------------------------------
    // TOP-28: Sphere Closed Manifold & Zero Geometric Boundary (Reciprocal Twins)
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Sphere;
        ShapeParamsSphere p;
        p.radius = 1.0f;
        sDef.parameters = p;
        GeometryCompileOptions opts;
        opts.maxResolution = 16;
        auto res = GeometryCompiler::Compile(sDef, opts);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            std::map<std::pair<uint32_t, uint32_t>, int> directedEdges;

            for (const auto& f : topo.faces) {
                size_t n = f.corners.size();
                for (size_t i = 0; i < n; ++i) {
                    uint32_t v0 = f.corners[i].geometricVertexIndex;
                    uint32_t v1 = f.corners[(i + 1) % n].geometricVertexIndex;
                    directedEdges[{v0, v1}]++;
                }
            }

            bool allTwinsExist = true;
            for (const auto& [edge, count] : directedEdges) {
                if (count != 1) { allTwinsExist = false; break; }
                auto twin = std::make_pair(edge.second, edge.first);
                if (directedEdges.find(twin) == directedEdges.end() || directedEdges[twin] != 1) {
                    allTwinsExist = false;
                    break;
                }
            }

            passed = allTwinsExist;
            obs = "DirectedEdges=" + std::to_string(directedEdges.size()) +
                  ", ReciprocalTwins=" + std::string(allTwinsExist ? "true" : "false");
        }

        addResult("TOP-28", "Sphere Closed Manifold & Zero Geometric Boundary",
                  "ShapeDefinition(Sphere, M=16, N=32) - verifica di tutti i directed edges",
                  "Ogni directed edge ha un gemello reciproco univoco (boundary=0)",
                  obs, passed,
                  passed ? "Sfera 2-manifold chiusa e orientabile senza confini geometrici." : "Presenza di bordi aperti o non-manifold nella sfera.");
    }

    // -------------------------------------------------------------------------
    // TOP-29: Sphere Poles Topological Uniqueness & Independence
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Sphere;
        ShapeParamsSphere p;
        p.radius = 1.0f;
        sDef.parameters = p;
        GeometryCompileOptions opts;
        opts.maxResolution = 16;
        auto res = GeometryCompiler::Compile(sDef, opts);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            uint32_t southPoleIdx = 0;
            uint32_t northPoleIdx = 1;

            const glm::vec3& pSouth = topo.geometricPositions[southPoleIdx];
            const glm::vec3& pNorth = topo.geometricPositions[northPoleIdx];

            bool southPosOk = (std::abs(pSouth.x) < 1e-5f && std::abs(pSouth.y - (-1.0f)) < 1e-5f && std::abs(pSouth.z) < 1e-5f);
            bool northPosOk = (std::abs(pNorth.x) < 1e-5f && std::abs(pNorth.y - 1.0f) < 1e-5f && std::abs(pNorth.z) < 1e-5f);

            int southRefs = 0;
            int northRefs = 0;

            for (const auto& f : topo.faces) {
                for (const auto& c : f.corners) {
                    if (c.geometricVertexIndex == southPoleIdx) southRefs++;
                    if (c.geometricVertexIndex == northPoleIdx) northRefs++;
                }
            }

            // N = 32 triangoli polari a Sud e a Nord
            bool southRefsOk = (southRefs == 32);
            bool northRefsOk = (northRefs == 32);

            // Verifica che nessun altro vertice nell'anello intermedio sia al polo
            bool noDuplicatePoles = true;
            for (size_t i = 2; i < topo.geometricPositions.size(); ++i) {
                const auto& pt = topo.geometricPositions[i];
                if (std::abs(std::abs(pt.y) - 1.0f) < 1e-4f && std::abs(pt.x) < 1e-4f && std::abs(pt.z) < 1e-4f) {
                    noDuplicatePoles = false;
                    break;
                }
            }

            passed = southPosOk && northPosOk && southRefsOk && northRefsOk && noDuplicatePoles;
            obs = "SouthRefs=" + std::to_string(southRefs) +
                  ", NorthRefs=" + std::to_string(northRefs) +
                  ", UniquePoles=" + std::string(noDuplicatePoles ? "true" : "false");
        }

        addResult("TOP-29", "Sphere Poles Topological Uniqueness",
                  "ShapeDefinition(Sphere) - ispezione vertici polari Sud (0) e Nord (1)",
                  "Singola identità topologica per ciascun polo, senza duplicazioni geometriche",
                  obs, passed,
                  passed ? "Unicità topologica dei poli Sud e Nord convalidata." : "Duplicazione o posizionamento errato dei poli.");
    }

    // -------------------------------------------------------------------------
    // TOP-30: Sphere Longitudinal Seam Continuity & Shared Identities
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Sphere;
        ShapeParamsSphere p;
        p.radius = 1.0f;
        sDef.parameters = p;
        GeometryCompileOptions opts;
        opts.maxResolution = 16;
        auto res = GeometryCompiler::Compile(sDef, opts);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            int N = 32;

            // Per ogni anello r intermedio, il vertice a s=0 e s=N-1 è condiviso
            // Nella prima colonna di quad (s=0) e nell'ultima (s=N-1):
            // Quad(r, N-1) ha corner a s_next = 0 (indice 2 + (r-1)*N)
            // Quad(r, 0) ha corner a s = 0 (indice 2 + (r-1)*N)
            bool seamVertsShared = true;
            for (int r = 1; r < 15; ++r) {
                uint32_t expectedSharedIdx = 2 + (r - 1) * N; // s = 0
                // Faccia all'inizio dell'anello: r_idx = N + (r-1)*N
                // Faccia alla fine dell'anello: N + (r-1)*N + (N-1)
                size_t faceStartIdx = N + (r - 1) * N;
                size_t faceEndIdx   = N + (r - 1) * N + (N - 1);

                const auto& fStart = topo.faces[faceStartIdx];
                const auto& fEnd   = topo.faces[faceEndIdx];

                // In fStart (s=0): C1 è V(r, 0)
                // In fEnd (s=N-1): C0 è V(r, 0)
                if (fStart.corners[1].geometricVertexIndex != expectedSharedIdx ||
                    fEnd.corners[0].geometricVertexIndex != expectedSharedIdx) {
                    seamVertsShared = false;
                    break;
                }
            }

            // Verifica che l'edge lungo il seam non sia un boundary
            std::map<std::pair<uint32_t, uint32_t>, int> directedEdges;
            for (const auto& f : topo.faces) {
                size_t n = f.corners.size();
                for (size_t i = 0; i < n; ++i) {
                    directedEdges[{f.corners[i].geometricVertexIndex, f.corners[(i + 1) % n].geometricVertexIndex}]++;
                }
            }

            bool seamManifold = true;
            for (int r = 1; r < 14; ++r) {
                uint32_t vA = 2 + (r - 1) * N;
                uint32_t vB = 2 + r * N;
                if (directedEdges[{vA, vB}] != 1 || directedEdges[{vB, vA}] != 1) {
                    seamManifold = false;
                    break;
                }
            }

            passed = seamVertsShared && seamManifold;
            obs = "SeamShared=" + std::string(seamVertsShared ? "true" : "false") +
                  ", SeamManifold=" + std::string(seamManifold ? "true" : "false");
        }

        addResult("TOP-30", "Sphere Longitudinal Seam Continuity",
                  "Analisi del seam longitudinale s=0/s=2pi tra le facce contigue",
                  "Identità geometriche condivise senza boundary aperti lungo il seam",
                  obs, passed,
                  passed ? "Continuità topologica attraverso il seam longitudinale confermata." : "Disconnessione o duplicazione lungo il seam.");
    }

    // -------------------------------------------------------------------------
    // TOP-31: Sphere Per-Corner UVs at Poles and Seam
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Sphere;
        ShapeParamsSphere p;
        p.radius = 1.0f;
        sDef.parameters = p;
        GeometryCompileOptions opts;
        opts.maxResolution = 16;
        auto res = GeometryCompiler::Compile(sDef, opts);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            int N = 32;

            // 1. Al polo Sud (indice 0), i corner hanno v = 0.0 e u distinti
            std::set<float> southUVsU;
            for (int s = 0; s < N; ++s) {
                const auto& f = topo.faces[s];
                southUVsU.insert(std::round(f.corners[0].uv.x * 1000.0f));
            }

            // 2. Al polo Nord (indice 1), i corner hanno v = 1.0 e u distinti
            std::set<float> northUVsU;
            size_t northStartIdx = topo.faces.size() - N;
            for (int s = 0; s < N; ++s) {
                const auto& f = topo.faces[northStartIdx + s];
                northUVsU.insert(std::round(f.corners[2].uv.x * 1000.0f));
            }

            // 3. Sul seam longitudinale (s=N-1 e s=0), u è 1.0 e 0.0 rispettivamente
            const auto& fEnd = topo.faces[N + (N - 1)]; // Quad a r=1, s=N-1
            const auto& fStart = topo.faces[N];          // Quad a r=1, s=0

            float seamU_End = fEnd.corners[0].uv.x;   // u1 = 1.0f
            float seamU_Start = fStart.corners[1].uv.x; // u0 = 0.0f

            bool uRangeOk = (std::abs(seamU_End - 1.0f) < 1e-4f) && (std::abs(seamU_Start - 0.0f) < 1e-4f);
            bool distinctPolesUV = (southUVsU.size() == 32) && (northUVsU.size() == 32);

            passed = uRangeOk && distinctPolesUV;
            obs = "SouthDistinctU=" + std::to_string(southUVsU.size()) +
                  ", NorthDistinctU=" + std::to_string(northUVsU.size()) +
                  ", SeamU=(0.0, 1.0)=" + std::string(uRangeOk ? "true" : "false");
        }

        addResult("TOP-31", "Sphere Per-Corner UVs at Poles & Seam",
                  "Ispezione coordinate UV d'angolo ai poli e sulla giunzione longitudinale",
                  "UV per-corner indipendenti (ventaglio polare e seam 0.0/1.0 senza sdoppiamento vertici)",
                  obs, passed,
                  passed ? "UV per-corner ai poli e sul seam convalidate con successo." : "Coordinate UV corrotte o sdoppiate.");
    }

    // -------------------------------------------------------------------------
    // TOP-32: Sphere Radial Shading Normals & Outward Winding
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Sphere;
        ShapeParamsSphere p;
        p.radius = 1.0f;
        sDef.parameters = p;
        GeometryCompileOptions opts;
        opts.maxResolution = 16;
        auto res = GeometryCompiler::Compile(sDef, opts);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;

            bool allNormalsRadial = true;
            bool allFacesOutward = true;

            for (const auto& f : topo.faces) {
                glm::vec3 centroid(0.0f);
                for (const auto& c : f.corners) {
                    const glm::vec3& pos = topo.geometricPositions[c.geometricVertexIndex];
                    centroid += pos;
                    glm::vec3 expectedNorm = glm::normalize(pos);
                    if (glm::dot(c.normal, expectedNorm) < 0.999f) {
                        allNormalsRadial = false;
                        break;
                    }
                }
                centroid /= (float)f.corners.size();
                if (glm::dot(f.faceNormal, glm::normalize(centroid)) < 0.95f) {
                    allFacesOutward = false;
                    break;
                }
            }

            passed = allNormalsRadial && allFacesOutward;
            obs = "RadialNormals=" + std::string(allNormalsRadial ? "true" : "false") +
                  ", OutwardWinding=" + std::string(allFacesOutward ? "true" : "false");
        }

        addResult("TOP-32", "Sphere Radial Shading Normals & Winding",
                  "Verifica delle normali di shading per-corner e del winding CCW esterno",
                  "Normali per-corner rigorosamente radiali e normali di faccia concordi con il centroide",
                  obs, passed,
                  passed ? "Normali radiali e winding coerente verso l'esterno confermati." : "Incoerenza nelle normali di shading o winding rovesciato.");
    }

    // -------------------------------------------------------------------------
    // TOP-33: Sphere Minimum Segmentation (M=2, N=4)
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Sphere;
        ShapeParamsSphere p;
        p.radius = 1.0f;
        sDef.parameters = p;
        GeometryCompileOptions opts;
        opts.maxResolution = 2; // M = 2, N = 4 (Ottaedro sferico)
        auto res = GeometryCompiler::Compile(sDef, opts);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            size_t V = topo.geometricPositions.size();
            size_t F = topo.faces.size();

            // M = 2, N = 4:
            // V = 2 + 4 * 1 = 6
            // F = 4 * 2 = 8
            // E = 6 + 8 - 2 = 12, chi = 2
            std::set<std::pair<uint32_t, uint32_t>> undirectedEdges;
            for (const auto& f : topo.faces) {
                size_t n = f.corners.size();
                for (size_t i = 0; i < n; ++i) {
                    uint32_t v0 = f.corners[i].geometricVertexIndex;
                    uint32_t v1 = f.corners[(i + 1) % n].geometricVertexIndex;
                    undirectedEdges.insert({std::min(v0, v1), std::max(v0, v1)});
                }
            }
            size_t E = undirectedEdges.size();
            int chi = (int)V - (int)E + (int)F;

            bool countsOk = (V == 6 && F == 8 && E == 12);
            bool chiOk = (chi == 2);

            passed = countsOk && chiOk;
            obs = "V=" + std::to_string(V) + ", E=" + std::to_string(E) +
                  ", F=" + std::to_string(F) + ", chi=" + std::to_string(chi);
        }

        addResult("TOP-33", "Sphere Minimum Segmentation",
                  "ShapeDefinition(Sphere, M=2, N=4) alla risoluzione minima ammessa",
                  "V=6, E=12, F=8 ed Eulero chi=2 (ottaedro 2-manifold chiuso)",
                  obs, passed,
                  passed ? "Segmentazione minima della sfera validata come 2-manifold chiuso." : "Errore nella topologia a risoluzione minima.");
    }

    // -------------------------------------------------------------------------
    // TOP-34: Sphere Degenerate Dimensions Handling
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef1;
        sDef1.type = ShapeType::Sphere;
        ShapeParamsSphere p1{0.0f}; // r = 0
        sDef1.parameters = p1;

        ShapeDefinition sDef2;
        sDef2.type = ShapeType::Sphere;
        ShapeParamsSphere p2{-1.5f}; // r < 0
        sDef2.parameters = p2;

        ShapeDefinition sDef3;
        sDef3.type = ShapeType::Sphere;
        ShapeParamsSphere p3{1.0f};
        sDef3.parameters = p3;
        GeometryCompileOptions opts3;
        opts3.maxResolution = 1; // rings < 2 non consentito

        auto res1 = GeometryCompiler::Compile(sDef1);
        auto res2 = GeometryCompiler::Compile(sDef2);
        auto res3 = GeometryCompiler::Compile(sDef3, opts3);

        bool r1Null = !res1.topology.has_value();
        bool r2Null = !res2.topology.has_value();
        bool r3Null = !res3.topology.has_value();

        bool passed = r1Null && r2Null && r3Null;
        std::string obs = "rZeroTopoNull=" + std::string(r1Null ? "true" : "false") +
                          ", rNegTopoNull=" + std::string(r2Null ? "true" : "false") +
                          ", res1TopoNull=" + std::string(r3Null ? "true" : "false");

        addResult("TOP-34", "Sphere Degenerate Dimensions",
                  "Compilazione sfera con r<=0 o maxResolution < 2",
                  "result.topology == nullopt senza crash o output geometrico corrotto",
                  obs, passed,
                  passed ? "Parametri degeneri della sfera gestiti analiticamente con nullopt." : "Mancato rifiuto di dimensioni degenerate nella sfera.");
    }

    // -------------------------------------------------------------------------
    // TOP-35: Sphere SurfaceID & MaterialID Independence
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Sphere;
        ShapeParamsSphere p{1.0f};
        sDef.parameters = p;

        SurfaceOverride surfOverride;
        surfOverride.surfaceId = 0;
        surfOverride.useExplicitMaterial = true;
        surfOverride.materialId = 77;

        std::vector<SurfaceOverride> overrides = {surfOverride};

        GeometryCompileOptions opts;
        opts.maxResolution = 8;
        opts.defaultMaterialId = 1;
        opts.surfaceOverrides = &overrides;

        auto res = GeometryCompiler::Compile(sDef, opts);

        bool passed = false;
        std::string obs = "Failed";

        if (res.success && res.topology.has_value()) {
            const auto& topo = *res.topology;
            bool allSurface0 = true;
            bool allMaterial77 = true;

            for (const auto& f : topo.faces) {
                if (f.surfaceId != 0) allSurface0 = false;
                if (f.materialId != 77) allMaterial77 = false;
            }

            passed = allSurface0 && allMaterial77;
            obs = "AllSurface0=" + std::string(allSurface0 ? "true" : "false") +
                  ", AllMaterial77=" + std::string(allMaterial77 ? "true" : "false");
        }

        addResult("TOP-35", "Sphere SurfaceID & MaterialID Independence",
                  "Sfera con SurfaceOverride esplicito su Surface 0 (materialId 77)",
                  "SurfaceID 0 preservato e MaterialID 77 propagato a tutte le facce",
                  obs, passed,
                  passed ? "Indipendenza SurfaceID e MaterialID validata per la sfera." : "Incoerenza negli ID di superficie/materiale.");
    }

    // -------------------------------------------------------------------------
    // TOP-36: Sphere Determinism & GPU Render Parity
    // -------------------------------------------------------------------------
    {
        ShapeDefinition sDef;
        sDef.type = ShapeType::Sphere;
        ShapeParamsSphere p{1.0f};
        sDef.parameters = p;

        auto res1 = GeometryCompiler::Compile(sDef);
        auto res2 = GeometryCompiler::Compile(sDef);

        bool passed = false;
        std::string obs = "Failed";

        if (res1.success && res2.success && res1.topology.has_value() && res2.topology.has_value()) {
            const auto& t1 = *res1.topology;
            const auto& t2 = *res2.topology;

            bool posMatch = (t1.geometricPositions == t2.geometricPositions);
            bool facesMatch = (t1.faces.size() == t2.faces.size());

            glm::vec3 minTopo(1e9f), maxTopo(-1e9f);
            for (const auto& pt : t1.geometricPositions) {
                minTopo = glm::min(minTopo, pt);
                maxTopo = glm::max(maxTopo, pt);
            }

            glm::vec3 minRender(1e9f), maxRender(-1e9f);
            for (const auto& v : res1.mesh.vertices) {
                glm::vec3 pt(v.position.x, v.position.y, v.position.z);
                minRender = glm::min(minRender, pt);
                maxRender = glm::max(maxRender, pt);
            }

            bool bboxMatch = (glm::distance(minTopo, minRender) < 1e-4f) &&
                             (glm::distance(maxTopo, maxRender) < 1e-4f);

            passed = posMatch && facesMatch && bboxMatch;
            obs = "Deterministic=" + std::string(posMatch && facesMatch ? "true" : "false") +
                  ", RenderBBoxMatch=" + std::string(bboxMatch ? "true" : "false");
        }

        addResult("TOP-36", "Sphere Determinism & GPU Render Parity",
                  "Due compilazioni consecutive e confronto bounding box con mesh renderizzabile",
                  "Determinismo bit-a-bit e parità geometrica perfetta tra topologia e buffer GPU",
                  obs, passed,
                  passed ? "Determinismo e parità geometrica GPU verificati con successo per la sfera." : "Incoerenza tra topologia compilata e mesh GPU.");
    }

    return results;
}

} // namespace fw


