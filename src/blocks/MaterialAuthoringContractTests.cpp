#include "pch.h"
#include "MaterialAuthoringContractTests.h"
#include "MaterialAuthoringWorkflow.h"
#include "ScientificMaterialRegistry.h"
#include "MaterialAppearanceRegistry.h"
#include "SurfaceDefinition.h"

#include <iostream>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <cmath>

namespace fw {

std::vector<AuthoringTestResult> MaterialAuthoringContractTests::RunAllTests() {
    std::vector<AuthoringTestResult> results;
    results.reserve(11);

    results.push_back(Test01_CustomMaterialCreationAndFieldIntegrity());
    results.push_back(Test02_PhysicalQuantitySIValidationRules());
    results.push_back(Test03_TransactionalDraftModificationAndIsolation());
    results.push_back(Test04_AtomicPersistenceAndCatalogReload());
    results.push_back(Test05_LifecycleStateTransitionPublish());
    results.push_back(Test06_LifecycleStateTransitionDeprecate());
    results.push_back(Test07_BlockMakerAvailabilityIndependentToggle());
    results.push_back(Test08_OptimisticConcurrencyLostUpdateConflictHandling());
    results.push_back(Test09_BuiltInMaterialsProtectionAgainstModificationAndDeprecation());
    results.push_back(Test10_ExistingBindingIntegrityAndVisualOrthogonality());
    results.push_back(Test11_CelsiusFirstUIBoundaryAndKelvinPersistence());

    return results;
}

AuthoringTestResult MaterialAuthoringContractTests::Test01_CustomMaterialCreationAndFieldIntegrity() {
    AuthoringTestResult r;
    r.testId = "AUTH-01";
    r.testName = "Custom Material Creation and Initial Field Integrity";
    r.inputDesc = "Avvio creazione custom con URI 'fairworld:mat/titanium_carbide', tipo Ceramic, commit in ScientificMaterialRegistry.";
    r.expectedDesc = "Sessione valida in stato EditingNew, ID candidato >= 1000, lifecycleState == Draft, isCustom == true, commit riuscito.";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    MaterialAuthoringWorkflow wf(reg);
    std::string err;

    bool beginOk = wf.BeginCreateCustom("fairworld:mat/titanium_carbide", "Titanium Carbide", ScientificKind::Compound, &err);
    if (!beginOk) {
        r.passed = false;
        r.observedDesc = "BeginCreateCustom fallito: " + err;
        return r;
    }

    const auto& wc = wf.GetWorkingCopy();
    bool fieldsOk = (wc.id >= CUSTOM_SCIENTIFIC_MATERIAL_ID_BASE) &&
                    (wc.canonicalId == "fairworld:mat/titanium_carbide") &&
                    (wc.displayName == "Titanium Carbide") &&
                    (wc.kind == ScientificKind::Compound) &&
                    (wc.isCustom == true) &&
                    (wc.lifecycleState == MaterialLifecycleState::Draft) &&
                    (wc.enabledInBlockMaker == true);

    bool setPropOk = wf.SetPhysicalProperty(ScientificPropertyNames::Density, 4930.0, "kg/m^3", DataQuality::PeerReviewed, "Handbook", &err);
    bool commitOk = wf.CommitToRegistry(&err);

    ScientificMaterialID committedId = wf.GetWorkingCopy().id;
    bool inRegistry = reg.HasMaterial(committedId) && reg.HasMaterial("fairworld:mat/titanium_carbide");

    ScientificMaterialDef regMat = reg.GetMaterial(committedId);
    bool propsMatch = (regMat.density.value == 4930.0) && (regMat.lifecycleState == MaterialLifecycleState::Draft);

    r.passed = beginOk && fieldsOk && setPropOk && commitOk && inRegistry && propsMatch;
    r.observedDesc = "Committed ID=" + std::to_string(committedId) + 
                    ", Canonical='" + regMat.canonicalId + "'" +
                    ", Density=" + std::to_string(regMat.density.value) + " kg/m^3" +
                    ", Lifecycle=" + MaterialLifecycleStateToString(regMat.lifecycleState);

    if (!r.passed) {
        r.details = "Divergenza nei campi iniziali del materiale custom o fallimento del commit: " + err;
    }

    return r;
}

AuthoringTestResult MaterialAuthoringContractTests::Test02_PhysicalQuantitySIValidationRules() {
    AuthoringTestResult r;
    r.testId = "AUTH-02";
    r.testName = "Physical Quantity SI Validation and Boundary Rules";
    r.inputDesc = "Test di validazione per valori non fisici (densita' negativa, restituzione > 1, porosita' negativa, punto di fusione negativo).";
    r.expectedDesc = "I valori non fisici vengono respinti da SetPhysicalProperty o ValidateWorkingCopy con diagnostica specifica; valori conformi accettati.";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    MaterialAuthoringWorkflow wf(reg);
    std::string err;
    wf.BeginCreateCustom("fairworld:mat/test_si", "Test SI Validation", ScientificKind::Compound, &err);

    // 1. Densità negativa o zero: respinta
    bool negDensityRejected = !wf.SetPhysicalProperty(ScientificPropertyNames::Density, -500.0, "", DataQuality::Estimated, "", &err);

    // 2. Restituzione > 1.0: respinta
    bool highRestitutionRejected = !wf.SetPhysicalProperty(ScientificPropertyNames::Restitution, 1.25, "", DataQuality::Estimated, "", &err);

    // 3. Porosità < 0.0: respinta
    bool negPorosityRejected = !wf.SetPhysicalProperty(ScientificPropertyNames::Porosity, -0.05, "", DataQuality::Estimated, "", &err);

    // 4. Punto di fusione < 0 K: respinto
    bool negMeltingRejected = !wf.SetPhysicalProperty(ScientificPropertyNames::MeltingPoint, -50.0, "", DataQuality::Estimated, "", &err);

    // 5. Attrito statico negativo: respinto
    bool negFrictionRejected = !wf.SetPhysicalProperty(ScientificPropertyNames::StaticFriction, -0.4, "", DataQuality::Estimated, "", &err);

    // 6. Impostazione valori fisicamente validi
    bool validDensityOk = wf.SetPhysicalProperty(ScientificPropertyNames::Density, 2700.0, "kg/m^3", DataQuality::PeerReviewed, "", &err);
    bool validRestitutionOk = wf.SetPhysicalProperty(ScientificPropertyNames::Restitution, 0.75, "", DataQuality::PeerReviewed, "", &err);
    bool validPorosityOk = wf.SetPhysicalProperty(ScientificPropertyNames::Porosity, 0.02, "", DataQuality::PeerReviewed, "", &err);
    bool validMeltingOk = wf.SetPhysicalProperty(ScientificPropertyNames::MeltingPoint, 933.47, "K", DataQuality::PeerReviewed, "", &err);

    auto valRes = wf.ValidateWorkingCopy();

    r.passed = negDensityRejected && highRestitutionRejected && negPorosityRejected && 
               negMeltingRejected && negFrictionRejected &&
               validDensityOk && validRestitutionOk && validPorosityOk && validMeltingOk &&
               valRes.isValid;

    r.observedDesc = "NegDensityRejected=" + std::to_string(negDensityRejected) +
                    ", HighRestitutionRejected=" + std::to_string(highRestitutionRejected) +
                    ", NegPorosityRejected=" + std::to_string(negPorosityRejected) +
                    ", NegMeltingRejected=" + std::to_string(negMeltingRejected) +
                    ", ValidWorkingCopy=" + (valRes.isValid ? "true" : "false");

    if (!r.passed) {
        r.details = "Mancato rispetto delle regole di validazione SI o accettazione di valori fisicamente impossibili.";
    }

    return r;
}

AuthoringTestResult MaterialAuthoringContractTests::Test03_TransactionalDraftModificationAndIsolation() {
    AuthoringTestResult r;
    r.testId = "AUTH-03";
    r.testName = "Transactional Draft Modification and Snapshot Isolation";
    r.inputDesc = "Modifica della copia di lavoro, verifica isolamento pre-commit, rollback con DiscardSession, commit definitivo con CoW.";
    r.expectedDesc = "Il registry non vede le mutazioni prima del commit. DiscardSession ripristina la baseline. Il commit incrementa revisione e snapshot.";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    MaterialAuthoringWorkflow wf(reg);
    std::string err;
    wf.BeginCreateCustom("fairworld:mat/aerogel", "Silica Aerogel", ScientificKind::Compound, &err);
    wf.SetPhysicalProperty(ScientificPropertyNames::Density, 100.0, "kg/m^3", DataQuality::PeerReviewed, "", &err);
    wf.CommitToRegistry(&err);

    ScientificMaterialID aerogelId = wf.GetWorkingCopy().id;
    uint64_t revInitial = reg.GetRevision();

    // 1. Inizia modifica materiale esistente
    wf.BeginEditExisting(aerogelId, &err);
    wf.SetPhysicalProperty(ScientificPropertyNames::Density, 120.0, "kg/m^3", DataQuality::Experimental, "", &err);

    // Verifica isolamento: il registro deve ancora leggere il valore originale (100.0)
    ScientificMaterialDef uncommittedDef = reg.GetMaterial(aerogelId);
    bool isolationPreCommitOk = (uncommittedDef.density.value == 100.0);

    // 2. Scarta le modifiche (Discard)
    wf.DiscardSession();
    ScientificMaterialDef discardedDef = reg.GetMaterial(aerogelId);
    bool discardOk = (discardedDef.density.value == 100.0) && (reg.GetRevision() == revInitial);

    // 3. Riapri, modifica e committa definitivamente
    wf.BeginEditExisting(aerogelId, &err);
    wf.SetPhysicalProperty(ScientificPropertyNames::Density, 130.0, "kg/m^3", DataQuality::PeerReviewed, "", &err);
    bool commitOk = wf.CommitToRegistry(&err);

    ScientificMaterialDef committedDef = reg.GetMaterial(aerogelId);
    uint64_t revAfterCommit = reg.GetRevision();
    bool commitEffective = (committedDef.density.value == 130.0) && (revAfterCommit > revInitial);

    r.passed = isolationPreCommitOk && discardOk && commitOk && commitEffective;
    r.observedDesc = "PreCommitIsolation=" + std::to_string(isolationPreCommitOk) +
                    ", DiscardOk=" + std::to_string(discardOk) +
                    ", FinalCommittedDensity=" + std::to_string(committedDef.density.value) +
                    ", RevisionProgression: " + std::to_string(revInitial) + " -> " + std::to_string(revAfterCommit);

    if (!r.passed) {
        r.details = "Violazione dell'isolamento transazionale o mancato incremento atomico di revisione.";
    }

    return r;
}

AuthoringTestResult MaterialAuthoringContractTests::Test04_AtomicPersistenceAndCatalogReload() {
    AuthoringTestResult r;
    r.testId = "AUTH-04";
    r.testName = "Atomic Custom Catalog Persistence and Clean Reload";
    r.inputDesc = "Creazione di 2 materiali custom via workflow, salvataggio atomico su file JSON, ricaricamento in registro indipendente.";
    r.expectedDesc = "Salvataggio atomico riuscito senza file residui; il registro indipendente ricarica entrambi i materiali con tutti i campi intatti.";

    auto tempDir = std::filesystem::temp_directory_path() / "fw_auth_test_04";
    std::filesystem::create_directories(tempDir);
    auto catPath = (tempDir / "custom_materials.json").string();

    ScientificMaterialRegistry regSource;
    regSource.Initialize();

    MaterialAuthoringWorkflow wfSource(regSource);
    std::string err;

    // Materiale 1
    wfSource.BeginCreateCustom("fairworld:mat/graphene_foam", "Graphene Foam", ScientificKind::Element, &err);
    wfSource.SetPhysicalProperty(ScientificPropertyNames::Density, 50.0, "kg/m^3", DataQuality::PeerReviewed, "Paper 2026", &err);
    wfSource.CommitToRegistry(&err);

    // Materiale 2
    wfSource.BeginCreateCustom("fairworld:mat/lunar_regolith", "Lunar Regolith Simulant", ScientificKind::Rock, &err);
    wfSource.SetPhysicalProperty(ScientificPropertyNames::Density, 1500.0, "kg/m^3", DataQuality::PeerReviewed, "NASA Compendium", &err);
    wfSource.SetPhysicalProperty(ScientificPropertyNames::MeltingPoint, 1500.0, "K", DataQuality::PeerReviewed, "", &err);
    wfSource.CommitToRegistry(&err);

    bool saveOk = wfSource.SaveCatalogToDisk(catPath, &err);

    // Nuovo registro indipendente
    ScientificMaterialRegistry regTarget;
    regTarget.Initialize();

    bool loadOk = regTarget.LoadCustomMaterialsFromJson(catPath, &err);

    bool mat1Ok = regTarget.HasMaterial("fairworld:mat/graphene_foam");
    bool mat2Ok = regTarget.HasMaterial("fairworld:mat/lunar_regolith");

    bool fieldsMatch = false;
    if (mat1Ok && mat2Ok) {
        auto m1 = regTarget.GetMaterial("fairworld:mat/graphene_foam");
        auto m2 = regTarget.GetMaterial("fairworld:mat/lunar_regolith");
        fieldsMatch = (m1.density.value == 50.0) &&
                      (m2.density.value == 1500.0) &&
                      (m2.meltingPoint.value == 1500.0) &&
                      (m1.isCustom && m2.isCustom);
    }

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);

