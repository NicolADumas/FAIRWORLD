#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// MATERIAL AUTHORING CONTRACT TESTS (P0.3C1)
// Suite contrattuale deterministica per il backend di authoring dei materiali scientifici.
// =============================================================================

struct AuthoringTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class MaterialAuthoringContractTests {
public:
    static std::vector<AuthoringTestResult> RunAllTests();

    // AUTH-01: Creazione custom e integrità dei campi iniziali (ID >= 1000, Draft, custom=true)
    static AuthoringTestResult Test01_CustomMaterialCreationAndFieldIntegrity();

    // AUTH-02: Validazione formale proprietà SI (range, positività, unità e limiti fisici)
    static AuthoringTestResult Test02_PhysicalQuantitySIValidationRules();

    // AUTH-03: Modifica Draft isolata (working copy isolata, commit atomico con CoW)
    static AuthoringTestResult Test03_TransactionalDraftModificationAndIsolation();

    // AUTH-04: Persistenza atomica e riavvio catalogo custom con integrità round-trip
    static AuthoringTestResult Test04_AtomicPersistenceAndCatalogReload();

    // AUTH-05: Transizione formale di ciclo di vita a Published
    static AuthoringTestResult Test05_LifecycleStateTransitionPublish();

    // AUTH-06: Transizione formale a Deprecated e conservazione referenziale storica
    static AuthoringTestResult Test06_LifecycleStateTransitionDeprecate();

    // AUTH-07: Attributo enabledInBlockMaker indipendente e rifiuto su Deprecated
    static AuthoringTestResult Test07_BlockMakerAvailabilityIndependentToggle();

    // AUTH-08: Rilevamento conflitti di concorrenza ottimistica (Lost Update) e risoluzione
    static AuthoringTestResult Test08_OptimisticConcurrencyLostUpdateConflictHandling();

    // AUTH-09: Protezione assoluta dei materiali built-in (< 1000) da mutazioni e deprecazione
    static AuthoringTestResult Test09_BuiltInMaterialsProtectionAgainstModificationAndDeprecation();

    // AUTH-10: Integrità dei binding esistenti su superfici e ortogonalità visiva PBR
    static AuthoringTestResult Test10_ExistingBindingIntegrityAndVisualOrthogonality();

    // AUTH-11: Celsius-First UI Conversion Boundary, Persistence & Round-Trip Validation
    static AuthoringTestResult Test11_CelsiusFirstUIBoundaryAndKelvinPersistence();
};

} // namespace fw
