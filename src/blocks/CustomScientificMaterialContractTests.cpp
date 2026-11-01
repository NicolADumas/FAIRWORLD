#include "CustomScientificMaterialContractTests.h"
#include "ScientificMaterialRegistry.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <cmath>

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
        Test11_AtomicReplaceFailureAndRecovery()
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

    // 1. Selezione di default (per nuovi blocchi: currentSelection = 0)
    auto selectableDef = reg.GetSelectableMaterials(0);
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
    auto selectableOld = reg.GetSelectableMaterials(1002);
    if (!hasId(selectableOld, 1002)) {
        res.passed = false;
        res.observedDesc = "Asset storico con ID 1002 non include il proprio materiale deprecato nella selezione";
        return res;
    }

    // 3. Asset storico con materiale 1001 (disabilitato in Block Maker)
    auto selectableDisabled = reg.GetSelectableMaterials(1001);
    if (!hasId(selectableDisabled, 1001)) {
        res.passed = false;
        res.observedDesc = "Asset storico con ID 1001 non include il proprio materiale nella selezione";
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

    if (!reg1.SetEnabledInBlockMaker(1001, false, &err)) {
        res.passed = false;
        res.observedDesc = "SetEnabledInBlockMaker(1001, false) fallito: " + err;
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

} // namespace fw