    r.passed = saveOk && loadOk && mat1Ok && mat2Ok && fieldsMatch;
    r.observedDesc = "SaveOk=" + std::to_string(saveOk) +
                    ", LoadOk=" + std::to_string(loadOk) +
                    ", Mat1Loaded=" + std::to_string(mat1Ok) +
                    ", Mat2Loaded=" + std::to_string(mat2Ok) +
                    ", AllFieldsMatch=" + std::to_string(fieldsMatch);

    if (!r.passed) {
        r.details = "Errore durante il salvataggio atomico o il ricaricamento del catalogo: " + err;
    }

    return r;
}

AuthoringTestResult MaterialAuthoringContractTests::Test05_LifecycleStateTransitionPublish() {
    AuthoringTestResult r;
    r.testId = "AUTH-05";
    r.testName = "Formal Lifecycle State Transition to Published";
    r.inputDesc = "Creazione materiale Draft con proprietà fisiche valide, chiamata PublishActiveMaterial, verifica stato e idempotenza.";
    r.expectedDesc = "Transizione formale Draft -> Published riuscita nel workflow e nel registry, idempotenza su ripetizione, revisione aggiornata.";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    MaterialAuthoringWorkflow wf(reg);
    std::string err;
    wf.BeginCreateCustom("fairworld:mat/carbon_aerogel", "Carbon Aerogel", ScientificKind::Element, &err);
    wf.SetPhysicalProperty(ScientificPropertyNames::Density, 160.0, "kg/m^3", DataQuality::PeerReviewed, "", &err);
    wf.CommitToRegistry(&err);

    ScientificMaterialID id = wf.GetWorkingCopy().id;
    bool initialDraft = (reg.GetMaterial(id).lifecycleState == MaterialLifecycleState::Draft);

    uint64_t revBefore = reg.GetRevision();
    bool pubOk = wf.PublishActiveMaterial(&err);

    ScientificMaterialDef publishedDef = reg.GetMaterial(id);
    bool publishedInReg = (publishedDef.lifecycleState == MaterialLifecycleState::Published);
    bool publishedInWc = (wf.GetWorkingCopy().lifecycleState == MaterialLifecycleState::Published);
    bool revIncremented = (reg.GetRevision() > revBefore);

    // Verifica idempotenza: pubblicare un materiale già pubblicato non genera errori
    bool pubIdempotent = wf.PublishActiveMaterial(&err);

    r.passed = initialDraft && pubOk && publishedInReg && publishedInWc && revIncremented && pubIdempotent;
    r.observedDesc = "InitialDraft=" + std::to_string(initialDraft) +
                    ", PublishOk=" + std::to_string(pubOk) +
                    ", StateInReg=" + MaterialLifecycleStateToString(publishedDef.lifecycleState) +
                    ", IdempotentRepetition=" + std::to_string(pubIdempotent);

    if (!r.passed) {
        r.details = "Transizione a Published fallita o non sincronizzata: " + err;
    }

    return r;
}

