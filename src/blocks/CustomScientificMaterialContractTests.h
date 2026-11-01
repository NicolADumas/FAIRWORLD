#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// MA-03.3: SUITE TEST REGRESSIONE CONTRATTO MATERIALI SCIENTIFICI PERSONALIZZATI
// Verifica formale isolata del backend di authoring per ScientificMaterialRegistry.
// =============================================================================

struct CustomScientificTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class CustomScientificMaterialContractTests {
public:
    static std::vector<CustomScientificTestResult> RunAllTests();

    // 1. Creazione materiale custom e integrita' di tutti i campi
    static CustomScientificTestResult Test01_CustomMaterialCreationAndFieldIntegrity();

    // 2. Assegnazione ID deterministica, stabile e non riutilizzabile (>= 1000)
    static CustomScientificTestResult Test02_StableAndNonReusableIdAllocation();

    // 3. Rifiuto di duplicati e collisioni (ID vs Canonical)
    static CustomScientificTestResult Test03_DuplicateAndCollisionRejection();

    // 4. Regole di validazione della singola definizione materiale
    static CustomScientificTestResult Test04_SingleDefinitionValidationRules();

    // 5. Transizioni di stato del ciclo di vita (Draft -> Published -> Deprecated)
    static CustomScientificTestResult Test05_LifecycleStateTransitionsDraftPublishedDeprecated();

    // 6. Attributo enabledInBlockMaker indipendente e risoluzione asset preesistenti
    static CustomScientificTestResult Test06_IndependentBlockMakerAvailabilityAttribute();

    // 7. Aggiornamento in-place e sincronizzazione active snapshot
    static CustomScientificTestResult Test07_InPlaceUpdateAndActiveSnapshotSync();

    // 8. Round-trip persistente e riavvio catalogo
    static CustomScientificTestResult Test08_PersistentRoundTripAndCatalogReload();

    // 9. Separazione catalogo custom e rifiuto collisioni di range standard (< 1000)
    static CustomScientificTestResult Test09_CustomVsStandardSeparationAndRangeCollisionRejection();

    // 10. Rollback atomico in caso di errore di persistenza o parsing
    static CustomScientificTestResult Test10_PersistenceFailureAtomicRollback();

    // 11. Sostituzione atomica del catalogo con gestione errori e round-trip Deprecated
    static CustomScientificTestResult Test11_AtomicReplaceFailureAndRecovery();
};

} // namespace fw
