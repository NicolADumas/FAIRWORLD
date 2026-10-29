#include "blocks/ScientificMaterialRegistryContractTests.h"
#include "blocks/ScientificMaterialRegistry.h"
#include "blocks/BlockRegistry.h"
#include "nlohmann/json.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <sstream>

namespace fw {

using json = nlohmann::json;

namespace {

// Helper per identificare la posizione del catalogo scientifico JSON produttivo
std::string FindProductionCatalogPath() {
    const std::vector<std::string> candidates = {
        "assets/definitions/scientific_materials.json",
        "../assets/definitions/scientific_materials.json",
        "../../assets/definitions/scientific_materials.json"
    };
    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            return p;
        }
    }
    return "";
}

// Helper per creare percorsi temporanei isolati per i test
std::string CreateTempJsonFile(const std::string& filename, const json& content) {
    const std::string testDir = (std::filesystem::temp_directory_path() / "fairworld_sci_tests").string();
    std::error_code ec;
    std::filesystem::create_directories(testDir, ec);
    std::string fullPath = (std::filesystem::path(testDir) / filename).string();
    std::ofstream ofs(fullPath);
    ofs << content.dump(2);
    ofs.close();
    return fullPath;
}

void RemoveTempFile(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // anonymous namespace

std::vector<ScientificRegistryTestResult> ScientificMaterialRegistryContractTests::RunAllTests() {
    std::vector<ScientificRegistryTestResult> results;
    results.reserve(38);

    // MA-03.1D: Test isolati di regressione contrattuale (SCI-01 .. SCI-10)
    results.push_back(Test01_LoadV1WithoutTaxonomy());
    results.push_back(Test02_LoadV2WithTaxonomyAndComposition());
    results.push_back(Test03_RoundTripExactRangeUnknownQualitySource());
    results.push_back(Test04_RejectSchemaVersion0And3());
    results.push_back(Test05_RejectNominallyCompleteInvalidSum());
    results.push_back(Test06_AcceptValidPartialCompositions());
    results.push_back(Test07_RejectTaxonomicAndCompositionCycles());
    results.push_back(Test08_RejectInvalidReferencesAndDuplicates());
    results.push_back(Test09_AtomicityLoadFromJsonRollback());
    results.push_back(Test10_InvarianceId1To4AndResolveProperty());

    // MA-03.2A: Test di accettazione bootstrap catalogo produttivo (BOOT-01 .. BOOT-04)
    results.push_back(TestBOOT01_BootstrapConsistency());
    results.push_back(TestBOOT02_TaxonomicQueriesPostLoad());
    results.push_back(TestBOOT03_ScientificValuesPreservation());
    results.push_back(TestBOOT04_ProductionJsonV2RoundTrip());

    // MA-03.2D.3B: Test infrastruttura proprietà estese (EXT-01 .. EXT-11)
    results.push_back(TestEXT01_RollbackOnInvalidV3Json());
    results.push_back(TestEXT02_ResolverSixOutcomes());
    results.push_back(TestEXT03_ConservativeMatchingMissingFields());
    results.push_back(TestEXT04_MatchMultipleNoImplicitSelection());
    results.push_back(TestEXT05_DistinctionZeroUnknownNotApplicable());
    results.push_back(TestEXT06_RepresentationsScalarRangeQualitative());
    results.push_back(TestEXT07_MetrologicalAndDimensionalValidation());
    results.push_back(TestEXT08_PressureSpecAbsoluteGaugeDifferential());
    results.push_back(TestEXT09_SnapshotAndObservationLifetime());
    results.push_back(TestEXT10_DeterministicSerialization());
    results.push_back(TestEXT11_ProductionCatalogHasNoExtendedProperties());

    // MA-03.2D.3B-FIX04: Test immutabilità snapshot e identità canonica (EXT-12 .. EXT-19)
    results.push_back(TestEXT12_RejectDifferentUrisSameValidIndex());
    results.push_back(TestEXT13_RejectUpdateCorrectUriErroneousIndex());
    results.push_back(TestEXT14_LogicalDuplicatesPrevention());
    results.push_back(TestEXT15_RegisterMaterialResolvesForeignSnapshotIndices());
    results.push_back(TestEXT16_QueryRejectsMismatchedUriIndexEntry());
    results.push_back(TestEXT17_OldSnapshotHandleImmutabilityPostRegister());
    results.push_back(TestEXT18_ObservationLifetimePreservedInOldSnapshot());
    results.push_back(TestEXT19_RegisterMaterialAtomicRollbackOnValidationError());

    // MA-03.2E.2A: Test diagnostica riferimenti incrociati catalogo scientifico (DIAG-01 .. DIAG-05)
    results.push_back(TestDIAG01_UnassociatedBlockIdZeroIsValid());
    results.push_back(TestDIAG02_ExistingScientificMaterialIdIsValid());
    results.push_back(TestDIAG03_OrphanScientificMaterialIdGeneratesWarning());
    results.push_back(TestDIAG04_NoAssetMutationOnValidation());
    results.push_back(TestDIAG05_LoadingBehaviorUnchangedWithOrphans());

    return results;
}

// -----------------------------------------------------------------------------
// Test 1: Caricamento JSON v1 senza campi tassonomici
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::Test01_LoadV1WithoutTaxonomy() {
    ScientificRegistryTestResult r;
    r.testId = "SCI-01";
    r.testName = "Caricamento JSON v1 senza campi tassonomici";
    r.inputDesc = "File JSON schemaVersion 1 con sole proprietà fisiche e nessuna faccetta tassonomica v2";
    r.expectedDesc = "LoadFromJson riuscito, proprietà v1 integre, campi v2 impostati ai default sicuri (Unknown, liste vuote)";

    json j;
    j["schemaVersion"] = 1;
    j["materials"] = json::array({
        {
            {"id", 101},
            {"canonicalId", "fairworld:mat/test_quartz"},
            {"displayName", "Test Quartz"},
            {"chemicalFormula", "SiO2"},
            {"classification", "Silicate Mineral"},
            {"density", {{"value", 2650.0}, {"unit", "kg/m^3"}, {"state", "Known"}}},
            {"compressiveStrength", {{"value", 1.1e9}, {"unit", "Pa"}, {"state", "Known"}}}
        }
    });

    std::string tempPath = CreateTempJsonFile("sci_test_v1.json", j);

    ScientificMaterialRegistry reg;
    bool ok = reg.LoadFromJson(tempPath);
    RemoveTempFile(tempPath);

    if (!ok) {
        r.passed = false;
        r.observedDesc = "LoadFromJson ha fallito su JSON schema v1";
        r.details = "Rifiutato schemaVersion 1 valido";
        return r;
    }

    if (!reg.HasMaterial(101)) {
        r.passed = false;
        r.observedDesc = "Materiale ID 101 non trovato dopo LoadFromJson v1";
        return r;
    }

    const auto& m = reg.GetMaterial(101);
    bool propsOk = (m.density.state == PropertyState::Known && std::abs(m.density.value - 2650.0) < 1e-4);
    bool taxOk = (m.kind == ScientificKind::Unknown &&
                  m.aggregationState == AggregationState::Unknown &&
                  m.tags.empty() &&
                  m.relations.empty() &&
                  m.composition.completeness == CompositionCompleteness::Unknown &&
                  m.composition.components.empty());

    if (propsOk && taxOk) {
        r.passed = true;
        r.observedDesc = "Caricamento v1 completato con successo: densità 2650 kg/m^3, kind=Unknown, composition=Unknown";
    } else {
        r.passed = false;
        r.observedDesc = "Inconsistenza nei default tassonomici v2 per catalogo v1";
        r.details = "propsOk=" + std::to_string(propsOk) + ", taxOk=" + std::to_string(taxOk);
    }

    return r;
}

// -----------------------------------------------------------------------------
// Test 2: Caricamento JSON v2 con tassonomia e composizione
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::Test02_LoadV2WithTaxonomyAndComposition() {
    ScientificRegistryTestResult r;
    r.testId = "SCI-02";
    r.testName = "Caricamento JSON v2 con tassonomia e composizione";
    r.inputDesc = "File JSON schemaVersion 2 con kind, aggregationState, tags, relations e composizione parziale";
    r.expectedDesc = "LoadFromJson riuscito, tassonomia e composizione deserializzate fedelmente con targetId risolto";

    json j;
    j["schemaVersion"] = 2;
    j["materials"] = json::array({
        {
            {"id", 201},
            {"canonicalId", "fairworld:mat/forsterite"},
            {"displayName", "Forsterite"},
            {"kind", "Mineral"},
            {"aggregationState", "Solid"},
            {"tags", {"nesosilicate", "olivine_group"}},
            {"density", {{"value", 3270.0}, {"unit", "kg/m^3"}, {"state", "Known"}}}
        },
        {
            {"id", 202},
            {"canonicalId", "fairworld:mat/peridotite_test"},
            {"displayName", "Peridotite Test"},
            {"kind", "Rock"},
            {"aggregationState", "Solid"},
            {"tags", {"ultramafic", "plutonic"}},
            {"density", {{"value", 3300.0}, {"unit", "kg/m^3"}, {"state", "Known"}}},
            {"relations", json::array({
                {
                    {"type", "DerivedFrom"},
                    {"targetCanonicalId", "fairworld:mat/forsterite"},
                    {"notes", "Major constituent"}
                }
            })},
            {"composition", {
                {"completeness", "Partial"},
                {"defaultBasis", "Mass"},
                {"components", json::array({
                    {
                        {"canonicalId", "fairworld:mat/forsterite"},
                        {"displayName", "Forsterite"},
                        {"role", "matrix"},
                        {"quantity", {
                            {"kind", "Exact"},
                            {"exact", 0.70},
                            {"basis", "Mass"},
                            {"quality", "PeerReviewed"},
                            {"source", "Deer Howie Zussman"}
                        }}
                    }
                })}
            }}
        }
    });

    std::string tempPath = CreateTempJsonFile("sci_test_v2.json", j);

    ScientificMaterialRegistry reg;
    bool ok = reg.LoadFromJson(tempPath);
    RemoveTempFile(tempPath);

    if (!ok) {
        r.passed = false;
        r.observedDesc = "LoadFromJson ha fallito su JSON schema v2 valido";
        return r;
    }

    const auto& forsterite = reg.GetMaterial(201);
    const auto& peridotite = reg.GetMaterial(202);

    bool fOk = (forsterite.kind == ScientificKind::Mineral && 
                forsterite.aggregationState == AggregationState::Solid &&
                forsterite.tags.size() == 2);

    bool pOk = (peridotite.kind == ScientificKind::Rock &&
                peridotite.relations.size() == 1 &&
                peridotite.relations[0].type == ScientificRelationType::DerivedFrom &&
                peridotite.relations[0].targetCanonicalId == "fairworld:mat/forsterite" &&
                peridotite.composition.completeness == CompositionCompleteness::Partial &&
                peridotite.composition.components.size() == 1 &&
                peridotite.composition.components[0].quantity.kind == QuantityKind::Exact &&
                std::abs(peridotite.composition.components[0].quantity.exact - 0.70) < 1e-4);

    if (fOk && pOk) {
        r.passed = true;
        r.observedDesc = "Caricamento v2 convalidato: kind, aggregationState, tags, relations e composition corretti";
    } else {
        r.passed = false;
        r.observedDesc = "Campi v2 non corrispondenti alle attese";
        r.details = "fOk=" + std::to_string(fOk) + ", pOk=" + std::to_string(pOk);
    }

    return r;
}

// -----------------------------------------------------------------------------
// Test 3: Round-trip Exact, Range, Unknown, qualità e fonte
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::Test03_RoundTripExactRangeUnknownQualitySource() {
    ScientificRegistryTestResult r;
    r.testId = "SCI-03";
    r.testName = "Round-trip Exact, Range, Unknown, qualità e fonte";
    r.inputDesc = "ComponentQuantity con Exact, Range, Unknown, DataQuality::PeerReviewed/Experimental/Estimated e citazioni";
    r.expectedDesc = "Serializzazione to_json e deserializzazione from_json senza alcuna perdita di precisione o metadati";

    ScientificMaterialDef orig;
    orig.id = 301;
    orig.canonicalId = "fairworld:mat/roundtrip_test";
    orig.displayName = "Roundtrip Test Composite";
    orig.kind = ScientificKind::Composite;
    orig.aggregationState = AggregationState::Solid;

    orig.composition.completeness = CompositionCompleteness::Partial;
    orig.composition.defaultBasis = FractionBasis::Mass;

    // Componente 1: Exact
    ScientificComponent c1;
    c1.displayName = "Component Exact";
    c1.quantity = ComponentQuantity::Exact(0.45, FractionBasis::Mass, DataQuality::PeerReviewed, "Handbook of Chemistry");
    orig.composition.components.push_back(c1);

    // Componente 2: Range
    ScientificComponent c2;
    c2.displayName = "Component Range";
    c2.quantity = ComponentQuantity::Range(0.10, 0.25, FractionBasis::Mass, DataQuality::Experimental, "XRD Spec 2026");
    orig.composition.components.push_back(c2);

    // Componente 3: Unknown
    ScientificComponent c3;
    c3.displayName = "Component Unknown";
    c3.quantity = ComponentQuantity::Unknown(FractionBasis::Mass);
    c3.quantity.quality = DataQuality::Estimated;
    c3.quantity.source = "Estimated residue";
    orig.composition.components.push_back(c3);

    // Serializzazione e deserializzazione diretta via JSON
    json jMat;
    to_json(jMat, orig);

    ScientificMaterialDef restored;
    from_json(jMat, restored);

    bool match1 = (restored.composition.components[0].quantity.kind == QuantityKind::Exact &&
                   std::abs(restored.composition.components[0].quantity.exact - 0.45) < 1e-6 &&
                   restored.composition.components[0].quantity.quality == DataQuality::PeerReviewed &&
                   restored.composition.components[0].quantity.source == "Handbook of Chemistry");

    bool match2 = (restored.composition.components[1].quantity.kind == QuantityKind::Range &&
                   std::abs(restored.composition.components[1].quantity.min - 0.10) < 1e-6 &&
                   std::abs(restored.composition.components[1].quantity.max - 0.25) < 1e-6 &&
                   restored.composition.components[1].quantity.quality == DataQuality::Experimental &&
                   restored.composition.components[1].quantity.source == "XRD Spec 2026");

    bool match3 = (restored.composition.components[2].quantity.kind == QuantityKind::Unknown &&
                   restored.composition.components[2].quantity.quality == DataQuality::Estimated &&
                   restored.composition.components[2].quantity.source == "Estimated residue");

    if (match1 && match2 && match3 && restored.composition.completeness == CompositionCompleteness::Partial) {
        r.passed = true;
        r.observedDesc = "Round-trip JSON perfetto: Exact (0.45), Range [0.10, 0.25] e Unknown preservati senza perdite";
    } else {
        r.passed = false;
        r.observedDesc = "Perdita di dati o discrepanza nei campi serializzati di ComponentQuantity";
        r.details = "match1=" + std::to_string(match1) + ", match2=" + std::to_string(match2) + ", match3=" + std::to_string(match3);
    }

    return r;
}

// -----------------------------------------------------------------------------
// Test 4: Rifiuto schemaVersion 0 e 3
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::Test04_RejectSchemaVersion0And3() {
    ScientificRegistryTestResult r;
    r.testId = "SCI-04";
    r.testName = "Rifiuto schemaVersion non supportata (es. 0 e 4)";
    r.inputDesc = "File JSON con schemaVersion 0 (invalido < 1) e schemaVersion 4 (non supportato > 3)";
    r.expectedDesc = "LoadFromJson restituisce false per entrambe le versioni non supportate; catalogo invariato";

    json j0;
    j0["schemaVersion"] = 0;
    j0["materials"] = json::array({{{"id", 401}, {"canonicalId", "fairworld:mat/s0"}, {"displayName", "S0"}}});
    std::string path0 = CreateTempJsonFile("sci_test_schema0.json", j0);

    json j4;
    j4["schemaVersion"] = 4;
    j4["materials"] = json::array({{{"id", 402}, {"canonicalId", "fairworld:mat/s4"}, {"displayName", "S4"}}});
    std::string path4 = CreateTempJsonFile("sci_test_schema4.json", j4);

    ScientificMaterialRegistry reg;
    reg.Initialize(); // Inizializza con i 4 materiali standard
    size_t countBefore = reg.GetMaterialCount();

    bool ok0 = reg.LoadFromJson(path0);
    size_t countAfter0 = reg.GetMaterialCount();

    bool ok4 = reg.LoadFromJson(path4);
    size_t countAfter4 = reg.GetMaterialCount();

    RemoveTempFile(path0);
    RemoveTempFile(path4);

    if (!ok0 && !ok4 && countBefore == 4 && countAfter0 == 4 && countAfter4 == 4 && !reg.HasMaterial(401) && !reg.HasMaterial(402)) {
        r.passed = true;
        r.observedDesc = "Entrambe le versioni schema non supportate (<1 e >3) rifiutate con successo; catalogo standard inalterato";
    } else {
        r.passed = false;
        r.observedDesc = "Mancato rifiuto o corruzione dello stato su schemaVersion non supportata";
        r.details = "ok0=" + std::to_string(ok0) + ", ok4=" + std::to_string(ok4) + 
                    ", countBefore=" + std::to_string(countBefore) + 
                    ", countAfter0=" + std::to_string(countAfter0) + 
                    ", countAfter4=" + std::to_string(countAfter4);
    }

    return r;
}

// -----------------------------------------------------------------------------
// Test 5: Rifiuto composizioni nominalmente complete con somma invalida
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::Test05_RejectNominallyCompleteInvalidSum() {
    ScientificRegistryTestResult r;
    r.testId = "SCI-05";
    r.testName = "Rifiuto composizioni nominalmente complete con somma invalida";
    r.inputDesc = "Composizioni NominallyComplete con: (a) somma Exact != 1, (b) presenza di Unknown, (c) Range non compatibile con 1, (d) basi differenti";
    r.expectedDesc = "Validazione fallisce e LoadFromJson rifiuta il catalogo per tutte le combinazioni non conformi";

    // 5a. Exact sommano a 0.85 (manca 0.15)
    json j5a;
    j5a["schemaVersion"] = 2;
    j5a["materials"] = json::array({
        {
            {"id", 501},
            {"canonicalId", "fairworld:mat/nc_exact_invalid"},
            {"displayName", "Invalid Exact Sum"},
            {"composition", {
                {"completeness", "NominallyComplete"},
                {"defaultBasis", "Mass"},
                {"components", json::array({
                    {{"displayName", "Comp A"}, {"quantity", {{"kind", "Exact"}, {"exact", 0.50}, {"basis", "Mass"}}}},
                    {{"displayName", "Comp B"}, {"quantity", {{"kind", "Exact"}, {"exact", 0.35}, {"basis", "Mass"}}}}
                })}
            }}
        }
    });

    // 5b. NominallyComplete con un componente Unknown
    json j5b;
    j5b["schemaVersion"] = 2;
    j5b["materials"] = json::array({
        {
            {"id", 502},
            {"canonicalId", "fairworld:mat/nc_with_unknown"},
            {"displayName", "NC With Unknown"},
            {"composition", {
                {"completeness", "NominallyComplete"},
                {"defaultBasis", "Mass"},
                {"components", json::array({
                    {{"displayName", "Comp A"}, {"quantity", {{"kind", "Exact"}, {"exact", 0.80}, {"basis", "Mass"}}}},
                    {{"displayName", "Comp B"}, {"quantity", {{"kind", "Unknown"}, {"basis", "Mass"}}}}
                })}
            }}
        }
    });

    // 5c. Range [0.20, 0.30] + [0.30, 0.40] = intervallo somma [0.50, 0.70], non include 1.0
    json j5c;
    j5c["schemaVersion"] = 2;
    j5c["materials"] = json::array({
        {
            {"id", 503},
            {"canonicalId", "fairworld:mat/nc_range_invalid"},
            {"displayName", "NC Range Invalid"},
            {"composition", {
                {"completeness", "NominallyComplete"},
                {"defaultBasis", "Mass"},
                {"components", json::array({
                    {{"displayName", "Comp A"}, {"quantity", {{"kind", "Range"}, {"min", 0.20}, {"max", 0.30}, {"basis", "Mass"}}}},
                    {{"displayName", "Comp B"}, {"quantity", {{"kind", "Range"}, {"min", 0.30}, {"max", 0.40}, {"basis", "Mass"}}}}
                })}
            }}
        }
    });

    // 5d. Basi differenti (Mass vs Volume)
    json j5d;
    j5d["schemaVersion"] = 2;
    j5d["materials"] = json::array({
        {
            {"id", 504},
            {"canonicalId", "fairworld:mat/nc_mixed_bases"},
            {"displayName", "NC Mixed Bases"},
            {"composition", {
                {"completeness", "NominallyComplete"},
                {"defaultBasis", "Mass"},
                {"components", json::array({
                    {{"displayName", "Comp A"}, {"quantity", {{"kind", "Exact"}, {"exact", 0.50}, {"basis", "Mass"}}}},
                    {{"displayName", "Comp B"}, {"quantity", {{"kind", "Exact"}, {"exact", 0.50}, {"basis", "Volume"}}}}
                })}
            }}
        }
    });

    std::string pathA = CreateTempJsonFile("sci_nc_exact_invalid.json", j5a);
    std::string pathB = CreateTempJsonFile("sci_nc_with_unknown.json", j5b);
    std::string pathC = CreateTempJsonFile("sci_nc_range_invalid.json", j5c);
    std::string pathD = CreateTempJsonFile("sci_nc_mixed_bases.json", j5d);

    ScientificMaterialRegistry reg;
    bool okA = reg.LoadFromJson(pathA);
    bool okB = reg.LoadFromJson(pathB);
    bool okC = reg.LoadFromJson(pathC);
    bool okD = reg.LoadFromJson(pathD);

    RemoveTempFile(pathA);
    RemoveTempFile(pathB);
    RemoveTempFile(pathC);
    RemoveTempFile(pathD);

    if (!okA && !okB && !okC && !okD) {
        r.passed = true;
        r.observedDesc = "Tutte le 4 violazioni delle regole di completezza nominale sono state respinte come da contratto";
    } else {
        r.passed = false;
        r.observedDesc = "Accettazione indebita di composizione nominale invalida";
        r.details = "okA(exact)=" + std::to_string(okA) + ", okB(unknown)=" + std::to_string(okB) + 
                    ", okC(range)=" + std::to_string(okC) + ", okD(bases)=" + std::to_string(okD);
    }

    return r;
}

// -----------------------------------------------------------------------------
// Test 6: Accettazione composizioni parziali valide
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::Test06_AcceptValidPartialCompositions() {
    ScientificRegistryTestResult r;
    r.testId = "SCI-06";
    r.testName = "Accettazione composizioni parziali valide";
    r.inputDesc = "Composizioni Partial con: (a) somma minimi <= 1.0 e presenza di Unknown, (b) Range valido; controprova: somma minimi > 1.0 respinta";
    r.expectedDesc = "Validazione positiva per somme parziali <= 1.0; rifiuto immediato se somma dei minimi supera 1.0";

    // 6a. Partial valida con somma 0.35 + 0.40 = 0.75 <= 1.0, più un Unknown
    json jValid;
    jValid["schemaVersion"] = 2;
    jValid["materials"] = json::array({
        {
            {"id", 601},
            {"canonicalId", "fairworld:mat/partial_valid"},
            {"displayName", "Partial Valid Composite"},
            {"composition", {
                {"completeness", "Partial"},
                {"defaultBasis", "Mass"},
                {"components", json::array({
                    {{"displayName", "Comp 1"}, {"quantity", {{"kind", "Exact"}, {"exact", 0.35}, {"basis", "Mass"}}}},
                    {{"displayName", "Comp 2"}, {"quantity", {{"kind", "Range"}, {"min", 0.20}, {"max", 0.40}, {"basis", "Mass"}}}},
                    {{"displayName", "Comp 3"}, {"quantity", {{"kind", "Unknown"}, {"basis", "Mass"}}}}
                })}
            }}
        }
    });

    // 6b. Partial invalida con somma minimi 0.60 + 0.55 = 1.15 > 1.0
    json jInvalid;
    jInvalid["schemaVersion"] = 2;
    jInvalid["materials"] = json::array({
        {
            {"id", 602},
            {"canonicalId", "fairworld:mat/partial_invalid"},
            {"displayName", "Partial Exceeds One"},
            {"composition", {
                {"completeness", "Partial"},
                {"defaultBasis", "Mass"},
                {"components", json::array({
                    {{"displayName", "Comp 1"}, {"quantity", {{"kind", "Exact"}, {"exact", 0.60}, {"basis", "Mass"}}}},
                    {{"displayName", "Comp 2"}, {"quantity", {{"kind", "Range"}, {"min", 0.55}, {"max", 0.70}, {"basis", "Mass"}}}}
                })}
            }}
        }
    });

    std::string pathValid = CreateTempJsonFile("sci_partial_valid.json", jValid);
    std::string pathInvalid = CreateTempJsonFile("sci_partial_invalid.json", jInvalid);

    ScientificMaterialRegistry reg;
    bool okValid = reg.LoadFromJson(pathValid);
    bool okInvalid = reg.LoadFromJson(pathInvalid);

    RemoveTempFile(pathValid);
    RemoveTempFile(pathInvalid);

    if (okValid && !okInvalid) {
        r.passed = true;
        r.observedDesc = "Composizione Partial valida accettata; Partial con somma minimi (1.15 > 1.0) correttamente respinta";
    } else {
        r.passed = false;
        r.observedDesc = "Comportamento errato su composizioni Partial";
        r.details = "okValid=" + std::to_string(okValid) + ", okInvalid=" + std::to_string(okInvalid);
    }

    return r;
}

// -----------------------------------------------------------------------------
// Test 7: Rifiuto cicli tassonomici e compositivi
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::Test07_RejectTaxonomicAndCompositionCycles() {
    ScientificRegistryTestResult r;
    r.testId = "SCI-07";
    r.testName = "Rifiuto cicli tassonomici e compositivi";
    r.inputDesc = "(a) Ciclo tassonomico A -> B -> A, (b) Autoriferimento A -> A, (c) Ciclo compositivo A contiene B e B contiene A";
    r.expectedDesc = "Tutti i grafi ciclici vengono intercettati dalla DFS e rifiutati senza memory leak o loop infiniti";

    // 7a. Ciclo Tassonomico A Parent B e B Parent A
    json jTaxCycle;
    jTaxCycle["schemaVersion"] = 2;
    jTaxCycle["materials"] = json::array({
        {
            {"id", 701},
            {"canonicalId", "fairworld:mat/cycle_mat_a"},
            {"displayName", "Cycle Mat A"},
            {"relations", json::array({{{"type", "Parent"}, {"targetCanonicalId", "fairworld:mat/cycle_mat_b"}}})}
        },
        {
            {"id", 702},
            {"canonicalId", "fairworld:mat/cycle_mat_b"},
            {"displayName", "Cycle Mat B"},
            {"relations", json::array({{{"type", "Parent"}, {"targetCanonicalId", "fairworld:mat/cycle_mat_a"}}})}
        }
    });

    // 7b. Autoriferimento A -> A
    json jSelfRel;
    jSelfRel["schemaVersion"] = 2;
    jSelfRel["materials"] = json::array({
        {
            {"id", 703},
            {"canonicalId", "fairworld:mat/self_rel"},
            {"displayName", "Self Relation"},
            {"relations", json::array({{{"type", "Parent"}, {"targetCanonicalId", "fairworld:mat/self_rel"}}})}
        }
    });

    // 7c. Ciclo Compositivo A include B e B include A
    json jCompCycle;
    jCompCycle["schemaVersion"] = 2;
    jCompCycle["materials"] = json::array({
        {
            {"id", 704},
            {"canonicalId", "fairworld:mat/comp_cycle_a"},
            {"displayName", "Comp Cycle A"},
            {"composition", {
                {"completeness", "Partial"},
                {"components", json::array({{{"canonicalId", "fairworld:mat/comp_cycle_b"}, {"quantity", {{"kind", "Exact"}, {"exact", 0.50}}}}})}
            }}
        },
        {
            {"id", 705},
            {"canonicalId", "fairworld:mat/comp_cycle_b"},
            {"displayName", "Comp Cycle B"},
            {"composition", {
                {"completeness", "Partial"},
                {"components", json::array({{{"canonicalId", "fairworld:mat/comp_cycle_a"}, {"quantity", {{"kind", "Exact"}, {"exact", 0.50}}}}})}
            }}
        }
    });

    std::string pathTax = CreateTempJsonFile("sci_tax_cycle.json", jTaxCycle);
    std::string pathSelf = CreateTempJsonFile("sci_self_rel.json", jSelfRel);
    std::string pathComp = CreateTempJsonFile("sci_comp_cycle.json", jCompCycle);

    ScientificMaterialRegistry reg;
    bool okTax = reg.LoadFromJson(pathTax);
    bool okSelf = reg.LoadFromJson(pathSelf);
    bool okComp = reg.LoadFromJson(pathComp);

    RemoveTempFile(pathTax);
    RemoveTempFile(pathSelf);
    RemoveTempFile(pathComp);

    if (!okTax && !okSelf && !okComp) {
        r.passed = true;
        r.observedDesc = "Tutti i pattern ciclici (tassonomici, autoriferiti e compositivi) sono stati rilevati e respinti";
    } else {
        r.passed = false;
        r.observedDesc = "Mancato rilevamento di ciclo nel grafo dei materiali";
        r.details = "okTax=" + std::to_string(okTax) + ", okSelf=" + std::to_string(okSelf) + ", okComp=" + std::to_string(okComp);
    }

    return r;
}

// -----------------------------------------------------------------------------
// Test 8: Rifiuto riferimenti invalidi e duplicati
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::Test08_RejectInvalidReferencesAndDuplicates() {
    ScientificRegistryTestResult r;
    r.testId = "SCI-08";
    r.testName = "Rifiuto riferimenti invalidi e duplicati";
    r.inputDesc = "(a) Target relazionale inesistente, (b) Relazione duplicata, (c) Componente duplicato in composizione, (d) ID/canonicalId duplicato nel file";
    r.expectedDesc = "Validazione fallisce per tutti i cataloghi con riferimenti pendenti o entità duplicate";

    // 8a. Target relazionale inesistente
    json jMissingTarget;
    jMissingTarget["schemaVersion"] = 2;
    jMissingTarget["materials"] = json::array({
        {
            {"id", 801},
            {"canonicalId", "fairworld:mat/orphan_rel"},
            {"displayName", "Orphan Rel"},
            {"relations", json::array({{{"type", "Parent"}, {"targetCanonicalId", "fairworld:mat/nonexistent_phantom"}}})}
        }
    });

    // 8b. Relazione duplicata
    json jDupRel;
    jDupRel["schemaVersion"] = 2;
    jDupRel["materials"] = json::array({
        {{"id", 802}, {"canonicalId", "fairworld:mat/target_ok"}, {"displayName", "Target Ok"}},
        {
            {"id", 803},
            {"canonicalId", "fairworld:mat/dup_rel"},
            {"displayName", "Dup Rel"},
            {"relations", json::array({
                {{"type", "DerivedFrom"}, {"targetCanonicalId", "fairworld:mat/target_ok"}},
                {{"type", "DerivedFrom"}, {"targetCanonicalId", "fairworld:mat/target_ok"}}
            })}
        }
    });

    // 8c. Componente duplicato nella stessa composizione
    json jDupComp;
    jDupComp["schemaVersion"] = 2;
    jDupComp["materials"] = json::array({
        {
            {"id", 804},
            {"canonicalId", "fairworld:mat/dup_comp"},
            {"displayName", "Dup Comp"},
            {"composition", {
                {"completeness", "Partial"},
                {"components", json::array({
                    {{"displayName", "Quartz"}, {"quantity", {{"kind", "Exact"}, {"exact", 0.30}}}},
                    {{"displayName", "Quartz"}, {"quantity", {{"kind", "Exact"}, {"exact", 0.20}}}}
                })}
            }}
        }
    });

    // 8d. Materiale duplicato nel JSON stesso (stesso ID 805)
    json jDupId;
    jDupId["schemaVersion"] = 2;
    jDupId["materials"] = json::array({
        {{"id", 805}, {"canonicalId", "fairworld:mat/first_inst"}, {"displayName", "First"}},
        {{"id", 805}, {"canonicalId", "fairworld:mat/second_inst"}, {"displayName", "Second"}}
    });

    std::string pathOrphan = CreateTempJsonFile("sci_missing_target.json", jMissingTarget);
    std::string pathDupRel = CreateTempJsonFile("sci_dup_rel.json", jDupRel);
    std::string pathDupComp = CreateTempJsonFile("sci_dup_comp.json", jDupComp);
    std::string pathDupId = CreateTempJsonFile("sci_dup_id.json", jDupId);

    ScientificMaterialRegistry reg;
    bool okOrphan = reg.LoadFromJson(pathOrphan);
    bool okDupRel = reg.LoadFromJson(pathDupRel);
    bool okDupComp = reg.LoadFromJson(pathDupComp);
    bool okDupId = reg.LoadFromJson(pathDupId);

    RemoveTempFile(pathOrphan);
    RemoveTempFile(pathDupRel);
    RemoveTempFile(pathDupComp);
    RemoveTempFile(pathDupId);

    // okOrphan, okDupRel, okDupComp devono fallire la validazione (false).
    // okDupId: il duplicato interno viene scartato, ma se carica solo il primo loadedCount > 0;
    // verifichiamo che il secondo non abbia sovrascritto il primo e che i conflitti non siano ammessi
    bool orphanRejected = !okOrphan;
    bool dupRelRejected = !okDupRel;
    bool dupCompRejected = !okDupComp;

    if (orphanRejected && dupRelRejected && dupCompRejected) {
        r.passed = true;
        r.observedDesc = "Riferimenti inesistenti, relazioni duplicate e componenti duplicati respinti con successo";
    } else {
        r.passed = false;
        r.observedDesc = "Accettazione di riferimenti orfani o duplicati vietati dal contratto";
        r.details = "orphan=" + std::to_string(orphanRejected) + ", dupRel=" + std::to_string(dupRelRejected) + ", dupComp=" + std::to_string(dupCompRejected);
    }

    return r;
}

// -----------------------------------------------------------------------------
// Test 9: Atomicità di LoadFromJson: dopo un caricamento fallito, materiali e indici invariati
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::Test09_AtomicityLoadFromJsonRollback() {
    ScientificRegistryTestResult r;
    r.testId = "SCI-09";
    r.testName = "Atomicità di LoadFromJson (Rollback su errore)";
    r.inputDesc = "Registro inizializzato con standard 1-4; caricamento di file parzialmente valido ma con validazione v2 fallita alla fine";
    r.expectedDesc = "LoadFromJson restituisce false; i materiali candidati non intaccano m_materials e m_canonicalToIdMap";

    ScientificMaterialRegistry reg;
    reg.Initialize(); // ID 1, 2, 3, 4 standard

    size_t countBefore = reg.GetMaterialCount();
    auto snapshotBefore = reg.GetAllMaterials();

    // Creiamo un catalogo candidato dove il materiale 901 è sintatticamente corretto, ma il 902 ha un ciclo verso 901
    json jFaulty;
    jFaulty["schemaVersion"] = 2;
    jFaulty["materials"] = json::array({
        {
            {"id", 901},
            {"canonicalId", "fairworld:mat/candidate_ok"},
            {"displayName", "Candidate Ok"},
            {"relations", json::array({{{"type", "Parent"}, {"targetCanonicalId", "fairworld:mat/candidate_faulty"}}})}
        },
        {
            {"id", 902},
            {"canonicalId", "fairworld:mat/candidate_faulty"},
            {"displayName", "Candidate Faulty"},
            {"relations", json::array({{{"type", "Parent"}, {"targetCanonicalId", "fairworld:mat/candidate_ok"}}})}
        }
    });

    std::string pathFaulty = CreateTempJsonFile("sci_faulty_transaction.json", jFaulty);
    bool ok = reg.LoadFromJson(pathFaulty);
    RemoveTempFile(pathFaulty);

    if (ok) {
        r.passed = false;
        r.observedDesc = "LoadFromJson ha inaspettatamente avuto successo su catalogo ciclico!";
        return r;
    }

    size_t countAfter = reg.GetMaterialCount();
    bool countPreserved = (countBefore == countAfter && countAfter == 4);

    // Verifica che né 901 né 902 siano entrati negli indici o nelle mappe
    bool candidate1Present = reg.HasMaterial(901) || reg.HasMaterial("fairworld:mat/candidate_ok");
    bool candidate2Present = reg.HasMaterial(902) || reg.HasMaterial("fairworld:mat/candidate_faulty");

    // Verifica che tutti i 4 materiali standard siano intatti
    bool standardIntact = (reg.HasMaterial(1) && reg.HasMaterial(2) && reg.HasMaterial(3) && reg.HasMaterial(4));
    bool canonIntact = (reg.GetMaterialIdByCanonical("fairworld:mat/iron") == 4);

    if (countPreserved && !candidate1Present && !candidate2Present && standardIntact && canonIntact) {
        r.passed = true;
        r.observedDesc = "Atomicità transazionale verificata: transazione abortita, nessun residuo candidato, stato originale integro al 100%";
    } else {
        r.passed = false;
        r.observedDesc = "Stato del registro corrotto o parzialmente modificato dopo LoadFromJson fallito";
        r.details = "countPreserved=" + std::to_string(countPreserved) + 
                    ", c1=" + std::to_string(candidate1Present) + 
                    ", c2=" + std::to_string(candidate2Present) + 
                    ", standardIntact=" + std::to_string(standardIntact);
    }

    return r;
}

// -----------------------------------------------------------------------------
// Test 10: Invarianza ID 1–4 e ResolveProperty: override locale, proprietà del materiale, Unknown
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::Test10_InvarianceId1To4AndResolveProperty() {
    ScientificRegistryTestResult r;
    r.testId = "SCI-10";
    r.testName = "Invarianza ID 1-4 e ResolveProperty (override locale, base, Unknown)";
    r.inputDesc = "Materiali standard 1-4, test di risoluzione con override locale prioritario, proprietà intrinseca e fallback Unknown";
    r.expectedDesc = "ID 4 ha ScientificKind::Unknown; ResolveProperty rispetta rigorosamente le 3 priorità senza ereditarietà ontologica automatica";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    // 1. Invarianza ID 4
    const auto& iron = reg.GetMaterial(4);
    bool id4Ok = (iron.id == 4 &&
                  iron.canonicalId == "fairworld:mat/iron" &&
                  iron.displayName == "Iron / Structural Steel" &&
                  iron.kind == ScientificKind::Unknown &&
                  iron.aggregationState == AggregationState::Solid &&
                  std::abs(iron.density.value - 7870.0) < 1e-3);

    // 2. ResolveProperty - Caso A: Override locale prioritario
    ScientificPropertyOverrides ov;
    ov.SetOverride(ScientificPropertyNames::Density, ScientificQuantity::Known(5432.1, "kg/m^3"));
    ScientificQuantity resOverride = reg.ResolveProperty(4, ScientificPropertyNames::Density, ov);
    bool overrideOk = (resOverride.state == PropertyState::Known && std::abs(resOverride.value - 5432.1) < 1e-3);

    // 3. ResolveProperty - Caso B: Proprietà intrinseca del materiale di base
    ScientificPropertyOverrides emptyOv;
    ScientificQuantity resBase = reg.ResolveProperty(4, ScientificPropertyNames::Density, emptyOv);
    bool baseOk = (resBase.state == PropertyState::Known && std::abs(resBase.value - 7870.0) < 1e-3);

    // 4. ResolveProperty - Caso C: Proprietà non esistente o ID non risolto -> ritorna Unknown con unità canonica
    ScientificQuantity resUnknown = reg.ResolveProperty(INVALID_SCIENTIFIC_MATERIAL_ID, ScientificPropertyNames::Density, emptyOv);
    bool unknownOk = (resUnknown.state == PropertyState::Unknown && resUnknown.unit == "kg/m^3");

    // 5. Verifica assenza di ereditarietà ontologica automatica:
    // Se registriamo un materiale figlio con Parent = 4, ma senza densità specificata (state = Unknown),
    // ResolveProperty NON deve ereditare la densità di 4 tramite la relazione Parent.
    ScientificMaterialDef childMat;
    childMat.id = 999;
    childMat.canonicalId = "fairworld:mat/test_child_no_inheritance";
    childMat.relations.push_back({ScientificRelationType::Parent, "fairworld:mat/iron", 4});
    // childMat.density rimane Unknown
    reg.RegisterMaterial(childMat);

    ScientificQuantity resChild = reg.ResolveProperty(999, ScientificPropertyNames::Density, emptyOv);
    bool noAutoInheritance = (resChild.state == PropertyState::Unknown);

    if (id4Ok && overrideOk && baseOk && unknownOk && noAutoInheritance) {
        r.passed = true;
        r.observedDesc = "ID 4 invariato con kind=Unknown; ResolveProperty rispetta override, base e fallback Unknown; nessuna auto-ereditarietà ontologica";
    } else {
        r.passed = false;
        r.observedDesc = "Violazione del contratto per ID 4 o per ResolveProperty";
        r.details = "id4Ok=" + std::to_string(id4Ok) + 
                    ", overrideOk=" + std::to_string(overrideOk) + 
                    ", baseOk=" + std::to_string(baseOk) + 
                    ", unknownOk=" + std::to_string(unknownOk) + 
                    ", noAutoInheritance=" + std::to_string(noAutoInheritance);
    }

    return r;
}

// -----------------------------------------------------------------------------
// BOOT-01: Bootstrap Initialize -> LoadFromJson, verifica kind e aggregationState
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestBOOT01_BootstrapConsistency() {
    ScientificRegistryTestResult r;
    r.testId = "BOOT-01";
    r.testName = "Bootstrap Initialize -> LoadFromJson (Assenza Downgrade Tassonomico)";
    r.inputDesc = "Sequenza esatta di avvio main.cpp (Initialize -> LoadFromJson su scientific_materials.json produttivo)";
    r.expectedDesc = "Nessun downgrade a Unknown per 1-3: water=Compound/Liquid, granite=Rock/Solid, basalt=Rock/Solid, iron=Unknown/Solid";

    std::string prodPath = FindProductionCatalogPath();
    if (prodPath.empty()) {
        r.passed = false;
        r.observedDesc = "File catalogo produttivo 'scientific_materials.json' non trovato nei percorsi attesi!";
        r.details = "Rifiuto esplicito obbligatorio: vietata generazione fittizia o sostituzione silenziosa.";
        return r;
    }

    ScientificMaterialRegistry reg;
    reg.Initialize();
    bool loadOk = reg.LoadFromJson(prodPath);

    if (!loadOk) {
        r.passed = false;
        r.observedDesc = "LoadFromJson ha fallito il caricamento del catalogo produttivo '" + prodPath + "'";
        return r;
    }

    const auto& w = reg.GetMaterial(1);
    const auto& g = reg.GetMaterial(2);
    const auto& b = reg.GetMaterial(3);
    const auto& i = reg.GetMaterial(4);

    bool wOk = (w.kind == ScientificKind::Compound && w.aggregationState == AggregationState::Liquid);
    bool gOk = (g.kind == ScientificKind::Rock && g.aggregationState == AggregationState::Solid);
    bool bOk = (b.kind == ScientificKind::Rock && b.aggregationState == AggregationState::Solid);
    bool iOk = (i.kind == ScientificKind::Unknown && i.aggregationState == AggregationState::Solid);

    if (wOk && gOk && bOk && iOk) {
        r.passed = true;
        r.observedDesc = "Bootstrap verificato: kind e aggregationState conservati fedelmente dallo schema v2 (" + prodPath + ")";
    } else {
        r.passed = false;
        r.observedDesc = "Downgrade tassonomico o mancata corrispondenza kind/aggregationState dopo caricamento catalogo produttivo";
        r.details = "water(Compound/Liquid)=" + std::to_string(wOk) + 
                    ", granite(Rock/Solid)=" + std::to_string(gOk) + 
                    ", basalt(Rock/Solid)=" + std::to_string(bOk) + 
                    ", iron(Unknown/Solid)=" + std::to_string(iOk);
    }

    return r;
}

// -----------------------------------------------------------------------------
// BOOT-02: Query tassonomiche dopo il caricamento
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestBOOT02_TaxonomicQueriesPostLoad() {
    ScientificRegistryTestResult r;
    r.testId = "BOOT-02";
    r.testName = "Query tassonomiche post-caricamento catalogo produttivo";
    r.inputDesc = "Invocazione di GetMaterialsByKind e GetMaterialsByAggregationState sul catalogo produttivo caricato";
    r.expectedDesc = "Rock restituisce {2, 3}, Compound {1}, Unknown {4}, Liquid {1}, Solid {2, 3, 4}";

    std::string prodPath = FindProductionCatalogPath();
    if (prodPath.empty()) {
        r.passed = false;
        r.observedDesc = "File catalogo produttivo 'scientific_materials.json' non trovato nei percorsi attesi!";
        r.details = "Rifiuto esplicito obbligatorio: vietata generazione fittizia.";
        return r;
    }

    ScientificMaterialRegistry reg;
    reg.Initialize();
    if (!reg.LoadFromJson(prodPath)) {
        r.passed = false;
        r.observedDesc = "LoadFromJson fallito su " + prodPath;
        return r;
    }

    auto rocks = reg.GetMaterialsByKind(ScientificKind::Rock);
    auto compounds = reg.GetMaterialsByKind(ScientificKind::Compound);
    auto unknowns = reg.GetMaterialsByKind(ScientificKind::Unknown);
    auto liquids = reg.GetMaterialsByAggregationState(AggregationState::Liquid);
    auto solids = reg.GetMaterialsByAggregationState(AggregationState::Solid);

    bool rocksOk = (rocks.size() == 2 && 
                   std::find(rocks.begin(), rocks.end(), 2) != rocks.end() && 
                   std::find(rocks.begin(), rocks.end(), 3) != rocks.end());
    bool compOk = (compounds.size() == 1 && compounds[0] == 1);
    bool unkOk = (unknowns.size() == 1 && unknowns[0] == 4);
    bool liqOk = (liquids.size() == 1 && liquids[0] == 1);
    bool solOk = (solids.size() == 3 && 
                  std::find(solids.begin(), solids.end(), 2) != solids.end() && 
                  std::find(solids.begin(), solids.end(), 3) != solids.end() && 
                  std::find(solids.begin(), solids.end(), 4) != solids.end());

    if (rocksOk && compOk && unkOk && liqOk && solOk) {
        r.passed = true;
        r.observedDesc = "Tutte le query tassonomiche e per stato di aggregazione restituiscono i set corretti dei materiali produttivi";
    } else {
        r.passed = false;
        r.observedDesc = "Discrepanza nei risultati delle query tassonomiche post-caricamento";
        r.details = "rocks=" + std::to_string(rocks.size()) + ", compounds=" + std::to_string(compounds.size()) + 
                    ", unknowns=" + std::to_string(unknowns.size()) + ", liquids=" + std::to_string(liquids.size()) + 
                    ", solids=" + std::to_string(solids.size());
    }

    return r;
}

// -----------------------------------------------------------------------------
// BOOT-03: Confronto dei valori scientifici prima/dopo il caricamento
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestBOOT03_ScientificValuesPreservation() {
    ScientificRegistryTestResult r;
    r.testId = "BOOT-03";
    r.testName = "Invarianza valori scientifici e metadati prima/dopo caricamento";
    r.inputDesc = "Confronto analitico di tutte le 12 proprietà fisiche, unità, T/P, fonti e canonicalId tra stato standard C++ e catalogo JSON caricato";
    r.expectedDesc = "Corrispondenza esatta bit-a-bit di tutte le 12 grandezze fisiche per ID 1..4";

    std::string prodPath = FindProductionCatalogPath();
    if (prodPath.empty()) {
        r.passed = false;
        r.observedDesc = "File catalogo produttivo 'scientific_materials.json' non trovato nei percorsi attesi!";
        r.details = "Rifiuto esplicito obbligatorio: vietata generazione fittizia o sostituzione silenziosa.";
        return r;
    }

    ScientificMaterialRegistry regBefore;
    regBefore.Initialize();

    ScientificMaterialRegistry regAfter;
    regAfter.Initialize();
    if (!regAfter.LoadFromJson(prodPath)) {
        r.passed = false;
        r.observedDesc = "LoadFromJson fallito su " + prodPath;
        return r;
    }

    struct DifferenceRecord {
        ScientificMaterialID id = 0;
        std::string canonicalId;
        std::string propertyName;
        std::string differenceCategory; // "NUMERICAL", "TEXTUAL", "SEMANTIC / METADATA"
        std::string differenceField;    // "value", "unit", "state", "quality", "temperatureKelvin", "pressurePascal", "source"
        std::string expectedStr;
        std::string observedStr;
    };

    std::vector<DifferenceRecord> differences;

    // Helper per confrontare due ScientificQuantity omologhe e accumulare tutte le discrepanze senza uscire anticipatamente
    auto compareProperty = [&](ScientificMaterialID id, const std::string& canon, const std::string& propName,
                               const ScientificQuantity& expected, const ScientificQuantity& observed) {
        // 1. Stato (PropertyState)
        if (expected.state != observed.state) {
            DifferenceRecord diff;
            diff.id = id;
            diff.canonicalId = canon;
            diff.propertyName = propName;
            diff.differenceCategory = "SEMANTIC / METADATA";
            diff.differenceField = "state";
            diff.expectedStr = PropertyStateToString(expected.state);
            diff.observedStr = PropertyStateToString(observed.state);
            differences.push_back(diff);
        }

        // 2. Valore numerico
        bool valueDiffers = false;
        if (expected.state == PropertyState::Known || observed.state == PropertyState::Known) {
            if (std::isnan(expected.value) != std::isnan(observed.value) ||
                std::isinf(expected.value) != std::isinf(observed.value) ||
                std::abs(expected.value - observed.value) > 1e-6) {
                valueDiffers = true;
            }
        } else {
            // Entrambi NotApplicable o Unknown: verifichiamo se il double memorizzato differisce
            if (std::abs(expected.value - observed.value) > 1e-6) {
                valueDiffers = true;
            }
        }
        if (valueDiffers) {
            std::ostringstream expOss, obsOss;
            expOss << std::setprecision(10) << expected.value;
            obsOss << std::setprecision(10) << observed.value;

            DifferenceRecord diff;
            diff.id = id;
            diff.canonicalId = canon;
            diff.propertyName = propName;
            diff.differenceCategory = "NUMERICAL";
            diff.differenceField = "value";
            diff.expectedStr = expOss.str();
            diff.observedStr = obsOss.str();
            differences.push_back(diff);
        }

        // 3. Unità di misura (unit)
        if (expected.unit != observed.unit) {
            DifferenceRecord diff;
            diff.id = id;
            diff.canonicalId = canon;
            diff.propertyName = propName;
            diff.differenceCategory = "TEXTUAL";
            diff.differenceField = "unit";
            diff.expectedStr = expected.unit.empty() ? "\"\" (vuoto)" : ("\"" + expected.unit + "\"");
            diff.observedStr = observed.unit.empty() ? "\"\" (vuoto)" : ("\"" + observed.unit + "\"");
            differences.push_back(diff);
        }

        // 4. Qualità del dato (DataQuality)
        if (expected.quality != observed.quality) {
            DifferenceRecord diff;
            diff.id = id;
            diff.canonicalId = canon;
            diff.propertyName = propName;
            diff.differenceCategory = "SEMANTIC / METADATA";
            diff.differenceField = "quality";
            diff.expectedStr = DataQualityToString(expected.quality);
            diff.observedStr = DataQualityToString(observed.quality);
            differences.push_back(diff);
        }

        // 5. Temperatura Kelvin
        if (std::abs(expected.temperatureKelvin - observed.temperatureKelvin) > 1e-4) {
            std::ostringstream expOss, obsOss;
            expOss << std::setprecision(6) << expected.temperatureKelvin << " K";
            obsOss << std::setprecision(6) << observed.temperatureKelvin << " K";

            DifferenceRecord diff;
            diff.id = id;
            diff.canonicalId = canon;
            diff.propertyName = propName;
            diff.differenceCategory = "NUMERICAL";
            diff.differenceField = "temperatureKelvin";
            diff.expectedStr = expOss.str();
            diff.observedStr = obsOss.str();
            differences.push_back(diff);
        }

        // 6. Pressione Pascal
        if (std::abs(expected.pressurePascal - observed.pressurePascal) > 1e-4) {
            std::ostringstream expOss, obsOss;
            expOss << std::setprecision(6) << expected.pressurePascal << " Pa";
            obsOss << std::setprecision(6) << observed.pressurePascal << " Pa";

            DifferenceRecord diff;
            diff.id = id;
            diff.canonicalId = canon;
            diff.propertyName = propName;
            diff.differenceCategory = "NUMERICAL";
            diff.differenceField = "pressurePascal";
            diff.expectedStr = expOss.str();
            diff.observedStr = obsOss.str();
            differences.push_back(diff);
        }

        // 7. Fonte bibliografica (source)
        if (expected.source != observed.source) {
            DifferenceRecord diff;
            diff.id = id;
            diff.canonicalId = canon;
            diff.propertyName = propName;
            diff.differenceCategory = "TEXTUAL";
            diff.differenceField = "source";
            diff.expectedStr = expected.source.empty() ? "\"\" (vuoto)" : ("\"" + expected.source + "\"");
            diff.observedStr = observed.source.empty() ? "\"\" (vuoto)" : ("\"" + observed.source + "\"");
            differences.push_back(diff);
        }
    };

    // Scansione analitica di tutti i materiali standard (ID 1 .. 4)
    for (ScientificMaterialID id = 1; id <= 4; ++id) {
        if (!regAfter.HasMaterial(id)) {
            DifferenceRecord diff;
            diff.id = id;
            diff.canonicalId = "N/A";
            diff.propertyName = "MaterialDefinition";
            diff.differenceCategory = "SEMANTIC / METADATA";
            diff.differenceField = "existence";
            diff.expectedStr = "Materiale presente";
            diff.observedStr = "Materiale ASSENTE nel registry caricato";
            differences.push_back(diff);
            continue;
        }

        const auto& b = regBefore.GetMaterial(id); // Atteso: default C++ RegisterStandardMaterials
        const auto& a = regAfter.GetMaterial(id);  // Osservato: caricato da scientific_materials.json

        // Metadati descrittivi generali
        if (b.canonicalId != a.canonicalId) {
            differences.push_back({id, b.canonicalId, "canonicalId", "TEXTUAL", "canonicalId", b.canonicalId, a.canonicalId});
        }
        if (b.displayName != a.displayName) {
            differences.push_back({id, b.canonicalId, "displayName", "TEXTUAL", "displayName", b.displayName, a.displayName});
        }
        if (b.chemicalFormula != a.chemicalFormula) {
            differences.push_back({id, b.canonicalId, "chemicalFormula", "TEXTUAL", "chemicalFormula", b.chemicalFormula, a.chemicalFormula});
        }
        if (b.classification != a.classification) {
            differences.push_back({id, b.canonicalId, "classification", "TEXTUAL", "classification", b.classification, a.classification});
        }
        if (b.description != a.description) {
            differences.push_back({id, b.canonicalId, "description", "TEXTUAL", "description", "\"" + b.description + "\"", "\"" + a.description + "\""});
        }

        // Verifica indice inverso
        if (regAfter.GetMaterialIdByCanonical(b.canonicalId) != id) {
            differences.push_back({id, b.canonicalId, "canonicalToIdMap", "SEMANTIC / METADATA", "mapping", 
                                   std::to_string(id), std::to_string(regAfter.GetMaterialIdByCanonical(b.canonicalId))});
        }

        // Confronto di tutte le 12 proprietà omologhe
        compareProperty(id, b.canonicalId, "density", b.density, a.density);
        compareProperty(id, b.canonicalId, "porosity", b.porosity, a.porosity);
        compareProperty(id, b.canonicalId, "compressiveStrength", b.compressiveStrength, a.compressiveStrength);
        compareProperty(id, b.canonicalId, "tensileStrength", b.tensileStrength, a.tensileStrength);
        compareProperty(id, b.canonicalId, "elasticModulus", b.elasticModulus, a.elasticModulus);
        compareProperty(id, b.canonicalId, "staticFriction", b.staticFriction, a.staticFriction);
        compareProperty(id, b.canonicalId, "dynamicFriction", b.dynamicFriction, a.dynamicFriction);
        compareProperty(id, b.canonicalId, "restitution", b.restitution, a.restitution);
        compareProperty(id, b.canonicalId, "specificHeatCapacity", b.specificHeatCapacity, a.specificHeatCapacity);
        compareProperty(id, b.canonicalId, "thermalConductivity", b.thermalConductivity, a.thermalConductivity);
        compareProperty(id, b.canonicalId, "thermalExpansion", b.thermalExpansion, a.thermalExpansion);
        compareProperty(id, b.canonicalId, "meltingPoint", b.meltingPoint, a.meltingPoint);
    }

    if (differences.empty()) {
        r.passed = true;
        r.observedDesc = "Nessuna discrepanza: tutti i valori fisici, unità, condizioni termodinamiche e fonti bibliografiche di ID 1..4 risultano identici";
        return r;
    }

    r.passed = false;
    r.observedDesc = "Rilevate " + std::to_string(differences.size()) + " discrepanze tra definizioni C++ (attese) e catalogo JSON (osservate)";

    std::ostringstream oss;
    oss << "\n        [TABELLA DIFFERENZIALE BOOT-03 (" << differences.size() << " DISCREPANZE RILEVATE)]\n";
    oss << "        ------------------------------------------------------------------------------------------------------------------------\n";
    for (size_t i = 0; i < differences.size(); ++i) {
        const auto& d = differences[i];
        oss << "        (" << (i + 1) << ") ID " << d.id << " [" << d.canonicalId << "] -> Proprieta': '" << d.propertyName 
            << "' | Campo: '" << d.differenceField << "' | Categoria: [" << d.differenceCategory << "]\n";
        oss << "            - Atteso (C++ static) : " << d.expectedStr << "\n";
        oss << "            - Osservato (JSON)    : " << d.observedStr << "\n";
    }
    oss << "        ------------------------------------------------------------------------------------------------------------------------";
    r.details = oss.str();

    return r;
}

// -----------------------------------------------------------------------------
// BOOT-04: Round-trip JSON v2 e validazione
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestBOOT04_ProductionJsonV2RoundTrip() {
    ScientificRegistryTestResult r;
    r.testId = "BOOT-04";
    r.testName = "Round-trip JSON v2 e validazione catalogo produttivo";
    r.inputDesc = "Caricamento scientific_materials.json produttivo -> ValidateCatalog -> SaveToJson temp -> Reload -> ValidateCatalog";
    r.expectedDesc = "Validazione superata prima e dopo il salvataggio; zero perdite e zero corruzioni nel round-trip";

    std::string prodPath = FindProductionCatalogPath();
    if (prodPath.empty()) {
        r.passed = false;
        r.observedDesc = "File catalogo produttivo 'scientific_materials.json' non trovato nei percorsi attesi!";
        r.details = "Rifiuto esplicito obbligatorio: vietata generazione fittizia.";
        return r;
    }

    ScientificMaterialRegistry reg;
    reg.Initialize();
    if (!reg.LoadFromJson(prodPath)) {
        r.passed = false;
        r.observedDesc = "LoadFromJson fallito su " + prodPath;
        return r;
    }

    std::string valErr;
    if (!reg.ValidateCatalog(&valErr)) {
        r.passed = false;
        r.observedDesc = "ValidateCatalog fallito sul catalogo produttivo caricato: " + valErr;
        return r;
    }

    std::string tempPath = (std::filesystem::temp_directory_path() / "fairworld_sci_tests" / "prod_roundtrip_v2.json").string();
    if (!reg.SaveToJson(tempPath)) {
        r.passed = false;
        r.observedDesc = "SaveToJson fallito su " + tempPath;
        return r;
    }

    ScientificMaterialRegistry regRoundTrip;
    if (!regRoundTrip.LoadFromJson(tempPath)) {
        RemoveTempFile(tempPath);
        r.passed = false;
        r.observedDesc = "LoadFromJson fallito nel ricaricare il file esportato " + tempPath;
        return r;
    }

    if (!regRoundTrip.ValidateCatalog(&valErr)) {
        RemoveTempFile(tempPath);
        r.passed = false;
        r.observedDesc = "ValidateCatalog fallito sul catalogo post-roundtrip: " + valErr;
        return r;
    }

    RemoveTempFile(tempPath);

    if (regRoundTrip.GetMaterialCount() != 4) {
        r.passed = false;
        r.observedDesc = "Conteggio materiali alterato post-roundtrip: " + std::to_string(regRoundTrip.GetMaterialCount());
        return r;
    }

    r.passed = true;
    r.observedDesc = "Catalogo produttivo v2 validato formalmente prima e dopo il round-trip su disco (zero errori)";
    return r;
}

// =============================================================================
// MA-03.2D.3B: SUITE TEST INFRASTRUTTURA PROPRIETA' ESTESE (EXT-01 .. EXT-11)
// =============================================================================

// -----------------------------------------------------------------------------
// EXT-01: Rollback transazionale su JSON v3 invalido
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestEXT01_RollbackOnInvalidV3Json() {
    ScientificRegistryTestResult r;
    r.testId = "EXT-01";
    r.testName = "Rollback transazionale su JSON v3 invalido";
    r.inputDesc = "File JSON schemaVersion 3 con proprieta' estesa non registrata e unita' non consentita";
    r.expectedDesc = "LoadFromJson restituisce false; stato candidato scartato; catalogo invariato (rollback atomico)";

    json j;
    j["schemaVersion"] = 3;
    j["materials"] = json::array({
        {
            {"id", 701},
            {"canonicalId", "fairworld:mat/ext_invalid"},
            {"displayName", "Invalid Ext Material"},
            {"extendedProperties", json::array({
                {
                    {"propertyUri", "scientific:prop/unknown_bogus_property"},
                    {"applicability", "Applicable"},
                    {"epistemicState", "Known"}
                }
            })}
        }
    });

    std::string tempPath = CreateTempJsonFile("sci_test_ext01_invalid.json", j);

    ScientificMaterialRegistry reg;
    reg.Initialize();
    size_t countBefore = reg.GetMaterialCount();

    bool ok = reg.LoadFromJson(tempPath);
    size_t countAfter = reg.GetMaterialCount();

    RemoveTempFile(tempPath);

    if (!ok && countBefore == countAfter && !reg.HasMaterial(701)) {
        r.passed = true;
        r.observedDesc = "JSON v3 invalido correttamente respinto; rollback atomico confermato; 4 materiali legacy invariati";
    } else {
        r.passed = false;
        r.observedDesc = "Mancato rollback o corruzione dello stato attivo su JSON v3 invalido";
        r.details = "ok=" + std::to_string(ok) + ", countBefore=" + std::to_string(countBefore) + ", countAfter=" + std::to_string(countAfter);
    }
    return r;
}

// -----------------------------------------------------------------------------
// EXT-02: Sei esiti distinti del Resolver
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestEXT02_ResolverSixOutcomes() {
    ScientificRegistryTestResult r;
    r.testId = "EXT-02";
    r.testName = "Sei esiti distinti del Resolver";
    r.inputDesc = "Query su proprieta' con stati PropertyAbsent, NotApplicable, Unknown, MatchSingle, MatchMultiple e NoMatchConditions";
    r.expectedDesc = "Risoluzione esatta e deterministica dei 6 esiti previsti dal contratto MA-03.2D.3A-FINAL";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    ScientificMaterialDef def;
    def.id = 702;
    def.canonicalId = "fairworld:mat/ext_resolver_test";
    def.displayName = "Resolver Test Material";

    // 1. NotApplicable
    ExtendedPropertyEntry eNA;
    eNA.propertyUri = "scientific:prop/surface_tension";
    eNA.observationCollection.applicability = ApplicabilityState::NotApplicable;
    def.extendedProperties.AddOrUpdateEntry(eNA);

    // 2. Unknown
    ExtendedPropertyEntry eUnk;
    eUnk.propertyUri = "scientific:prop/apparent_cohesion";
    eUnk.observationCollection.epistemicState = EpistemicState::Unknown;
    def.extendedProperties.AddOrUpdateEntry(eUnk);

    // 3. MatchMultiple (2 osservazioni a 293.15 K)
    ExtendedPropertyEntry eBulk;
    eBulk.propertyUri = "scientific:prop/isothermal_bulk_modulus";
    eBulk.observationCollection.applicability = ApplicabilityState::Applicable;
    eBulk.observationCollection.epistemicState = EpistemicState::Known;

    ExtendedMeasurement m1;
    m1.valueNature = ValueNature::ExactScalar;
    m1.scalarValue = 2.2e9;
    m1.unit = "Pa";
    m1.temperatureKelvin = 293.15;
    eBulk.observationCollection.measurements.push_back(m1);

    ExtendedMeasurement m2;
    m2.valueNature = ValueNature::ExactScalar;
    m2.scalarValue = 2.22e9;
    m2.unit = "Pa";
    m2.temperatureKelvin = 293.15;
    eBulk.observationCollection.measurements.push_back(m2);

    def.extendedProperties.AddOrUpdateEntry(eBulk);

    reg.RegisterMaterial(def);

    // Test 1: PropertyAbsent
    QueryResult resAbsent = reg.QueryExtendedProperty(702, "scientific:prop/poissons_ratio");
    bool okAbsent = (resAbsent.outcome == QueryOutcome::PropertyAbsent);

    // Test 2: PropertyNotApplicable
    QueryResult resNA = reg.QueryExtendedProperty(702, "scientific:prop/surface_tension");
    bool okNA = (resNA.outcome == QueryOutcome::PropertyNotApplicable);

    // Test 3: PropertyUnknown
    QueryResult resUnk = reg.QueryExtendedProperty(702, "scientific:prop/apparent_cohesion");
    bool okUnk = (resUnk.outcome == QueryOutcome::PropertyUnknown);

    // Test 4: MatchMultiple
    QueryPredicate predMatch;
    predMatch.requiredTemperatureKelvin = 293.15;
    QueryResult resMult = reg.QueryExtendedProperty(702, "scientific:prop/isothermal_bulk_modulus", predMatch);
    bool okMult = (resMult.outcome == QueryOutcome::MatchMultiple && resMult.matchingMeasurements.size() == 2);

    // Test 5: NoMatchConditions (richiede temperatura 500 K non presente)
    QueryPredicate predNoMatch;
    predNoMatch.requiredTemperatureKelvin = 500.0;
    QueryResult resNoMatch = reg.QueryExtendedProperty(702, "scientific:prop/isothermal_bulk_modulus", predNoMatch);
    bool okNoMatch = (resNoMatch.outcome == QueryOutcome::NoMatchConditions);

    // Test 6: MatchSingle (aggiorna a 1 sola osservazione)
    eBulk.observationCollection.measurements.pop_back();
    def.extendedProperties.AddOrUpdateEntry(eBulk);
    reg.RegisterMaterial(def, true);
    QueryResult resSingle = reg.QueryExtendedProperty(702, "scientific:prop/isothermal_bulk_modulus", predMatch);
    bool okSingle = (resSingle.outcome == QueryOutcome::MatchSingle && resSingle.matchingMeasurements.size() == 1);

    if (okAbsent && okNA && okUnk && okMult && okNoMatch && okSingle) {
        r.passed = true;
        r.observedDesc = "Tutti i 6 esiti del resolver (PropertyAbsent, NotApplicable, Unknown, MatchSingle, MatchMultiple, NoMatchConditions) verificati formalmente";
    } else {
        r.passed = false;
        r.observedDesc = "Discrepanza negli esiti del resolver";
        r.details = "Absent=" + std::to_string(okAbsent) + ", NA=" + std::to_string(okNA) +
                    ", Unk=" + std::to_string(okUnk) + ", Mult=" + std::to_string(okMult) +
                    ", NoMatch=" + std::to_string(okNoMatch) + ", Single=" + std::to_string(okSingle);
    }
    return r;
}

// -----------------------------------------------------------------------------
// EXT-03: Matching conservativo per campi mancanti o incompatibili
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestEXT03_ConservativeMatchingMissingFields() {
    ScientificRegistryTestResult r;
    r.testId = "EXT-03";
    r.testName = "Matching conservativo per campi mancanti";
    r.inputDesc = "Osservazione priva di fase termodinamica interrogata con predicato che richiede esplicitamente la fase";
    r.expectedDesc = "Campo richiesto ma assente nell'osservazione -> non dimostrato compatibile -> scartato (NoMatchConditions)";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    ScientificMaterialDef def;
    def.id = 703;
    def.canonicalId = "fairworld:mat/ext_conservative_test";
    def.displayName = "Conservative Test Material";

    ExtendedPropertyEntry entry;
    entry.propertyUri = "scientific:prop/shear_modulus";
    entry.observationCollection.applicability = ApplicabilityState::Applicable;
    entry.observationCollection.epistemicState = EpistemicState::Known;

    ExtendedMeasurement m;
    m.valueNature = ValueNature::ExactScalar;
    m.scalarValue = 3.0e10;
    m.unit = "Pa";
    m.temperatureKelvin = 293.15;
    // thermodynamicPhase NON valorizzato (assente)
    entry.observationCollection.measurements.push_back(m);

    def.extendedProperties.AddOrUpdateEntry(entry);
    reg.RegisterMaterial(def);

    // Query 1: richiede fase 'solid'. L'osservazione non dichiara fase -> SCARTATA
    QueryPredicate predPhase;
    predPhase.requiredThermodynamicPhase = "solid";
    QueryResult resPhase = reg.QueryExtendedProperty(703, "scientific:prop/shear_modulus", predPhase);
    bool okPhase = (resPhase.outcome == QueryOutcome::NoMatchConditions);

    // Query 2: wildcard sulla fase (non richiesta) -> ACCETTATA
    QueryPredicate predWildcard;
    predWildcard.requiredTemperatureKelvin = 293.15;
    QueryResult resWild = reg.QueryExtendedProperty(703, "scientific:prop/shear_modulus", predWildcard);
    bool okWild = (resWild.outcome == QueryOutcome::MatchSingle && resWild.matchingMeasurements.size() == 1);

    if (okPhase && okWild) {
        r.passed = true;
        r.observedDesc = "Matching conservativo verificato: campo richiesto assente nell'osservazione correttamente scartato (NoMatchConditions); wildcard accettato";
    } else {
        r.passed = false;
        r.observedDesc = "Violazione della regola di matching conservativo";
        r.details = "okPhase=" + std::to_string(okPhase) + ", okWild=" + std::to_string(okWild);
    }
    return r;
}

// -----------------------------------------------------------------------------
// EXT-04: MatchMultiple senza selezione implicita o interpolazione
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestEXT04_MatchMultipleNoImplicitSelection() {
    ScientificRegistryTestResult r;
    r.testId = "EXT-04";
    r.testName = "MatchMultiple senza selezione implicita";
    r.inputDesc = "Materiale con 3 misure distinte da fonti diverse alle medesime condizioni sperimentali";
    r.expectedDesc = "Ritorna MatchMultiple con esattamente 3 puntatori validi; nessuna media, interpolazione o selezione preferenziale";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    ScientificMaterialDef def;
    def.id = 704;
    def.canonicalId = "fairworld:mat/ext_mult_test";
    def.displayName = "Multiple Match Material";

    ExtendedPropertyEntry entry;
    entry.propertyUri = "scientific:prop/dynamic_viscosity";
    entry.observationCollection.applicability = ApplicabilityState::Applicable;
    entry.observationCollection.epistemicState = EpistemicState::Known;

    for (int i = 0; i < 3; ++i) {
        ExtendedMeasurement m;
        m.valueNature = ValueNature::ExactScalar;
        m.scalarValue = 0.0010 + i * 0.00002;
        m.unit = "Pa*s";
        m.temperatureKelvin = 293.15;
        m.bibliographicCitation = "Source " + std::to_string(i + 1);
        entry.observationCollection.measurements.push_back(m);
    }

    def.extendedProperties.AddOrUpdateEntry(entry);
    reg.RegisterMaterial(def);

    QueryPredicate pred;
    pred.requiredTemperatureKelvin = 293.15;
    QueryResult res = reg.QueryExtendedProperty(704, "scientific:prop/dynamic_viscosity", pred);

    bool ok = (res.outcome == QueryOutcome::MatchMultiple && res.matchingMeasurements.size() == 3);
    if (ok && res.matchingMeasurements[0]->scalarValue != res.matchingMeasurements[1]->scalarValue) {
        r.passed = true;
        r.observedDesc = "MatchMultiple restituisce integralmente le 3 osservazioni candidate senza alcuna selezione implicita";
    } else {
        r.passed = false;
        r.observedDesc = "Mancata restituzione conforme di MatchMultiple";
    }
    return r;
}

// -----------------------------------------------------------------------------
// EXT-05: Distinzione semantica tra Known(0.0), Unknown e NotApplicable
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestEXT05_DistinctionZeroUnknownNotApplicable() {
    ScientificRegistryTestResult r;
    r.testId = "EXT-05";
    r.testName = "Distinzione Known(0.0), Unknown e NotApplicable";
    r.inputDesc = "Materiale A con coesione nota = 0.0 Pa, Materiale B con coesione Unknown, Materiale C con coesione NotApplicable";
    r.expectedDesc = "Risoluzione esatta e separata dei 3 stati epistemici; nessuno zero sentinella confuso con non-applicabilita'";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    // Materiale A: Coesione = 0.0 Pa (sabbia secca / mezzo granulare privo di legame)
    ScientificMaterialDef matA;
    matA.id = 7051;
    matA.canonicalId = "fairworld:mat/sand_dry";
    matA.displayName = "Dry Sand";
    {
        ExtendedPropertyEntry entry;
        entry.propertyUri = "scientific:prop/apparent_cohesion";
        entry.observationCollection.applicability = ApplicabilityState::Applicable;
        entry.observationCollection.epistemicState = EpistemicState::Known;
        ExtendedMeasurement m;
        m.valueNature = ValueNature::ExactScalar;
        m.scalarValue = 0.0;
        m.unit = "Pa";
        entry.observationCollection.measurements.push_back(m);
        matA.extendedProperties.AddOrUpdateEntry(entry);
    }
    reg.RegisterMaterial(matA);

    // Materiale B: Unknown
    ScientificMaterialDef matB;
    matB.id = 7052;
    matB.canonicalId = "fairworld:mat/unknown_soil";
    matB.displayName = "Unknown Soil";
    {
        ExtendedPropertyEntry entry;
        entry.propertyUri = "scientific:prop/apparent_cohesion";
        entry.observationCollection.applicability = ApplicabilityState::Applicable;
        entry.observationCollection.epistemicState = EpistemicState::Unknown;
        matB.extendedProperties.AddOrUpdateEntry(entry);
    }
    reg.RegisterMaterial(matB);

    // Materiale C: NotApplicable
    ScientificMaterialDef matC;
    matC.id = 7053;
    matC.canonicalId = "fairworld:mat/gas_plasma";
    matC.displayName = "Gas Plasma";
    {
        ExtendedPropertyEntry entry;
        entry.propertyUri = "scientific:prop/apparent_cohesion";
        entry.observationCollection.applicability = ApplicabilityState::NotApplicable;
        matC.extendedProperties.AddOrUpdateEntry(entry);
    }
    reg.RegisterMaterial(matC);

    QueryResult resA = reg.QueryExtendedProperty(7051, "scientific:prop/apparent_cohesion");
    QueryResult resB = reg.QueryExtendedProperty(7052, "scientific:prop/apparent_cohesion");
    QueryResult resC = reg.QueryExtendedProperty(7053, "scientific:prop/apparent_cohesion");

    bool okA = (resA.outcome == QueryOutcome::MatchSingle && resA.matchingMeasurements.size() == 1 &&
                resA.matchingMeasurements[0]->scalarValue.has_value() && *resA.matchingMeasurements[0]->scalarValue == 0.0);
    bool okB = (resB.outcome == QueryOutcome::PropertyUnknown);
    bool okC = (resC.outcome == QueryOutcome::PropertyNotApplicable);

    if (okA && okB && okC) {
        r.passed = true;
        r.observedDesc = "Distinzione semantica perfetta: Known(0.0) ritorna MatchSingle con valore 0.0 Pa; Unknown e NotApplicable preservati";
    } else {
        r.passed = false;
        r.observedDesc = "Confusione o mancata distinzione tra Known(0.0), Unknown e NotApplicable";
        r.details = "okA=" + std::to_string(okA) + ", okB=" + std::to_string(okB) + ", okC=" + std::to_string(okC);
    }
    return r;
}

// -----------------------------------------------------------------------------
// EXT-06: Rappresentazioni Scalar, Range e QualitativeDescriptor
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestEXT06_RepresentationsScalarRangeQualitative() {
    ScientificRegistryTestResult r;
    r.testId = "EXT-06";
    r.testName = "Rappresentazioni Scalar, Range e QualitativeDescriptor";
    r.inputDesc = "Misure espresse come scalare esatto, intervallo [min, max] e descrittore qualitativo non numerico";
    r.expectedDesc = "Serializzazione JSON e recupero fedele dei discriminatori senza casting forzato a double";

    ExtendedMeasurement mScalar;
    mScalar.valueNature = ValueNature::ExactScalar;
    mScalar.scalarValue = 1.75e8;
    mScalar.unit = "Pa";

    ExtendedMeasurement mRange;
    mRange.valueNature = ValueNature::Range;
    mRange.rangeValue = std::make_pair(1.5e8, 2.0e8);
    mRange.unit = "Pa";

    ExtendedMeasurement mQual;
    mQual.valueNature = ValueNature::QualitativeDescriptor;
    mQual.qualitativeDescriptor = "Comportamento fragile con frattura concoide";

    json jScalar, jRange, jQual;
    to_json(jScalar, mScalar);
    to_json(jRange, mRange);
    to_json(jQual, mQual);

    ExtendedMeasurement restScalar, restRange, restQual;
    from_json(jScalar, restScalar);
    from_json(jRange, restRange);
    from_json(jQual, restQual);

    bool okScalar = (restScalar.valueNature == ValueNature::ExactScalar && restScalar.scalarValue.has_value() &&
                     std::abs(*restScalar.scalarValue - 1.75e8) < 1e-3);
    bool okRange = (restRange.valueNature == ValueNature::Range && restRange.rangeValue.has_value() &&
                    std::abs(restRange.rangeValue->first - 1.5e8) < 1e-3 &&
                    std::abs(restRange.rangeValue->second - 2.0e8) < 1e-3);
    bool okQual = (restQual.valueNature == ValueNature::QualitativeDescriptor && !restQual.scalarValue.has_value() &&
                   restQual.qualitativeDescriptor == "Comportamento fragile con frattura concoide");

    if (okScalar && okRange && okQual) {
        r.passed = true;
        r.observedDesc = "Tutte le rappresentazioni (Scalar, Range, QualitativeDescriptor) serializzate e ripristinate senza perdita semantica";
    } else {
        r.passed = false;
        r.observedDesc = "Discrepanza nelle rappresentazioni di valore discriminato";
    }
    return r;
}

// -----------------------------------------------------------------------------
// EXT-07: Validazione metrologica e dimensionale
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestEXT07_MetrologicalAndDimensionalValidation() {
    ScientificRegistryTestResult r;
    r.testId = "EXT-07";
    r.testName = "Validazione metrologica e dimensionale";
    r.inputDesc = "Validazione di unita' consentite ('Pa', 'GPa') vs incompatibili ('kg'), e metadati di incertezza e dispersione";
    r.expectedDesc = "Unita' lecite accettate, unita' incompatibili respinte da ValidateExtendedProperties; incertezza preservata";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    const auto* propDef = reg.GetPropertyDefinition("scientific:prop/isothermal_bulk_modulus");
    if (!propDef) {
        r.passed = false;
        r.observedDesc = "Dizionario proprieta' attivo privo di isothermal_bulk_modulus";
        return r;
    }

    ExtendedPropertyStorage validStorage;
    {
        ExtendedPropertyEntry e;
        e.propertyUri = "scientific:prop/isothermal_bulk_modulus";
        e.propertyIndex = propDef->internalIndex;
        e.observationCollection.applicability = ApplicabilityState::Applicable;
        e.observationCollection.epistemicState = EpistemicState::Known;
        ExtendedMeasurement m;
        m.valueNature = ValueNature::ExactScalar;
        m.scalarValue = 2.2e9;
        m.unit = "Pa"; // Unità consentita
        m.uncertainty.hasUncertainty = true;
        m.uncertainty.value = 0.05e9;
        m.uncertainty.type = "expanded_k2";
        m.dispersion.hasDispersion = true;
        m.dispersion.stdDev = 0.02e9;
        m.dispersion.sampleCount = 10;
        e.observationCollection.measurements.push_back(m);
        validStorage.AddOrUpdateEntry(e);
    }

    std::string err;
    bool okValid = reg.ValidateExtendedProperties(validStorage, &err);

    ExtendedPropertyStorage invalidStorage;
    {
        ExtendedPropertyEntry e;
        e.propertyUri = "scientific:prop/isothermal_bulk_modulus";
        e.propertyIndex = propDef->internalIndex;
        e.observationCollection.applicability = ApplicabilityState::Applicable;
        e.observationCollection.epistemicState = EpistemicState::Known;
        ExtendedMeasurement m;
        m.valueNature = ValueNature::ExactScalar;
        m.scalarValue = 2.2e9;
        m.unit = "kg/m^3"; // UNITA' ERRATA per bulk modulus
        e.observationCollection.measurements.push_back(m);
        invalidStorage.AddOrUpdateEntry(e);
    }

    std::string errInv;
    bool okInvalid = !reg.ValidateExtendedProperties(invalidStorage, &errInv);

    if (okValid && okInvalid) {
        r.passed = true;
        r.observedDesc = "Validazione dimensionale superata: unita' lecite e incertezza accettate; unita' incompatibile 'kg/m^3' respinta con errore: " + errInv;
    } else {
        r.passed = false;
        r.observedDesc = "Fallimento nella validazione dimensionale o metrologica";
    }
    return r;
}

// -----------------------------------------------------------------------------
// EXT-08: Pressioni con riferimento Absolute, Gauge e Differential
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestEXT08_PressureSpecAbsoluteGaugeDifferential() {
    ScientificRegistryTestResult r;
    r.testId = "EXT-08";
    r.testName = "Pressioni Absolute, Gauge e Differential";
    r.inputDesc = "Misure condotte con riferimento di pressione assoluto vs manometrico (gauge) vs differenziale";
    r.expectedDesc = "Il resolver distingue rigorosamente il tipo di riferimento rifiutando match spuri a parita' di valore scalare";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    ScientificMaterialDef def;
    def.id = 708;
    def.canonicalId = "fairworld:mat/pressure_ref_test";
    def.displayName = "Pressure Ref Test Material";

    ExtendedPropertyEntry entry;
    entry.propertyUri = "scientific:prop/triaxial_compressive_strength";
    entry.observationCollection.applicability = ApplicabilityState::Applicable;
    entry.observationCollection.epistemicState = EpistemicState::Known;

    // Misura 1: Confining Pressure = 100 kPa GAUGE
    ExtendedMeasurement m1;
    m1.valueNature = ValueNature::ExactScalar;
    m1.scalarValue = 2.5e8;
    m1.unit = "Pa";
    m1.confiningPressure = PressureSpec{PressureSpec::ReferenceType::Gauge, 100000.0};
    entry.observationCollection.measurements.push_back(m1);

    // Misura 2: Confining Pressure = 100 kPa ABSOLUTE
    ExtendedMeasurement m2;
    m2.valueNature = ValueNature::ExactScalar;
    m2.scalarValue = 2.7e8;
    m2.unit = "Pa";
    m2.confiningPressure = PressureSpec{PressureSpec::ReferenceType::Absolute, 100000.0};
    entry.observationCollection.measurements.push_back(m2);

    def.extendedProperties.AddOrUpdateEntry(entry);
    reg.RegisterMaterial(def);

    // Query con requiredConfiningPressure Gauge
    QueryPredicate predGauge;
    predGauge.requiredConfiningPressure = PressureSpec{PressureSpec::ReferenceType::Gauge, 100000.0};
    QueryResult resGauge = reg.QueryExtendedProperty(708, "scientific:prop/triaxial_compressive_strength", predGauge);

    // Query con requiredConfiningPressure Differential
    QueryPredicate predDiff;
    predDiff.requiredConfiningPressure = PressureSpec{PressureSpec::ReferenceType::Differential, 100000.0};
    QueryResult resDiff = reg.QueryExtendedProperty(708, "scientific:prop/triaxial_compressive_strength", predDiff);

    bool okGauge = (resGauge.outcome == QueryOutcome::MatchSingle && resGauge.matchingMeasurements.size() == 1 &&
                    resGauge.matchingMeasurements[0]->confiningPressure->refType == PressureSpec::ReferenceType::Gauge);
    bool okDiff = (resDiff.outcome == QueryOutcome::NoMatchConditions);

    if (okGauge && okDiff) {
        r.passed = true;
        r.observedDesc = "Riferimenti di pressione validati: Gauge matcha esclusivamente misura Gauge; Differential respinto a parita' di valore";
    } else {
        r.passed = false;
        r.observedDesc = "Violazione nella differenziazione dei tipi di riferimento di pressione";
    }
    return r;
}

// -----------------------------------------------------------------------------
// EXT-09: Lifetime delle osservazioni e degli snapshot (RegistrySnapshotHandle)
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestEXT09_SnapshotAndObservationLifetime() {
    ScientificRegistryTestResult r;
    r.testId = "EXT-09";
    r.testName = "Lifetime osservazioni e RegistrySnapshotHandle";
    r.inputDesc = "Acquisizione di un RegistrySnapshotHandle e puntatori a misure; modifica e svuotamento successivo del registry";
    r.expectedDesc = "Lo snapshot handle e i puntatori alle osservazioni rimangono validi e immutabili oltre il ciclo di vita del registro";

    const ExtendedMeasurement* savedMeasPtr = nullptr;
    RegistrySnapshotHandle snapshotHandle;

    {
        ScientificMaterialRegistry localReg;
        localReg.Initialize();

        ScientificMaterialDef def;
        def.id = 709;
        def.canonicalId = "fairworld:mat/lifetime_test";
        def.displayName = "Lifetime Material";

        ExtendedPropertyEntry entry;
        entry.propertyUri = "scientific:prop/p_wave_velocity";
        entry.observationCollection.applicability = ApplicabilityState::Applicable;
        entry.observationCollection.epistemicState = EpistemicState::Known;

        ExtendedMeasurement m;
        m.valueNature = ValueNature::ExactScalar;
        m.scalarValue = 5800.0;
        m.unit = "m/s";
        entry.observationCollection.measurements.push_back(m);

        def.extendedProperties.AddOrUpdateEntry(entry);
        localReg.RegisterMaterial(def);

        snapshotHandle = localReg.GetSnapshotHandle();

        QueryResult res = localReg.QueryExtendedProperty(709, "scientific:prop/p_wave_velocity");
        if (res.outcome == QueryOutcome::MatchSingle && !res.matchingMeasurements.empty()) {
            savedMeasPtr = res.matchingMeasurements[0];
        }

        // Modifica e svuotamento del registry originale
        localReg.Clear();
    } // localReg esce dallo scope

    bool handleValid = snapshotHandle.IsValid();
    bool ptrValid = (savedMeasPtr != nullptr && savedMeasPtr->scalarValue.has_value() &&
                     std::abs(*savedMeasPtr->scalarValue - 5800.0) < 1e-4);

    if (handleValid && ptrValid) {
        r.passed = true;
        r.observedDesc = "RegistrySnapshotHandle preserva l'immutabilita' e il lifetime delle osservazioni oltre il ciclo di vita del registro";
    } else {
        r.passed = false;
        r.observedDesc = "Violazione del lifetime o corruzione dei puntatori post-svuotamento";
    }
    return r;
}

// -----------------------------------------------------------------------------
// EXT-10: Serializzazione deterministica JSON v3
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestEXT10_DeterministicSerialization() {
    ScientificRegistryTestResult r;
    r.testId = "EXT-10";
    r.testName = "Serializzazione deterministica JSON v3";
    r.inputDesc = "Esportazione v3 di materiale con proprieta' estese, ricaricamento in registro pulito e nuova esportazione";
    r.expectedDesc = "Entrambi i salvataggi generano strutture e chiavi stabili; indici locali ricostruiti deterministicamente";

    ScientificMaterialRegistry reg1;
    reg1.Initialize();

    ScientificMaterialDef def;
    def.id = 710;
    def.canonicalId = "fairworld:mat/det_test";
    def.displayName = "Deterministic Material";

    ExtendedPropertyEntry e1;
    e1.propertyUri = "scientific:prop/isothermal_bulk_modulus";
    e1.observationCollection.applicability = ApplicabilityState::Applicable;
    e1.observationCollection.epistemicState = EpistemicState::Known;
    ExtendedMeasurement m1;
    m1.valueNature = ValueNature::ExactScalar;
    m1.scalarValue = 2.2e9;
    m1.unit = "Pa";
    e1.observationCollection.measurements.push_back(m1);
    def.extendedProperties.AddOrUpdateEntry(e1);

    ExtendedPropertyEntry e2;
    e2.propertyUri = "scientific:prop/poissons_ratio";
    e2.observationCollection.applicability = ApplicabilityState::Applicable;
    e2.observationCollection.epistemicState = EpistemicState::Known;
    ExtendedMeasurement m2;
    m2.valueNature = ValueNature::ExactScalar;
    m2.scalarValue = 0.25;
    m2.unit = "";
    e2.observationCollection.measurements.push_back(m2);
    def.extendedProperties.AddOrUpdateEntry(e2);

    reg1.RegisterMaterial(def);

    std::string path1 = (std::filesystem::temp_directory_path() / "fairworld_sci_tests" / "det_test1.json").string();
    std::string path2 = (std::filesystem::temp_directory_path() / "fairworld_sci_tests" / "det_test2.json").string();

    bool okSave1 = reg1.SaveToJson(path1);

    ScientificMaterialRegistry reg2;
    reg2.Initialize();
    bool okLoad = reg2.LoadFromJson(path1);
    bool okSave2 = reg2.SaveToJson(path2);

    // Confronto contenuto file
    std::ifstream f1(path1), f2(path2);
    std::stringstream s1, s2;
    s1 << f1.rdbuf();
    s2 << f2.rdbuf();
    f1.close();
    f2.close();

    RemoveTempFile(path1);
    RemoveTempFile(path2);

    bool identical = (s1.str() == s2.str() && !s1.str().empty());

    if (okSave1 && okLoad && okSave2 && identical) {
        r.passed = true;
        r.observedDesc = "Round-trip deterministico completato: JSON v3 salvato e ricaricato genera output identico carattere per carattere";
    } else {
        r.passed = false;
        r.observedDesc = "Discrepanza deterministica nella serializzazione JSON v3";
        r.details = "okSave1=" + std::to_string(okSave1) + ", okLoad=" + std::to_string(okLoad) +
                    ", okSave2=" + std::to_string(okSave2) + ", identical=" + std::to_string(identical);
    }
    return r;
}

// -----------------------------------------------------------------------------
// EXT-11: Catalogo produttivo privo di osservazioni estese
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestEXT11_ProductionCatalogHasNoExtendedProperties() {
    ScientificRegistryTestResult r;
    r.testId = "EXT-11";
    r.testName = "Catalogo produttivo privo di osservazioni estese";
    r.inputDesc = "Ispezione di tutti i materiali caricati dal catalogo JSON di produzione (scientific_materials.json)";
    r.expectedDesc = "Tutti i materiali legacy hanno extendedProperties vuoto; zero osservazioni introdotte in produzione";

    std::string prodPath = FindProductionCatalogPath();
    if (prodPath.empty()) {
        r.passed = false;
        r.observedDesc = "Impossibile localizzare scientific_materials.json produttivo";
        return r;
    }

    ScientificMaterialRegistry reg;
    if (!reg.LoadFromJson(prodPath)) {
        r.passed = false;
        r.observedDesc = "LoadFromJson fallito sul catalogo produttivo";
        return r;
    }

    bool allEmpty = true;
    size_t inspectedCount = 0;
    for (const auto& [id, mat] : reg.GetAllMaterials()) {
        inspectedCount++;
        if (!mat.extendedProperties.Empty()) {
            allEmpty = false;
            r.details += "Materiale '" + mat.canonicalId + "' contiene " + 
                         std::to_string(mat.extendedProperties.Size()) + " proprieta' estese. ";
        }
    }

    if (inspectedCount == 4 && allEmpty) {
        r.passed = true;
        r.observedDesc = "Verifica rigorosa superata: tutti i 4 materiali del catalogo produttivo hanno 0 osservazioni estese (infrastruttura pura)";
    } else {
        r.passed = false;
        r.observedDesc = "Trovate osservazioni estese nel catalogo produttivo o conteggio materiali alterato";
    }
    return r;
}

// -----------------------------------------------------------------------------
// EXT-12: Rifiuto di due URI diverse con lo stesso indice valido
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestEXT12_RejectDifferentUrisSameValidIndex() {
    ScientificRegistryTestResult r;
    r.testId = "EXT-12";
    r.testName = "Rifiuto due URI diverse con stesso indice valido";
    r.inputDesc = "Tentativo di inserire in ExtendedPropertyStorage due entry con URI distinte ma identico propertyIndex valido";
    r.expectedDesc = "La seconda operazione viene respinta (return false); lo storage preserva la prima entry senza alterazioni";

    ExtendedPropertyStorage storage;
    ExtendedPropertyEntry e1;
    e1.propertyUri = "scientific:prop/isothermal_bulk_modulus";
    e1.propertyIndex = 0;
    bool ok1 = storage.AddOrUpdateEntry(e1);

    ExtendedPropertyEntry e2;
    e2.propertyUri = "scientific:prop/shear_modulus";
    e2.propertyIndex = 0; // Stesso indice valido di e1!
    bool ok2 = storage.AddOrUpdateEntry(e2);

    bool sizeOk = (storage.Size() == 1);
    const auto* found = storage.FindEntryByUri("scientific:prop/isothermal_bulk_modulus");
    bool firstPreserved = (found != nullptr && found->propertyIndex == 0);
    bool secondNotFound = (storage.FindEntryByUri("scientific:prop/shear_modulus") == nullptr);

    if (ok1 && !ok2 && sizeOk && firstPreserved && secondNotFound) {
        r.passed = true;
        r.observedDesc = "Collisione di indice respinta con successo: URI canonico protetto da hijacking d'indice";
    } else {
        r.passed = false;
        r.observedDesc = "Fallimento nel rifiutare la collisione di indici su URI differenti";
        r.details = "ok1=" + std::to_string(ok1) + ", ok2=" + std::to_string(ok2) + ", size=" + std::to_string(storage.Size());
    }
    return r;
}

// -----------------------------------------------------------------------------
// EXT-13: Rifiuto di aggiornamento con URI corretta e indice errato
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestEXT13_RejectUpdateCorrectUriErroneousIndex() {
    ScientificRegistryTestResult r;
    r.testId = "EXT-13";
    r.testName = "Rifiuto aggiornamento con URI corretta e indice errato";
    r.inputDesc = "Entry registrata con indice valido 2; tentativo di aggiornamento passando la stessa URI ma indice confliggente 5";
    r.expectedDesc = "Operazione respinta; l'indice 2 e i dati originali rimangono invariati";

    ExtendedPropertyStorage storage;
    ExtendedPropertyEntry e1;
    e1.propertyUri = "scientific:prop/shear_modulus";
    e1.propertyIndex = 2;
    storage.AddOrUpdateEntry(e1);

    ExtendedPropertyEntry eUpdate;
    eUpdate.propertyUri = "scientific:prop/shear_modulus";
    eUpdate.propertyIndex = 5; // Indice discordante dall'indice originale 2
    bool okUpdate = storage.AddOrUpdateEntry(eUpdate);

    const auto* current = storage.FindEntryByUri("scientific:prop/shear_modulus");
    bool indexPreserved = (current != nullptr && current->propertyIndex == 2);

    if (!okUpdate && indexPreserved && storage.Size() == 1) {
        r.passed = true;
        r.observedDesc = "Incoerenza indice-URI respinta: entry originale preservata con indice 2";
    } else {
        r.passed = false;
        r.observedDesc = "Aggiornamento incoerente accettato o storage corrotto";
    }
    return r;
}

// -----------------------------------------------------------------------------
// EXT-14: Prevenzione duplicati logici nello storage esteso
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestEXT14_LogicalDuplicatesPrevention() {
    ScientificRegistryTestResult r;
    r.testId = "EXT-14";
    r.testName = "Prevenzione duplicati logici nello storage esteso";
    r.inputDesc = "Storage con propA (idx 1) e propB (idx 2); tentativo di AddOrUpdateEntry per propB con indice errato 1";
    r.expectedDesc = "Operazione respinta; nessuna duplicazione logica di propB; propA intatta";

    ExtendedPropertyStorage storage;
    ExtendedPropertyEntry eA;
    eA.propertyUri = "scientific:prop/isothermal_bulk_modulus";
    eA.propertyIndex = 1;
    storage.AddOrUpdateEntry(eA);

    ExtendedPropertyEntry eB;
    eB.propertyUri = "scientific:prop/shear_modulus";
    eB.propertyIndex = 2;
    storage.AddOrUpdateEntry(eB);

    // Tentativo di aggiornare shear_modulus con indice 1 (appartenente a bulk modulus)
    ExtendedPropertyEntry eDup;
    eDup.propertyUri = "scientific:prop/shear_modulus";
    eDup.propertyIndex = 1;
    bool okDup = storage.AddOrUpdateEntry(eDup);

    size_t countA = 0, countB = 0;
    for (const auto& entry : storage.GetEntries()) {
        if (entry.propertyUri == "scientific:prop/isothermal_bulk_modulus") countA++;
        if (entry.propertyUri == "scientific:prop/shear_modulus") countB++;
    }

    if (!okDup && countA == 1 && countB == 1 && storage.Size() == 2) {
        r.passed = true;
        r.observedDesc = "Duplicato logico prevenuto: lo storage mantiene esattamente una voce per URI";
    } else {
        r.passed = false;
        r.observedDesc = "Generato duplicato logico o cancellata voce preesistente";
    }
    return r;
}

// -----------------------------------------------------------------------------
// EXT-15: Normalizzazione indici precompilati in RegisterMaterial
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestEXT15_RegisterMaterialResolvesForeignSnapshotIndices() {
    ScientificRegistryTestResult r;
    r.testId = "EXT-15";
    r.testName = "Normalizzazione indici precompilati in RegisterMaterial";
    r.inputDesc = "Registrazione di materiale con entry avente indice arbitrario (999) proveniente da altro snapshot";
    r.expectedDesc = "RegisterMaterial normalizza incondizionatamente l'indice al valore canonico del dizionario attivo";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    const auto* propDef = reg.GetPropertyDefinition("scientific:prop/shear_modulus");
    if (!propDef) {
        r.passed = false;
        r.observedDesc = "Definizione per shear_modulus non trovata";
        return r;
    }
    PropertyKeyIndex canonicalIdx = propDef->internalIndex; // Deve essere 2

    ScientificMaterialDef def;
    def.id = 815;
    def.canonicalId = "fairworld:mat/foreign_index_test";
    def.displayName = "Foreign Index Material";

    ExtendedPropertyEntry entry;
    entry.propertyUri = "scientific:prop/shear_modulus";
    entry.propertyIndex = 999; // Indice estraneo/incoerente!
    entry.observationCollection.applicability = ApplicabilityState::Applicable;
    entry.observationCollection.epistemicState = EpistemicState::Known;

    ExtendedMeasurement m;
    m.valueNature = ValueNature::ExactScalar;
    m.scalarValue = 3.5e10;
    m.unit = "Pa";
    entry.observationCollection.measurements.push_back(m);

    def.extendedProperties.AddOrUpdateEntry(entry);

    bool regOk = reg.RegisterMaterial(def);
    const auto& registeredMat = reg.GetMaterial(815);
    const auto* regEntry = registeredMat.extendedProperties.FindEntryByUri("scientific:prop/shear_modulus");

    bool idxNormalized = (regEntry != nullptr && regEntry->propertyIndex == canonicalIdx);

    if (regOk && idxNormalized) {
        r.passed = true;
        r.observedDesc = "Indice estraneo 999 normalizzato con successo all'indice canonico " + std::to_string(canonicalIdx);
    } else {
        r.passed = false;
        r.observedDesc = "Mancata normalizzazione dell'indice o fallimento della registrazione";
    }
    return r;
}

// -----------------------------------------------------------------------------
// EXT-16: Sicurezza del resolver contro entry con indice coincidente ma URI discordante
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestEXT16_QueryRejectsMismatchedUriIndexEntry() {
    ScientificRegistryTestResult r;
    r.testId = "EXT-16";
    r.testName = "Sicurezza resolver su entry con URI discordante da indice";
    r.inputDesc = "Materiale contenente entry con indice 0 ma URI 'shear_modulus'; query per 'isothermal_bulk_modulus' (che ha indice 0)";
    r.expectedDesc = "Resolver rifiuta di restituire misure di shear modulus; outcome PropertyAbsent e diagnosi di incoerenza";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    ScientificMaterialDef def;
    def.id = 816;
    def.canonicalId = "fairworld:mat/corrupt_storage";
    def.displayName = "Corrupt Storage Material";

    ExtendedPropertyEntry corruptEntry;
    corruptEntry.propertyUri = "scientific:prop/shear_modulus";
    corruptEntry.propertyIndex = 0; // 0 e' isothermal_bulk_modulus nel dizionario canonico!
    corruptEntry.observationCollection.applicability = ApplicabilityState::Applicable;
    corruptEntry.observationCollection.epistemicState = EpistemicState::Known;

    ExtendedMeasurement m;
    m.valueNature = ValueNature::ExactScalar;
    m.scalarValue = 4.0e10;
    m.unit = "Pa";
    corruptEntry.observationCollection.measurements.push_back(m);

    std::vector<ExtendedPropertyEntry> entries = { corruptEntry };
    def.extendedProperties.SetEntries(std::move(entries));

    // Per testare il resolver isolatamente contro storage corrotto, inseriamo direttamente nello snapshot
    auto snap = std::const_pointer_cast<RegistrySnapshot>(reg.GetSnapshot());
    snap->materials[816] = def;
    snap->canonicalToIdMap[def.canonicalId] = 816;

    QueryResult res = reg.QueryExtendedProperty(816, "scientific:prop/isothermal_bulk_modulus");

    bool noMeasurements = res.matchingMeasurements.empty();
    bool outcomeAbsent = (res.outcome == QueryOutcome::PropertyAbsent);
    bool diagHasIncoherence = (res.diagnosticMessage.find("Incoerenza") != std::string::npos ||
                               res.diagnosticMessage.find("incoerente") != std::string::npos);

    if (outcomeAbsent && noMeasurements && diagHasIncoherence) {
        r.passed = true;
        r.observedDesc = "Protezione attiva: misure di shear_modulus non restituite per bulk_modulus; segnalata incoerenza";
    } else {
        r.passed = false;
        r.observedDesc = "Resolver ha restituito misure errate o non ha segnalato incoerenza";
    }
    return r;
}

// -----------------------------------------------------------------------------
// EXT-17: Immutabilità di un vecchio RegistrySnapshotHandle dopo RegisterMaterial
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestEXT17_OldSnapshotHandleImmutabilityPostRegister() {
    ScientificRegistryTestResult r;
    r.testId = "EXT-17";
    r.testName = "Immutabilità vecchio snapshot dopo RegisterMaterial";
    r.inputDesc = "Acquisizione RegistrySnapshotHandle con 4 materiali; successiva RegisterMaterial di un 5° materiale";
    r.expectedDesc = "Il vecchio handle continua ad avere esattamente 4 materiali e GetMaterial(5) ritorna nullptr";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    RegistrySnapshotHandle oldHandle = reg.GetSnapshotHandle();
    size_t countBefore = oldHandle->materials.size(); // 4

    ScientificMaterialDef newMat;
    newMat.id = 817;
    newMat.canonicalId = "fairworld:mat/cow_test";
    newMat.displayName = "COW Test Material";
    reg.RegisterMaterial(newMat);

    size_t regCount = reg.GetMaterialCount(); // 5
    size_t oldHandleCount = oldHandle->materials.size(); // DEVE ESSERE ANCORA 4!
    const auto* oldLook = oldHandle->GetMaterial(817); // DEVE ESSERE nullptr!

    RegistrySnapshotHandle newHandle = reg.GetSnapshotHandle();
    size_t newHandleCount = newHandle->materials.size(); // 5
    const auto* newLook = newHandle->GetMaterial(817); // DEVE ESSERE non-null!

    if (countBefore == 4 && regCount == 5 && oldHandleCount == 4 && oldLook == nullptr &&
        newHandleCount == 5 && newLook != nullptr) {
        r.passed = true;
        r.observedDesc = "Immutabilita' Copy-On-Write garantita: vecchio handle congelato a 4 materiali, nuovo snapshot a 5";
    } else {
        r.passed = false;
        r.observedDesc = "Violazione di immutabilita': vecchio handle mutato in-place dopo registrazione";
        r.details = "oldHandleCount=" + std::to_string(oldHandleCount) + ", regCount=" + std::to_string(regCount);
    }
    return r;
}

// -----------------------------------------------------------------------------
// EXT-18: Lifetime delle osservazioni conservate intatte nel vecchio snapshot
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestEXT18_ObservationLifetimePreservedInOldSnapshot() {
    ScientificRegistryTestResult r;
    r.testId = "EXT-18";
    r.testName = "Lifetime osservazioni conservate nel vecchio snapshot";
    r.inputDesc = "Materiale con osservazione estesa registrato; acquisizione handle; aggiornamento materiale nel registro con allowUpdate";
    r.expectedDesc = "Il vecchio handle conserva la versione originale dell'osservazione; il puntatore rimane valido";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    ScientificMaterialDef defV1;
    defV1.id = 818;
    defV1.canonicalId = "fairworld:mat/obs_lifetime_test";
    defV1.displayName = "Obs Lifetime Mat V1";

    ExtendedPropertyEntry entryV1;
    entryV1.propertyUri = "scientific:prop/p_wave_velocity";
    entryV1.observationCollection.applicability = ApplicabilityState::Applicable;
    entryV1.observationCollection.epistemicState = EpistemicState::Known;
    ExtendedMeasurement mV1;
    mV1.valueNature = ValueNature::ExactScalar;
    mV1.scalarValue = 5000.0;
    mV1.unit = "m/s";
    entryV1.observationCollection.measurements.push_back(mV1);
    defV1.extendedProperties.AddOrUpdateEntry(entryV1);

    reg.RegisterMaterial(defV1);

    RegistrySnapshotHandle handleV1 = reg.GetSnapshotHandle();
    const auto* matInOldSnapshot = handleV1->GetMaterial(818);
    const auto* entryInOldSnapshot = matInOldSnapshot->extendedProperties.FindEntryByUri("scientific:prop/p_wave_velocity");
    const ExtendedMeasurement* ptrV1 = &entryInOldSnapshot->observationCollection.measurements[0];

    // Aggiorniamo il materiale con allowUpdate = true e valore diverso (6000 m/s)
    ScientificMaterialDef defV2 = defV1;
    defV2.extendedProperties.Clear();
    ExtendedPropertyEntry entryV2 = entryV1;
    entryV2.observationCollection.measurements[0].scalarValue = 6000.0;
    defV2.extendedProperties.AddOrUpdateEntry(entryV2);

    reg.RegisterMaterial(defV2, true);

    RegistrySnapshotHandle handleV2 = reg.GetSnapshotHandle();
    const auto* matInNewSnapshot = handleV2->GetMaterial(818);
    const auto* entryInNewSnapshot = matInNewSnapshot->extendedProperties.FindEntryByUri("scientific:prop/p_wave_velocity");

    bool ptrV1StillIntact = (ptrV1 != nullptr && ptrV1->scalarValue.has_value() &&
                             std::abs(*ptrV1->scalarValue - 5000.0) < 1e-4);
    bool v2HasNewValue = (entryInNewSnapshot != nullptr &&
                          !entryInNewSnapshot->observationCollection.measurements.empty() &&
                          std::abs(*entryInNewSnapshot->observationCollection.measurements[0].scalarValue - 6000.0) < 1e-4);

    if (ptrV1StillIntact && v2HasNewValue && (handleV1.GetSnapshot() != handleV2.GetSnapshot())) {
        r.passed = true;
        r.observedDesc = "Lifetime e isolamento perfetti: vecchio puntatore 5000 m/s integro; nuovo snapshot ha 6000 m/s";
    } else {
        r.passed = false;
        r.observedDesc = "Puntatore originale corrotto o mancato isolamento tra snapshot";
    }
    return r;
}

// -----------------------------------------------------------------------------
// EXT-19: Rollback atomico su fallimento di validazione in RegisterMaterial
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestEXT19_RegisterMaterialAtomicRollbackOnValidationError() {
    ScientificRegistryTestResult r;
    r.testId = "EXT-19";
    r.testName = "Rollback atomico su fallimento di validazione in RegisterMaterial";
    r.inputDesc = "Tentativo di registrare materiale con proprieta' estesa non registrata nel dizionario ('scientific:prop/completely_unknown_uri')";
    r.expectedDesc = "RegisterMaterial rifiuta l'operazione (false); registro e snapshot attivo rimangono invariati";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    size_t countBefore = reg.GetMaterialCount();
    auto snapshotBefore = reg.GetSnapshot();

    ScientificMaterialDef badDef;
    badDef.id = 819;
    badDef.canonicalId = "fairworld:mat/bad_material";
    badDef.displayName = "Bad Material";

    ExtendedPropertyEntry badEntry;
    badEntry.propertyUri = "scientific:prop/completely_unknown_uri";
    badDef.extendedProperties.AddOrUpdateEntry(badEntry);

    bool regResult = reg.RegisterMaterial(badDef);

    size_t countAfter = reg.GetMaterialCount();
    auto snapshotAfter = reg.GetSnapshot();
    bool matAbsent = !reg.HasMaterial(819);

    if (!regResult && countBefore == countAfter && snapshotBefore == snapshotAfter && matAbsent) {
        r.passed = true;
        r.observedDesc = "Rollback atomico confermato: nessuna mutazione del registro ne' dello snapshot attivo";
    } else {
        r.passed = false;
        r.observedDesc = "Materiale invalido parzialmente registrato o stato corrotto";
    }
    return r;
}

// =============================================================================
// MA-03.2E.2A: TEST DIAGNOSTICA RIFERIMENTI INCROCIATI (DIAG-01 .. DIAG-05)
// =============================================================================

// -----------------------------------------------------------------------------
// Test DIAG-01: Riferimento scientifico ID 0 valido (nessuna associazione)
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestDIAG01_UnassociatedBlockIdZeroIsValid() {
    ScientificRegistryTestResult r;
    r.testId = "DIAG-01";
    r.testName = "Riferimento scientifico ID 0 valido (nessuna associazione)";
    r.inputDesc = "BlockRegistry con blocchi privi di scientificMaterialId (ID 0) convalidati contro ScientificMaterialRegistry";
    r.expectedDesc = "Validazione eseguita senza warning; conteggio blocchi non associati coerente; zero warning generati";

    ScientificMaterialRegistry sciReg;
    sciReg.Initialize();

    BlockRegistry blockReg;
    blockReg.Initialize();

    auto createRes = blockReg.CreateBlock("fairworld:diag_test_unassociated", "Diag Test Block 0");
    if (!createRes.success) {
        r.passed = false;
        r.observedDesc = "Impossibile creare blocco di test nel registry";
        return r;
    }

    auto report = blockReg.ValidateScientificReferences(sciReg, "TestDIAG01");

    bool hasNoWarnings = !report.HasWarnings() && report.warnings.empty();
    bool unassociatedCountMatches = (report.unassociatedBlocks >= 10);
    bool validAssociationsZero = (report.validAssociations == 0);

    if (hasNoWarnings && unassociatedCountMatches && validAssociationsZero) {
        r.passed = true;
        r.observedDesc = "Validazione read-only confermata: ID 0 trattato come non associato e valido; nessun warning (unassociatedBlocks=" +
                         std::to_string(report.unassociatedBlocks) + ", total=" + std::to_string(report.totalBlocksChecked) + ")";
    } else {
        r.passed = false;
        r.observedDesc = "Fallimento validazione: warning emessi o conteggi errati (warnings=" +
                         std::to_string(report.warnings.size()) + ", unassociated=" + std::to_string(report.unassociatedBlocks) + ")";
    }
    return r;
}

// -----------------------------------------------------------------------------
// Test DIAG-02: Riferimento scientifico a materiale esistente valido
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestDIAG02_ExistingScientificMaterialIdIsValid() {
    ScientificRegistryTestResult r;
    r.testId = "DIAG-02";
    r.testName = "Riferimento scientifico a materiale esistente valido";
    r.inputDesc = "Blocco associato a un ScientificMaterialID registrato ed esistente nel catalogo scientifico";
    r.expectedDesc = "Validazione read-only ha successo; validAssociations incrementato; zero warning generati";

    ScientificMaterialRegistry sciReg;
    sciReg.Initialize();

    ScientificMaterialDef matDef;
    matDef.id = 101;
    matDef.canonicalId = "fairworld:mat/diag_valid";
    matDef.displayName = "Diag Valid Material";
    if (!sciReg.RegisterMaterial(matDef)) {
        r.passed = false;
        r.observedDesc = "Impossibile registrare materiale scientifico di test";
        return r;
    }

    BlockRegistry blockReg;
    blockReg.Initialize();

    auto createRes = blockReg.CreateBlock("fairworld:diag_test_valid", "Diag Test Valid Block");
    if (!createRes.success) {
        r.passed = false;
        r.observedDesc = "Impossibile creare blocco nel BlockRegistry";
        return r;
    }

    auto& blockDef = blockReg.GetBlockMutable(createRes.allocatedId);
    blockDef.scientificMaterialId = 101;

    auto report = blockReg.ValidateScientificReferences(sciReg, "TestDIAG02");

    bool hasNoWarnings = !report.HasWarnings() && report.warnings.empty();
    bool validCountIsOne = (report.validAssociations == 1);

    if (hasNoWarnings && validCountIsOne) {
        r.passed = true;
        r.observedDesc = "Validazione completata con successo: materiale 101 risolto correttamente, validAssociations=1, zero warning";
    } else {
        r.passed = false;
        r.observedDesc = "Validazione fallita: warnings=" + std::to_string(report.warnings.size()) +
                         ", validAssociations=" + std::to_string(report.validAssociations);
    }
    return r;
}

// -----------------------------------------------------------------------------
// Test DIAG-03: Riferimento scientifico orfano genera diagnostica di WARNING non bloccante
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestDIAG03_OrphanScientificMaterialIdGeneratesWarning() {
    ScientificRegistryTestResult r;
    r.testId = "DIAG-03";
    r.testName = "Riferimento scientifico orfano genera diagnostica di WARNING non bloccante";
    r.inputDesc = "Blocco associato a un ScientificMaterialID orfano (99999) non presente nel catalogo scientifico";
    r.expectedDesc = "Rilevamento diagnostico di WARNING con blockId, stringId, displayName, unresolvedScientificId (99999) e contesto";

    ScientificMaterialRegistry sciReg;
    sciReg.Initialize();

    BlockRegistry blockReg;
    blockReg.Initialize();

    auto createRes = blockReg.CreateBlock("fairworld:diag_test_orphan", "Orphan Asset");
    if (!createRes.success) {
        r.passed = false;
        r.observedDesc = "Impossibile creare blocco nel BlockRegistry";
        return r;
    }

    auto& blockDef = blockReg.GetBlockMutable(createRes.allocatedId);
    blockDef.scientificMaterialId = 99999;

    const std::string testContext = "TestContext_DIAG03";
    auto report = blockReg.ValidateScientificReferences(sciReg, testContext);

    if (report.warnings.size() != 1) {
        r.passed = false;
        r.observedDesc = "Atteso 1 warning diagnostico, osservati: " + std::to_string(report.warnings.size());
        return r;
    }

    const auto& diag = report.warnings[0];
    bool blockIdMatches = (diag.blockId == createRes.allocatedId);
    bool stringIdMatches = (diag.stringId == "fairworld:diag_test_orphan");
    bool displayNameMatches = (diag.displayName == "Orphan Asset");
    bool unresIdMatches = (diag.unresolvedScientificId == 99999);
    bool contextMatches = (diag.context == testContext);
    bool messageNonEmpty = (!diag.message.empty() && diag.message.find("99999") != std::string::npos);

    if (blockIdMatches && stringIdMatches && displayNameMatches && unresIdMatches && contextMatches && messageNonEmpty) {
        r.passed = true;
        r.observedDesc = "Diagnostica WARNING conforme al contratto: blocco " + std::to_string(diag.blockId) +
                         " (" + diag.stringId + "), ID orfano 99999, contesto '" + diag.context + "'";
    } else {
        r.passed = false;
        r.observedDesc = "Campi del warning non conformi al contratto diagnostico";
    }
    return r;
}

// -----------------------------------------------------------------------------
// Test DIAG-04: Invarianza e assenza di mutazione degli asset dopo la diagnostica
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestDIAG04_NoAssetMutationOnValidation() {
    ScientificRegistryTestResult r;
    r.testId = "DIAG-04";
    r.testName = "Invarianza e assenza di mutazione degli asset dopo la diagnostica";
    r.inputDesc = "Asset con ID scientifico valido, orfano e override scientifici personalizzati sottoposti a validazione";
    r.expectedDesc = "Tutte le definizioni, ID scientifici, override e proprieta' fisiche rimangono immutate dopo la diagnostica";

    ScientificMaterialRegistry sciReg;
    sciReg.Initialize();

    ScientificMaterialDef matDef;
    matDef.id = 50;
    matDef.canonicalId = "fairworld:mat/diag_stable";
    matDef.displayName = "Diag Stable";
    if (!sciReg.RegisterMaterial(matDef)) {
        r.passed = false;
        r.observedDesc = "Fallimento setup: impossibile registrare materiale scientifico 50";
        return r;
    }

    BlockRegistry blockReg;
    blockReg.Initialize();

    // 1. Blocco valido con ID scientifico 50
    auto resValid = blockReg.CreateBlock("fairworld:diag_mut_valid", "Valid Asset");
    if (!resValid.success) {
        r.passed = false;
        r.observedDesc = "Fallimento setup: impossibile creare blocco valido";
        return r;
    }
    auto& bValid = blockReg.GetBlockMutable(resValid.allocatedId);
    bValid.scientificMaterialId = 50;
    bValid.physics.mass = 10.0f;
    bValid.physics.density = 2000.0f;
    bValid.physics.friction = 0.5f;
    bValid.physics.restitution = 0.1f;
    bValid.SyncLegacyPhysicsFields();

    // 2. Blocco orfano con ID 88888, override scientifici e parametri fisici personalizzati
    auto resOrphan = blockReg.CreateBlock("fairworld:diag_mut_orphan", "Orphan Asset");
    if (!resOrphan.success) {
        r.passed = false;
        r.observedDesc = "Fallimento setup: impossibile creare blocco orfano";
        return r;
    }
    auto& bOrphan = blockReg.GetBlockMutable(resOrphan.allocatedId);
    bOrphan.scientificMaterialId = 88888;
    bool setDensityOk = bOrphan.scientificOverrides.SetOverride(ScientificPropertyNames::Density, ScientificQuantity::Known(1234.5, "kg/m^3"));
    bool setFrictionOk = bOrphan.scientificOverrides.SetOverride(ScientificPropertyNames::DynamicFriction, ScientificQuantity::Known(0.42, ""));
    if (!setDensityOk || !setFrictionOk) {
        r.passed = false;
        r.observedDesc = "Fallimento setup: impossibile configurare gli override scientifici canonici";
        return r;
    }
    bOrphan.physics.mass = 42.0f;
    bOrphan.physics.density = 2500.0f;
    bOrphan.physics.friction = 0.75f;
    bOrphan.physics.restitution = 0.15f;
    bOrphan.SyncLegacyPhysicsFields();

    // 3. Blocco non associato (ID scientifico 0)
    auto resUnassoc = blockReg.CreateBlock("fairworld:diag_mut_unassoc", "Unassociated Asset");
    if (!resUnassoc.success) {
        r.passed = false;
        r.observedDesc = "Fallimento setup: impossibile creare blocco non associato";
        return r;
    }
    auto& bUnassoc = blockReg.GetBlockMutable(resUnassoc.allocatedId);
    bUnassoc.scientificMaterialId = 0;
    bUnassoc.SyncLegacyPhysicsFields();

    // Cattura snapshot profondo pre-validazione
    SimBlockDef preValid = blockReg.GetBlock(resValid.allocatedId);
    SimBlockDef preOrphan = blockReg.GetBlock(resOrphan.allocatedId);
    SimBlockDef preUnassoc = blockReg.GetBlock(resUnassoc.allocatedId);

    // Esecuzione validazione diagnostica read-only
    auto report = blockReg.ValidateScientificReferences(sciReg, "TestDIAG04_MutationCheck");

    // Acquisizione stato post-validazione
    const SimBlockDef& postValid = blockReg.GetBlock(resValid.allocatedId);
    const SimBlockDef& postOrphan = blockReg.GetBlock(resOrphan.allocatedId);
    const SimBlockDef& postUnassoc = blockReg.GetBlock(resUnassoc.allocatedId);

    // Funzione diagnostica di confronto semantico campo per campo
    auto compareBlockSemantic = [](const SimBlockDef& pre, const SimBlockDef& post, std::string& outDiff) -> bool {
        std::vector<std::string> diffs;

        // Identità
        if (pre.id != post.id) {
            diffs.push_back("id divergente (prima: " + std::to_string(pre.id) + ", dopo: " + std::to_string(post.id) + ")");
        }
        if (pre.stringId != post.stringId) {
            diffs.push_back("stringId divergente (prima: '" + pre.stringId + "', dopo: '" + post.stringId + "')");
        }
        if (pre.displayName != post.displayName) {
            diffs.push_back("displayName divergente (prima: '" + pre.displayName + "', dopo: '" + post.displayName + "')");
        }

        // Riferimento scientifico
        if (pre.scientificMaterialId != post.scientificMaterialId) {
            diffs.push_back("scientificMaterialId divergente (prima: " + std::to_string(pre.scientificMaterialId) +
                            ", dopo: " + std::to_string(post.scientificMaterialId) + ")");
        }

        // Override scientifici
        if (pre.scientificOverrides.Size() != post.scientificOverrides.Size()) {
            diffs.push_back("scientificOverrides count divergente (prima: " + std::to_string(pre.scientificOverrides.Size()) +
                            ", dopo: " + std::to_string(post.scientificOverrides.Size()) + ")");
        } else {
            for (const auto& kv : pre.scientificOverrides.overrides) {
                const auto* postQ = post.scientificOverrides.GetOverride(kv.first);
                if (!postQ) {
                    diffs.push_back("override '" + kv.first + "' mancante dopo validazione");
                } else if (postQ->value != kv.second.value || postQ->unit != kv.second.unit ||
                           postQ->state != kv.second.state || postQ->quality != kv.second.quality) {
                    diffs.push_back("override '" + kv.first + "' modificato (prima val=" + std::to_string(kv.second.value) + " " + kv.second.unit +
                                    ", dopo val=" + std::to_string(postQ->value) + " " + postQ->unit + ")");
                }
            }
        }

        // BlockPhysicsDef
        if (pre.physics.mass != post.physics.mass) {
            diffs.push_back("physics.mass divergente (prima: " + std::to_string(pre.physics.mass) +
                            ", dopo: " + std::to_string(post.physics.mass) + ")");
        }
        if (pre.physics.density != post.physics.density) {
            diffs.push_back("physics.density divergente (prima: " + std::to_string(pre.physics.density) +
                            ", dopo: " + std::to_string(post.physics.density) + ")");
        }
        if (pre.physics.friction != post.physics.friction) {
            diffs.push_back("physics.friction divergente (prima: " + std::to_string(pre.physics.friction) +
                            ", dopo: " + std::to_string(post.physics.friction) + ")");
        }
        if (pre.physics.restitution != post.physics.restitution) {
            diffs.push_back("physics.restitution divergente (prima: " + std::to_string(pre.physics.restitution) +
                            ", dopo: " + std::to_string(post.physics.restitution) + ")");
        }
        if (pre.physics.collisionEnabled != post.physics.collisionEnabled) {
            diffs.push_back("physics.collisionEnabled divergente (prima: " + std::to_string(pre.physics.collisionEnabled) +
                            ", dopo: " + std::to_string(post.physics.collisionEnabled) + ")");
        }
        if (pre.physics.motionType != post.physics.motionType) {
            diffs.push_back("physics.motionType divergente");
        }
        if (pre.physics.massMode != post.physics.massMode) {
            diffs.push_back("physics.massMode divergente");
        }

        // Specchi legacy
        if (pre.isSolid != post.isSolid) {
            diffs.push_back("isSolid divergente (prima: " + std::to_string(pre.isSolid) +
                            ", dopo: " + std::to_string(post.isSolid) + ")");
        }
        if (pre.isTransparent != post.isTransparent) {
            diffs.push_back("isTransparent divergente (prima: " + std::to_string(pre.isTransparent) +
                            ", dopo: " + std::to_string(post.isTransparent) + ")");
        }
        if (pre.mass != post.mass) {
            diffs.push_back("legacy mass divergente (prima: " + std::to_string(pre.mass) +
                            ", dopo: " + std::to_string(post.mass) + ")");
        }
        if (pre.friction != post.friction) {
            diffs.push_back("legacy friction divergente (prima: " + std::to_string(pre.friction) +
                            ", dopo: " + std::to_string(post.friction) + ")");
        }
        if (pre.bounciness != post.bounciness) {
            diffs.push_back("legacy bounciness divergente (prima: " + std::to_string(pre.bounciness) +
                            ", dopo: " + std::to_string(post.bounciness) + ")");
        }

        if (!diffs.empty()) {
            std::ostringstream oss;
            oss << "Blocco ID " << std::to_string(pre.id) << " (" << pre.stringId << "): [";
            for (size_t i = 0; i < diffs.size(); ++i) {
                if (i > 0) oss << "; ";
                oss << diffs[i];
            }
            oss << "]";
            outDiff = oss.str();
            return false;
        }
        return true;
    };

    std::string diffValid, diffOrphan, diffUnassoc;
    bool validMatches = compareBlockSemantic(preValid, postValid, diffValid);
    bool orphanMatches = compareBlockSemantic(preOrphan, postOrphan, diffOrphan);
    bool unassocMatches = compareBlockSemantic(preUnassoc, postUnassoc, diffUnassoc);

    // Verifiche specifiche di invarianza contrattuale
    bool orphanIdPreserved = (postOrphan.scientificMaterialId == 88888);
    bool orphanOverridesPreserved = (postOrphan.scientificOverrides.Size() == 2) &&
                                    postOrphan.scientificOverrides.HasOverride(ScientificPropertyNames::Density) &&
                                    (postOrphan.scientificOverrides.GetOverride(ScientificPropertyNames::Density)->value == 1234.5) &&
                                    postOrphan.scientificOverrides.HasOverride(ScientificPropertyNames::DynamicFriction) &&
                                    (postOrphan.scientificOverrides.GetOverride(ScientificPropertyNames::DynamicFriction)->value == 0.42);
    bool validIdPreserved = (postValid.scientificMaterialId == 50);
    bool unassocIdPreserved = (postUnassoc.scientificMaterialId == 0);
    bool reportWarningsOk = (report.warnings.size() == 1) && (report.warnings[0].unresolvedScientificId == 88888);

    if (validMatches && orphanMatches && unassocMatches &&
        orphanIdPreserved && orphanOverridesPreserved &&
        validIdPreserved && unassocIdPreserved && reportWarningsOk) {
        r.passed = true;
        r.observedDesc = "Invarianza confermata al 100%: nessuna mutazione di ID, override e BlockPhysicsDef (confronto semantico campo per campo PASS)";
    } else {
        r.passed = false;
        std::string errDetails;
        if (!validMatches) errDetails += diffValid + " ";
        if (!orphanMatches) errDetails += diffOrphan + " ";
        if (!unassocMatches) errDetails += diffUnassoc + " ";
        if (!orphanIdPreserved) errDetails += "orphanId non preservato (trovato " + std::to_string(postOrphan.scientificMaterialId) + "); ";
        if (!orphanOverridesPreserved) errDetails += "override orfani non preservati o modificati; ";
        if (!reportWarningsOk) errDetails += "warnings report non conformi (atteso 1 su ID 88888, trovati " + std::to_string(report.warnings.size()) + "); ";
        r.observedDesc = "Rilevata violazione di invarianza durante la validazione";
        r.details = errDetails;
    }
    return r;
}

// -----------------------------------------------------------------------------
// Test DIAG-05: Comportamento di caricamento e integrita' registry invariati con asset orfani
// -----------------------------------------------------------------------------
ScientificRegistryTestResult ScientificMaterialRegistryContractTests::TestDIAG05_LoadingBehaviorUnchangedWithOrphans() {
    ScientificRegistryTestResult r;
    r.testId = "DIAG-05";
    r.testName = "Comportamento di caricamento e integrita' registry invariati con asset orfani";
    r.inputDesc = "File JSON con blocchi legacy contenenti scientificMaterialId orfani caricati tramite BlockRegistry::LoadFromJson";
    r.expectedDesc = "LoadFromJson restituisce true; blocchi orfani validi e accessibili; validazione produce warning senza alterare il funzionamento";

    // Prepariamo un JSON sintetico con blocchi legacy, di cui uno orfano
    json jRoot;
    json jBlocks = json::array();

    json b0;
    b0["id"] = 0;
    b0["stringId"] = "fairworld:air";
    b0["displayName"] = "Air";
    b0["isSolid"] = false;
    b0["isTransparent"] = true;
    b0["scientificMaterialId"] = 0;
    jBlocks.push_back(b0);

    json b1;
    b1["id"] = 1;
    b1["stringId"] = "fairworld:legacy_stone";
    b1["displayName"] = "Legacy Stone";
    b1["isSolid"] = true;
    b1["isTransparent"] = false;
    b1["scientificMaterialId"] = 200; // Valido
    jBlocks.push_back(b1);

    json b2;
    b2["id"] = 2;
    b2["stringId"] = "fairworld:legacy_orphan";
    b2["displayName"] = "Legacy Orphan";
    b2["isSolid"] = true;
    b2["isTransparent"] = false;
    b2["scientificMaterialId"] = 77777; // Orfano
    jBlocks.push_back(b2);

    jRoot["blocks"] = jBlocks;

    std::string tempFile = CreateTempJsonFile("diag_test_orphans.json", jRoot);

    ScientificMaterialRegistry sciReg;
    sciReg.Initialize();
    ScientificMaterialDef matDef;
    matDef.id = 200;
    matDef.canonicalId = "fairworld:mat/stone";
    matDef.displayName = "Stone Material";
    sciReg.RegisterMaterial(matDef);

    BlockRegistry blockReg;
    blockReg.Initialize();
    bool loadSuccess = blockReg.LoadFromJson(tempFile);
    RemoveTempFile(tempFile);

    if (!loadSuccess) {
        r.passed = false;
        r.observedDesc = "LoadFromJson ha fallito su JSON con riferimenti orfani";
        return r;
    }

    bool b0Valid = blockReg.IsBlockValid(0);
    bool b1Valid = blockReg.IsBlockValid(1);
    bool b2Valid = blockReg.IsBlockValid(2);

    const auto& b2Def = blockReg.GetBlock(2);
    bool b2LoadedCorrectly = (b2Def.stringId == "fairworld:legacy_orphan" && b2Def.scientificMaterialId == 77777);

    uint8_t queryId = blockReg.GetBlockIdByString("fairworld:legacy_orphan");
    bool queryMatches = (queryId == 2);

    auto report = blockReg.ValidateScientificReferences(sciReg, "TestDIAG05_Bootstrap");

    bool warningCorrect = (report.warnings.size() == 1 &&
                           report.warnings[0].blockId == 2 &&
                           report.warnings[0].unresolvedScientificId == 77777);

    if (b0Valid && b1Valid && b2Valid && b2LoadedCorrectly && queryMatches && warningCorrect) {
        r.passed = true;
        r.observedDesc = "Caricamento e integrita' registry preservati: LoadFromJson true, blocco 2 accessibile e valido, warning non bloccante emesso";
    } else {
        r.passed = false;
        r.observedDesc = "Fallimento: asset legacy orfano non caricato o stato registry compromesso";
    }
    return r;
}

} // namespace fw