AuthoringTestResult MaterialAuthoringContractTests::Test06_LifecycleStateTransitionDeprecate() {
    AuthoringTestResult r;
    r.testId = "AUTH-06";
    r.testName = "Formal Lifecycle Deprecation and Historical Preservation";
    r.inputDesc = "Pubblicazione e successiva deprecazione di un materiale custom, verifica disattivazione automatica e persistenza dell'ID storico.";
    r.expectedDesc = "Transizione a Deprecated riuscita, enabledInBlockMaker commutato a false, ID e proprietà storiche ancora accessibili per compatibilità.";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    MaterialAuthoringWorkflow wf(reg);
    std::string err;
    wf.BeginCreateCustom("fairworld:mat/legacy_composite_v1", "Legacy Composite v1", ScientificKind::Composite, &err);
    wf.SetPhysicalProperty(ScientificPropertyNames::Density, 1800.0, "kg/m^3", DataQuality::PeerReviewed, "", &err);
    wf.CommitToRegistry(&err);
    wf.PublishActiveMaterial(&err);

    ScientificMaterialID id = wf.GetWorkingCopy().id;

    bool depOk = wf.DeprecateActiveMaterial(&err);

    ScientificMaterialDef depDef = reg.GetMaterial(id);
    bool isDeprecated = (depDef.lifecycleState == MaterialLifecycleState::Deprecated);
    bool isBlockMakerDisabled = (depDef.enabledInBlockMaker == false);

    // Verifica conservazione referenziale: il materiale esiste ancora e mantiene tutte le sue proprietà
    bool referentialIntegrity = reg.HasMaterial(id) && (depDef.density.value == 1800.0);

    // Tentativo di ri-pubblicare un materiale deprecato deve essere respinto
    bool republishRejected = !wf.PublishActiveMaterial(&err);

    r.passed = depOk && isDeprecated && isBlockMakerDisabled && referentialIntegrity && republishRejected;
    r.observedDesc = "DeprecateOk=" + std::to_string(depOk) +
                    ", DeprecatedState=" + MaterialLifecycleStateToString(depDef.lifecycleState) +
                    ", EnabledInBM=" + (depDef.enabledInBlockMaker ? "true" : "false") +
                    ", HistoricPreservation=" + (referentialIntegrity ? "true" : "false") +
                    ", RepublishRejected=" + (republishRejected ? "true" : "false");

    if (!r.passed) {
        r.details = "Mancata transizione a Deprecated o violazione della conservazione referenziale storica: " + err;
    }

    return r;
}

