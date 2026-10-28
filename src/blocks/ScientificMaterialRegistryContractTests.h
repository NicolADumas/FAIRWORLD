#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// MA-03.1D: SUITE TEST REGRESSIONE CONTRATTO REGISTRO SCIENTIFICO (SCI-01 .. SCI-10)
// Verifica formale isolata del contratto MA-03.1C.2 per il ScientificMaterialRegistry.
// =============================================================================

struct ScientificRegistryTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class ScientificMaterialRegistryContractTests {
public:
    static std::vector<ScientificRegistryTestResult> RunAllTests();

    // 1. Caricamento JSON v1 senza campi tassonomici
    static ScientificRegistryTestResult Test01_LoadV1WithoutTaxonomy();

    // 2. Caricamento JSON v2 con tassonomia e composizione
    static ScientificRegistryTestResult Test02_LoadV2WithTaxonomyAndComposition();

    // 3. Round-trip Exact, Range, Unknown, qualità e fonte
    static ScientificRegistryTestResult Test03_RoundTripExactRangeUnknownQualitySource();

    // 4. Rifiuto schemaVersion 0 e 3
    static ScientificRegistryTestResult Test04_RejectSchemaVersion0And3();

    // 5. Rifiuto composizioni nominalmente complete con somma invalida
    static ScientificRegistryTestResult Test05_RejectNominallyCompleteInvalidSum();

    // 6. Accettazione composizioni parziali valide
    static ScientificRegistryTestResult Test06_AcceptValidPartialCompositions();

    // 7. Rifiuto cicli tassonomici e compositivi
    static ScientificRegistryTestResult Test07_RejectTaxonomicAndCompositionCycles();

    // 8. Rifiuto riferimenti invalidi e duplicati
    static ScientificRegistryTestResult Test08_RejectInvalidReferencesAndDuplicates();

    // 9. Atomicità di LoadFromJson: dopo un caricamento fallito, materiali e indici invariati
    static ScientificRegistryTestResult Test09_AtomicityLoadFromJsonRollback();

    // 10. Invarianza ID 1–4 e ResolveProperty: override locale, proprietà del materiale, Unknown
    static ScientificRegistryTestResult Test10_InvarianceId1To4AndResolveProperty();

    // =========================================================================
    // MA-03.2A: TEST DI ACCETTAZIONE BOOTSTRAP CATALOGO PRODUTTIVO (BOOT-01 .. BOOT-04)
    // =========================================================================

    // BOOT-01: Bootstrap Initialize -> LoadFromJson, verifica kind e aggregationState
    static ScientificRegistryTestResult TestBOOT01_BootstrapConsistency();

    // BOOT-02: Query tassonomiche dopo il caricamento
    static ScientificRegistryTestResult TestBOOT02_TaxonomicQueriesPostLoad();

    // BOOT-03: Confronto dei valori scientifici prima/dopo il caricamento, inclusi metadati e canonicalId
    static ScientificRegistryTestResult TestBOOT03_ScientificValuesPreservation();

    // BOOT-04: Round-trip JSON v2 e validazione catalogo produttivo
    static ScientificRegistryTestResult TestBOOT04_ProductionJsonV2RoundTrip();

    // =========================================================================
    // MA-03.2D.3B: TEST INFRASTRUTTURA PROPRIETA' ESTESE (EXT-01 .. EXT-11)
    // =========================================================================

    // EXT-01: Rollback transazionale su JSON v3 invalido
    static ScientificRegistryTestResult TestEXT01_RollbackOnInvalidV3Json();

    // EXT-02: Sei esiti distinti del Resolver
    static ScientificRegistryTestResult TestEXT02_ResolverSixOutcomes();

    // EXT-03: Matching conservativo per campi mancanti o incompatibili
    static ScientificRegistryTestResult TestEXT03_ConservativeMatchingMissingFields();

    // EXT-04: MatchMultiple senza selezione implicita o interpolazione
    static ScientificRegistryTestResult TestEXT04_MatchMultipleNoImplicitSelection();

    // EXT-05: Distinzione semantica tra Known(0.0), Unknown e NotApplicable
    static ScientificRegistryTestResult TestEXT05_DistinctionZeroUnknownNotApplicable();

    // EXT-06: Rappresentazioni Scalar, Range e QualitativeDescriptor
    static ScientificRegistryTestResult TestEXT06_RepresentationsScalarRangeQualitative();

    // EXT-07: Validazione metrologica e dimensionale (unità ammesse, incertezza, dispersione)
    static ScientificRegistryTestResult TestEXT07_MetrologicalAndDimensionalValidation();

    // EXT-08: Pressioni con riferimento Absolute, Gauge e Differential
    static ScientificRegistryTestResult TestEXT08_PressureSpecAbsoluteGaugeDifferential();

    // EXT-09: Lifetime delle osservazioni e degli snapshot (RegistrySnapshotHandle)
    static ScientificRegistryTestResult TestEXT09_SnapshotAndObservationLifetime();

    // EXT-10: Serializzazione deterministica JSON v3 e recupero degli indici
    static ScientificRegistryTestResult TestEXT10_DeterministicSerialization();

    // EXT-11: Verifica che il catalogo produttivo contenga zero osservazioni estese
    static ScientificRegistryTestResult TestEXT11_ProductionCatalogHasNoExtendedProperties();
};

} // namespace fw
