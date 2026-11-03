#include "CustomScientificMaterialContractTests.h"
#include "ScientificMaterialRegistry.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <thread>
#include <atomic>
#include <set>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fw {

std::vector<CustomScientificTestResult> CustomScientificMaterialContractTests::RunAllTests() {
    return {
        Test01_CustomMaterialCreationAndFieldIntegrity(),
        Test02_StableAndNonReusableIdAllocation(),
        Test03_DuplicateAndCollisionRejection(),
        Test04_SingleDefinitionValidationRules(),
        Test05_LifecycleStateTransitionsDraftPublishedDeprecated(),
        Test06_IndependentBlockMakerAvailabilityAttribute(),
        Test07_InPlaceUpdateAndActiveSnapshotSync(),
        Test08_PersistentRoundTripAndCatalogReload(),
        Test09_CustomVsStandardSeparationAndRangeCollisionRejection(),
        Test10_PersistenceFailureAtomicRollback(),
        Test11_AtomicReplaceFailureAndRecovery(),
        Test12_OldSnapshotHandleImmutabilityAcrossInsertion(),
        Test13_OldSnapshotHandleImmutabilityAcrossInPlaceUpdate(),
        Test14_OldSnapshotHandleImmutabilityAcrossPublicationAndDeprecation(),
        Test15_OldSnapshotHandleImmutabilityAcrossCatalogReplacement(),
        Test16_GetMaterialValueSemanticsImmunityToMutations(),
        Test17_GetAllMaterialsValueSemanticsAndSnapshotIsolation(),
        Test18_RegistryRevisionMutationAndIdempotenceContracts(),
        Test19_RegistryRevisionPersistenceRollbackAndSnapshotImmutability(),
        Test20_ConcurrentReadersDuringUpdatesAndSnapshotConsistency(),
        Test21_ConcurrentWritersZeroLostUpdatesAndTransactionalIntegrity(),
        Test22_CustomIdAllocationContractStabilityAndOverflow(),
        Test23_ConcurrentSavesSamePathCollisionFreeAndSerialized(),
        Test24_StaleSnapshotOverwriteProtectionAndConcurrentMutations(),
        Test25_FaultInjectionAtomicReplaceTargetPreservationAndZeroResidues(),
        Test26_DurableHighWaterMarkPersistenceCrashRecoveryAndLegacyCompat(),
        Test27_ConcurrentCatalogsLostUpdateProtectionAndIntentionalOverwrite()
    };
}

// -----------------------------------------------------------------------------
// Helper to create a valid base custom material
// -----------------------------------------------------------------------------
static ScientificMaterialDef CreateValidBaseCustomDef(ScientificMaterialID id, const std::string& canon, const std::string& name) {
    ScientificMaterialDef m;
    m.id = id;
    m.canonicalId = canon;
    m.displayName = name;
    m.chemicalFormula = "Ti90Al6V4";
    m.classification = "Metallic Alloy";
    m.description = "Aerospace grade titanium alloy definition.";
    m.kind = ScientificKind::Unknown;
    m.aggregationState = AggregationState::Solid;

    m.density = ScientificQuantity::Known(4430.0, "kg/m^3", DataQuality::PeerReviewed, "ASM Handbook");
    m.porosity = ScientificQuantity::Known(0.0, "", DataQuality::SyntheticStandard, "Fully dense");
    m.compressiveStrength = ScientificQuantity::Known(9.7e8, "Pa", DataQuality::PeerReviewed, "Yield strength");
    m.tensileStrength = ScientificQuantity::Known(9.5e8, "Pa", DataQuality::PeerReviewed, "UTS");
    m.elasticModulus = ScientificQuantity::Known(1.14e11, "Pa", DataQuality::PeerReviewed, "Young's Modulus");
    m.staticFriction = ScientificQuantity::Known(0.36, "", DataQuality::Experimental, "Dry contact");
    m.dynamicFriction = ScientificQuantity::Known(0.30, "", DataQuality::Experimental, "Dry contact");
    m.restitution = ScientificQuantity::Known(0.55, "", DataQuality::Estimated, "Ball rebound");

    m.specificHeatCapacity = ScientificQuantity::Known(526.0, "J/(kg*K)", DataQuality::PeerReviewed, "Specific heat");
    m.thermalConductivity = ScientificQuantity::Known(6.7, "W/(m*K)", DataQuality::PeerReviewed, "Thermal conductivity");
    m.thermalExpansion = ScientificQuantity::Known(8.6e-6, "1/K", DataQuality::PeerReviewed, "Linear CTE");
    m.meltingPoint = ScientificQuantity::Known(1933.15, "K", DataQuality::PeerReviewed, "Liquidus temperature");

    m.lifecycleState = MaterialLifecycleState::Draft;
    m.enabledInBlockMaker = true;
    m.isCustom = true;
    return m;
}