AuthoringTestResult MaterialAuthoringContractTests::Test07_BlockMakerAvailabilityIndependentToggle() {
    AuthoringTestResult r;
    r.testId = "AUTH-07";
    r.testName = "Independent Block Maker Availability Attribute Toggle";
    r.inputDesc = "Commutazione del flag enabledInBlockMaker su materiale Published, verifica isolamento dal ciclo di vita e rifiuto su Deprecated.";
    r.expectedDesc = "Il flag puo' essere attivato/disattivato liberamente senza alterare lo stato Published; l'abilitazione e' respinta se Deprecated.";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    MaterialAuthoringWorkflow wf(reg);
    std::string err;
    wf.BeginCreateCustom("fairworld:mat/optical_glass", "Precision Optical Glass", ScientificKind::Mixture, &err);
    wf.CommitToRegistry(&err);
    wf.PublishActiveMaterial(&err);

    ScientificMaterialID id = wf.GetWorkingCopy().id;

    // 1. Disattiva disponibilità in Block Maker
    bool disableOk = wf.SetEnabledInBlockMaker(false, &err);
    ScientificMaterialDef defDisabled = reg.GetMaterial(id);
    bool disabledState = (!defDisabled.enabledInBlockMaker) && (defDisabled.lifecycleState == MaterialLifecycleState::Published);

    // 2. Riattiva disponibilità in Block Maker
    bool enableOk = wf.SetEnabledInBlockMaker(true, &err);
    ScientificMaterialDef defEnabled = reg.GetMaterial(id);
    bool enabledState = (defEnabled.enabledInBlockMaker) && (defEnabled.lifecycleState == MaterialLifecycleState::Published);

    // 3. Depreca il materiale
    wf.DeprecateActiveMaterial(&err);

    // 4. Tentativo di riabilitare in Block Maker un materiale deprecato: DEVE essere respinto
    bool reEnableOnDeprecatedRejected = !wf.SetEnabledInBlockMaker(true, &err);

    r.passed = disableOk && disabledState && enableOk && enabledState && reEnableOnDeprecatedRejected;
    r.observedDesc = "DisableOk=" + std::to_string(disableOk) +
                    ", EnableOk=" + std::to_string(enableOk) +
                    ", RejectedOnDeprecated=" + std::to_string(reEnableOnDeprecatedRejected);

    if (!r.passed) {
        r.details = "Comportamento errato del flag enabledInBlockMaker: " + err;
    }

    return r;
}

