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

    // =========================================================================
    // P0.2A: TEST DI SICUREZZA DEI RIFERIMENTI E LIFETIME SNAPSHOT (CUST-12 .. CUST-16)
    // =========================================================================

    // 12. Inserimento di nuovi materiali mentre un vecchio snapshot è ancora utilizzato
    static CustomScientificTestResult Test12_OldSnapshotHandleImmutabilityAcrossInsertion();

    // 13. Aggiornamento in-place di materiale mentre un vecchio snapshot è ancora utilizzato
    static CustomScientificTestResult Test13_OldSnapshotHandleImmutabilityAcrossInPlaceUpdate();

    // 14. Pubblicazione e deprecazione mentre vecchi snapshot sono ancora utilizzati
    static CustomScientificTestResult Test14_OldSnapshotHandleImmutabilityAcrossPublicationAndDeprecation();

    // 15. Sostituzione del catalogo (Clear e Load) mentre un vecchio snapshot è ancora utilizzato
    static CustomScientificTestResult Test15_OldSnapshotHandleImmutabilityAcrossCatalogReplacement();

    // 16. Semantica per valore di GetMaterial: copie immuni da mutazioni e deallocazioni
    static CustomScientificTestResult Test16_GetMaterialValueSemanticsImmunityToMutations();

    // 17. Semantica per valore di GetAllMaterials e isolamento dello storage tramite snapshot
    static CustomScientificTestResult Test17_GetAllMaterialsValueSemanticsAndSnapshotIsolation();

    // =========================================================================
    // P0.2B: REVISIONE MONOTONA DEL REGISTRY E INVALIDAZIONE CACHE (CUST-18 .. CUST-19)
    // =========================================================================

    // 18. Revisione monotona su mutazioni, idempotenza e conteggio invariato
    static CustomScientificTestResult Test18_RegistryRevisionMutationAndIdempotenceContracts();

    // 19. Immutabilità revisione snapshot, rollback di persistenza e Clear/Load
    static CustomScientificTestResult Test19_RegistryRevisionPersistenceRollbackAndSnapshotImmutability();

    // =========================================================================
    // P0.2C: CONCORRENZA REGISTRY E STABILITÀ ID CUSTOM (CUST-20 .. CUST-22)
    // =========================================================================

    // 20. Lettori concorrenti durante aggiornamenti, validità snapshot storici e coerenza snapshot
    static CustomScientificTestResult Test20_ConcurrentReadersDuringUpdatesAndSnapshotConsistency();

    // 21. Scrittori concorrenti con serializzazione, integrità transazionale e zero aggiornamenti persi
    static CustomScientificTestResult Test21_ConcurrentWritersZeroLostUpdatesAndTransactionalIntegrity();

    // 22. Contratto allocazione ID custom: prenotazione atomica concorrente, non-riciclo, persistenza HWM e overflow
    static CustomScientificTestResult Test22_CustomIdAllocationContractStabilityAndOverflow();

    // =========================================================================
    // P0.2D: SCIENTIFIC MATERIAL PERSISTENCE HARDENING (CUST-23 .. CUST-26)
    // =========================================================================

    // 23. Salvataggi concorrenti sullo stesso percorso: collision-free temp files e serializzazione per destinazione
    static CustomScientificTestResult Test23_ConcurrentSavesSamePathCollisionFreeAndSerialized();

    // 24. Protezione contro sovrascrittura da snapshot obsoleti e mutazioni concorrenti durante salvataggio
    static CustomScientificTestResult Test24_StaleSnapshotOverwriteProtectionAndConcurrentMutations();

    // 25. Fault injection deterministica su temp write e atomic replacement con conservazione target e zero residui
    static CustomScientificTestResult Test25_FaultInjectionAtomicReplaceTargetPreservationAndZeroResidues();

    // 26. Durabilità High-Water Mark: prenotazione durevole, persistenza, recovery al riavvio e compatibilità legacy
    static CustomScientificTestResult Test26_DurableHighWaterMarkPersistenceCrashRecoveryAndLegacyCompat();

    // 27. Rilevamento modifiche concorrenti tra cataloghi distinti: protezione lost update e sostituzione intenzionale
    static CustomScientificTestResult Test27_ConcurrentCatalogsLostUpdateProtectionAndIntentionalOverwrite();
};

} // namespace fw