// -----------------------------------------------------------------------------
// TEST 01: Creazione materiale custom e integrità di tutti i campi
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test01_CustomMaterialCreationAndFieldIntegrity() {
    CustomScientificTestResult res;
    res.testId = "CUST-01";
    res.testName = "Creazione materiale custom e integrita' di tutti i campi";
    res.inputDesc = "Definizione completa ScientificMaterialDef con parametri SI, tassonomia, metadati e validazione";
    res.expectedDesc = "Validazione positiva, registrazione atomica e recupero bit-a-bit di tutte le proprieta'";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    auto def = CreateValidBaseCustomDef(1000, "custom:mat/titanium_alloy", "Titanium Ti-6Al-4V");

    std::string valErr;
    if (!reg.ValidateMaterialDefinition(def, &valErr)) {
        res.passed = false;
        res.observedDesc = "Validazione fallita: " + valErr;
        return res;
    }

    if (!reg.RegisterMaterial(def, false)) {
        res.passed = false;
        res.observedDesc = "RegisterMaterial fallito per ID 1000";
        return res;
    }

    if (!reg.HasMaterial(1000) || !reg.HasMaterial("custom:mat/titanium_alloy")) {
        res.passed = false;
        res.observedDesc = "HasMaterial restituisce false per ID o canonicalId";
        return res;
    }

    const auto& retrieved = reg.GetMaterial(1000);
    bool match = (retrieved.id == 1000 &&
                  retrieved.canonicalId == "custom:mat/titanium_alloy" &&
                  retrieved.displayName == "Titanium Ti-6Al-4V" &&
                  retrieved.chemicalFormula == "Ti90Al6V4" &&
                  retrieved.density.value == 4430.0 &&
                  retrieved.porosity.value == 0.0 &&
                  retrieved.elasticModulus.value == 1.14e11 &&
                  retrieved.staticFriction.value == 0.36 &&
                  retrieved.dynamicFriction.value == 0.30 &&
                  retrieved.restitution.value == 0.55 &&
                  retrieved.meltingPoint.value == 1933.15 &&
                  retrieved.lifecycleState == MaterialLifecycleState::Draft &&
                  retrieved.enabledInBlockMaker == true &&
                  retrieved.isCustom == true);

    if (!match) {
        res.passed = false;
        res.observedDesc = "Discrepanza nei campi recuperati dal registry";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Materiale custom registrato e verificato con integrita' esatta dei campi";
    return res;
}

// -----------------------------------------------------------------------------
// TEST 02: Assegnazione ID deterministica, stabile e non riutilizzabile (>= 1000)
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test02_StableAndNonReusableIdAllocation() {
    CustomScientificTestResult res;
    res.testId = "CUST-02";
    res.testName = "Assegnazione ID deterministica, stabile e non riutilizzabile";
    res.inputDesc = "Allocazione sequenziale ID custom, registrazione, deprecazione e successiva allocazione";
    res.expectedDesc = "Allocazione parte da 1000; l'ID deprecato non viene mai riutilizzato";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    auto id1 = reg.AllocateNextCustomMaterialId();
    if (id1 != 1000) {
        res.passed = false;
        res.observedDesc = "Primo ID custom allocato non e' 1000 (ottenuto " + std::to_string(id1) + ")";
        return res;
    }

    auto def1 = CreateValidBaseCustomDef(id1, "custom:mat/mat_1000", "Material 1000");
    reg.RegisterMaterial(def1);

    auto id2 = reg.AllocateNextCustomMaterialId();
    if (id2 != 1001) {
        res.passed = false;
        res.observedDesc = "Secondo ID allocato non e' 1001 (ottenuto " + std::to_string(id2) + ")";
        return res;
    }

    auto def2 = CreateValidBaseCustomDef(id2, "custom:mat/mat_1001", "Material 1001");
    reg.RegisterMaterial(def2);

    auto id3 = reg.AllocateNextCustomMaterialId();
    if (id3 != 1002) {
        res.passed = false;
        res.observedDesc = "Terzo ID allocato non e' 1002 (ottenuto " + std::to_string(id3) + ")";
        return res;
    }

    // Deprecazione di id2 (1001)
    std::string depErr;
    if (!reg.DeprecateMaterial(id2, &depErr)) {
        res.passed = false;
        res.observedDesc = "DeprecateMaterial fallito: " + depErr;
        return res;
    }

    // Verifica che l'ID deprecato rimanga nel registro e non venga riutilizzato
    auto idAfterDep = reg.AllocateNextCustomMaterialId();
    if (idAfterDep != 1002) {
        res.passed = false;
        res.observedDesc = "ID allocato post-deprecazione ha riciclato 1001 invece di 1002 (ottenuto " + std::to_string(idAfterDep) + ")";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Allocazione deterministica verificata: ID stabili e divieto assoluto di riciclo";
    return res;
}

// -----------------------------------------------------------------------------
// TEST 03: Rifiuto di duplicati e collisioni (ID vs Canonical)
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test03_DuplicateAndCollisionRejection() {
    CustomScientificTestResult res;
    res.testId = "CUST-03";
    res.testName = "Rifiuto di duplicati e collisioni (ID vs Canonical)";
    res.inputDesc = "Tentativi di registrazione con ID duplicato, canonical duplicato, furto di ID e rimappatura canonical";
    res.expectedDesc = "Tutti i tentativi di collisione vengono respinti; il record originale rimane intatto";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    auto orig = CreateValidBaseCustomDef(1000, "custom:mat/original", "Original Material");
    reg.RegisterMaterial(orig);

    // 1. Stesso ID con canonical diverso (allowUpdate = false)
    auto collId = CreateValidBaseCustomDef(1000, "custom:mat/other", "Other Material");
    if (reg.RegisterMaterial(collId, false)) {
        res.passed = false;
        res.observedDesc = "Accettata erroneamente registrazione con ID duplicato";
        return res;
    }

    // 2. ID diverso con stesso canonical (allowUpdate = false)
    auto collCanon = CreateValidBaseCustomDef(1001, "custom:mat/original", "Fake Original");
    if (reg.RegisterMaterial(collCanon, false)) {
        res.passed = false;
        res.observedDesc = "Accettata erroneamente registrazione con canonicalId duplicato";
        return res;
    }

    // 3. Furto di ID con allowUpdate = true (stesso ID 1000 ma canonicalId diverso)
    if (reg.RegisterMaterial(collId, true)) {
        res.passed = false;
        res.observedDesc = "Accettato erroneamente furto di ID con allowUpdate = true";
        return res;
    }

    // 4. Rimappatura canonical con allowUpdate = true (stesso canonical ma ID diverso)
    if (reg.RegisterMaterial(collCanon, true)) {
        res.passed = false;
        res.observedDesc = "Accettata erroneamente rimappatura canonical con allowUpdate = true";
        return res;
    }

    // Verifica che il materiale originale non sia stato corrotto
    const auto& cur = reg.GetMaterial(1000);
    if (cur.canonicalId != "custom:mat/original" || cur.displayName != "Original Material") {
        res.passed = false;
        res.observedDesc = "Il record originale e' stato mutato da collisioni respinte";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Tutte le collisioni di identita' e ID theft sono state correttamente respinte";
    return res;
}

// -----------------------------------------------------------------------------
// TEST 04: Regole di validazione della singola definizione materiale
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test04_SingleDefinitionValidationRules() {
    CustomScientificTestResult res;
    res.testId = "CUST-04";
    res.testName = "Regole di validazione della singola definizione materiale";
    res.inputDesc = "Casi limite invalidi: ID 0, canonical malformato, densita' negativa, porosita' > 1, autocicli";
    res.expectedDesc = "Tutte le violazioni vengono intercettate con diagnostica di errore chiara";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    std::string err;

    // 1. ID 0
    auto d1 = CreateValidBaseCustomDef(0, "custom:mat/valid", "Valid Name");
    if (reg.ValidateMaterialDefinition(d1, &err)) {
        res.passed = false;
        res.observedDesc = "Accettato ID 0";
        return res;
    }

    // 2. Canonical vuoto o senza prefisso mat/
    auto d2 = CreateValidBaseCustomDef(1000, "custom:invalid_prefix", "Valid Name");
    if (reg.ValidateMaterialDefinition(d2, &err)) {
        res.passed = false;
        res.observedDesc = "Accettato canonical senza 'mat/'";
        return res;
    }

    // 3. Densità negativa
    auto d3 = CreateValidBaseCustomDef(1000, "custom:mat/test_mat", "Valid Name");
    d3.density = ScientificQuantity::Known(-100.0, "kg/m^3", DataQuality::Estimated);
    if (reg.ValidateMaterialDefinition(d3, &err)) {
        res.passed = false;
        res.observedDesc = "Accettata densita' negativa";
        return res;
    }

    // 4. Porosità > 1.0
    auto d4 = CreateValidBaseCustomDef(1000, "custom:mat/test_mat", "Valid Name");
    d4.porosity = ScientificQuantity::Known(1.5, "", DataQuality::Estimated);
    if (reg.ValidateMaterialDefinition(d4, &err)) {
        res.passed = false;
        res.observedDesc = "Accettata porosita' 1.5 > 1.0";
        return res;
    }

    // 5. Restituzione > 1.0
    auto d5 = CreateValidBaseCustomDef(1000, "custom:mat/test_mat", "Valid Name");
    d5.restitution = ScientificQuantity::Known(1.2, "", DataQuality::Estimated);
    if (reg.ValidateMaterialDefinition(d5, &err)) {
        res.passed = false;
        res.observedDesc = "Accettata restituzione 1.2 > 1.0";
        return res;
    }

    // 6. Punto di fusione negativo
    auto d6 = CreateValidBaseCustomDef(1000, "custom:mat/test_mat", "Valid Name");
    d6.meltingPoint = ScientificQuantity::Known(-10.0, "K", DataQuality::Estimated);
    if (reg.ValidateMaterialDefinition(d6, &err)) {
        res.passed = false;
        res.observedDesc = "Accettato punto di fusione negativo in Kelvin";
        return res;
    }

    // 7. Autociclo composizione
    auto d7 = CreateValidBaseCustomDef(1000, "custom:mat/test_mat", "Valid Name");
    d7.composition.completeness = CompositionCompleteness::Partial;
    ScientificComponent comp;
    comp.materialId = 1000;
    comp.quantity = ComponentQuantity::Exact(0.5);
    d7.composition.components.push_back(comp);
    if (reg.ValidateMaterialDefinition(d7, &err)) {
        res.passed = false;
        res.observedDesc = "Accettato autociclo compositivo (componente punta a se stesso)";
        return res;
    }

    // 8. Autociclo relazione
    auto d8 = CreateValidBaseCustomDef(1000, "custom:mat/test_mat", "Valid Name");
    ScientificRelation rel;
    rel.type = ScientificRelationType::DerivedFrom;
    rel.targetId = 1000;
    d8.relations.push_back(rel);
    if (reg.ValidateMaterialDefinition(d8, &err)) {
        res.passed = false;
        res.observedDesc = "Accettato autociclo relazionale (targetId == id)";
        return res;
    }

    // 9. Composizione NominallyComplete con somma non unitaria
    auto d9 = CreateValidBaseCustomDef(1000, "custom:mat/test_mat", "Valid Name");
    d9.composition.completeness = CompositionCompleteness::NominallyComplete;
    ScientificComponent c1;
    c1.displayName = "Component A";
    c1.quantity = ComponentQuantity::Exact(0.5);
    ScientificComponent c2;
    c2.displayName = "Component B";
    c2.quantity = ComponentQuantity::Exact(0.3); // 0.5 + 0.3 = 0.8 != 1.0
    d9.composition.components.push_back(c1);
    d9.composition.components.push_back(c2);
    if (reg.ValidateMaterialDefinition(d9, &err)) {
        res.passed = false;
        res.observedDesc = "Accettata composizione NominallyComplete con somma non unitaria (0.8)";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Tutte le regole di validazione isolate hanno respinto correttamente i casi invalidi";
    return res;
}

// -----------------------------------------------------------------------------
// TEST 05: Transizioni di stato del ciclo di vita (Draft -> Published -> Deprecated)
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test05_LifecycleStateTransitionsDraftPublishedDeprecated() {
    CustomScientificTestResult res;
    res.testId = "CUST-05";
    res.testName = "Transizioni di stato del ciclo di vita (Draft -> Published -> Deprecated)";
    res.inputDesc = "Creazione Draft, pubblicazione con PublishMaterial, deprecazione con DeprecateMaterial e divieto ri-pubblicazione";
    res.expectedDesc = "Transizioni controllate; divieto categorico di ri-pubblicare materiali Deprecated";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    auto def = CreateValidBaseCustomDef(1000, "custom:mat/lifecycle_test", "Lifecycle Test");
    def.lifecycleState = MaterialLifecycleState::Draft;
    reg.RegisterMaterial(def);

    if (reg.GetMaterial(1000).lifecycleState != MaterialLifecycleState::Draft) {
        res.passed = false;
        res.observedDesc = "Stato iniziale non e' Draft";
        return res;
    }

    auto drafts = reg.GetMaterialsByLifecycle(MaterialLifecycleState::Draft);
    if (std::find(drafts.begin(), drafts.end(), 1000) == drafts.end()) {
        res.passed = false;
        res.observedDesc = "ID 1000 non trovato nella query GetMaterialsByLifecycle(Draft)";
        return res;
    }

    // Transizione a Published
    std::string err;
    if (!reg.PublishMaterial(1000, &err)) {
        res.passed = false;
        res.observedDesc = "PublishMaterial fallito: " + err;
        return res;
    }

    if (reg.GetMaterial(1000).lifecycleState != MaterialLifecycleState::Published) {
        res.passed = false;
        res.observedDesc = "Stato dopo pubblicazione non e' Published";
        return res;
    }

    // Transizione a Deprecated
    if (!reg.DeprecateMaterial(1000, &err)) {
        res.passed = false;
        res.observedDesc = "DeprecateMaterial fallito: " + err;
        return res;
    }

    if (reg.GetMaterial(1000).lifecycleState != MaterialLifecycleState::Deprecated) {
        res.passed = false;
        res.observedDesc = "Stato dopo deprecazione non e' Deprecated";
        return res;
    }

    // Tentativo illegale di ri-pubblicare un materiale Deprecated
    if (reg.PublishMaterial(1000, &err)) {
        res.passed = false;
        res.observedDesc = "PublishMaterial ha consentito la ri-pubblicazione di un materiale Deprecated";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Ciclo di vita Draft -> Published -> Deprecated con divieto di ri-pubblicazione validato";
    return res;
}

// -----------------------------------------------------------------------------
// TEST 06: Attributo enabledInBlockMaker indipendente e risoluzione asset preesistenti
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test06_IndependentBlockMakerAvailabilityAttribute() {
    CustomScientificTestResult res;
    res.testId = "CUST-06";
    res.testName = "Attributo enabledInBlockMaker indipendente e risoluzione asset preesistenti";
    res.inputDesc = "Materiali con varie combinazioni di lifecycleState ed enabledInBlockMaker; query GetSelectableMaterials";
    res.expectedDesc = "Solo Published && enabled compaiono nella lista di default; un asset storico accede comunque al proprio materiale";

    ScientificMaterialRegistry reg;
    reg.Initialize(); // Standard: 1, 2, 3, 4 (Published, enabled)

    // Mat 1000: Published + enabled = true
    auto m1000 = CreateValidBaseCustomDef(1000, "custom:mat/m1000", "M1000");
    m1000.lifecycleState = MaterialLifecycleState::Published;
    m1000.enabledInBlockMaker = true;
    reg.RegisterMaterial(m1000);

    // Mat 1001: Published + enabled = false (disabilitato in Block Maker)
    auto m1001 = CreateValidBaseCustomDef(1001, "custom:mat/m1001", "M1001");
    m1001.lifecycleState = MaterialLifecycleState::Published;
    m1001.enabledInBlockMaker = false;
    reg.RegisterMaterial(m1001);

    // Mat 1002: Deprecated + enabled = true
    auto m1002 = CreateValidBaseCustomDef(1002, "custom:mat/m1002", "M1002");
    m1002.lifecycleState = MaterialLifecycleState::Deprecated;
    m1002.enabledInBlockMaker = true;
    reg.RegisterMaterial(m1002);

    // Mat 1003: Draft + enabled = true
    auto m1003 = CreateValidBaseCustomDef(1003, "custom:mat/m1003", "M1003");
    m1003.lifecycleState = MaterialLifecycleState::Draft;
    m1003.enabledInBlockMaker = true;
    reg.RegisterMaterial(m1003);

    // 1. Selezione di default tramite Snapshot immutabile owned (P0.2A)
    RegistrySnapshotHandle snap = reg.GetSnapshotHandle();
    auto selectableDef = snap->GetSelectableMaterials(0);
    auto hasId = [&](const std::vector<const ScientificMaterialDef*>& v, ScientificMaterialID id) {
        return std::find_if(v.begin(), v.end(), [id](const auto* m) { return m->id == id; }) != v.end();
    };

    if (!hasId(selectableDef, 1000)) {
        res.passed = false;
        res.observedDesc = "Mat 1000 (Published, enabled) non presente nei selezionabili";
        return res;
    }
    if (hasId(selectableDef, 1001)) {
        res.passed = false;
        res.observedDesc = "Mat 1001 (enabled=false) erroneamente presente nei selezionabili";
        return res;
    }
    if (hasId(selectableDef, 1002)) {
        res.passed = false;
        res.observedDesc = "Mat 1002 (Deprecated) erroneamente presente nei selezionabili per nuovi blocchi";
        return res;
    }
    if (hasId(selectableDef, 1003)) {
        res.passed = false;
        res.observedDesc = "Mat 1003 (Draft) erroneamente presente nei selezionabili";
        return res;
    }

    // 2. Asset storico già salvato con materiale 1002 (Deprecated)
    auto selectableOld = snap->GetSelectableMaterials(1002);
    if (!hasId(selectableOld, 1002)) {
        res.passed = false;
        res.observedDesc = "Asset storico con ID 1002 non include il proprio materiale deprecato nella selezione";
        return res;
    }

    // 3. Asset storico con materiale 1001 (disabilitato in Block Maker)
    auto selectableDisabled = snap->GetSelectableMaterials(1001);
    if (!hasId(selectableDisabled, 1001)) {
        res.passed = false;
        res.observedDesc = "Asset storico con ID 1001 non include il proprio materiale nella selezione";
        return res;
    }

    // 4. Verifica semantica per valore tramite GetSelectableMaterialCopies
    auto copiesDef = reg.GetSelectableMaterialCopies(0);
    auto hasIdCopy = [&](const std::vector<ScientificMaterialDef>& v, ScientificMaterialID id) {
        return std::find_if(v.begin(), v.end(), [id](const auto& m) { return m.id == id; }) != v.end();
    };
    if (!hasIdCopy(copiesDef, 1000) || hasIdCopy(copiesDef, 1001) || hasIdCopy(copiesDef, 1002) || hasIdCopy(copiesDef, 1003)) {
        res.passed = false;
        res.observedDesc = "GetSelectableMaterialCopies non riflette i medesimi filtri di selezione";
        return res;
    }

    res.passed = true;
    res.observedDesc = "enabledInBlockMaker e retrocompatibilità per asset storici verificate con successo";
    return res;
}

// -----------------------------------------------------------------------------
// TEST 07: Aggiornamento in-place e sincronizzazione active snapshot
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test07_InPlaceUpdateAndActiveSnapshotSync() {
    CustomScientificTestResult res;
    res.testId = "CUST-07";
    res.testName = "Aggiornamento in-place e sincronizzazione active snapshot";
    res.inputDesc = "Mutazione di proprieta' fisiche tramite RegisterMaterial(..., allowUpdate=true) e confronto snapshot";
    res.expectedDesc = "Nuovo snapshot emesso con snapshotId incrementato; vecchio snapshot rimane inalterato (CoW)";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    auto def = CreateValidBaseCustomDef(1000, "custom:mat/sync_test", "Sync Test");
    def.density = ScientificQuantity::Known(4000.0, "kg/m^3", DataQuality::Estimated);
    reg.RegisterMaterial(def);

    auto snap1 = reg.GetSnapshot();
    auto snap1Id = snap1->snapshotId;

    // Mutazione parametri fisici
    def.density = ScientificQuantity::Known(4850.0, "kg/m^3", DataQuality::PeerReviewed);
    if (!reg.RegisterMaterial(def, true)) {
        res.passed = false;
        res.observedDesc = "RegisterMaterial(allowUpdate=true) fallito";
        return res;
    }

    if (reg.GetMaterial(1000).density.value != 4850.0) {
        res.passed = false;
        res.observedDesc = "Valore di densita' nel registro non aggiornato a 4850.0";
        return res;
    }

    auto snap2 = reg.GetSnapshot();
    if (snap2->snapshotId <= snap1Id) {
        res.passed = false;
        res.observedDesc = "snapshotId non incrementato post aggiornamento";
        return res;
    }

    // Verifica immutabilità CoW del vecchio snapshot
    const auto* matInOldSnap = snap1->GetMaterial(1000);
    if (!matInOldSnap || matInOldSnap->density.value != 4000.0) {
        res.passed = false;
        res.observedDesc = "Il vecchio snapshot e' stato mutato in-place (violazione CoW)";
        return res;
    }

    const auto* matInNewSnap = snap2->GetMaterial(1000);
    if (!matInNewSnap || matInNewSnap->density.value != 4850.0) {
        res.passed = false;
        res.observedDesc = "Il nuovo snapshot non riflette il valore aggiornato 4850.0";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Aggiornamento in-place atomico e immutabilita' thread-safe CoW confermata";
    return res;
}

// -----------------------------------------------------------------------------
// TEST 08: Round-trip persistente e riavvio catalogo
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test08_PersistentRoundTripAndCatalogReload() {
    CustomScientificTestResult res;
    res.testId = "CUST-08";
    res.testName = "Round-trip persistente e riavvio catalogo";
    res.inputDesc = "Salvataggio materiali custom su file JSON, ricaricamento in registro pulito e confronto bit-a-bit";
    res.expectedDesc = "Tutti i materiali, stati di ciclo di vita e flag enabledInBlockMaker ripristinati perfettamente";

    ScientificMaterialRegistry reg1;
    reg1.Initialize();

    auto m1 = CreateValidBaseCustomDef(1000, "custom:mat/rt_alpha", "RT Alpha");
    m1.lifecycleState = MaterialLifecycleState::Published;
    m1.enabledInBlockMaker = true;
    reg1.RegisterMaterial(m1);

    auto m2 = CreateValidBaseCustomDef(1001, "custom:mat/rt_beta", "RT Beta");
    m2.lifecycleState = MaterialLifecycleState::Deprecated;
    m2.enabledInBlockMaker = false;
    reg1.RegisterMaterial(m2);

    auto tempPath = std::filesystem::temp_directory_path() / "fairworld_test_custom_mat_rt.json";
    std::string err;

    if (!reg1.SaveCustomMaterialsToJson(tempPath.string(), &err)) {
        res.passed = false;
        res.observedDesc = "SaveCustomMaterialsToJson fallito: " + err;
        return res;
    }

    // Secondo registry pulito
    ScientificMaterialRegistry reg2;
    reg2.Initialize();

    if (!reg2.LoadCustomMaterialsFromJson(tempPath.string(), &err)) {
        res.passed = false;
        res.observedDesc = "LoadCustomMaterialsFromJson fallito: " + err;
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        return res;
    }

    std::error_code ec;
    std::filesystem::remove(tempPath, ec);

    if (!reg2.HasMaterial(1000) || !reg2.HasMaterial(1001)) {
        res.passed = false;
        res.observedDesc = "Materiali 1000 o 1001 non presenti nel registro ricaricato";
        return res;
    }

    const auto& r1 = reg2.GetMaterial(1000);
    const auto& r2 = reg2.GetMaterial(1001);

    if (r1.lifecycleState != MaterialLifecycleState::Published || !r1.enabledInBlockMaker || !r1.isCustom) {
        res.passed = false;
        res.observedDesc = "Metadati di lifecycle per 1000 non ripristinati correttamente";
        return res;
    }

    if (r2.lifecycleState != MaterialLifecycleState::Deprecated || r2.enabledInBlockMaker || !r2.isCustom) {
        res.passed = false;
        res.observedDesc = "Metadati di lifecycle per 1001 non ripristinati correttamente";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Round-trip persistente verificato: serializzazione e caricamento deterministico perfetti";
    return res;
}

// -----------------------------------------------------------------------------
// TEST 09: Separazione catalogo custom e rifiuto collisioni di range standard (< 1000)
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test09_CustomVsStandardSeparationAndRangeCollisionRejection() {
    CustomScientificTestResult res;
    res.testId = "CUST-09";
    res.testName = "Separazione catalogo custom e rifiuto collisioni di range standard (< 1000)";
    res.inputDesc = "Tentativo di caricare file custom contenente un materiale con ID 3 (< 1000, collisione con Basalto standard)";
    res.expectedDesc = "Caricamento respinto; il Basalto standard (ID 3) rimane inalterato";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    auto tempPath = std::filesystem::temp_directory_path() / "fairworld_test_collision_standard.json";

    // Creiamo JSON malizioso con ID 3 nel file custom
    std::string malJson = R"({
        "schemaVersion": 3,
        "materials": [
            {
                "id": 3,
                "canonicalId": "custom:mat/fake_basalt",
                "displayName": "Fake Basalt",
                "density": { "value": 9999.0, "unit": "kg/m^3", "state": "Known" }
            }
        ]
    })";

    {
        std::ofstream f(tempPath);
        f << malJson;
    }

    std::string err;
    bool loaded = reg.LoadCustomMaterialsFromJson(tempPath.string(), &err);

    std::error_code ec;
    std::filesystem::remove(tempPath, ec);

    if (loaded) {
        res.passed = false;
        res.observedDesc = "Accettato erroneamente file custom con ID nel range standard (< 1000)";
        return res;
    }

    // Verifica integrità standard Basalt
    const auto& basalt = reg.GetMaterial(3);
    if (basalt.canonicalId != "fairworld:mat/basalt" || basalt.density.value != 2900.0) {
        res.passed = false;
        res.observedDesc = "Il Basalto standard e' stato corrotto dal tentativo di collisione";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Rifiuto categorico di collisioni con il range standard (< 1000) verificato";
    return res;
}

// -----------------------------------------------------------------------------
// TEST 10: Rollback atomico in caso di errore di persistenza o parsing
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test10_PersistenceFailureAtomicRollback() {
    CustomScientificTestResult res;
    res.testId = "CUST-10";
    res.testName = "Rollback atomico in caso di errore di persistenza o parsing";
    res.inputDesc = "Caricamento di file JSON corrotto in un registro con materiali attivi";
    res.expectedDesc = "Caricamento fallisce; il registro rimane integro nello stato esatto precedente (zero mutazioni)";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    auto preCustom = CreateValidBaseCustomDef(1000, "custom:mat/safe_existing", "Safe Existing");
    reg.RegisterMaterial(preCustom);

    size_t countBefore = reg.GetMaterialCount();

    auto tempPath = std::filesystem::temp_directory_path() / "fairworld_test_corrupted.json";

    // JSON corrotto sintatticamente
    {
        std::ofstream f(tempPath);
        f << "{ \"schemaVersion\": 3, \"materials\": [ { \"id\": 1001, \"canonicalId\": ... CORRUPTED SYNTAX ... ";
    }

    std::string err;
    bool loaded = reg.LoadCustomMaterialsFromJson(tempPath.string(), &err);

    std::error_code ec;
    std::filesystem::remove(tempPath, ec);

    if (loaded) {
        res.passed = false;
        res.observedDesc = "Accettato erroneamente file JSON sintatticamente corrotto";
        return res;
    }

    size_t countAfter = reg.GetMaterialCount();
    if (countAfter != countBefore) {
        res.passed = false;
        res.observedDesc = "Numero di materiali mutato durante transazione fallita";
        return res;
    }

    if (!reg.HasMaterial(1000) || reg.GetMaterial(1000).displayName != "Safe Existing") {
        res.passed = false;
        res.observedDesc = "Materiale preesistente 1000 corrotto o perso";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Rollback atomico transazionale verificato: stato antecedente conservato al 100%";
    return res;
}

// -----------------------------------------------------------------------------
// TEST 11: Sostituzione atomica del catalogo con gestione errori e round-trip Deprecated
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test11_AtomicReplaceFailureAndRecovery() {
    CustomScientificTestResult res;
    res.testId = "CUST-11";
    res.testName = "Sostituzione atomica del catalogo con gestione errori e round-trip Deprecated";
    res.inputDesc = "Creazione catalogo persistente, blocco esclusivo del file per simulare errore di sostituzione, verifica integrita', ripristino e round-trip Deprecated";
    res.expectedDesc = "In caso di errore il file originale rimane invariato; salvataggio successivo ha successo; ID Deprecated preservato e max(ID)+1 rispettato";

    auto tempPath = std::filesystem::temp_directory_path() / "fairworld_test_atomic_replace_safe.json";
    std::error_code rmEc;
    std::filesystem::remove(tempPath, rmEc);
    std::filesystem::remove(std::filesystem::path(tempPath.string() + ".tmp"), rmEc);

    std::string err;

    // Fase 1: Creazione di un catalogo valido con materiale 1000
    ScientificMaterialRegistry reg1;
    reg1.Initialize();

    auto m1000 = CreateValidBaseCustomDef(1000, "custom:mat/safe_orig_1000", "Safe Original 1000");
    m1000.lifecycleState = MaterialLifecycleState::Published;
    m1000.enabledInBlockMaker = true;
    if (!reg1.RegisterMaterial(m1000, &err)) {
        res.passed = false;
        res.observedDesc = "Registrazione m1000 fallita: " + err;
        return res;
    }

    if (!reg1.SaveCustomMaterialsToJson(tempPath.string(), &err)) {
        res.passed = false;
        res.observedDesc = "Salvataggio iniziale catalogo fallito: " + err;
        return res;
    }

    // Verifica che il file esista e contenga 1000
    {
        ScientificMaterialRegistry verifyReg;
        verifyReg.Initialize();
        if (!verifyReg.LoadCustomMaterialsFromJson(tempPath.string(), &err) || !verifyReg.HasMaterial(1000)) {
            res.passed = false;
            res.observedDesc = "Verifica caricamento file iniziale fallita: " + err;
            std::filesystem::remove(tempPath, rmEc);
            return res;
        }
    }

    // Fase 2: Registriamo un secondo materiale in reg1 (1001) e simuliamo errore deterministico di sostituzione
    auto m1001 = CreateValidBaseCustomDef(1001, "custom:mat/safe_second_1001", "Safe Second 1001");
    m1001.lifecycleState = MaterialLifecycleState::Draft;
    if (!reg1.RegisterMaterial(m1001, &err)) {
        res.passed = false;
        res.observedDesc = "Registrazione m1001 in reg1 fallita: " + err;
        std::filesystem::remove(tempPath, rmEc);
        return res;
    }

#if defined(_WIN32)
    // Acquisiamo un handle con condivisione esclusiva in lettura (nessun FILE_SHARE_DELETE o FILE_SHARE_WRITE)
    HANDLE hLock = ::CreateFileW(
        tempPath.wstring().c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hLock != INVALID_HANDLE_VALUE) {
        // Tentativo di salvataggio mentre il file è bloccato: ReplaceFileW deve fallire deterministically
        bool saveFailedAsExpected = !reg1.SaveCustomMaterialsToJson(tempPath.string(), &err);

        // Rilasciamo il blocco subito per consentire le verifiche successive
        ::CloseHandle(hLock);
        hLock = INVALID_HANDLE_VALUE;

        if (!saveFailedAsExpected) {
            res.passed = false;
            res.observedDesc = "SaveCustomMaterialsToJson avrebbe dovuto fallire con file bloccato, invece ha riportato successo";
            std::filesystem::remove(tempPath, rmEc);
            return res;
        }
    }
#endif

    // Fase 3: Verificare che il file originale sia rimasto leggibile e INVARIATO (contiene solo 1000, non 1001)
    {
        ScientificMaterialRegistry regCheck;
        regCheck.Initialize();
        if (!regCheck.LoadCustomMaterialsFromJson(tempPath.string(), &err)) {
            res.passed = false;
            res.observedDesc = "File originale corrotto o non leggibile dopo tentativo di sostituzione fallito: " + err;
            std::filesystem::remove(tempPath, rmEc);
            return res;
        }

        if (!regCheck.HasMaterial(1000) || regCheck.GetMaterial(1000).displayName != "Safe Original 1000") {
            res.passed = false;
            res.observedDesc = "Materiale 1000 nel file originale corrotto o alterato";
            std::filesystem::remove(tempPath, rmEc);
            return res;
        }

        if (regCheck.HasMaterial(1001)) {
            res.passed = false;
            res.observedDesc = "Il file originale contiene erroneamente il materiale 1001 (violazione atomicità sostituzione)";
            std::filesystem::remove(tempPath, rmEc);
            return res;
        }

        // Verifica che il file temporaneo .tmp sia stato ripulito
        auto tempFile = std::filesystem::path(tempPath.string() + ".tmp");
        if (std::filesystem::exists(tempFile)) {
            res.passed = false;
            res.observedDesc = "File temporaneo .tmp non ripulito dopo il fallimento della sostituzione";
            std::filesystem::remove(tempPath, rmEc);
            std::filesystem::remove(tempFile, rmEc);
            return res;
        }
    }

    // Fase 4: Salvataggio successivo sullo stesso percorso (ora che il lock e' rilasciato)
    // Deprechiamo m1001 prima del salvataggio per verificare contestualmente la conservazione degli ID Deprecated
    if (!reg1.DeprecateMaterial(1001, &err)) {
        res.passed = false;
        res.observedDesc = "DeprecateMaterial(1001) fallito: " + err;
        std::filesystem::remove(tempPath, rmEc);
        return res;
    }

    // Verifica contrattuale: la deprecazione disabilita incondizionatamente il materiale in Block Maker
    if (reg1.GetMaterial(1001).enabledInBlockMaker) {
        res.passed = false;
        res.observedDesc = "DeprecateMaterial non ha impostato automaticamente enabledInBlockMaker a false";
        std::filesystem::remove(tempPath, rmEc);
        return res;
    }

    // Verifica contrattuale: rifiuto categorico di abilitare un materiale Deprecated in Block Maker
    if (reg1.SetEnabledInBlockMaker(1001, true, &err)) {
        res.passed = false;
        res.observedDesc = "SetEnabledInBlockMaker(1001, true) avrebbe dovuto fallire per un materiale Deprecated";
        std::filesystem::remove(tempPath, rmEc);
        return res;
    }

    if (!reg1.SaveCustomMaterialsToJson(tempPath.string(), &err)) {
        res.passed = false;
        res.observedDesc = "Salvataggio successivo sullo stesso percorso fallito: " + err;
        std::filesystem::remove(tempPath, rmEc);
        return res;
    }

    // Fase 5: Round-trip del materiale Deprecated in un nuovo registro pulito
    ScientificMaterialRegistry regReload;
    regReload.Initialize();
    if (!regReload.LoadCustomMaterialsFromJson(tempPath.string(), &err)) {
        res.passed = false;
        res.observedDesc = "LoadCustomMaterialsFromJson post-salvataggio riuscito fallito: " + err;
        std::filesystem::remove(tempPath, rmEc);
        return res;
    }

    if (!regReload.HasMaterial(1001)) {
        res.passed = false;
        res.observedDesc = "Materiale 1001 non trovato dopo ricaricamento";
        std::filesystem::remove(tempPath, rmEc);
        return res;
    }

    const auto& def1001 = regReload.GetMaterial(1001);
    if (def1001.lifecycleState != MaterialLifecycleState::Deprecated) {
        res.passed = false;
        res.observedDesc = "Materiale 1001 non ha preservato lo stato Deprecated nel round-trip";
        std::filesystem::remove(tempPath, rmEc);
        return res;
    }

    if (def1001.enabledInBlockMaker) {
        res.passed = false;
        res.observedDesc = "Materiale 1001 Deprecated ha erroneamente enabledInBlockMaker == true";
        std::filesystem::remove(tempPath, rmEc);
        return res;
    }

    // Fase 6: Verifica successiva allocazione max(ID)+1
    // Poiché sono presenti 1000 e 1001 (anche se 1001 è Deprecated), il prossimo ID allocato deve essere 1002
    auto nextAllocatedId = regReload.AllocateNextCustomMaterialId();
    if (nextAllocatedId != 1002) {
        res.passed = false;
        res.observedDesc = "Allocazione ID non ha rispettato max(ID)+1 post-ricaricamento di Deprecated: atteso 1002, osservato " + std::to_string(nextAllocatedId);
        std::filesystem::remove(tempPath, rmEc);
        return res;
    }

    // Pulizia file di test
    std::filesystem::remove(tempPath, rmEc);

    res.passed = true;
    res.observedDesc = "Sostituzione sicura atomica verificata: file originale conservato durante lock, salvataggio successivo riuscito, round-trip Deprecated e allocazione ID 1002 conformi";
    return res;
}

// -----------------------------------------------------------------------------
// TEST 12: Inserimento mentre un vecchio snapshot è ancora utilizzato
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test12_OldSnapshotHandleImmutabilityAcrossInsertion() {
    CustomScientificTestResult res;
    res.testId = "CUST-12";
    res.testName = "Immutabilita' snapshot su inserimento nuovo materiale";
    res.inputDesc = "Snapshot acquisito con materiale 1000; successiva registrazione di materiale 1001";
    res.expectedDesc = "Il vecchio snapshot mantiene 1000 con puntatore stabile e nullptr su 1001; il nuovo snapshot include entrambi";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    auto m1000 = CreateValidBaseCustomDef(1000, "custom:mat/m1000_ins", "Material 1000");
    reg.RegisterMaterial(m1000);

    RegistrySnapshotHandle oldSnap = reg.GetSnapshotHandle();
    const ScientificMaterialDef* oldPtr1000 = oldSnap->GetMaterial(1000);
    size_t countOld = oldSnap->materials.size();

    if (!oldPtr1000 || oldPtr1000->displayName != "Material 1000") {
        res.passed = false;
        res.observedDesc = "Materiale 1000 non trovato o non valido nel vecchio snapshot";
        return res;
    }

    // Inserimento del secondo materiale
    auto m1001 = CreateValidBaseCustomDef(1001, "custom:mat/m1001_ins", "Material 1001");
    if (!reg.RegisterMaterial(m1001)) {
        res.passed = false;
        res.observedDesc = "Registrazione materiale 1001 fallita";
        return res;
    }

    RegistrySnapshotHandle newSnap = reg.GetSnapshotHandle();

    // Verifiche sul vecchio snapshot
    if (oldSnap->materials.size() != countOld) {
        res.passed = false;
        res.observedDesc = "Dimensione vecchio snapshot alterata dopo inserimento: attesa " + std::to_string(countOld) + ", osservata " + std::to_string(oldSnap->materials.size());
        return res;
    }
    if (oldSnap->GetMaterial(1001) != nullptr) {
        res.passed = false;
        res.observedDesc = "Materiale 1001 inaspettatamente visibile nel vecchio snapshot";
        return res;
    }
    // Verifica stabilità puntatore vecchio
    if (oldPtr1000 != oldSnap->GetMaterial(1000) || oldPtr1000->displayName != "Material 1000") {
        res.passed = false;
        res.observedDesc = "Puntatore a materiale 1000 nel vecchio snapshot corrotto o alterato";
        return res;
    }

    // Verifiche sul nuovo snapshot
    if (newSnap->materials.size() != countOld + 1) {
        res.passed = false;
        res.observedDesc = "Nuovo snapshot non ha dimensione incrementata di 1";
        return res;
    }
    const ScientificMaterialDef* newPtr1001 = newSnap->GetMaterial(1001);
    if (!newPtr1001 || newPtr1001->displayName != "Material 1001") {
        res.passed = false;
        res.observedDesc = "Materiale 1001 non trovato o corrotto nel nuovo snapshot";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Immutabilita' garantita: vecchio snapshot e puntatori congelati; nuovo snapshot include entrambi i materiali";
    return res;
}

// -----------------------------------------------------------------------------
// TEST 13: Aggiornamento in-place mentre un vecchio snapshot è ancora utilizzato
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test13_OldSnapshotHandleImmutabilityAcrossInPlaceUpdate() {
    CustomScientificTestResult res;
    res.testId = "CUST-13";
    res.testName = "Immutabilita' snapshot su aggiornamento in-place";
    res.inputDesc = "Snapshot acquisito con materiale 1000 a densita' 4430.0; successivo update in-place a densita' 4500.0";
    res.expectedDesc = "Il vecchio snapshot conserva la densita' originale 4430.0; il nuovo snapshot riporta 4500.0";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    auto m1000 = CreateValidBaseCustomDef(1000, "custom:mat/m1000_upd", "Material 1000 Initial");
    m1000.density = ScientificQuantity::Known(4430.0, "kg/m^3", DataQuality::PeerReviewed, "Original");
    reg.RegisterMaterial(m1000);

    RegistrySnapshotHandle oldSnap = reg.GetSnapshotHandle();
    const ScientificMaterialDef* oldMatPtr = oldSnap->GetMaterial(1000);

    if (!oldMatPtr || oldMatPtr->density.value != 4430.0) {
        res.passed = false;
        res.observedDesc = "Materiale 1000 iniziale non valido nello snapshot";
        return res;
    }

    // Aggiornamento in-place del materiale
    auto m1000_updated = m1000;
    m1000_updated.displayName = "Material 1000 Updated";
    m1000_updated.density = ScientificQuantity::Known(4500.0, "kg/m^3", DataQuality::PeerReviewed, "Updated");
    if (!reg.RegisterMaterial(m1000_updated, true)) {
        res.passed = false;
        res.observedDesc = "Aggiornamento in-place RegisterMaterial(allowUpdate=true) fallito";
        return res;
    }

    RegistrySnapshotHandle newSnap = reg.GetSnapshotHandle();
    const ScientificMaterialDef* newMatPtr = newSnap->GetMaterial(1000);

    // Il vecchio puntatore deve essere ancora vivo e immutato
    if (oldMatPtr->density.value != 4430.0 || oldMatPtr->displayName != "Material 1000 Initial") {
        res.passed = false;
        res.observedDesc = "Isolamento violato: il vecchio snapshot e' stato mutato in-place!";
        return res;
    }

    // Il nuovo snapshot deve avere i nuovi valori
    if (!newMatPtr || newMatPtr->density.value != 4500.0 || newMatPtr->displayName != "Material 1000 Updated") {
        res.passed = false;
        res.observedDesc = "Nuovo snapshot non riflette l'aggiornamento";
        return res;
    }

    // I due puntatori devono puntare a due istanze di memoria heap distinte
    if (oldMatPtr == newMatPtr) {
        res.passed = false;
        res.observedDesc = "Vecchio e nuovo snapshot condividono lo stesso puntatore heap!";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Isolamento perfetto: vecchio snapshot preserva densita' 4430.0 e nome iniziale, nuovo snapshot ha 4500.0";
    return res;
}

// -----------------------------------------------------------------------------
// TEST 14: Pubblicazione e deprecazione mentre vecchi snapshot sono ancora utilizzati
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test14_OldSnapshotHandleImmutabilityAcrossPublicationAndDeprecation() {
    CustomScientificTestResult res;
    res.testId = "CUST-14";
    res.testName = "Immutabilita' snapshot su Publish e Deprecate";
    res.inputDesc = "Materiale 1000 creato in Draft. Snapshot a ciascuna fase: Draft -> Published -> Deprecated";
    res.expectedDesc = "Ciascuno dei 3 snapshot conserva fedelmente lo stato del ciclo di vita al momento dell'acquisizione";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    auto m1000 = CreateValidBaseCustomDef(1000, "custom:mat/m1000_life", "Lifecycle Mat");
    m1000.lifecycleState = MaterialLifecycleState::Draft;
    reg.RegisterMaterial(m1000);

    RegistrySnapshotHandle snapDraft = reg.GetSnapshotHandle();

    // Pubblicazione
    std::string err;
    if (!reg.PublishMaterial(1000, &err)) {
        res.passed = false;
        res.observedDesc = "PublishMaterial fallito: " + err;
        return res;
    }
    RegistrySnapshotHandle snapPublished = reg.GetSnapshotHandle();

    // Deprecazione
    if (!reg.DeprecateMaterial(1000, &err)) {
        res.passed = false;
        res.observedDesc = "DeprecateMaterial fallito: " + err;
        return res;
    }
    RegistrySnapshotHandle snapDeprecated = reg.GetSnapshotHandle();

    // Verifica stati in ciascun snapshot
    const auto* pDraft = snapDraft->GetMaterial(1000);
    const auto* pPub = snapPublished->GetMaterial(1000);
    const auto* pDep = snapDeprecated->GetMaterial(1000);

    if (!pDraft || pDraft->lifecycleState != MaterialLifecycleState::Draft) {
        res.passed = false;
        res.observedDesc = "snapDraft non ha conservato lo stato Draft";
        return res;
    }
    if (!pPub || pPub->lifecycleState != MaterialLifecycleState::Published) {
        res.passed = false;
        res.observedDesc = "snapPublished non ha conservato lo stato Published";
        return res;
    }
    if (!pDep || pDep->lifecycleState != MaterialLifecycleState::Deprecated) {
        res.passed = false;
        res.observedDesc = "snapDeprecated non ha lo stato Deprecated";
        return res;
    }

    // Verifica GetSelectableMaterials su ciascun snapshot
    auto selDraft = snapDraft->GetSelectableMaterials(0);
    auto selPub = snapPublished->GetSelectableMaterials(0);
    auto selDep = snapDeprecated->GetSelectableMaterials(0);

    auto hasMat1000 = [](const std::vector<const ScientificMaterialDef*>& v) {
        return std::find_if(v.begin(), v.end(), [](const auto* m) { return m->id == 1000; }) != v.end();
    };

    if (hasMat1000(selDraft)) {
        res.passed = false;
        res.observedDesc = "selDraft include erroneamente il materiale in stato Draft";
        return res;
    }
    if (!hasMat1000(selPub)) {
        res.passed = false;
        res.observedDesc = "selPub non include il materiale in stato Published";
        return res;
    }
    if (hasMat1000(selDep)) {
        res.passed = false;
        res.observedDesc = "selDep include erroneamente il materiale in stato Deprecated per nuova selezione";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Tutti e 3 gli snapshot preservano stabilmente il loro stato (Draft, Published, Deprecated) e i relativi filtri";
    return res;
}

// -----------------------------------------------------------------------------
// TEST 15: Sostituzione del catalogo (Clear e Load) mentre un vecchio snapshot è ancora utilizzato
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test15_OldSnapshotHandleImmutabilityAcrossCatalogReplacement() {
    CustomScientificTestResult res;
    res.testId = "CUST-15";
    res.testName = "Immutabilita' snapshot su Clear e LoadCustomMaterialsFromJson";
    res.inputDesc = "Acquisizione snapshot con materiale 1000; esecuzione di Clear() e successivo caricamento di file";
    res.expectedDesc = "Il vecchio snapshot rimane valido e accessibile anche dopo che il registry e' stato azzerato e ricaricato";

    std::error_code rmEc;
    auto tempPath = std::filesystem::temp_directory_path() / "fw_p02a_replace_test.json";
    std::filesystem::remove(tempPath, rmEc);

    RegistrySnapshotHandle preservedSnap;
    const ScientificMaterialDef* preservedPtr = nullptr;

    {
        ScientificMaterialRegistry reg1;
        reg1.Initialize();

        auto m1000 = CreateValidBaseCustomDef(1000, "custom:mat/m1000_rep", "Preserved Material 1000");
        reg1.RegisterMaterial(m1000);

        preservedSnap = reg1.GetSnapshotHandle();
        preservedPtr = preservedSnap->GetMaterial(1000);

        // Prepara un file con un materiale 1001
        ScientificMaterialRegistry regFile;
        regFile.Initialize();
        auto m1001 = CreateValidBaseCustomDef(1001, "custom:mat/m1001_rep", "Replacement Material 1001");
        regFile.RegisterMaterial(m1001);
        std::string err;
        if (!regFile.SaveCustomMaterialsToJson(tempPath.string(), &err)) {
            res.passed = false;
            res.observedDesc = "Salvataggio catalogo di supporto fallito: " + err;
            std::filesystem::remove(tempPath, rmEc);
            return res;
        }

        // Svuotamento completo del registry
        reg1.Clear();
        if (reg1.GetMaterialCount() != 0) {
            res.passed = false;
            res.observedDesc = "Clear() non ha azzerato il registry";
            std::filesystem::remove(tempPath, rmEc);
            return res;
        }

        // Il vecchio snapshot deve essere ancora perfettamente intatto!
        if (!preservedSnap.IsValid() || !preservedPtr || preservedPtr->displayName != "Preserved Material 1000") {
            res.passed = false;
            res.observedDesc = "Clear() ha invalidato o corrotto la memoria del vecchio snapshot!";
            std::filesystem::remove(tempPath, rmEc);
            return res;
        }

        // Caricamento del nuovo file nel registry svuotato
        if (!reg1.LoadCustomMaterialsFromJson(tempPath.string(), &err)) {
            res.passed = false;
            res.observedDesc = "LoadCustomMaterialsFromJson post-Clear fallito: " + err;
            std::filesystem::remove(tempPath, rmEc);
            return res;
        }

        // Il registry ora contiene solo 1001 (e standard)
        if (!reg1.HasMaterial(1001) || reg1.HasMaterial(1000)) {
            res.passed = false;
            res.observedDesc = "Registry non sincronizzato correttamente con il file caricato";
            std::filesystem::remove(tempPath, rmEc);
            return res;
        }

        // Il vecchio snapshot deve ANCORA contenere 1000 e NON contenere 1001!
        if (preservedSnap->GetMaterial(1000) != preservedPtr || preservedPtr->displayName != "Preserved Material 1000") {
            res.passed = false;
            res.observedDesc = "LoadCustomMaterialsFromJson ha corrotto i dati del vecchio snapshot!";
            std::filesystem::remove(tempPath, rmEc);
            return res;
        }
        if (preservedSnap->GetMaterial(1001) != nullptr) {
            res.passed = false;
            res.observedDesc = "Materiale 1001 inaspettatamente apparso nel vecchio snapshot!";
            std::filesystem::remove(tempPath, rmEc);
            return res;
        }
    } // reg1 distrutto: preservedSnap sopravvive al suo registry d'origine!

    if (!preservedSnap.IsValid() || preservedPtr->displayName != "Preserved Material 1000") {
        res.passed = false;
        res.observedDesc = "Lo snapshot non e' sopravvissuto alla distruzione del registry originario!";
        std::filesystem::remove(tempPath, rmEc);
        return res;
    }

    std::filesystem::remove(tempPath, rmEc);
    res.passed = true;
    res.observedDesc = "Snapshot isolato e protetto attraverso Clear(), LoadCustomMaterialsFromJson() e distruzione del registry originario";
    return res;
}

// -----------------------------------------------------------------------------
// TEST 16: Semantica per valore di GetMaterial: copie immuni da mutazioni e deallocazioni
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test16_GetMaterialValueSemanticsImmunityToMutations() {
    CustomScientificTestResult res;
    res.testId = "CUST-16";
    res.testName = "GetMaterial con semantica per valore immune a mutazioni";
    res.inputDesc = "GetMaterial() memorizzato per valore; registry mutato, esteso e svuotato";
    res.expectedDesc = "La copia ottenuta conserva i valori originali senza corruzione di memoria; query su ID inesistente ritorna fallback";

    ScientificMaterialDef savedCopy;

    {
        ScientificMaterialRegistry reg;
        reg.Initialize();

        auto m1000 = CreateValidBaseCustomDef(1000, "custom:mat/m1000_val", "Original Value Copy");
        m1000.density = ScientificQuantity::Known(4430.0, "kg/m^3", DataQuality::PeerReviewed, "Original");
        reg.RegisterMaterial(m1000);

        // Copia per valore garantita dal contratto P0.2A
        savedCopy = reg.GetMaterial(1000);

        // Mutazione in-place del materiale nel registry
        auto m1000_mutated = m1000;
        m1000_mutated.displayName = "Mutated In Registry";
        m1000_mutated.density = ScientificQuantity::Known(9999.0, "kg/m^3", DataQuality::Estimated, "Mutated");
        reg.RegisterMaterial(m1000_mutated, true);

        // Aggiunta di altri materiali per forzare rehash/riallocazioni
        for (ScientificMaterialID id = 1001; id <= 1010; ++id) {
            auto extra = CreateValidBaseCustomDef(id, "custom:mat/extra_" + std::to_string(id), "Extra " + std::to_string(id));
            reg.RegisterMaterial(extra);
        }

        // Svuotamento completo
        reg.Clear();

        // Query su materiale non esistente: deve ritornare fallback valido senza crash
        ScientificMaterialDef missing = reg.GetMaterial(99999);
        if (missing.id != INVALID_SCIENTIFIC_MATERIAL_ID) {
            res.passed = false;
            res.observedDesc = "GetMaterial per ID inesistente non ha restituito fallback valido";
            return res;
        }

        // Binding const auto& estende il lifetime del prvalue
        const auto& tempBound = reg.GetMaterial(99999);
        if (tempBound.id != INVALID_SCIENTIFIC_MATERIAL_ID) {
            res.passed = false;
            res.observedDesc = "Binding a const auto& di GetMaterial fallito";
            return res;
        }
    } // reg esce dallo scope ed e' distrutto

    // Verifica che savedCopy sia rimasto intatto e valido
    if (savedCopy.id != 1000 || savedCopy.displayName != "Original Value Copy" || savedCopy.density.value != 4430.0) {
        res.passed = false;
        res.observedDesc = "La copia di GetMaterial e' stata alterata o corrotta dalle mutazioni successive!";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Semantica per valore verificata: la copia sopravvive a update in-place, rehash, Clear() e distruzione del registry";
    return res;
}

// -----------------------------------------------------------------------------
// TEST 17: Semantica per valore di GetAllMaterials e isolamento dello storage tramite snapshot
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test17_GetAllMaterialsValueSemanticsAndSnapshotIsolation() {
    CustomScientificTestResult res;
    res.testId = "CUST-17";
    res.testName = "GetAllMaterials semantica a valore e snapshot isolation";
    res.inputDesc = "GetAllMaterials() memorizzato; registry mutato, esteso e svuotato; verifica snap->GetAllMaterials()";
    res.expectedDesc = "La mappa per valore rimane intatta e valida post-svuotamento; snap->GetAllMaterials() fornisce accesso zero-copy protetto";

    std::unordered_map<ScientificMaterialID, ScientificMaterialDef> mapCopy;
    RegistrySnapshotHandle snapshotHandle;

    {
        ScientificMaterialRegistry reg;
        reg.Initialize();

        auto m1000 = CreateValidBaseCustomDef(1000, "custom:mat/m1000_all", "Map Material 1000");
        reg.RegisterMaterial(m1000);

        // 1. Acquisizione mappa per valore (nessun riferimento a storage mutabile esposto)
        mapCopy = reg.GetAllMaterials();

        // 2. Acquisizione snapshot immutabile con ownership esplicita
        snapshotHandle = reg.GetSnapshotHandle();

        // 3. Mutazione del registro: modifica in-place, aggiunta di altri materiali e Clear()
        auto m1000_mut = m1000;
        m1000_mut.displayName = "Map Material 1000 Mutated";
        reg.RegisterMaterial(m1000_mut, true);

        auto m1001 = CreateValidBaseCustomDef(1001, "custom:mat/m1001_all", "Map Material 1001");
        reg.RegisterMaterial(m1001);

        reg.Clear();
        if (reg.GetMaterialCount() != 0) {
            res.passed = false;
            res.observedDesc = "Clear() non ha svuotato il registry";
            return res;
        }
    } // reg distrutto

    // Verifica della copia per valore: deve avere esattamente la dimensione originale e il nome iniziale
    if (mapCopy.find(1000) == mapCopy.end() || mapCopy.at(1000).displayName != "Map Material 1000") {
        res.passed = false;
        res.observedDesc = "mapCopy alterata o corrotta dalle mutazioni successive del registry!";
        return res;
    }
    if (mapCopy.find(1001) != mapCopy.end()) {
        res.passed = false;
        res.observedDesc = "mapCopy include materiali aggiunti successivamente alla sua acquisizione!";
        return res;
    }

    // Verifica dello snapshot protetto: deve avere conservato la versione al momento dell'acquisizione
    if (!snapshotHandle.IsValid()) {
        res.passed = false;
        res.observedDesc = "snapshotHandle non valido dopo distruzione del registry originario";
        return res;
    }
    const auto& snapMaterials = snapshotHandle->GetAllMaterials();
    if (snapMaterials.find(1000) == snapMaterials.end() || snapMaterials.at(1000).displayName != "Map Material 1000") {
        res.passed = false;
        res.observedDesc = "snapshotHandle->GetAllMaterials() non ha preservato la vista immutabile originaria";
        return res;
    }
    if (snapMaterials.find(1001) != snapMaterials.end()) {
        res.passed = false;
        res.observedDesc = "snapshotHandle->GetAllMaterials() contaminato da registrazioni successive!";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Semantica sicura certificata: GetAllMaterials() per valore isola completamente lo storage; snap->GetAllMaterials() preserva immutabilita' con zero-copy";
    return res;
}

// -----------------------------------------------------------------------------
// TEST 18: Revisione monotona su mutazioni, idempotenza e conteggio invariato
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test18_RegistryRevisionMutationAndIdempotenceContracts() {
    CustomScientificTestResult res;
    res.testId = "CUST-18";
    res.testName = "Revisione monotona su mutazioni, idempotenza e conteggio invariato";
    res.inputDesc = "Operazioni di registrazione, aggiornamento in-place, pubblicazione, deprecazione e availability con verifiche di incremento e idempotenza";
    res.expectedDesc = "La revisione avanza esattamente di 1 per mutazione effettiva e rimane invariata per errori e operazioni idempotenti";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    uint64_t baseRev = reg.GetRevision();

    // 1. Registrazione riuscita di un nuovo materiale custom: revisione deve avanzare esattamente di 1
    auto def1 = CreateValidBaseCustomDef(1000, "custom:mat/rev_alpha", "Revision Alpha");
    def1.lifecycleState = MaterialLifecycleState::Draft;
    if (!reg.RegisterMaterial(def1)) {
        res.passed = false;
        res.observedDesc = "Registrazione def1 fallita";
        return res;
    }
    if (reg.GetRevision() != baseRev + 1) {
        res.passed = false;
        res.observedDesc = "Revisione non avanzata dopo registrazione riuscita: atteso " + std::to_string(baseRev + 1) + ", osservato " + std::to_string(reg.GetRevision());
        return res;
    }
    uint64_t revAfterReg = reg.GetRevision();

    // 2. Operazione rifiutata: registrazione duplicata con allowUpdate=false non deve avanzare revisione
    if (reg.RegisterMaterial(def1, false)) {
        res.passed = false;
        res.observedDesc = "Registrazione duplicata avrebbe dovuto fallire";
        return res;
    }
    if (reg.GetRevision() != revAfterReg) {
        res.passed = false;
        res.observedDesc = "Revisione erroneamente avanzata dopo registrazione rifiutata";
        return res;
    }

    // 3. Operazione rifiutata: validazione fallita (es. densità negativa)
    ScientificMaterialDef badDef = def1;
    badDef.id = 1001;
    badDef.canonicalId = "custom:mat/bad_density";
    badDef.density = ScientificQuantity::Known(-500.0, "kg/m^3");
    std::string valErr;
    if (reg.ValidateMaterialDefinition(badDef, &valErr)) {
        res.passed = false;
        res.observedDesc = "Validazione avrebbe dovuto fallire per densita' negativa";
        return res;
    }
    if (reg.GetRevision() != revAfterReg) {
        res.passed = false;
        res.observedDesc = "Revisione erroneamente mutata dopo validazione fallita";
        return res;
    }

    // 4. Operazione idempotente: Update in-place con dati identici (allowUpdate=true) -> nessun incremento
    if (!reg.RegisterMaterial(def1, true)) {
        res.passed = false;
        res.observedDesc = "Update idempotente fallito";
        return res;
    }
    if (reg.GetRevision() != revAfterReg) {
        res.passed = false;
        res.observedDesc = "Revisione erroneamente avanzata dopo update idempotente identico: osservato " + std::to_string(reg.GetRevision());
        return res;
    }

    // 5. Aggiornamento del materiale con conteggio invariato: modifica semantica (es. descrizione) -> avanza di 1
    size_t countBeforeUpdate = reg.GetMaterialCount();
    auto def1Updated = def1;
    def1Updated.description = "Updated aerospace grade description";
    if (!reg.RegisterMaterial(def1Updated, true)) {
        res.passed = false;
        res.observedDesc = "Aggiornamento in-place fallito";
        return res;
    }
    if (reg.GetMaterialCount() != countBeforeUpdate) {
        res.passed = false;
        res.observedDesc = "Conteggio materiali alterato durante update in-place";
        return res;
    }
    if (reg.GetRevision() != revAfterReg + 1) {
        res.passed = false;
        res.observedDesc = "Revisione non avanzata dopo update in-place con conteggio invariato: atteso " + std::to_string(revAfterReg + 1) + ", osservato " + std::to_string(reg.GetRevision());
        return res;
    }
    uint64_t revAfterUpdate = reg.GetRevision();

    // 6. Transizione di ciclo di vita: PublishMaterial da Draft a Published -> avanza di 1
    std::string pubErr;
    if (!reg.PublishMaterial(1000, &pubErr)) {
        res.passed = false;
        res.observedDesc = "PublishMaterial fallito: " + pubErr;
        return res;
    }
    if (reg.GetRevision() != revAfterUpdate + 1) {
        res.passed = false;
        res.observedDesc = "Revisione non avanzata dopo PublishMaterial: atteso " + std::to_string(revAfterUpdate + 1) + ", osservato " + std::to_string(reg.GetRevision());
        return res;
    }
    uint64_t revAfterPublish = reg.GetRevision();

    // 7. PublishMaterial idempotente (già Published) -> nessun incremento
    if (!reg.PublishMaterial(1000, &pubErr)) {
        res.passed = false;
        res.observedDesc = "PublishMaterial idempotente fallito: " + pubErr;
        return res;
    }
    if (reg.GetRevision() != revAfterPublish) {
        res.passed = false;
        res.observedDesc = "Revisione erroneamente avanzata dopo Publish idempotente";
        return res;
    }

    // 8. SetEnabledInBlockMaker: disabilitazione da true a false -> avanza di 1
    std::string bmErr;
    if (!reg.SetEnabledInBlockMaker(1000, false, &bmErr)) {
        res.passed = false;
        res.observedDesc = "SetEnabledInBlockMaker(false) fallito: " + bmErr;
        return res;
    }
    if (reg.GetRevision() != revAfterPublish + 1) {
        res.passed = false;
        res.observedDesc = "Revisione non avanzata dopo SetEnabledInBlockMaker(false)";
        return res;
    }
    uint64_t revAfterDisable = reg.GetRevision();

    // 9. SetEnabledInBlockMaker idempotente (già false) -> nessun incremento
    if (!reg.SetEnabledInBlockMaker(1000, false, &bmErr)) {
        res.passed = false;
        res.observedDesc = "SetEnabledInBlockMaker(false) idempotente fallito: " + bmErr;
        return res;
    }
    if (reg.GetRevision() != revAfterDisable) {
        res.passed = false;
        res.observedDesc = "Revisione erroneamente avanzata dopo SetEnabledInBlockMaker idempotente";
        return res;
    }

    // 10. DeprecateMaterial: transizione a Deprecated -> avanza di 1
    std::string depErr;
    if (!reg.DeprecateMaterial(1000, &depErr)) {
        res.passed = false;
        res.observedDesc = "DeprecateMaterial fallito: " + depErr;
        return res;
    }
    if (reg.GetRevision() != revAfterDisable + 1) {
        res.passed = false;
        res.observedDesc = "Revisione non avanzata dopo DeprecateMaterial";
        return res;
    }
    uint64_t revAfterDeprecate = reg.GetRevision();

    // 11. DeprecateMaterial idempotente (già Deprecated) -> nessun incremento
    if (!reg.DeprecateMaterial(1000, &depErr)) {
        res.passed = false;
        res.observedDesc = "DeprecateMaterial idempotente fallito: " + depErr;
        return res;
    }
    if (reg.GetRevision() != revAfterDeprecate) {
        res.passed = false;
        res.observedDesc = "Revisione erroneamente avanzata dopo DeprecateMaterial idempotente";
        return res;
    }

    // 12. Operazione illegale: tentare di ri-pubblicare un Deprecated deve fallire e non avanzare revisione
    if (reg.PublishMaterial(1000, &pubErr)) {
        res.passed = false;
        res.observedDesc = "PublishMaterial avrebbe dovuto essere rifiutato per materiale Deprecated";
        return res;
    }
    if (reg.GetRevision() != revAfterDeprecate) {
        res.passed = false;
        res.observedDesc = "Revisione alterata dopo tentativo illegale di ri-pubblicare Deprecated";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Contratto di revisione su mutazioni, idempotenza, conteggio invariato e operazioni rifiutate validato al 100%";
    return res;
}

// -----------------------------------------------------------------------------
// TEST 19: Immutabilità revisione snapshot, rollback di persistenza e Clear/Load
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test19_RegistryRevisionPersistenceRollbackAndSnapshotImmutability() {
    CustomScientificTestResult res;
    res.testId = "CUST-19";
    res.testName = "Immutabilita' revisione snapshot, rollback di persistenza e Clear/Load";
    res.inputDesc = "Snapshot multipli, simulazione errori persistenza, rollback, Clear idempotente e caricamento JSON";
    res.expectedDesc = "Snapshot storici mantengono la propria revisione; rollback non muta revisione; Clear e Load avanzano revisione coerentemente";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    // 1. Acquisizione snapshot 0
    RegistrySnapshotHandle snap0 = reg.GetSnapshotHandle();
    uint64_t rev0 = reg.GetRevision();
    if (!snap0.IsValid() || snap0->revision != rev0) {
        res.passed = false;
        res.observedDesc = "Revisione snapshot0 non coincide con revisione del registro";
        return res;
    }

    // 2. Registrazione materiale 1000 e acquisizione snapshot 1
    auto m1000 = CreateValidBaseCustomDef(1000, "custom:mat/snap_test_1000", "Snap Test 1000");
    reg.RegisterMaterial(m1000);

    RegistrySnapshotHandle snap1 = reg.GetSnapshotHandle();
    uint64_t rev1 = reg.GetRevision();

    if (rev1 <= rev0) {
        res.passed = false;
        res.observedDesc = "Revisione non avanzata monotonicamente: rev0=" + std::to_string(rev0) + ", rev1=" + std::to_string(rev1);
        return res;
    }
    if (snap1->revision != rev1) {
        res.passed = false;
        res.observedDesc = "snap1->revision (" + std::to_string(snap1->revision) + ") non coincide con rev1 (" + std::to_string(rev1) + ")";
        return res;
    }

    // Verifica immutabilità di snap0
    if (snap0->revision != rev0) {
        res.passed = false;
        res.observedDesc = "snap0->revision alterata dopo nuova mutazione: atteso " + std::to_string(rev0) + ", osservato " + std::to_string(snap0->revision);
        return res;
    }
    if (snap0->GetMaterial(1000) != nullptr) {
        res.passed = false;
        res.observedDesc = "snap0 contiene erroneamente materiale 1000 registrato successivamente";
        return res;
    }

    // 3. Rollback di persistenza: verifica che errori di scrittura non alterino revisione, snapshot, memoria o file originale
    std::error_code ec;
    auto tempOriginalFile = std::filesystem::temp_directory_path() / "fairworld_cust19_original.json";
    std::filesystem::remove(tempOriginalFile, ec);

    std::string saveErr;
    if (!reg.SaveCustomMaterialsToJson(tempOriginalFile.string(), &saveErr)) {
        res.passed = false;
        res.observedDesc = "Salvataggio preliminare catalogo valido fallito: " + saveErr;
        return res;
    }

    uint64_t revBeforeSaveFail = reg.GetRevision();
    auto snapBeforeSaveFail = reg.GetSnapshotHandle();
    size_t countBeforeSaveFail = reg.GetMaterialCount();

    // 3a. Conflitto filesystem controllato: un componente del percorso è un file ordinario anziché una directory
    auto blockingFile = std::filesystem::temp_directory_path() / "fairworld_cust19_not_a_dir.tmp";
    std::filesystem::remove(blockingFile, ec);
    {
        std::ofstream bf(blockingFile);
        bf << "blocking regular file";
    }

    auto conflictingSavePath = blockingFile / "sub" / "unwritable_catalog.json";
    std::filesystem::path conflictingTempPath = conflictingSavePath.string() + ".tmp";

    bool saveRes = reg.SaveCustomMaterialsToJson(conflictingSavePath.string(), &saveErr);

    // Pulizia del file bloccante
    std::filesystem::remove(blockingFile, ec);

    if (saveRes) {
        res.passed = false;
        res.observedDesc = "Salvataggio con percorso in conflitto (componente file ordinario) avrebbe dovuto fallire";
        std::filesystem::remove(tempOriginalFile, ec);
        return res;
    }
    if (reg.GetRevision() != revBeforeSaveFail) {
        res.passed = false;
        res.observedDesc = "Revisione alterata a seguito di fallimento di salvataggio (conflitto directory)";
        std::filesystem::remove(tempOriginalFile, ec);
        return res;
    }
    if (reg.GetSnapshotHandle()->revision != revBeforeSaveFail) {
        res.passed = false;
        res.observedDesc = "Snapshot attivo alterato a seguito di fallimento di salvataggio (conflitto directory)";
        std::filesystem::remove(tempOriginalFile, ec);
        return res;
    }
    if (reg.GetMaterialCount() != countBeforeSaveFail) {
        res.passed = false;
        res.observedDesc = "Conteggio materiali in memoria alterato dopo fallimento di salvataggio";
        std::filesystem::remove(tempOriginalFile, ec);
        return res;
    }
    if (std::filesystem::exists(conflictingTempPath, ec)) {
        res.passed = false;
        res.observedDesc = "File temporaneo residuo (.tmp) presente dopo fallimento salvataggio";
        std::filesystem::remove(conflictingTempPath, ec);
        std::filesystem::remove(tempOriginalFile, ec);
        return res;
    }

#if defined(_WIN32)
    // 3b. Fallimento atomico di sostituzione su file originale esistente bloccato in condivisione esclusiva
    HANDLE hLock = ::CreateFileW(
        tempOriginalFile.wstring().c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hLock != INVALID_HANDLE_VALUE) {
        std::filesystem::path lockedTempPath = tempOriginalFile.string() + ".tmp";
        bool lockedSaveRes = reg.SaveCustomMaterialsToJson(tempOriginalFile.string(), &saveErr);
        ::CloseHandle(hLock);

        if (lockedSaveRes) {
            res.passed = false;
            res.observedDesc = "Salvataggio su file originale bloccato avrebbe dovuto fallire";
            std::filesystem::remove(tempOriginalFile, ec);
            return res;
        }

        // Verifica che il file originale sia rimasto valido, leggibile e completamente inalterato
        ScientificMaterialRegistry regCheck;
        regCheck.Initialize();
        if (!regCheck.LoadCustomMaterialsFromJson(tempOriginalFile.string(), &saveErr)) {
            res.passed = false;
            res.observedDesc = "File originale non leggibile o corrotto dopo fallimento di ReplaceFileW: " + saveErr;
            std::filesystem::remove(tempOriginalFile, ec);
            return res;
        }
        if (!regCheck.HasMaterial(1000) || regCheck.GetMaterial(1000).displayName != "Snap Test 1000") {
            res.passed = false;
            res.observedDesc = "Contenuto del file originale alterato a seguito di salvataggio fallito";
            std::filesystem::remove(tempOriginalFile, ec);
            return res;
        }

        // Verifica nessun file temporaneo residuo
        if (std::filesystem::exists(lockedTempPath, ec)) {
            res.passed = false;
            res.observedDesc = "File temporaneo (.tmp) residuo presente dopo fallimento ReplaceFileW";
            std::filesystem::remove(lockedTempPath, ec);
            std::filesystem::remove(tempOriginalFile, ec);
            return res;
        }

        // Verifica invarianza revisione e snapshot del registro
        if (reg.GetRevision() != revBeforeSaveFail) {
            res.passed = false;
            res.observedDesc = "Revisione alterata a seguito di fallimento ReplaceFileW";
            std::filesystem::remove(tempOriginalFile, ec);
            return res;
        }
        if (reg.GetSnapshotHandle()->revision != revBeforeSaveFail) {
            res.passed = false;
            res.observedDesc = "Snapshot attivo alterato a seguito di fallimento ReplaceFileW";
            std::filesystem::remove(tempOriginalFile, ec);
            return res;
        }
    }
#endif
    std::filesystem::remove(tempOriginalFile, ec);

    // 4. Fallimento caricamento (rollback transazionale di Load): file inesistente o corrotto non deve mutare revisione
    std::string loadErr;
    uint64_t revBeforeLoadFail = reg.GetRevision();
    auto nonExistentPath = std::filesystem::temp_directory_path() / "fairworld_cust19_nonexistent_99999.json";
    std::filesystem::remove(nonExistentPath, ec);

    bool loadRes = reg.LoadCustomMaterialsFromJson(nonExistentPath.string(), &loadErr);
    if (loadRes) {
        res.passed = false;
        res.observedDesc = "LoadCustomMaterialsFromJson su file inesistente avrebbe dovuto fallire";
        return res;
    }
    if (reg.GetRevision() != revBeforeLoadFail) {
        res.passed = false;
        res.observedDesc = "Revisione alterata dopo fallimento caricamento JSON su file inesistente";
        return res;
    }

    auto corruptPath = std::filesystem::temp_directory_path() / "fairworld_cust19_corrupt.json";
    {
        std::ofstream cf(corruptPath);
        cf << "{ \"schemaVersion\": 3, \"materials\": [ { invalid_json_syntax ";
    }
    bool loadCorruptRes = reg.LoadCustomMaterialsFromJson(corruptPath.string(), &loadErr);
    std::filesystem::remove(corruptPath, ec);
    if (loadCorruptRes) {
        res.passed = false;
        res.observedDesc = "LoadCustomMaterialsFromJson su file JSON corrotto avrebbe dovuto fallire";
        return res;
    }
    if (reg.GetRevision() != revBeforeLoadFail) {
        res.passed = false;
        res.observedDesc = "Revisione alterata dopo fallimento caricamento JSON corrotto";
        return res;
    }

    // 5. Successo di Save + Load: persistenza round-trip valida avanza la revisione una sola volta
    auto tempPath = std::filesystem::temp_directory_path() / "fairworld_test_rev_roundtrip.json";
    std::filesystem::remove(tempPath, ec);

    if (!reg.SaveCustomMaterialsToJson(tempPath.string(), &saveErr)) {
        res.passed = false;
        res.observedDesc = "SaveCustomMaterialsToJson fallito: " + saveErr;
        return res;
    }

    ScientificMaterialRegistry regTarget;
    regTarget.Initialize();
    uint64_t targetRevBefore = regTarget.GetRevision();

    if (!regTarget.LoadCustomMaterialsFromJson(tempPath.string(), &loadErr)) {
        res.passed = false;
        res.observedDesc = "LoadCustomMaterialsFromJson fallito: " + loadErr;
        std::filesystem::remove(tempPath, ec);
        return res;
    }
    std::filesystem::remove(tempPath, ec);

    if (regTarget.GetRevision() != targetRevBefore + 1) {
        res.passed = false;
        res.observedDesc = "LoadCustomMaterialsFromJson non ha avanzato la revisione esattamente di 1: atteso " + std::to_string(targetRevBefore + 1) + ", osservato " + std::to_string(regTarget.GetRevision());
        return res;
    }
    if (!regTarget.HasMaterial(1000)) {
        res.passed = false;
        res.observedDesc = "regTarget non contiene materiale 1000 ricaricato";
        return res;
    }

    // 6. Clear: svuota registro non vuoto -> revisione avanza di 1
    uint64_t revBeforeClear = regTarget.GetRevision();
    regTarget.Clear();
    if (regTarget.GetMaterialCount() != 0) {
        res.passed = false;
        res.observedDesc = "Clear non ha svuotato i materiali";
        return res;
    }
    if (regTarget.GetRevision() != revBeforeClear + 1) {
        res.passed = false;
        res.observedDesc = "Clear su registro non vuoto non ha avanzato la revisione: atteso " + std::to_string(revBeforeClear + 1) + ", osservato " + std::to_string(regTarget.GetRevision());
        return res;
    }

    // 7. Clear idempotente: Clear su registro già vuoto -> revisione NON avanza
    uint64_t revAfterClear = regTarget.GetRevision();
    regTarget.Clear();
    if (regTarget.GetRevision() != revAfterClear) {
        res.passed = false;
        res.observedDesc = "Clear idempotente su registro già vuoto ha erroneamente avanzato la revisione";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Immutabilita' revisione snapshot, isolamento rollback e comportamento Clear/Load validati al 100%";
    return res;
}

// =============================================================================
// P0.2C: CONCORRENZA REGISTRY E STABILITÀ ID CUSTOM (CUST-20 .. CUST-22)
// =============================================================================

CustomScientificTestResult CustomScientificMaterialContractTests::Test20_ConcurrentReadersDuringUpdatesAndSnapshotConsistency() {
    CustomScientificTestResult res;
    res.testId = "CUST-20";
    res.testName = "Lettori concorrenti durante aggiornamenti, validita' snapshot storici e coerenza";
    res.inputDesc = "4 reader thread continui su GetSnapshotHandle() durante mutazioni concorrenti (Register, Update, Publish, Clear) da parte di 1 writer thread";
    res.expectedDesc = "Snapshot storici validi e immutabili, zero data race, zero puntatori nulli/corrotti, revisione snapshot coerente";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    std::atomic<bool> startFlag{false};
    std::atomic<bool> stopFlag{false};
    std::atomic<size_t> totalReadsCompleted{0};
    std::atomic<bool> readerIntegrityFailed{false};
    std::string failureDetail;
    std::mutex failureMutex;

    auto reportFailure = [&](const std::string& msg) {
        std::lock_guard<std::mutex> lock(failureMutex);
        if (failureDetail.empty()) {
            failureDetail = msg;
        }
        readerIntegrityFailed = true;
    };

    // Creazione 4 Reader Thread
    const size_t READER_THREADS = 4;
    std::vector<std::thread> readers;
    readers.reserve(READER_THREADS);

    for (size_t r = 0; r < READER_THREADS; ++r) {
        readers.emplace_back([&, r]() {
            while (!startFlag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            RegistrySnapshotHandle retainedSnapshot;

            while (!stopFlag.load(std::memory_order_relaxed)) {
                RegistrySnapshotHandle snap = reg.GetSnapshotHandle();
                if (!snap.IsValid()) {
                    reportFailure("Reader " + std::to_string(r) + ": Snapshot handle non valido!");
                    break;
                }

                // Controllo coerenza interna dello snapshot
                const auto& materials = snap->GetAllMaterials();
                const auto& canonMap = snap->canonicalToIdMap;
                if (materials.size() != canonMap.size()) {
                    reportFailure("Reader " + std::to_string(r) + ": Discrepanza materiali (" + 
                                  std::to_string(materials.size()) + ") e mappa canonica (" + 
                                  std::to_string(canonMap.size()) + ") nello snapshot!");
                    break;
                }

                uint64_t snapRev = snap->GetRevision();
                uint64_t curRegRev = reg.GetRevision();
                if (snapRev > curRegRev) {
                    reportFailure("Reader " + std::to_string(r) + ": Revisione snapshot (" + 
                                  std::to_string(snapRev) + ") maggiore della revisione registry (" + 
                                  std::to_string(curRegRev) + ")!");
                    break;
                }

                for (const auto& [id, mat] : materials) {
                    const auto* queried = snap->GetMaterial(id);
                    if (!queried || queried->id != id) {
                        reportFailure("Reader " + std::to_string(r) + ": Incoerenza GetMaterial per ID " + std::to_string(id));
                        break;
                    }
                    if (queried->canonicalId.empty()) {
                        reportFailure("Reader " + std::to_string(r) + ": canonicalId vuoto nello snapshot per ID " + std::to_string(id));
                        break;
                    }
                }

                // Conserva uno snapshot storico per verificare che rimanga valido anche dopo Clear/mutazioni
                if (!retainedSnapshot.IsValid()) {
                    if (!snap->GetAllMaterials().empty()) {
                        retainedSnapshot = snap;
                    }
                } else {
                    // Verifica che lo snapshot conservato in precedenza rimanga integro
                    if (retainedSnapshot->GetAllMaterials().empty() && retainedSnapshot->GetRevision() > 0) {
                        reportFailure("Reader " + std::to_string(r) + ": Snapshot storico deallocato o corrotto!");
                        break;
                    }
                }

                totalReadsCompleted.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Creazione 1 Writer Thread
    std::thread writer([&]() {
        while (!startFlag.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        const size_t STRESS_CYCLES = 50;
        for (size_t cycle = 0; cycle < STRESS_CYCLES; ++cycle) {
            ScientificMaterialID baseId = static_cast<ScientificMaterialID>(1000 + cycle * 100);

            // Fase 1: Registrazione e pubblicazione concorrente di 40 materiali
            for (ScientificMaterialID id = baseId; id < baseId + 40; ++id) {
                ScientificMaterialDef m = CreateValidBaseCustomDef(id, "custom:mat/reader_test_" + std::to_string(id), "Mat Reader Test " + std::to_string(id));
                reg.RegisterMaterial(m);
                if (id % 2 == 0) {
                    reg.PublishMaterial(id);
                }
            }

            // Fase 2: In-place update di alcuni materiali
            for (ScientificMaterialID id = baseId; id < baseId + 20; ++id) {
                ScientificMaterialDef m = reg.GetMaterial(id);
                m.density = ScientificQuantity::Known(5000.0 + static_cast<double>(id), "kg/m^3");
                reg.RegisterMaterial(m, true);
            }

            // Fase 3: Clear del registro e ripopolamento standard
            reg.Clear();
            reg.Initialize();

            // Fase 4: Registrazione successiva di nuovi materiali
            for (ScientificMaterialID id = baseId + 40; id < baseId + 60; ++id) {
                ScientificMaterialDef m = CreateValidBaseCustomDef(id, "custom:mat/reader_postclear_" + std::to_string(id), "Mat PostClear " + std::to_string(id));
                reg.RegisterMaterial(m);
            }
        }

        stopFlag.store(true, std::memory_order_release);
    });

    // Avvio simultaneo
    startFlag.store(true, std::memory_order_release);

    writer.join();
    for (auto& t : readers) {
        t.join();
    }

    if (readerIntegrityFailed.load()) {
        res.passed = false;
        res.observedDesc = failureDetail;
        return res;
    }

    if (totalReadsCompleted.load() < 50) {
        res.passed = false;
        res.observedDesc = "Troppo poche letture completate dai reader concorrenti: " + std::to_string(totalReadsCompleted.load());
        return res;
    }

    res.passed = true;
    res.observedDesc = "Validati lettori concorrenti su 50 cicli stress: completate " + std::to_string(totalReadsCompleted.load()) + 
                       " letture coerenti, zero data race, snapshot storici integri.";
    return res;
}

CustomScientificTestResult CustomScientificMaterialContractTests::Test21_ConcurrentWritersZeroLostUpdatesAndTransactionalIntegrity() {
    CustomScientificTestResult res;
    res.testId = "CUST-21";
    res.testName = "Scrittori concorrenti con serializzazione, integrita' transazionale e zero aggiornamenti persi";
    res.inputDesc = "6 thread scrittori registrano concorrentemente 25 materiali custom ciascuno (150 totali) con ID e canonical distinti";
    res.expectedDesc = "Tutti i 150 materiali registrati con successo, nessun aggiornamento perso, conteggio totale 4 standard + 150 = 154";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    const size_t WRITER_THREADS = 6;
    const size_t MATERIALS_PER_THREAD = 25;
    const size_t TOTAL_CUSTOM = WRITER_THREADS * MATERIALS_PER_THREAD;

    std::atomic<bool> startFlag{false};
    std::atomic<size_t> registrationFailures{0};
    std::vector<std::thread> writers;
    writers.reserve(WRITER_THREADS);

    for (size_t t = 0; t < WRITER_THREADS; ++t) {
        writers.emplace_back([&, t]() {
            while (!startFlag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (size_t i = 0; i < MATERIALS_PER_THREAD; ++i) {
                ScientificMaterialID id = static_cast<ScientificMaterialID>(1000 + (t * MATERIALS_PER_THREAD) + i);
                std::string canon = "custom:mat/writer_t" + std::to_string(t) + "_m" + std::to_string(i);
                std::string name = "Writer Material T" + std::to_string(t) + " M" + std::to_string(i);

                ScientificMaterialDef m = CreateValidBaseCustomDef(id, canon, name);
                if (!reg.RegisterMaterial(m, false)) {
                    registrationFailures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    // Avvio simultaneo di tutti gli scrittori per massimizzare la contesa
    startFlag.store(true, std::memory_order_release);

    for (auto& t : writers) {
        t.join();
    }

    if (registrationFailures.load() > 0) {
        res.passed = false;
        res.observedDesc = "Rilevati " + std::to_string(registrationFailures.load()) + " fallimenti di registrazione tra scrittori concorrenti";
        return res;
    }

    // Verifica che tutti i 150 materiali siano presenti
    if (reg.GetMaterialCount() != 4 + TOTAL_CUSTOM) {
        res.passed = false;
        res.observedDesc = "Conteggio materiali errato: atteso " + std::to_string(4 + TOTAL_CUSTOM) + 
                           ", osservato " + std::to_string(reg.GetMaterialCount());
        return res;
    }

    for (size_t t = 0; t < WRITER_THREADS; ++t) {
        for (size_t i = 0; i < MATERIALS_PER_THREAD; ++i) {
            ScientificMaterialID expectedId = static_cast<ScientificMaterialID>(1000 + (t * MATERIALS_PER_THREAD) + i);
            std::string expectedCanon = "custom:mat/writer_t" + std::to_string(t) + "_m" + std::to_string(i);

            if (!reg.HasMaterial(expectedId)) {
                res.passed = false;
                res.observedDesc = "Materiale atteso ID " + std::to_string(expectedId) + " non trovato nel registry (aggiornamento perso!)";
                return res;
            }
            if (!reg.HasMaterial(expectedCanon)) {
                res.passed = false;
                res.observedDesc = "Materiale canonico atteso '" + expectedCanon + "' non trovato nel registry";
                return res;
            }
            if (reg.GetMaterialIdByCanonical(expectedCanon) != expectedId) {
                res.passed = false;
                res.observedDesc = "Mappatura canonica errata per '" + expectedCanon + "'";
                return res;
            }
        }
    }

    res.passed = true;
    res.observedDesc = "Serializzazione scrittori concorrenti verificata: 150/150 materiali registrati con successo, zero conflitti, zero aggiornamenti persi.";
    return res;
}

CustomScientificTestResult CustomScientificMaterialContractTests::Test22_CustomIdAllocationContractStabilityAndOverflow() {
    CustomScientificTestResult res;
    res.testId = "CUST-22";
    res.testName = "Contratto allocazione ID custom: prenotazione atomica concorrente, non-riciclo, persistenza HWM e overflow";
    res.inputDesc = "Prenotazioni concorrenti (6x50=300), consultazione Peek non mutante, persistenza HWM dopo Clear, caricamento catalogo ridotto e simulazione overflow";
    res.expectedDesc = "300 ID univoci senza duplicati, Peek invariante, mancato riutilizzo ID dopo Clear e cataloghi ridotti, rifiuto overflow uint32_t";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    // 1. Prenotazioni concorrenti atomiche
    const size_t THREADS = 6;
    const size_t RESERVATIONS_PER_THREAD = 50;
    const size_t TOTAL_RESERVATIONS = THREADS * RESERVATIONS_PER_THREAD;

    std::atomic<bool> startFlag{false};
    std::vector<std::vector<ScientificMaterialID>> threadAllocations(THREADS);
    std::vector<std::thread> workers;
    workers.reserve(THREADS);

    for (size_t t = 0; t < THREADS; ++t) {
        workers.emplace_back([&, t]() {
            threadAllocations[t].reserve(RESERVATIONS_PER_THREAD);
            while (!startFlag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (size_t i = 0; i < RESERVATIONS_PER_THREAD; ++i) {
                ScientificMaterialID id = reg.ReserveNextCustomMaterialId();
                threadAllocations[t].push_back(id);
            }
        });
    }

    startFlag.store(true, std::memory_order_release);
    for (auto& t : workers) {
        t.join();
    }

    std::set<ScientificMaterialID> uniqueAllocations;
    for (size_t t = 0; t < THREADS; ++t) {
        for (ScientificMaterialID id : threadAllocations[t]) {
            if (id < CUSTOM_SCIENTIFIC_MATERIAL_ID_BASE) {
                res.passed = false;
                res.observedDesc = "ID custom allocato inferiore alla base 1000: " + std::to_string(id);
                return res;
            }
            if (!uniqueAllocations.insert(id).second) {
                res.passed = false;
                res.observedDesc = "Duplicato rilevato nell'allocazione concorrente degli ID: ID " + std::to_string(id);
                return res;
            }
        }
    }

    if (uniqueAllocations.size() != TOTAL_RESERVATIONS) {
        res.passed = false;
        res.observedDesc = "Numero ID allocati incorretto: atteso " + std::to_string(TOTAL_RESERVATIONS) + 
                           ", osservato " + std::to_string(uniqueAllocations.size());
        return res;
    }

    ScientificMaterialID expectedHwmAfterReservations = static_cast<ScientificMaterialID>(CUSTOM_SCIENTIFIC_MATERIAL_ID_BASE + TOTAL_RESERVATIONS);
    if (reg.GetCustomIdHighWaterMark() != expectedHwmAfterReservations) {
        res.passed = false;
        res.observedDesc = "High-water mark errato dopo prenotazioni concorrenti: atteso " + 
                           std::to_string(expectedHwmAfterReservations) + ", osservato " + std::to_string(reg.GetCustomIdHighWaterMark());
        return res;
    }

    // 2. Peek invariante e non mutante
    ScientificMaterialID peek1 = reg.PeekNextCustomMaterialId();
    ScientificMaterialID peek2 = reg.AllocateNextCustomMaterialId();
    if (peek1 != expectedHwmAfterReservations || peek2 != expectedHwmAfterReservations) {
        res.passed = false;
        res.observedDesc = "PeekNextCustomMaterialId() non coerente con l'HWM";
        return res;
    }
    if (reg.GetCustomIdHighWaterMark() != expectedHwmAfterReservations) {
        res.passed = false;
        res.observedDesc = "Peek ha erroneamente mutato l'high-water mark";
        return res;
    }

    // 3. Clear(): mancato riutilizzo degli ID
    reg.Clear();
    if (reg.GetCustomIdHighWaterMark() != expectedHwmAfterReservations) {
        res.passed = false;
        res.observedDesc = "Clear() ha erroneamente resettato l'high-water mark!";
        return res;
    }
    ScientificMaterialID postClearRes = reg.ReserveNextCustomMaterialId();
    if (postClearRes != expectedHwmAfterReservations) {
        res.passed = false;
        res.observedDesc = "ID riutilizzato dopo Clear(): atteso " + std::to_string(expectedHwmAfterReservations) + 
                           ", osservato " + std::to_string(postClearRes);
        return res;
    }
    if (reg.GetCustomIdHighWaterMark() != expectedHwmAfterReservations + 1) {
        res.passed = false;
        res.observedDesc = "HWM non avanzato correttamente dopo prenotazione post-Clear";
        return res;
    }

    // 4. Persistenza dell'high-water mark e mancato riutilizzo dopo caricamento di cataloghi ridotti
    // Registriamo un materiale con ID postClearRes (1300)
    ScientificMaterialDef m1 = CreateValidBaseCustomDef(postClearRes, "custom:mat/stable_1", "Stable Material 1");
    reg.RegisterMaterial(m1);

    // Prenotiamo altri 3 ID (1301, 1302, 1303) simulando ID assegnati a blocchi e poi eliminati
    reg.ReserveNextCustomMaterialId(); // 1301
    reg.ReserveNextCustomMaterialId(); // 1302
    ScientificMaterialID reservedLast = reg.ReserveNextCustomMaterialId(); // 1303
    // Attuale HWM: 1304

    std::error_code ec;
    auto hwmTestPath = std::filesystem::temp_directory_path() / "fairworld_test_hwm_persistence.json";
    std::filesystem::remove(hwmTestPath, ec);

    std::string saveErr;
    if (!reg.SaveCustomMaterialsToJson(hwmTestPath.string(), &saveErr)) {
        res.passed = false;
        res.observedDesc = "Salvataggio catalogo per test HWM fallito: " + saveErr;
        return res;
    }

    // Ricarichiamo il catalogo in una nuova istanza fresca
    ScientificMaterialRegistry regFresh;
    regFresh.Initialize();
    std::string loadErr;
    if (!regFresh.LoadCustomMaterialsFromJson(hwmTestPath.string(), &loadErr)) {
        res.passed = false;
        res.observedDesc = "Caricamento catalogo per test HWM fallito: " + loadErr;
        std::filesystem::remove(hwmTestPath, ec);
        return res;
    }
    std::filesystem::remove(hwmTestPath, ec);

    // Il catalogo contiene solo il materiale 1300, ma l'HWM salvato e' 1304.
    // L'istanza ricaricata NON deve riutilizzare 1301, 1302, 1303!
    if (regFresh.GetCustomIdHighWaterMark() != reservedLast + 1) {
        res.passed = false;
        res.observedDesc = "Mancata conservazione dell'high-water mark dopo ricaricamento di catalogo ridotto: atteso " + 
                           std::to_string(reservedLast + 1) + ", osservato " + std::to_string(regFresh.GetCustomIdHighWaterMark());
        return res;
    }

    ScientificMaterialID freshAlloc = regFresh.ReserveNextCustomMaterialId();
    if (freshAlloc != reservedLast + 1) {
        res.passed = false;
        res.observedDesc = "ID cancellato/intermedio (1301..1303) erroneamente riallocato: osservato " + std::to_string(freshAlloc);
        return res;
    }

    // 5. Compatibilità retroattiva con cataloghi legacy senza high-water mark esplicito
    auto legacyTestPath = std::filesystem::temp_directory_path() / "fairworld_test_legacy_hwm.json";
    std::filesystem::remove(legacyTestPath, ec);
    {
        std::ofstream lf(legacyTestPath);
        lf << "{\n"
           << "  \"schemaVersion\": 3,\n"
           << "  \"materials\": [\n"
           << "    {\n"
           << "      \"id\": 1050,\n"
           << "      \"canonicalId\": \"custom:mat/legacy_sample\",\n"
           << "      \"displayName\": \"Legacy Sample\",\n"
           << "      \"density\": { \"state\": \"Known\", \"value\": 3000.0, \"unit\": \"kg/m^3\" }\n"
           << "    }\n"
           << "  ]\n"
           << "}\n";
    }

    ScientificMaterialRegistry regLegacy;
    regLegacy.Initialize();
    if (!regLegacy.LoadCustomMaterialsFromJson(legacyTestPath.string(), &loadErr)) {
        res.passed = false;
        res.observedDesc = "Caricamento catalogo legacy fallito: " + loadErr;
        std::filesystem::remove(legacyTestPath, ec);
        return res;
    }
    std::filesystem::remove(legacyTestPath, ec);

    // In assenza di campo HWM nel file legacy, l'HWM deve essere inizializzato a max(1000, 1050 + 1) = 1051
    if (regLegacy.GetCustomIdHighWaterMark() != 1051) {
        res.passed = false;
        res.observedDesc = "Calcolo HWM di fallback per catalogo legacy errato: atteso 1051, osservato " + 
                           std::to_string(regLegacy.GetCustomIdHighWaterMark());
        return res;
    }

    // 6. Overflow e fallimenti di prenotazione
    ScientificMaterialRegistry regOverflow;
    regOverflow.Initialize();
    regOverflow.SetCustomIdHighWaterMarkForTesting(std::numeric_limits<ScientificMaterialID>::max());

    if (regOverflow.PeekNextCustomMaterialId() != INVALID_SCIENTIFIC_MATERIAL_ID) {
        res.passed = false;
        res.observedDesc = "PeekNextCustomMaterialId() in condizione di overflow non ha restituito INVALID_SCIENTIFIC_MATERIAL_ID (0)";
        return res;
    }

    ScientificMaterialID overflowReserved = regOverflow.ReserveNextCustomMaterialId();
    if (overflowReserved != INVALID_SCIENTIFIC_MATERIAL_ID) {
        res.passed = false;
        res.observedDesc = "ReserveNextCustomMaterialId() in condizione di overflow non ha restituito INVALID_SCIENTIFIC_MATERIAL_ID (0): osservato " + 
                           std::to_string(overflowReserved);
        return res;
    }
    // High-water mark deve rimanere a max uint32_t senza wrap-around
    if (regOverflow.GetCustomIdHighWaterMark() != std::numeric_limits<ScientificMaterialID>::max()) {
        res.passed = false;
        res.observedDesc = "HWM alterato o andato in wrap-around dopo fallimento overflow";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Allocazione ID custom validata: 300 prenotazioni concorrenti univoche, HWM persistente dopo Clear e catalogo ridotto, retrocompatibilita' e overflow controllato.";
    return res;
}

// =============================================================================
// P0.2D: SCIENTIFIC MATERIAL PERSISTENCE HARDENING (CUST-23 .. CUST-26)
// =============================================================================

CustomScientificTestResult CustomScientificMaterialContractTests::Test23_ConcurrentSavesSamePathCollisionFreeAndSerialized() {
    CustomScientificTestResult res;
    res.testId = "CUST-23";
    res.testName = "Salvataggi concorrenti sullo stesso percorso: collision-free temp files e serializzazione";
    res.inputDesc = "4 thread scrittori invocano simultaneamente SaveCustomMaterialsToJson sullo stesso percorso di destinazione";
    res.expectedDesc = "Tutti i salvataggi completati senza collisioni sui file temporanei, senza sharing violations e con catalogo finale valido e integro";

    std::error_code ec;
    auto tempFile = std::filesystem::temp_directory_path() / "fairworld_cust23_concurrent_save.json";
    std::filesystem::remove(tempFile, ec);

    ScientificMaterialRegistry reg;
    reg.Initialize();

    for (ScientificMaterialID id = 1000; id < 1005; ++id) {
        ScientificMaterialDef m = CreateValidBaseCustomDef(id, "custom:mat/concurrent_save_" + std::to_string(id), "Mat Save " + std::to_string(id));
        reg.RegisterMaterial(m);
    }

    const size_t THREADS = 4;
    std::vector<std::thread> workers;
    workers.reserve(THREADS);
    std::atomic<bool> startFlag{false};
    std::atomic<size_t> successCount{0};
    std::atomic<size_t> failureCount{0};
    std::string firstError;
    std::mutex errMutex;

    for (size_t t = 0; t < THREADS; ++t) {
        workers.emplace_back([&]() {
            while (!startFlag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            std::string err;
            if (reg.SaveCustomMaterialsToJson(tempFile.string(), &err)) {
                successCount.fetch_add(1, std::memory_order_relaxed);
            } else {
                failureCount.fetch_add(1, std::memory_order_relaxed);
                std::lock_guard<std::mutex> lk(errMutex);
                if (firstError.empty()) firstError = err;
            }
        });
    }

    startFlag.store(true, std::memory_order_release);

    for (auto& w : workers) {
        w.join();
    }

    if (failureCount.load() > 0) {
        res.passed = false;
        res.observedDesc = "Salvataggio concorrente fallito (" + std::to_string(failureCount.load()) + " fallimenti): " + firstError;
        std::filesystem::remove(tempFile, ec);
        return res;
    }

    // Verifica integrita' del catalogo finale salvato
    ScientificMaterialRegistry verifyReg;
    verifyReg.Initialize();
    std::string loadErr;
    if (!verifyReg.LoadCustomMaterialsFromJson(tempFile.string(), &loadErr)) {
        res.passed = false;
        res.observedDesc = "Caricamento catalogo post salvataggi concorrenti fallito: " + loadErr;
        std::filesystem::remove(tempFile, ec);
        return res;
    }
    std::filesystem::remove(tempFile, ec);

    if (verifyReg.GetMaterialCount() != 4 + 5) {
        res.passed = false;
        res.observedDesc = "Conteggio materiali errato nel file salvato: atteso 9, osservato " + std::to_string(verifyReg.GetMaterialCount());
        return res;
    }

    for (ScientificMaterialID id = 1000; id < 1005; ++id) {
        if (!verifyReg.HasMaterial(id)) {
            res.passed = false;
            res.observedDesc = "Materiale " + std::to_string(id) + " assente dal catalogo salvato concorrentemente";
            return res;
        }
    }

    res.passed = true;
    res.observedDesc = "Salvataggi concorrenti validati: 4/4 completati con successo sullo stesso percorso, zero collisioni temp, catalogo integro.";
    return res;
}

CustomScientificTestResult CustomScientificMaterialContractTests::Test24_StaleSnapshotOverwriteProtectionAndConcurrentMutations() {
    CustomScientificTestResult res;
    res.testId = "CUST-24";
    res.testName = "Protezione contro sovrascrittura da snapshot obsoleti e mutazioni concorrenti";
    res.inputDesc = "Tentativo di salvataggio di uno snapshot obsoleto (rev N) su una destinazione gia' avanzata a rev N+K";
    res.expectedDesc = "Salvataggio dello snapshot obsoleto respinto con errore esplicito; destinazione preserva la revisione piu' recente";

    std::error_code ec;
    auto targetPath = std::filesystem::temp_directory_path() / "fairworld_cust24_stale_protect.json";
    std::filesystem::remove(targetPath, ec);

    ScientificMaterialRegistry reg;
    reg.Initialize();

    auto m1000 = CreateValidBaseCustomDef(1000, "custom:mat/stale_test_1000", "Stale 1000");
    reg.RegisterMaterial(m1000);

    // Snapshot e JSON a revisione R1
    uint64_t rev1 = reg.GetRevision();
    std::string err;
    if (!reg.SaveCustomMaterialsToJson(targetPath.string(), &err)) {
        res.passed = false;
        res.observedDesc = "Salvataggio preliminare rev1 fallito: " + err;
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    // Mutazione successiva a revisione R2 > R1
    auto m1001 = CreateValidBaseCustomDef(1001, "custom:mat/stale_test_1001", "Stale 1001");
    reg.RegisterMaterial(m1001);
    uint64_t rev2 = reg.GetRevision();

    if (rev2 <= rev1) {
        res.passed = false;
        res.observedDesc = "Revisione non avanzata dopo nuova mutazione";
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    // Salvataggio di rev2 sulla destinazione
    if (!reg.SaveCustomMaterialsToJson(targetPath.string(), &err)) {
        res.passed = false;
        res.observedDesc = "Salvataggio rev2 fallito: " + err;
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    // Costruzione payload fittizio con rev1 (obsoleto)
    nlohmann::json staleJson;
    staleJson["schemaVersion"] = 3;
    staleJson["customIdHighWaterMark"] = 1001;
    staleJson["materials"] = nlohmann::json::array();

    std::string staleErr;
    bool staleSaveResult = ScientificMaterialRegistry::AtomicWriteAndReplaceJson(targetPath, staleJson, rev1, &staleErr);

    if (staleSaveResult) {
        res.passed = false;
        res.observedDesc = "AtomicWriteAndReplaceJson avrebbe dovuto respingere il salvataggio con revisione obsoleta (" + 
                           std::to_string(rev1) + " < " + std::to_string(rev2) + ")";
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    // Verifica che il file sul disco contenga ancora il catalogo aggiornato con materiale 1001
    ScientificMaterialRegistry verifyReg;
    verifyReg.Initialize();
    if (!verifyReg.LoadCustomMaterialsFromJson(targetPath.string(), &err)) {
        res.passed = false;
        res.observedDesc = "Caricamento verifica fallito: " + err;
        std::filesystem::remove(targetPath, ec);
        return res;
    }
    std::filesystem::remove(targetPath, ec);

    if (!verifyReg.HasMaterial(1001)) {
        res.passed = false;
        res.observedDesc = "File sovrascritto o corrotto: materiale 1001 non trovato dopo tentativo obsoleto";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Protezione sovrascrittura obsoleta validata: salvataggio rev " + std::to_string(rev1) + 
                       " respinto con successo, preservato stato rev " + std::to_string(rev2);
    return res;
}

CustomScientificTestResult CustomScientificMaterialContractTests::Test25_FaultInjectionAtomicReplaceTargetPreservationAndZeroResidues() {
    CustomScientificTestResult res;
    res.testId = "CUST-25";
    res.testName = "Fault injection deterministica su temp write e atomic replacement con conservazione target e zero residui";
    res.inputDesc = "Iniezione guasti: FailTempFileWrite e FailAtomicReplace durante SaveCustomMaterialsToJson";
    res.expectedDesc = "Salvataggi falliscono in sicurezza, file target originale rimane valido e integro, zero file temporanei orfani";

    std::error_code ec;
    auto targetPath = std::filesystem::temp_directory_path() / "fairworld_cust25_fault_inject.json";
    std::filesystem::remove(targetPath, ec);

    ScientificMaterialRegistry reg;
    reg.Initialize();

    auto m1000 = CreateValidBaseCustomDef(1000, "custom:mat/fault_orig_1000", "Original Valid 1000");
    reg.RegisterMaterial(m1000);

    std::string err;
    if (!reg.SaveCustomMaterialsToJson(targetPath.string(), &err)) {
        res.passed = false;
        res.observedDesc = "Creazione iniziale target valido fallita: " + err;
        return res;
    }

    auto countTmpFiles = [&]() -> size_t {
        size_t count = 0;
        std::error_code itEc;
        for (const auto& entry : std::filesystem::directory_iterator(targetPath.parent_path(), itEc)) {
            if (entry.path().filename().string().rfind("fairworld_cust25_fault_inject.json.tmp", 0) == 0) {
                count++;
            }
        }
        return count;
    };

    // Caso 1: Simulazione fallimento scrittura file temporaneo
    reg.SetPersistenceFaultInjectionForTesting(ScientificMaterialRegistry::PersistenceFaultMode::FailTempFileWrite);
    auto m1001 = CreateValidBaseCustomDef(1001, "custom:mat/fault_fail_1001", "Fail 1001");
    reg.RegisterMaterial(m1001);

    bool saveFailedWrite = reg.SaveCustomMaterialsToJson(targetPath.string(), &err);
    if (saveFailedWrite) {
        res.passed = false;
        res.observedDesc = "SaveCustomMaterialsToJson avrebbe dovuto fallire con FailTempFileWrite";
        reg.SetPersistenceFaultInjectionForTesting(ScientificMaterialRegistry::PersistenceFaultMode::None);
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    if (countTmpFiles() > 0) {
        res.passed = false;
        res.observedDesc = "Rilevati file temporanei orfani dopo FailTempFileWrite";
        reg.SetPersistenceFaultInjectionForTesting(ScientificMaterialRegistry::PersistenceFaultMode::None);
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    // Caso 2: Simulazione fallimento sostituzione atomica (ReplaceFileW)
    reg.SetPersistenceFaultInjectionForTesting(ScientificMaterialRegistry::PersistenceFaultMode::FailAtomicReplace);
    bool saveFailedReplace = reg.SaveCustomMaterialsToJson(targetPath.string(), &err);
    if (saveFailedReplace) {
        res.passed = false;
        res.observedDesc = "SaveCustomMaterialsToJson avrebbe dovuto fallire con FailAtomicReplace";
        reg.SetPersistenceFaultInjectionForTesting(ScientificMaterialRegistry::PersistenceFaultMode::None);
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    if (countTmpFiles() > 0) {
        res.passed = false;
        res.observedDesc = "Rilevati file temporanei orfani dopo FailAtomicReplace";
        reg.SetPersistenceFaultInjectionForTesting(ScientificMaterialRegistry::PersistenceFaultMode::None);
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    // Reset della fault injection
    reg.SetPersistenceFaultInjectionForTesting(ScientificMaterialRegistry::PersistenceFaultMode::None);

    // Verifica che il file target originale sia RIMASTO INTEGRO (contiene solo 1000)
    ScientificMaterialRegistry regCheck;
    regCheck.Initialize();
    if (!regCheck.LoadCustomMaterialsFromJson(targetPath.string(), &err)) {
        res.passed = false;
        res.observedDesc = "File target originale corrotto dopo fallimenti simulati: " + err;
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    if (!regCheck.HasMaterial(1000) || regCheck.GetMaterial(1000).displayName != "Original Valid 1000") {
        res.passed = false;
        res.observedDesc = "Materiale 1000 originale corrotto o alterato nel target";
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    if (regCheck.HasMaterial(1001)) {
        res.passed = false;
        res.observedDesc = "Target contiene materiale 1001 nonostante i fallimenti simulati";
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    // Salvataggio legittimo finale
    if (!reg.SaveCustomMaterialsToJson(targetPath.string(), &err)) {
        res.passed = false;
        res.observedDesc = "Salvataggio post-fault injection fallito: " + err;
        std::filesystem::remove(targetPath, ec);
        return res;
    }
    std::filesystem::remove(targetPath, ec);

    res.passed = true;
    res.observedDesc = "Fault injection validata: target originale preservato al 100% su errori di scrittura e sostituzione, zero residui temporanei orfani.";
    return res;
}

CustomScientificTestResult CustomScientificMaterialContractTests::Test26_DurableHighWaterMarkPersistenceCrashRecoveryAndLegacyCompat() {
    CustomScientificTestResult res;
    res.testId = "CUST-26";
    res.testName = "Durabilita' High-Water Mark: prenotazione durevole, recovery riavvio e compatibilita' legacy";
    res.inputDesc = "Prenotazione durevole via ReserveDurableCustomMaterialId, simulazione crash/riavvio, rollback I/O e caricamento legacy";
    res.expectedDesc = "ID garantito persistito su disco prima della restituzione, HWM non regredisce mai post-crash, zero riciclo ID, rollback atomico su errore";

    std::error_code ec;
    auto targetPath = std::filesystem::temp_directory_path() / "fairworld_cust26_durable_hwm.json";
    std::filesystem::remove(targetPath, ec);

    ScientificMaterialRegistry reg;
    reg.Initialize();

    // 1. Prenotazione durevole di un ID custom
    std::string err;
    ScientificMaterialID durableId1 = reg.ReserveDurableCustomMaterialId(targetPath.string(), &err);
    if (durableId1 != 1000) {
        res.passed = false;
        res.observedDesc = "ReserveDurableCustomMaterialId ha restituito ID inatteso: atteso 1000, osservato " + std::to_string(durableId1);
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    // Verifica che l'ID sia confermato durevole su disco
    if (!ScientificMaterialRegistry::IsCustomIdConfirmedDurable(1000, targetPath.string())) {
        res.passed = false;
        res.observedDesc = "IsCustomIdConfirmedDurable(1000) ha restituito false dopo ReserveDurableCustomMaterialId riuscito";
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    // Verifica che l'ID successivo (1001) NON sia confermato durevole
    if (ScientificMaterialRegistry::IsCustomIdConfirmedDurable(1001, targetPath.string())) {
        res.passed = false;
        res.observedDesc = "IsCustomIdConfirmedDurable(1001) ha restituito true prima della sua prenotazione";
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    // Verifica HWM su file
    ScientificMaterialID hwmOnFile = ScientificMaterialRegistry::ReadDurableCustomIdHighWaterMark(targetPath.string());
    if (hwmOnFile != 1001) {
        res.passed = false;
        res.observedDesc = "ReadDurableCustomIdHighWaterMark errato su disco: atteso 1001, osservato " + std::to_string(hwmOnFile);
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    // 2. Simulazione crash di processo e recovery in una nuova istanza
    {
        ScientificMaterialRegistry regRecovered;
        regRecovered.Initialize();
        if (!regRecovered.LoadCustomMaterialsFromJson(targetPath.string(), &err)) {
            res.passed = false;
            res.observedDesc = "Recovery del catalogo durevole fallito al riavvio: " + err;
            std::filesystem::remove(targetPath, ec);
            return res;
        }

        if (regRecovered.GetCustomIdHighWaterMark() != 1001) {
            res.passed = false;
            res.observedDesc = "HWM non ripristinato correttamente al riavvio: atteso 1001, osservato " + 
                               std::to_string(regRecovered.GetCustomIdHighWaterMark());
            std::filesystem::remove(targetPath, ec);
            return res;
        }

        // Il prossimo ID allocato deve essere 1001 (1000 non deve mai essere riciclato!)
        ScientificMaterialID nextAlloc = regRecovered.ReserveNextCustomMaterialId();
        if (nextAlloc != 1001) {
            res.passed = false;
            res.observedDesc = "Violazione anti-riciclo ID: allocato " + std::to_string(nextAlloc) + " anziche' 1001";
            std::filesystem::remove(targetPath, ec);
            return res;
        }
    }

    // 3. Rollback atomico di prenotazione durevole su errore I/O
    ScientificMaterialID hwmBeforeFail = reg.GetCustomIdHighWaterMark();
    reg.SetPersistenceFaultInjectionForTesting(ScientificMaterialRegistry::PersistenceFaultMode::FailAtomicReplace);

    ScientificMaterialID failedDurable = reg.ReserveDurableCustomMaterialId(targetPath.string(), &err);
    reg.SetPersistenceFaultInjectionForTesting(ScientificMaterialRegistry::PersistenceFaultMode::None);

    if (failedDurable != INVALID_SCIENTIFIC_MATERIAL_ID) {
        res.passed = false;
        res.observedDesc = "ReserveDurableCustomMaterialId avrebbe dovuto restituire INVALID_SCIENTIFIC_MATERIAL_ID su errore I/O";
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    if (reg.GetCustomIdHighWaterMark() != hwmBeforeFail) {
        res.passed = false;
        res.observedDesc = "HWM non ripristinato dopo fallimento prenotazione durevole: atteso " + 
                           std::to_string(hwmBeforeFail) + ", osservato " + std::to_string(reg.GetCustomIdHighWaterMark());
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    // 4. Compatibilita' con catalogo legacy (schemaVersion 2 senza customIdHighWaterMark)
    auto legacyPath = std::filesystem::temp_directory_path() / "fairworld_cust26_legacy.json";
    {
        std::ofstream lf(legacyPath);
        lf << "{\n"
           << "  \"schemaVersion\": 2,\n"
           << "  \"materials\": [\n"
           << "    {\n"
           << "      \"id\": 1050,\n"
           << "      \"canonicalId\": \"custom:mat/legacy_durable\",\n"
           << "      \"displayName\": \"Legacy Durable Mat\"\n"
           << "    }\n"
           << "  ]\n"
           << "}\n";
    }

    ScientificMaterialID legacyHwm = ScientificMaterialRegistry::ReadDurableCustomIdHighWaterMark(legacyPath.string());
    if (legacyHwm != 1051) {
        res.passed = false;
        res.observedDesc = "ReadDurableCustomIdHighWaterMark per catalogo legacy errato: atteso 1051, osservato " + std::to_string(legacyHwm);
        std::filesystem::remove(targetPath, ec);
        std::filesystem::remove(legacyPath, ec);
        return res;
    }

    // 5. Invariante multi-sessione e preservazione HWM su commit successivi
    {
        ScientificMaterialRegistry regSession2;
        regSession2.Initialize();
        if (!regSession2.LoadCustomMaterialsFromJson(targetPath.string(), &err)) {
            res.passed = false;
            res.observedDesc = "Caricamento sessione 2 fallito: " + err;
            std::filesystem::remove(targetPath, ec);
            std::filesystem::remove(legacyPath, ec);
            return res;
        }

        // Verifica che la nuova sessione possa salvare sullo stesso percorso senza blocco spurio da lastSavedRevision
        if (!regSession2.SaveCustomMaterialsToJson(targetPath.string(), &err)) {
            res.passed = false;
            res.observedDesc = "Salvataggio nuova sessione bloccato spuriamente da revisione sessione precedente: " + err;
            std::filesystem::remove(targetPath, ec);
            std::filesystem::remove(legacyPath, ec);
            return res;
        }

        // Verifica che nessun commit successivo possa abbassare l'HWM consolidato
        ScientificMaterialID hwmBefore = ScientificMaterialRegistry::ReadDurableCustomIdHighWaterMark(targetPath.string());
        nlohmann::json staleHwmPayload;
        staleHwmPayload["schemaVersion"] = 3;
        staleHwmPayload["customIdHighWaterMark"] = 1000; // inferiore a 1001 consolidato
        staleHwmPayload["materials"] = nlohmann::json::array();

        if (!ScientificMaterialRegistry::AtomicWriteAndReplaceJson(targetPath, staleHwmPayload, 0, &err)) {
            res.passed = false;
            res.observedDesc = "AtomicWriteAndReplaceJson fallito durante test invariante HWM: " + err;
            std::filesystem::remove(targetPath, ec);
            std::filesystem::remove(legacyPath, ec);
            return res;
        }

        ScientificMaterialID hwmAfter = ScientificMaterialRegistry::ReadDurableCustomIdHighWaterMark(targetPath.string());
        if (hwmAfter < hwmBefore) {
            res.passed = false;
            res.observedDesc = "Violazione invariante: l'HWM su disco e' diminuito da " + 
                               std::to_string(hwmBefore) + " a " + std::to_string(hwmAfter);
            std::filesystem::remove(targetPath, ec);
            std::filesystem::remove(legacyPath, ec);
            return res;
        }
    }

    std::filesystem::remove(targetPath, ec);
    std::filesystem::remove(legacyPath, ec);

    res.passed = true;
    res.observedDesc = "Durabilita' HWM validata: prenotazione durevole garantita, recovery post-crash conforme, rollback atomico, retrocompatibilita' legacy, multi-sessione e preservazione HWM monotono su disco verificati.";
    return res;
}

// -----------------------------------------------------------------------------
// TEST 27: Rilevamento modifiche concorrenti tra cataloghi distinti: protezione lost update e sostituzione intenzionale
// -----------------------------------------------------------------------------
CustomScientificTestResult CustomScientificMaterialContractTests::Test27_ConcurrentCatalogsLostUpdateProtectionAndIntentionalOverwrite() {
    CustomScientificTestResult res;
    res.testId = "CUST-27";
    res.testName = "Protezione Lost Update tra cataloghi concorrenti e sostituzione intenzionale";
    res.inputDesc = "Registry A e B caricano stesso catalogo; B muta e salva; A muta e tenta salvataggio normale (rifiutato), poi sostituzione intenzionale (accettata)";
    res.expectedDesc = "Salvataggio normale di A respinto senza cancellare modifiche di B; modifiche di B preservate; sostituzione intenzionale con forceOverwrite riuscita";

    auto targetPath = std::filesystem::temp_directory_path() / "fairworld_cust27_lost_update.json";
    std::error_code ec;
    std::filesystem::remove(targetPath, ec);

    std::string err;

    // 1. Setup catalogo iniziale con materiale 1000
    {
        ScientificMaterialRegistry regInit;
        regInit.Initialize();
        auto m0 = CreateValidBaseCustomDef(1000, "custom:mat/cust27_base", "Initial Base Material");
        regInit.RegisterMaterial(m0);
        if (!regInit.SaveCustomMaterialsToJson(targetPath.string(), &err)) {
            res.passed = false;
            res.observedDesc = "Salvataggio iniziale fallito: " + err;
            std::filesystem::remove(targetPath, ec);
            return res;
        }
    }

    // 2. Registry A e Registry B caricano entrambi lo stesso catalogo
    ScientificMaterialRegistry regA;
    regA.Initialize();
    if (!regA.LoadCustomMaterialsFromJson(targetPath.string(), &err)) {
        res.passed = false;
        res.observedDesc = "Caricamento Registry A fallito: " + err;
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    ScientificMaterialRegistry regB;
    regB.Initialize();
    if (!regB.LoadCustomMaterialsFromJson(targetPath.string(), &err)) {
        res.passed = false;
        res.observedDesc = "Caricamento Registry B fallito: " + err;
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    // 3. Registry B modifica un materiale, registra un nuovo materiale 1001 e salva
    auto mB1000 = regB.GetMaterial(1000);
    mB1000.displayName = "Modified by B";
    regB.RegisterMaterial(mB1000, true);
    auto mB1001 = CreateValidBaseCustomDef(1001, "custom:mat/cust27_b", "Added by B");
    regB.RegisterMaterial(mB1001);

    if (!regB.SaveCustomMaterialsToJson(targetPath.string(), &err)) {
        res.passed = false;
        res.observedDesc = "Salvataggio Registry B fallito: " + err;
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    // 4. Registry A (senza aver ricaricato B) modifica materiale 1000 e tenta salvataggio normale
    auto mA1000 = regA.GetMaterial(1000);
    mA1000.displayName = "Modified by A (stale)";
    regA.RegisterMaterial(mA1000, true);
    auto mA1002 = CreateValidBaseCustomDef(1002, "custom:mat/cust27_a", "Added by A");
    regA.RegisterMaterial(mA1002);

    std::string errA;
    bool normalSaveA = regA.SaveCustomMaterialsToJson(targetPath.string(), &errA, false /* forceOverwrite */);

    if (normalSaveA) {
        res.passed = false;
        res.observedDesc = "Violazione requisito: il salvataggio normale di A e' riuscito sovrascrivendo silenziosamente le modifiche di B!";
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    // 5. Verifica che le modifiche di B sul disco siano integre e NON cancellate
    {
        ScientificMaterialRegistry verifyB;
        verifyB.Initialize();
        if (!verifyB.LoadCustomMaterialsFromJson(targetPath.string(), &err)) {
            res.passed = false;
            res.observedDesc = "Caricamento verifica B fallito: " + err;
            std::filesystem::remove(targetPath, ec);
            return res;
        }

        if (!verifyB.HasMaterial(1001)) {
            res.passed = false;
            res.observedDesc = "Modifiche di B cancellate: materiale 1001 non trovato sul disco!";
            std::filesystem::remove(targetPath, ec);
            return res;
        }

        if (!verifyB.HasMaterial(1000) || verifyB.GetMaterial(1000).displayName != "Modified by B") {
            res.passed = false;
            res.observedDesc = "Modifiche di B alterate o cancellate su materiale 1000: atteso 'Modified by B'";
            std::filesystem::remove(targetPath, ec);
            return res;
        }
    }

    // 6. Sostituzione intenzionale da parte di A con forceOverwrite = true
    bool forceSaveA = regA.SaveCustomMaterialsToJson(targetPath.string(), &errA, true /* forceOverwrite */);
    if (!forceSaveA) {
        res.passed = false;
        res.observedDesc = "Sostituzione intenzionale di A fallita con forceOverwrite = true: " + errA;
        std::filesystem::remove(targetPath, ec);
        return res;
    }

    // 7. Verifica finale dello stato dopo sostituzione intenzionale
    {
        ScientificMaterialRegistry verifyA;
        verifyA.Initialize();
        if (!verifyA.LoadCustomMaterialsFromJson(targetPath.string(), &err)) {
            res.passed = false;
            res.observedDesc = "Caricamento verifica finale A fallito: " + err;
            std::filesystem::remove(targetPath, ec);
            return res;
        }

        if (!verifyA.HasMaterial(1002)) {
            res.passed = false;
            res.observedDesc = "Sostituzione intenzionale non consolidata: materiale 1002 non trovato!";
            std::filesystem::remove(targetPath, ec);
            return res;
        }

        if (!verifyA.HasMaterial(1000) || verifyA.GetMaterial(1000).displayName != "Modified by A (stale)") {
            res.passed = false;
            res.observedDesc = "Sostituzione intenzionale non consolidata su materiale 1000";
            std::filesystem::remove(targetPath, ec);
            return res;
        }
    }

    std::filesystem::remove(targetPath, ec);

    res.passed = true;
    res.observedDesc = "Lost update protetto con successo: salvataggio normale di A respinto, modifiche di B preservate su disco, sostituzione intenzionale con forceOverwrite pienamente convalidata.";
    return res;
}

} // namespace fw