AuthoringTestResult MaterialAuthoringContractTests::Test08_OptimisticConcurrencyLostUpdateConflictHandling() {
    AuthoringTestResult r;
    r.testId = "AUTH-08";
    r.testName = "Optimistic Concurrency Lost Update Conflict Detection and Resolution";
    r.inputDesc = "Due registri indipendenti (A e B) caricano lo stesso catalogo. B salva una modifica. A tenta di salvare senza ricaricare.";
    r.expectedDesc = "Il salvataggio ordinario di A fallisce per Lost Update; A puo' ricaricare o forzare con forceOverwrite=true.";

    auto tempDir = std::filesystem::temp_directory_path() / "fw_auth_test_08";
    std::filesystem::create_directories(tempDir);
    auto catPath = (tempDir / "conflict_catalog.json").string();

    std::string err;

    // Creazione catalogo base con 1 materiale
    {
        ScientificMaterialRegistry regBase;
        regBase.Initialize();
        MaterialAuthoringWorkflow wfBase(regBase);
        wfBase.BeginCreateCustom("fairworld:mat/base_mat", "Base Material", ScientificKind::Mineral, &err);
        wfBase.CommitToRegistry(&err);
        wfBase.SaveCatalogToDisk(catPath, &err);
    }

    // Registro A carica il catalogo
    ScientificMaterialRegistry regA;
    regA.Initialize();
    regA.LoadCustomMaterialsFromJson(catPath, &err);
    MaterialAuthoringWorkflow wfA(regA);

    // Registro B carica lo stesso catalogo
    ScientificMaterialRegistry regB;
    regB.Initialize();
    regB.LoadCustomMaterialsFromJson(catPath, &err);
    MaterialAuthoringWorkflow wfB(regB);

    // B apporta una modifica e salva
    wfB.BeginCreateCustom("fairworld:mat/b_material", "B Material", ScientificKind::Alloy, &err);
    wfB.CommitToRegistry(&err);
    bool bSaveOk = wfB.SaveCatalogToDisk(catPath, &err);

    // A tenta di salvare senza ricaricare
    wfA.BeginCreateCustom("fairworld:mat/a_material", "A Material", ScientificKind::Organic, &err);
    wfA.CommitToRegistry(&err);
    bool aSaveWithoutForceRejected = !wfA.SaveCatalogToDisk(catPath, &err, false);

    // A risolve il conflitto con forceOverwrite intenzionale
    bool aSaveWithForceOk = wfA.SaveCatalogToDisk(catPath, &err, true);

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);

    r.passed = bSaveOk && aSaveWithoutForceRejected && aSaveWithForceOk;
    r.observedDesc = "BSaveOk=" + std::to_string(bSaveOk) +
                    ", ASaveWithoutForceRejected=" + std::to_string(aSaveWithoutForceRejected) +
                    ", ASaveWithForceOk=" + std::to_string(aSaveWithForceOk);

    if (!r.passed) {
        r.details = "Mancata protezione contro il Lost Update concorrente: " + err;
    }

    return r;
}

AuthoringTestResult MaterialAuthoringContractTests::Test09_BuiltInMaterialsProtectionAgainstModificationAndDeprecation() {
    AuthoringTestResult r;
    r.testId = "AUTH-09";
    r.testName = "Absolute Protection of Built-In Materials (< 1000) Against Mutation and Deprecation";
    r.inputDesc = "Tentativi di modifica diretta, deprecazione e sovrascrittura di materiali standard built-in (es. Iron ID 1) via workflow.";
    r.expectedDesc = "I tentativi di modifica diretta o deprecazione vengono categoricamente respinti; il cloning come nuovo custom è consentito.";

    ScientificMaterialRegistry reg;
    reg.Initialize();

    MaterialAuthoringWorkflow wf(reg);
    std::string err;

    // ID 1 = Iron (built-in standard)
    ScientificMaterialID ironId = 1;
    ScientificMaterialDef ironBaseline = reg.GetMaterial(ironId);

    // 1. Tentativo di edit diretto su built-in: DEVE essere respinto
    bool editBuiltInRejected = !wf.BeginEditExisting(ironId, &err);

    // 2. Tentativo di clonazione di Iron come base per un nuovo materiale custom: DEVE riuscire
    bool cloneOk = wf.BeginCloneAsCustom(ironId, "fairworld:mat/high_carbon_iron", "High Carbon Iron Alloy", &err);
    bool cloneIsCustom = false;
    if (cloneOk) {
        const auto& wc = wf.GetWorkingCopy();
        cloneIsCustom = (wc.id >= CUSTOM_SCIENTIFIC_MATERIAL_ID_BASE) && (wc.isCustom == true);
        wf.CommitToRegistry(&err);
    }

    // 3. Verifica che il materiale Iron originale sia rimasto assolutamente invariato
    ScientificMaterialDef ironAfter = reg.GetMaterial(ironId);
    bool ironIntact = (ironBaseline == ironAfter);

    r.passed = editBuiltInRejected && cloneOk && cloneIsCustom && ironIntact;
    r.observedDesc = "EditBuiltInRejected=" + std::to_string(editBuiltInRejected) +
                    ", CloneOk=" + std::to_string(cloneOk) +
                    ", CloneIsCustom=" + std::to_string(cloneIsCustom) +
                    ", OriginalIronIntact=" + std::to_string(ironIntact);

    if (!r.passed) {
        r.details = "Violazione della protezione dei materiali built-in: " + err;
    }

    return r;
}

AuthoringTestResult MaterialAuthoringContractTests::Test10_ExistingBindingIntegrityAndVisualOrthogonality() {
    AuthoringTestResult r;
    r.testId = "AUTH-10";
    r.testName = "Existing Surface Binding Integrity and PBR Visual Orthogonality";
    r.inputDesc = "Superficie legata a materiale custom; modifica aspetto PBR in MaterialAppearanceRegistry; verifica ortogonalita' scientifica.";
    r.expectedDesc = "Il binding di superficie risolve con successo; le mutazioni visive lasciano inalterate le proprieta' scientifiche e viceversa.";

    ScientificMaterialRegistry sciReg;
    sciReg.Initialize();

    MaterialAuthoringWorkflow wf(sciReg);
    std::string err;
    wf.BeginCreateCustom("fairworld:mat/aerospace_aluminum", "Aerospace Aluminum", ScientificKind::Alloy, &err);
    wf.SetPhysicalProperty(ScientificPropertyNames::Density, 2810.0, "kg/m^3", DataQuality::PeerReviewed, "", &err);
    wf.CommitToRegistry(&err);
    wf.PublishActiveMaterial(&err);

    ScientificMaterialID aluId = wf.GetWorkingCopy().id;

    // 1. Verifica binding superficie
    SurfaceOverride so;
    so.surfaceId = 5; // Top face
    so.scientificMaterialId = aluId;

    auto bindingRes = ResolveSurfaceScientificBinding(5, {so}, aluId, &sciReg);
    bool bindingOk = (bindingRes.resolvedId == aluId) && (!bindingRes.isOrphan);

    // 2. Verifica ortogonalità visiva PBR
    MaterialAppearance appBefore;
    appBefore.id = 50;
    appBefore.canonicalId = "fairworld:appearance/aerospace_alu_pbr";
    appBefore.displayName = "Aerospace Aluminum PBR";
    appBefore.suggestedScientificMaterialId = aluId;
    appBefore.baseColorFallback = glm::vec3(0.85f, 0.85f, 0.90f);
    appBefore.roughnessFallback = 0.35f;
    appBefore.metallicFallback = 0.95f;

    MaterialAppearance appAfter = appBefore;
    appAfter.baseColorFallback = glm::vec3(0.20f, 0.80f, 0.30f); // Mutazione colore visivo
    appAfter.roughnessFallback = 0.10f;
    appAfter.albedoPath = "textures/custom_green_aluminum.png";

    bool orthogonalityOk = MaterialAuthoringWorkflow::VerifyAppearanceOrthogonality(sciReg, aluId, appBefore, appAfter);

    r.passed = bindingOk && orthogonalityOk;
    r.observedDesc = "SurfaceResolvedId=" + std::to_string(bindingRes.resolvedId) +
                    ", IsOverridden=" + (bindingRes.isOverridden ? "true" : "false") +
                    ", IsOrphan=" + (bindingRes.isOrphan ? "true" : "false") +
                    ", VisualOrthogonalityVerified=" + (orthogonalityOk ? "true" : "false");

    if (!r.passed) {
        r.details = "Risoluzione del binding di superficie fallita o violazione dell'ortogonalita' tra fisica e grafica visiva.";
    }

    return r;
}

AuthoringTestResult MaterialAuthoringContractTests::Test11_CelsiusFirstUIBoundaryAndKelvinPersistence() {
    AuthoringTestResult r;
    r.testId = "AUTH-11";
    r.testName = "Celsius-First UI Boundary, Kelvin Persistence and Full Lifecycle Verification";
    r.inputDesc = "Verifica conversioni °C/K, isolamento del confine UI, validazione limiti fisici (< 0 K), persistenza JSON in Kelvin, round-trip, discard session, protezione built-in e ortogonalita' PBR.";
    r.expectedDesc = "Tutte le conversioni esatte; JSON e Registry memorizzano esclusivamente K; UI opera in °C; validazione, rollback, salvataggio e protezione built-in conformi al 100%.";

    // 1. Verifica conversioni termometriche ed esattezza valori di riferimento (Titanium Carbide, Ferro, Acqua, Zero Assoluto)
    double tiCarbideK = 3430.0;
    double tiCarbideC = MaterialAuthoringWorkflow::CelsiusFromKelvin(tiCarbideK);
    bool tiCarbideOk = (std::abs(tiCarbideC - 3156.85) < 1e-6) &&
                       (std::abs(MaterialAuthoringWorkflow::KelvinFromCelsius(tiCarbideC) - 3430.0) < 1e-6);

    double ironK = 1811.0;
    double ironC = MaterialAuthoringWorkflow::CelsiusFromKelvin(ironK);
    bool ironOk = (std::abs(ironC - 1537.85) < 1e-6) &&
                  (std::abs(MaterialAuthoringWorkflow::KelvinFromCelsius(ironC) - 1811.0) < 1e-6);

    double waterK = 273.15;
    double waterC = MaterialAuthoringWorkflow::CelsiusFromKelvin(waterK);
    bool waterOk = (std::abs(waterC - 0.0) < 1e-6) &&
                   (std::abs(MaterialAuthoringWorkflow::KelvinFromCelsius(0.0) - 273.15) < 1e-6);

    double absZeroK = 0.0;
    double absZeroC = MaterialAuthoringWorkflow::CelsiusFromKelvin(absZeroK);
    bool absZeroOk = (std::abs(absZeroC - (-273.15)) < 1e-6) &&
                     (std::abs(MaterialAuthoringWorkflow::KelvinFromCelsius(-273.15) - 0.0) < 1e-6);

    bool mathOk = tiCarbideOk && ironOk && waterOk && absZeroOk;

    // 2. Creazione materiale custom con temperatura inserita in °C al confine UI
    ScientificMaterialRegistry reg;
    reg.Initialize();

    MaterialAuthoringWorkflow wf(reg);
    std::string err;
    bool createOk = wf.BeginCreateCustom("fairworld:mat/pyro_ceramic", "Pyro Ceramic", ScientificKind::Compound, &err);

    // Test validazione: inserimento temperatura non fisica via UI (es. -274 °C -> -0.85 K)
    double invalidInputC = -274.0;
    double invalidK = MaterialAuthoringWorkflow::KelvinFromCelsius(invalidInputC);
    bool nonPhysicalRejected = !wf.SetPhysicalProperty(ScientificPropertyNames::MeltingPoint, invalidK, "K", DataQuality::Estimated, "", &err);

    // Inserimento valido via UI: 500 °C -> 773.15 K
    double inputC = 500.0;
    double validK = MaterialAuthoringWorkflow::KelvinFromCelsius(inputC);
    bool validMeltingOk = wf.SetPhysicalProperty(ScientificPropertyNames::MeltingPoint, validK, "K", DataQuality::PeerReviewed, "Ceramic Lab 2026", &err);
    wf.SetPhysicalProperty(ScientificPropertyNames::Density, 3200.0, "kg/m^3", DataQuality::PeerReviewed, "", &err);

    auto valRes = wf.ValidateWorkingCopy();
    bool commitOk = wf.CommitToRegistry(&err);

    ScientificMaterialID pyroId = wf.GetWorkingCopy().id;
    auto registeredDef = reg.GetMaterial(pyroId);
    bool registryInKelvin = (registeredDef.meltingPoint.value == 773.15) && (registeredDef.meltingPoint.unit == "K");

    // 3. Persistenza su file JSON: verifica che su disco sia memorizzato esclusivamente K
    auto tempDir = std::filesystem::temp_directory_path() / "fw_auth_test_11";
    std::filesystem::create_directories(tempDir);
    auto catPath = (tempDir / "custom_materials.json").string();

    bool saveOk = wf.SaveCatalogToDisk(catPath, &err);

    bool jsonInKelvin = false;
    uint64_t firstGeneration = 0;
    {
        std::ifstream f(catPath);
        if (f.is_open()) {
            nlohmann::json j;
            f >> j;
            firstGeneration = j.value("catalogGeneration", static_cast<uint64_t>(0));
            if (j.contains("materials") && j["materials"].is_array()) {
                for (const auto& matJson : j["materials"]) {
                    if (matJson.value("canonicalId", "") == "fairworld:mat/pyro_ceramic") {
                        if (matJson.contains("meltingPoint")) {
                            const auto& mp = matJson["meltingPoint"];
                            double jVal = mp.value("value", 0.0);
                            std::string jUnit = mp.value("unit", "");
                            if (std::abs(jVal - 773.15) < 1e-4 && jUnit == "K") {
                                jsonInKelvin = true;
                            }
                        }
                    }
                }
            }
        }
    }

    // 4. Ricaricamento in registro indipendente e lettura al confine UI
    ScientificMaterialRegistry regTarget;
    regTarget.Initialize();
    bool loadOk = regTarget.LoadCustomMaterialsFromJson(catPath, &err);
    bool reloadMatch = false;
    double uiReadC = 0.0;
    if (loadOk && regTarget.HasMaterial(pyroId)) {
        auto loadedDef = regTarget.GetMaterial(pyroId);
        uiReadC = MaterialAuthoringWorkflow::CelsiusFromKelvin(loadedDef.meltingPoint.value);
        reloadMatch = (std::abs(loadedDef.meltingPoint.value - 773.15) < 1e-6) &&
                      (loadedDef.meltingPoint.unit == "K") &&
                      (std::abs(uiReadC - 500.0) < 1e-6);
    }

    // 5. Seconda modifica e secondo salvataggio con avanzamento di generazione
    MaterialAuthoringWorkflow wfTarget(regTarget);
    bool edit2BeginOk = wfTarget.BeginEditExisting(pyroId, &err);
    // Nuova temperatura UI: 600 °C -> 873.15 K
    double inputC2 = 600.0;
    double validK2 = MaterialAuthoringWorkflow::KelvinFromCelsius(inputC2);
    bool edit2SetOk = wfTarget.SetPhysicalProperty(ScientificPropertyNames::MeltingPoint, validK2, "K", DataQuality::PeerReviewed, "Lab update", &err);
    bool secondCommitOk = wfTarget.CommitToRegistry(&err);
    bool secondSaveOk = wfTarget.SaveCatalogToDisk(catPath, &err);

    bool json2InKelvin = false;
    uint64_t secondGeneration = 0;
    {
        std::ifstream f(catPath);
        if (f.is_open()) {
            nlohmann::json j;
            f >> j;
            secondGeneration = j.value("catalogGeneration", static_cast<uint64_t>(0));
            if (j.contains("materials") && j["materials"].is_array()) {
                for (const auto& matJson : j["materials"]) {
                    if (matJson.value("canonicalId", "") == "fairworld:mat/pyro_ceramic") {
                        if (matJson.contains("meltingPoint")) {
                            const auto& mp = matJson["meltingPoint"];
                            double jVal = mp.value("value", 0.0);
                            std::string jUnit = mp.value("unit", "");
                            if (std::abs(jVal - 873.15) < 1e-4 && jUnit == "K") {
                                json2InKelvin = true;
                            }
                        }
                    }
                }
            }
        }
    }
    bool genProgressionOk = (firstGeneration > 0) && (secondGeneration > firstGeneration);

    // 6. Verifica Annullamento Modifiche (Discard Session / Rollback)
    bool edit3BeginOk = wfTarget.BeginEditExisting(pyroId, &err);
    wfTarget.SetPhysicalProperty(ScientificPropertyNames::MeltingPoint, MaterialAuthoringWorkflow::KelvinFromCelsius(1500.0), "K", DataQuality::PeerReviewed, "", &err);
    wfTarget.DiscardSession();
    auto postDiscardDef = regTarget.GetMaterial(pyroId);
    bool discardRollbackOk = (std::abs(postDiscardDef.meltingPoint.value - 873.15) < 1e-6) &&
                             (wfTarget.GetSessionState() == AuthoringSessionState::Idle) &&
                             (!wfTarget.HasActiveSession());

    // 7. Verifica Protezione Materiali Built-in (< 1000)
    bool builtIn1Prot = MaterialAuthoringWorkflow::IsBuiltInProtected(1); // Water
    bool builtIn2Prot = MaterialAuthoringWorkflow::IsBuiltInProtected(2); // Granite
    bool builtIn3Prot = MaterialAuthoringWorkflow::IsBuiltInProtected(3); // Basalt
    bool builtIn4Prot = MaterialAuthoringWorkflow::IsBuiltInProtected(4); // Iron
    bool customNotProt = !MaterialAuthoringWorkflow::IsBuiltInProtected(pyroId);

    bool builtInPresent = regTarget.HasMaterial(2);
    ScientificMaterialDef builtInBaseline = builtInPresent ? regTarget.GetMaterial(2) : ScientificMaterialDef{};
    std::string editBuiltInErr;
    bool editBuiltInRejected = !wfTarget.BeginEditExisting(2, &editBuiltInErr);
    ScientificMaterialDef builtInAfter = builtInPresent ? regTarget.GetMaterial(2) : ScientificMaterialDef{};
    bool builtInUnchanged = builtInPresent &&
                            (builtInBaseline == builtInAfter) &&
                            (std::abs(builtInAfter.meltingPoint.value - builtInBaseline.meltingPoint.value) < 1e-6);

    bool protectionOk = builtIn1Prot && builtIn2Prot && builtIn3Prot && builtIn4Prot &&
                        customNotProt && builtInPresent && editBuiltInRejected && builtInUnchanged;

    // 8. Verifica Separazione dai Materiali PBR (Visual Appearance Orthogonality)
    MaterialAppearance appBefore;
    appBefore.id = 70;
    appBefore.canonicalId = "fairworld:appearance/pyro_ceramic_pbr";
    appBefore.displayName = "Pyro Ceramic PBR";
    appBefore.suggestedScientificMaterialId = pyroId;
    appBefore.baseColorFallback = glm::vec3(0.9f, 0.4f, 0.1f);
    appBefore.roughnessFallback = 0.5f;

    MaterialAppearance appAfter = appBefore;
    appAfter.baseColorFallback = glm::vec3(0.1f, 0.1f, 0.9f); // Mutazione PBR radicale
    appAfter.roughnessFallback = 0.05f;

    bool visualOrthogonalityOk = MaterialAuthoringWorkflow::VerifyAppearanceOrthogonality(regTarget, pyroId, appBefore, appAfter);
    auto finalDef = regTarget.GetMaterial(pyroId);
    bool physicalIntegrityOk = (std::abs(finalDef.meltingPoint.value - 873.15) < 1e-6) && (finalDef.meltingPoint.unit == "K");

    // Pulizia file temporanei
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);

    r.passed = mathOk && createOk && nonPhysicalRejected && validMeltingOk && valRes.isValid &&
               commitOk && registryInKelvin && saveOk && jsonInKelvin && loadOk &&
               reloadMatch && edit2BeginOk && edit2SetOk && secondCommitOk && secondSaveOk &&
               json2InKelvin && genProgressionOk && edit3BeginOk && discardRollbackOk &&
               protectionOk && visualOrthogonalityOk && physicalIntegrityOk;

    r.observedDesc = "MathOk=" + std::to_string(mathOk) +
                    ", FirstGeneration=" + std::to_string(firstGeneration) +
                    ", SecondGeneration=" + std::to_string(secondGeneration) +
                    ", SecondCommitOk=" + std::to_string(secondCommitOk) +
                    ", SecondSaveOk=" + std::to_string(secondSaveOk) +
                    ", BuiltInPresent=" + std::to_string(builtInPresent) +
                    ", BuiltInEditRejected=" + std::to_string(editBuiltInRejected) +
                    ", BuiltInUnchanged=" + std::to_string(builtInUnchanged) +
                    ", ReloadMatch=" + std::to_string(reloadMatch) +
                    ", DiscardRollbackOk=" + std::to_string(discardRollbackOk) +
                    ", VisualOrthogonalityOk=" + std::to_string(visualOrthogonalityOk && physicalIntegrityOk);

    if (!r.passed) {
        r.details = "Fallimento in una delle verifiche contrattuali di conversione termometrica, persistenza o isolamento P0.3C2.";
    }

    return r;
}

} // namespace fw
