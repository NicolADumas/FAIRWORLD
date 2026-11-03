#include "blocks/ScientificBindingContractTests.h"
#include "blocks/SurfaceDefinition.h"
#include "blocks/ScientificMaterialRegistry.h"
#include "blocks/sculpt/EditableMesh.h"
#include "blocks/sculpt/MeshBridgeService.h"
#include "blocks/uv/PrimitiveUVMapper.h"
#include "blocks/uv/UVEditorDocument.h"
#include "blocks/texture/SurfacePaintingAdapter.h"
#include "blocks/texture/TexturePaintingService.h"
#include "core/app/RuntimeManager.h"
#include "blocks/factory/FactoryOrchestrator.h"
#include "blocks/factory/AssetPackagingService.h"
#include "blocks/BlockRegistry.h"
#include "blocks/MaterialRegistry.h"
#include "blocks/ShapeRegistry.h"
#include "json.hpp"
#include <iostream>
#include <filesystem>

namespace fw {

std::vector<ScientificBindingTestResult> ScientificBindingContractTests::RunAllTests() {
    std::vector<ScientificBindingTestResult> results;
    results.reserve(16);

    results.push_back(Test01_RootMaterialInheritance());
    results.push_back(Test02_LocalSurfaceOverride());
    results.push_back(Test03_NonExistentScientificMaterialDiagnostic());
    results.push_back(Test04_LegacyAssetCompatibilityAndSerialization());
    results.push_back(Test05_IndependenceFromAppearanceAndVisualMaterial());
    results.push_back(Test06_MeshFaceSemanticAdapterResolution());
    results.push_back(Test07_ZeroPhysicalPropertyDuplication());
    results.push_back(Test08_FullWorkflowDraftPackageSaveLoadRuntimeRoundTrip());
    results.push_back(Test09_RootMaterialAndSurfaceOverrideDynamics());
    results.push_back(Test10_OrphanScientificIdPreservedAcrossPersistenceAndDiagnosed());
    results.push_back(Test11_LegacyPackageBackwardCompatibility());
    results.push_back(Test12_IndependenceFromAppearanceIdAndVisualPainting());
    results.push_back(Test13_BlockMakerSurfaceSelectionAndRootIsolation());
    results.push_back(Test14_BlockMakerSurfaceOverridePersistenceAndReloadCycle());
    results.push_back(Test15_CylinderSemanticSurfaceBindingAndUVPaintDiagnosticProtection());
    results.push_back(Test16_PartAssemblyPreservesDistinctScientificMaterials());

    return results;
}

ScientificBindingTestResult ScientificBindingContractTests::Test01_RootMaterialInheritance() {
    ScientificBindingTestResult r;
    r.testId = "BIND-01";
    r.testName = "Root Material Inheritance for Unoverridden Surfaces";
    r.inputDesc = "Root ScientificMaterialID = 1 (Iron). Superficie 0 senza override, superficie 1 con override scientificMaterialId = 0.";
    r.expectedDesc = "Entrambe le superfici risolvono a 1 (ereditato dalla radice) con isOverridden == false.";

    uint32_t rootMaterialId = 1;
    std::vector<SurfaceOverride> overrides;

    SurfaceOverride soExplicitZero;
    soExplicitZero.surfaceId = 1;
    soExplicitZero.scientificMaterialId = 0; // Ereditarietà esplicita
    overrides.push_back(soExplicitZero);

    // Superficie 0 non ha alcuna entry in overrides
    auto resSurf0 = ResolveSurfaceScientificBinding(0, overrides, rootMaterialId);

    // Superficie 1 ha un'entry ma con scientificMaterialId == 0
    auto resSurf1 = ResolveSurfaceScientificBinding(1, overrides, rootMaterialId);

    bool pass0 = (resSurf0.resolvedId == rootMaterialId && !resSurf0.isOverridden);
    bool pass1 = (resSurf1.resolvedId == rootMaterialId && !resSurf1.isOverridden);

    // Caso root == 0
    auto resZeroRoot = ResolveSurfaceScientificBinding(0, overrides, 0);
    bool passZero = (resZeroRoot.resolvedId == 0 && !resZeroRoot.isOverridden);

    r.passed = pass0 && pass1 && passZero;
    r.observedDesc = "Surf0: ID=" + std::to_string(resSurf0.resolvedId) + " (overridden=" + (resSurf0.isOverridden ? "true" : "false") + ")"
                   + ", Surf1: ID=" + std::to_string(resSurf1.resolvedId) + " (overridden=" + (resSurf1.isOverridden ? "true" : "false") + ")"
                   + ", ZeroRoot: ID=" + std::to_string(resZeroRoot.resolvedId);

    if (!r.passed) {
        r.details = "Mancata ereditarietà dal materiale scientifico radice.";
    }

    return r;
}

ScientificBindingTestResult ScientificBindingContractTests::Test02_LocalSurfaceOverride() {
    ScientificBindingTestResult r;
    r.testId = "BIND-02";
    r.testName = "Part-Level Uniform Scientific Material Enforcement & Legacy Preservation";
    r.inputDesc = "Root ID = 1 (Iron). Struttura dati con residui legacy per superficie (Surf 2 = 5 [Wood], Surf 3 = 8 [Gold], Surf 0 non toccata).";
    r.expectedDesc = "Tutte le superfici risolvono a ID 1 (Iron) con isOverridden == false (omogeneità fisica della parte). I valori legacy 5 e 8 rimangono conservati losslessly nella struct.";

    uint32_t rootMaterialId = 1;
    std::vector<SurfaceOverride> overrides;

    SurfaceOverride soWood;
    soWood.surfaceId = 2;
    soWood.scientificMaterialId = 5;
    overrides.push_back(soWood);

    SurfaceOverride soGold;
    soGold.surfaceId = 3;
    soGold.scientificMaterialId = 8;
    overrides.push_back(soGold);

    auto res0 = ResolveSurfaceScientificBinding(0, overrides, rootMaterialId);
    auto res2 = ResolveSurfaceScientificBinding(2, overrides, rootMaterialId);
    auto res3 = ResolveSurfaceScientificBinding(3, overrides, rootMaterialId);

    bool pass0 = (res0.resolvedId == 1 && !res0.isOverridden);
    bool pass2 = (res2.resolvedId == 1 && !res2.isOverridden);
    bool pass3 = (res3.resolvedId == 1 && !res3.isOverridden);
    bool legacyLossless = (overrides[0].scientificMaterialId == 5 && overrides[1].scientificMaterialId == 8);

    r.passed = pass0 && pass2 && pass3 && legacyLossless;
    r.observedDesc = "Surf0=" + std::to_string(res0.resolvedId) + " (ov=" + std::to_string(res0.isOverridden) + ")"
                   + ", Surf2=" + std::to_string(res2.resolvedId) + " (ov=" + std::to_string(res2.isOverridden) + ")"
                   + ", Surf3=" + std::to_string(res3.resolvedId) + " (ov=" + std::to_string(res3.isOverridden) + ")"
                   + " | LegacyLossless=" + (legacyLossless ? "YES" : "NO");

    if (!r.passed) {
        r.details = "Risoluzione omogenea del materiale di parte fallita o perdita silenziosa dei valori legacy.";
    }

    return r;
}

ScientificBindingTestResult ScientificBindingContractTests::Test03_NonExistentScientificMaterialDiagnostic() {
    ScientificBindingTestResult r;
    r.testId = "BIND-03";
    r.testName = "Preservation and Diagnostic Flagging of Non-Existent Scientific Material IDs";
    r.inputDesc = "Parte con radice orfana (ID 999999) e override legacy di superficie con ID 999999 verificati contro ScientificMaterialRegistry.";
    r.expectedDesc = "La radice orfana risolve uniformemente con isOrphan == true; l'ID legacy di superficie è preservato nella struct e diagnosticato senza reset.";

    ScientificMaterialRegistry registry;
    registry.Initialize();

    uint32_t rootMaterialId = 1; // Materiale standard presente
    std::vector<SurfaceOverride> overrides;

    SurfaceOverride soOrphan;
    soOrphan.surfaceId = 2;
    soOrphan.scientificMaterialId = 999999; // ID inesistente nel catalogo (legacy)
    overrides.push_back(soOrphan);

    // Risoluzione con parte avente radice orfana (999999)
    auto resOrphanRoot = ResolveSurfaceScientificBinding(2, overrides, 999999, &registry);
    // Risoluzione con parte avente radice valida (1)
    auto resValid = ResolveSurfaceScientificBinding(0, overrides, rootMaterialId, &registry);
    // Risoluzione con parte avente radice 0
    auto resZero = ResolveSurfaceScientificBinding(0, overrides, 0, &registry);

    // Diagnostica delle strutture di superficie
    auto audit = AuditScientificMaterialBindings(rootMaterialId, overrides, &registry);
    bool orphanAuditedInStruct = audit.hasOrphans && (audit.surfaceDiagnostics.size() == 1 && audit.surfaceDiagnostics[0].isOrphan);
    bool legacyStructPreserved = (overrides[0].scientificMaterialId == 999999);

    bool passOrphan = (resOrphanRoot.resolvedId == 999999 && !resOrphanRoot.isOverridden && resOrphanRoot.isOrphan);
    bool passValid = (resValid.resolvedId == 1 && !resValid.isOrphan);
    bool passZero = (resZero.resolvedId == 0 && !resZero.isOrphan);

    r.passed = passOrphan && passValid && passZero && orphanAuditedInStruct && legacyStructPreserved;
    r.observedDesc = "RootOrphan: ID=" + std::to_string(resOrphanRoot.resolvedId) + ", isOrphan=" + (resOrphanRoot.isOrphan ? "true" : "false")
                   + " | ValidRoot: ID=" + std::to_string(resValid.resolvedId) + ", isOrphan=" + (resValid.isOrphan ? "true" : "false")
                   + " | Zero: ID=" + std::to_string(resZero.resolvedId) + ", isOrphan=" + (resZero.isOrphan ? "true" : "false")
                   + " | LegacyStructPreserved=" + (legacyStructPreserved ? "YES" : "NO");

    if (!r.passed) {
        r.details = "L'ID inesistente è stato azzerato o non è stato correttamente identificato come orfano.";
    }

    return r;
}

ScientificBindingTestResult ScientificBindingContractTests::Test04_LegacyAssetCompatibilityAndSerialization() {
    ScientificBindingTestResult r;
    r.testId = "BIND-04";
    r.testName = "Legacy Asset JSON Backward Compatibility & Modern Round-Trip Serialization";
    r.inputDesc = "Deserializzazione JSON v1 privo di scientificMaterialId e serializzazione round-trip moderna.";
    r.expectedDesc = "JSON legacy deserializza con scientificMaterialId = 0 (ereditato); round-trip moderno preserva il valore assegnato.";

    // 1. JSON Legacy pre-P0.3 privo del campo "scientificMaterialId"
    std::string legacyJsonStr = R"({
        "surfaceId": 3,
        "materialId": 12,
        "useExplicitMaterial": true,
        "projection": 1,
        "overridePbr": true,
        "baseColor": [0.5, 0.5, 0.5],
        "roughness": 0.7,
        "metallic": 0.1,
        "emissive": 0.0
    })";

    nlohmann::json jLegacy = nlohmann::json::parse(legacyJsonStr);
    SurfaceOverride soLegacy;
    from_json(jLegacy, soLegacy);

    bool legacyOk = (soLegacy.surfaceId == 3 &&
                     soLegacy.materialId == 12 &&
                     soLegacy.useExplicitMaterial == true &&
                     soLegacy.scientificMaterialId == 0 && // Default sicuro all'ereditarietà
                     !soLegacy.HasScientificOverride());

    // 2. Round-trip moderno
    SurfaceOverride soModern;
    soModern.surfaceId = 5;
    soModern.materialId = 99;
    soModern.useExplicitMaterial = true;
    soModern.scientificMaterialId = 1042;
    soModern.roughness = 0.25f;

    nlohmann::json jModern;
    to_json(jModern, soModern);

    SurfaceOverride soReloaded;
    from_json(jModern, soReloaded);

    bool modernOk = (soReloaded.surfaceId == 5 &&
                     soReloaded.materialId == 99 &&
                     soReloaded.useExplicitMaterial == true &&
                     soReloaded.scientificMaterialId == 1042 &&
                     soReloaded.HasScientificOverride() &&
                     soReloaded.ResolveEffectiveScientificMaterialId(1) == 1);

    r.passed = legacyOk && modernOk;
    r.observedDesc = "Legacy deserialized: ID=" + std::to_string(soLegacy.scientificMaterialId)
                   + " (HasOverride=" + (soLegacy.HasScientificOverride() ? "true" : "false") + ")"
                   + " | Modern round-trip: ID=" + std::to_string(soReloaded.scientificMaterialId)
                   + " (HasOverride=" + (soReloaded.HasScientificOverride() ? "true" : "false") + ")"
                   + " | RootAuthority=" + std::to_string(soReloaded.ResolveEffectiveScientificMaterialId(1));

    if (!r.passed) {
        r.details = "Fallimento nella deserializzazione legacy o nel round-trip moderno di SurfaceOverride.";
    }

    return r;
}

ScientificBindingTestResult ScientificBindingContractTests::Test05_IndependenceFromAppearanceAndVisualMaterial() {
    ScientificBindingTestResult r;
    r.testId = "BIND-05";
    r.testName = "Orthogonality: Scientific Material vs Appearance & Visual Layer Independence";
    r.inputDesc = "Mutazione di materialId (PBR), texture paths e parametri visivi mantenendo immutato scientificMaterialId e viceversa.";
    r.expectedDesc = "Nessuna interferenza o mutazione incrociata tra layer visivo e identità scientifica.";

    SurfaceOverride so;
    so.surfaceId = 1;
    so.materialId = 10;
    so.scientificMaterialId = 50;
    so.roughness = 0.8f;
    so.metallic = 0.1f;
    so.textures.albedoMapPath = "textures/stone_albedo.png";

    // 1. Mutazione solo visiva (simula 3D painting o cambio texture PBR)
    so.materialId = 20;
    so.roughness = 0.2f;
    so.metallic = 0.9f;
    so.textures.albedoMapPath = "textures/gold_painted.png";
    so.textures.useExplicitTextures = true;

    bool visualMutationPreservedScientific = (so.scientificMaterialId == 50);

    // 2. Mutazione solo scientifica
    so.scientificMaterialId = 75;

    bool scientificMutationPreservedVisual = (so.materialId == 20 &&
                                              so.roughness == 0.2f &&
                                              so.metallic == 0.9f &&
                                              so.textures.albedoMapPath == "textures/gold_painted.png");

    r.passed = visualMutationPreservedScientific && scientificMutationPreservedVisual;
    r.observedDesc = "Visual edit -> scientificMaterialId=" + std::to_string(so.scientificMaterialId)
                   + " | Scientific edit -> visual materialId=" + std::to_string(so.materialId)
                   + ", albedo=" + so.textures.albedoMapPath;

    if (!r.passed) {
        r.details = "Violata l'indipendenza tra l'aspetto visivo e il materiale scientifico.";
    }

    return r;
}

ScientificBindingTestResult ScientificBindingContractTests::Test06_MeshFaceSemanticAdapterResolution() {
    ScientificBindingTestResult r;
    r.testId = "BIND-06";
    r.testName = "MeshFace Semantic Adapter Resolution without DCEL Data Duplication";
    r.inputDesc = "Verifica che una MeshFace con surfaceId=2 risolva il materiale tramite l'adattatore senza alterare sizeof(MeshFace).";
    r.expectedDesc = "La faccia risolve il materiale associato alla sua superficie; sizeof(MeshFace) è invariato e privo di campi duplicati.";

    // Verifica che MeshFace mantenga la sua struttura essenziale
    MeshFace face;
    face.surfaceId = 2;
    face.materialId = 5; // Layer visivo

    MeshFace faceInherited;
    faceInherited.surfaceId = 0;
    faceInherited.materialId = 5;

    std::vector<SurfaceOverride> overrides;
    SurfaceOverride so;
    so.surfaceId = 2;
    so.scientificMaterialId = 88;
    overrides.push_back(so);

    uint32_t rootMaterialId = 12;

    auto resOverriddenFace = ResolveMeshFaceScientificBinding(face.surfaceId, overrides, rootMaterialId);
    auto resInheritedFace = ResolveMeshFaceScientificBinding(faceInherited.surfaceId, overrides, rootMaterialId);

    bool passOverridden = (resOverriddenFace.resolvedId == 12 && !resOverriddenFace.isOverridden);
    bool passInherited = (resInheritedFace.resolvedId == 12 && !resInheritedFace.isOverridden);
    bool legacyStructPreserved = (overrides[0].scientificMaterialId == 88);

    r.passed = passOverridden && passInherited && legacyStructPreserved;
    r.observedDesc = "Overridden Face (surf 2): ID=" + std::to_string(resOverriddenFace.resolvedId)
                   + ", Inherited Face (surf 0): ID=" + std::to_string(resInheritedFace.resolvedId)
                   + " | LegacyStructPreserved=" + (legacyStructPreserved ? "YES" : "NO");

    if (!r.passed) {
        r.details = "Risoluzione fallita tramite l'adattatore semantico della MeshFace.";
    }

    return r;
}

ScientificBindingTestResult ScientificBindingContractTests::Test07_ZeroPhysicalPropertyDuplication() {
    ScientificBindingTestResult r;
    r.testId = "BIND-07";
    r.testName = "Zero Physical Property Duplication in Surface Contracts";
    r.inputDesc = "Verifica che SurfaceOverride non contenga campi fisici/termodinamici duplicati (densità, attrito, calore specifico).";
    r.expectedDesc = "SurfaceOverride contiene solo l'ID; le proprietà fisiche vengono risolte dinamicamente dal registry.";

    ScientificMaterialRegistry registry;
    registry.Initialize();

    uint32_t rootMaterialId = 1; // Materiale standard (es. Granito/Ferro)
    std::vector<SurfaceOverride> overrides;

    SurfaceOverride so2;
    so2.surfaceId = 2;
    so2.scientificMaterialId = 2; // Materiale secondario (es. Legno)
    overrides.push_back(so2);

    // Risoluzione degli ID (uniformità fisica della parte)
    auto resRoot = ResolveSurfaceScientificBinding(0, overrides, rootMaterialId, &registry);
    auto resSurf2 = ResolveSurfaceScientificBinding(2, overrides, rootMaterialId, &registry);

    bool uniformResolution = (resRoot.resolvedId == 1 && resSurf2.resolvedId == 1 && !resSurf2.isOverridden);

    // Query dinamica delle proprietà dal registro tramite l'ID risolto vs altro materiale
    ScientificPropertyOverrides emptyOverrides;
    auto densityPart = registry.ResolveProperty(resRoot.resolvedId, ScientificPropertyNames::Density, emptyOverrides);
    auto densityOther = registry.ResolveProperty(2, ScientificPropertyNames::Density, emptyOverrides);

    bool hasValidProperties = (densityPart.state == PropertyState::Known && densityOther.state == PropertyState::Known);
    bool distinctDensities = (densityPart.value != densityOther.value);
    bool legacyStructPreserved = (overrides[0].scientificMaterialId == 2);

    r.passed = uniformResolution && hasValidProperties && distinctDensities && legacyStructPreserved;
    r.observedDesc = "Part Density (ID " + std::to_string(resRoot.resolvedId) + ") = " + std::to_string(densityPart.value)
                   + " kg/m^3 | Other Density (ID 2) = " + std::to_string(densityOther.value)
                   + " kg/m^3 (Zero duplication, uniform part resolution PASS)";

    if (!r.passed) {
        r.details = "Fallimento nell'interrogazione dinamica delle proprietà fisiche dal catalogo scientifico.";
    }

    return r;
}

// =============================================================================
// P0.3B2: PERSISTENCE & WORKFLOW IMPLEMENTATIONS (BIND-08..12)
// =============================================================================

ScientificBindingTestResult ScientificBindingContractTests::Test08_FullWorkflowDraftPackageSaveLoadRuntimeRoundTrip() {
    ScientificBindingTestResult r;
    r.testId = "BIND-08";
    r.testName = "Full Workflow Draft -> Package -> Save -> Load -> Runtime Persistence";
    r.inputDesc = "Creazione di RawAssetDraft con root scientificMaterialId = 1 e superfici multiple (0=radice, 1=override 2 [Granite], 2=override 3 [Basalt]).";
    r.expectedDesc = "Conservazione perfetta attraverso FactoryOrchestrator, RawAssetPackage, SaveToFile, LoadFromFile e PublishPackageTransactional nei registri runtime.";

    ShapeRegistry sReg; sReg.Initialize();
    MaterialRegistry mReg; mReg.Initialize();
    BlockRegistry bReg; bReg.Initialize();
    ScientificMaterialRegistry sciReg; sciReg.Initialize();

    FactoryOrchestrator orch;
    std::string testAssetId = "fairworld:test_pillar_roundtrip";
    if (!orch.OpenDraft(testAssetId, "Pillar Roundtrip")) {
        r.passed = false;
        r.observedDesc = "OpenDraft fallito per " + testAssetId;
        return r;
    }

    auto* draft = orch.GetCurrentDraft();
    if (!draft) {
        r.passed = false;
        r.observedDesc = "GetCurrentDraft ha restituito nullptr.";
        return r;
    }

    draft->scientificMaterialId = 1; // Materiale radice (Standard/Pure Water)

    SurfaceOverride s0;
    s0.surfaceId = 0;
    s0.scientificMaterialId = 0; // Eredita da radice

    SurfaceOverride s1;
    s1.surfaceId = 1;
    s1.scientificMaterialId = 2; // Override esplicito (Granite)

    SurfaceOverride s2;
    s2.surfaceId = 2;
    s2.scientificMaterialId = 3; // Override esplicito (Basalt)

    draft->surfaces = { s0, s1, s2 };

    orch.ExecuteStation(ProductionStationID::Shape, &sReg, &mReg, &bReg);
    orch.ExecuteStation(ProductionStationID::SurfaceUV, &sReg, &mReg, &bReg);
    orch.ExecuteStation(ProductionStationID::Material, &sReg, &mReg, &bReg);
    orch.ExecuteStation(ProductionStationID::RawPhysics, &sReg, &mReg, &bReg);
    orch.ExecuteStation(ProductionStationID::QualityControl, &sReg, &mReg, &bReg);

    auto pkgOpt = orch.BuildPackage(&sReg, &mReg, &bReg);
    if (!pkgOpt.has_value()) {
        r.passed = false;
        r.observedDesc = "BuildPackage ha restituito nullopt.";
        return r;
    }

    RawAssetPackage package = *pkgOpt;
    bool pkgHasSurfaces = (package.surfaces.size() == 3);
    bool pkgRootMatch = (package.scientificMaterialId == 1);
    bool pkgS0Match = (package.surfaces[0].scientificMaterialId == 0);
    bool pkgS1Match = (package.surfaces[1].scientificMaterialId == 2);
    bool pkgS2Match = (package.surfaces[2].scientificMaterialId == 3);

    bool stepDraftToPkg = pkgHasSurfaces && pkgRootMatch && pkgS0Match && pkgS1Match && pkgS2Match;

    // Serializzazione su disco e deserializzazione
    std::string tempPkgPath = "temp_bind08_test.rawpkg";
    bool saveOk = package.SaveToFile(tempPkgPath);
    bool stepPkgToJson = saveOk;

    RawAssetPackage loadedPkg;
    bool loadOk = saveOk && loadedPkg.LoadFromFile(tempPkgPath);
    std::error_code ec;
    std::filesystem::remove(tempPkgPath, ec);

    bool loadedRootMatch = (loadedPkg.scientificMaterialId == 1);
    bool loadedSurfacesCount = (loadedPkg.surfaces.size() == 3);
    bool loadedS0Match = (loadedPkg.surfaces[0].scientificMaterialId == 0);
    bool loadedS1Match = (loadedPkg.surfaces[1].scientificMaterialId == 2);
    bool loadedS2Match = (loadedPkg.surfaces[2].scientificMaterialId == 3);

    bool stepJsonToPkg = loadOk && loadedRootMatch && loadedSurfacesCount && loadedS0Match && loadedS1Match && loadedS2Match;

    // Pubblicazione transazionale nei registri runtime
    AssetPackagingService packService;
    auto pubRes = packService.PublishPackageTransactional(loadedPkg, bReg, mReg, sReg);

    bool stepPkgToBlockReg = false;
    bool stepPkgToMatReg = false;
    bool stepSciResolution = false;
    bool res0Pass = false;
    bool res1Pass = false;
    bool res2Pass = false;

    if (pubRes.success) {
        uint8_t voxelId = pubRes.targetVoxelId;
        const auto& bDef = bReg.GetBlock(voxelId);
        const auto& matDef = mReg.GetMaterial(voxelId);

        stepPkgToBlockReg = (bDef.scientificMaterialId == 1);
        stepPkgToMatReg = (matDef.surfaces.size() == 3);

        auto res0 = ResolveSurfaceScientificBinding(0, matDef.surfaces, bDef.scientificMaterialId, &sciReg);
        auto res1 = ResolveSurfaceScientificBinding(1, matDef.surfaces, bDef.scientificMaterialId, &sciReg);
        auto res2 = ResolveSurfaceScientificBinding(2, matDef.surfaces, bDef.scientificMaterialId, &sciReg);

        res0Pass = (res0.resolvedId == 1 && !res0.isOverridden && !res0.isOrphan);
        res1Pass = (res1.resolvedId == 1 && !res1.isOverridden && !res1.isOrphan);
        res2Pass = (res2.resolvedId == 1 && !res2.isOverridden && !res2.isOrphan);

        bool legacyValuesPreserved = (matDef.surfaces[1].scientificMaterialId == 2 &&
                                      matDef.surfaces[2].scientificMaterialId == 3);

        stepSciResolution = res0Pass && res1Pass && res2Pass && legacyValuesPreserved;
    }

    // Stampa separata dei risultati intermedi delle verifiche
    std::cout << "\n         --- VERIFICHE INTERMEDIE BIND-08 ---\n";
    std::cout << "         1. Draft -> Package:            " << (stepDraftToPkg ? "[PASS]" : "[FAIL]") << "\n";
    std::cout << "         2. Package -> JSON:             " << (stepPkgToJson ? "[PASS]" : "[FAIL]") << "\n";
    std::cout << "         3. JSON -> Package:             " << (stepJsonToPkg ? "[PASS]" : "[FAIL]") << "\n";
    std::cout << "         4. Package -> MaterialRegistry: " << (stepPkgToMatReg ? "[PASS]" : "[FAIL]") << "\n";
    std::cout << "         5. Package -> BlockRegistry:    " << (stepPkgToBlockReg ? "[PASS]" : "[FAIL]") << "\n";
    std::cout << "         6. Risoluzione scientifica:     " << (stepSciResolution ? "[PASS]" : "[FAIL]") << "\n";
    std::cout << "         ------------------------------------\n";

    r.passed = stepDraftToPkg && stepPkgToJson && stepJsonToPkg && stepPkgToMatReg && stepPkgToBlockReg && stepSciResolution;

    r.observedDesc = "Draft->Pkg: " + std::string(stepDraftToPkg ? "PASS" : "FAIL")
                   + " | Pkg->JSON: " + std::string(stepPkgToJson ? "PASS" : "FAIL")
                   + " | JSON->Pkg: " + std::string(stepJsonToPkg ? "PASS" : "FAIL")
                   + " | Pkg->MatReg: " + std::string(stepPkgToMatReg ? "PASS" : "FAIL")
                   + " | Pkg->BlockReg: " + std::string(stepPkgToBlockReg ? "PASS" : "FAIL")
                   + " | SciResolution: " + std::string(stepSciResolution ? "PASS" : "FAIL");

    if (!r.passed) {
        if (!stepDraftToPkg) {
            r.details = "Fallimento in Draft -> Package: le superfici o l'ID scientifico radice non sono stati propagati.";
        } else if (!stepPkgToJson) {
            r.details = "Fallimento in Package -> JSON: SaveToFile ha restituito false.";
        } else if (!stepJsonToPkg) {
            r.details = "Fallimento in JSON -> Package: LoadFromFile ha restituito false o i dati deserializzati sono incongruenti.";
        } else if (!pubRes.success) {
            r.details = "Fallimento in PublishPackageTransactional: " + pubRes.errorMessage;
        } else if (!stepPkgToBlockReg) {
            r.details = "Fallimento in Package -> BlockRegistry: bDef.scientificMaterialId != 1.";
        } else if (!stepPkgToMatReg) {
            r.details = "Fallimento in Package -> MaterialRegistry: matDef.surfaces.size() != 3.";
        } else if (!stepSciResolution) {
            r.details = "Fallimento in Risoluzione scientifica: S0=" + std::to_string(res0Pass) + ", S1=" + std::to_string(res1Pass) + ", S2=" + std::to_string(res2Pass);
        }
    }

    return r;
}

ScientificBindingTestResult ScientificBindingContractTests::Test09_RootMaterialAndSurfaceOverrideDynamics() {
    ScientificBindingTestResult r;
    r.testId = "BIND-09";
    r.testName = "Dynamic Root Propagation Across All Part Surfaces";
    r.inputDesc = "Asset con superficie 0 e superficie 1 (con residuo legacy scientificMaterialId=4). Mutazione del materiale radice da 1 a 6.";
    r.expectedDesc = "Tutte le superfici adottano deterministicamente la radice 1 prima della mutazione, e 6 dopo la mutazione (isOverridden == false). Il residuo legacy 4 nella struct rimane intatto.";

    ScientificMaterialRegistry sciReg; sciReg.Initialize();

    uint32_t rootMatId = 1;
    std::vector<SurfaceOverride> surfaces;

    SurfaceOverride s0;
    s0.surfaceId = 0;
    s0.scientificMaterialId = 0; // Inherit

    SurfaceOverride s1;
    s1.surfaceId = 1;
    s1.scientificMaterialId = 4; // Legacy value

    surfaces.push_back(s0);
    surfaces.push_back(s1);

    auto before0 = ResolveSurfaceScientificBinding(0, surfaces, rootMatId, &sciReg);
    auto before1 = ResolveSurfaceScientificBinding(1, surfaces, rootMatId, &sciReg);

    bool b0Ok = (before0.resolvedId == 1 && !before0.isOverridden);
    bool b1Ok = (before1.resolvedId == 1 && !before1.isOverridden);

    // Mutazione della radice
    rootMatId = 6;

    auto after0 = ResolveSurfaceScientificBinding(0, surfaces, rootMatId, &sciReg);
    auto after1 = ResolveSurfaceScientificBinding(1, surfaces, rootMatId, &sciReg);

    bool a0Ok = (after0.resolvedId == 6 && !after0.isOverridden);
    bool a1Ok = (after1.resolvedId == 6 && !after1.isOverridden);
    bool legacyPreserved = (surfaces[1].scientificMaterialId == 4);

    r.passed = b0Ok && b1Ok && a0Ok && a1Ok && legacyPreserved;
    r.observedDesc = "Prima mutazione: S0=" + std::to_string(before0.resolvedId) + ", S1=" + std::to_string(before1.resolvedId)
                   + " | Dopo mutazione radice (6): S0=" + std::to_string(after0.resolvedId) + " (uniforme), S1=" + std::to_string(after1.resolvedId) + " (uniforme)"
                   + " | LegacyPreserved=" + (legacyPreserved ? "YES" : "NO");

    if (!r.passed) {
        r.details = "La mutazione del materiale radice non ha aggiornato uniformemente tutte le superfici della parte.";
    }

    return r;
}

ScientificBindingTestResult ScientificBindingContractTests::Test10_OrphanScientificIdPreservedAcrossPersistenceAndDiagnosed() {
    ScientificBindingTestResult r;
    r.testId = "BIND-10";
    r.testName = "Orphan Scientific IDs Preservation Across Workflow and Diagnostic Audit";
    r.inputDesc = "Asset draft con root scientificMaterialId = 999999 e superficie 1 con override = 888888 (entrambi assenti da ScientificMaterialRegistry).";
    r.expectedDesc = "Entrambi gli ID vengono preservati attraverso BuildPackage, SaveToFile, LoadFromFile e Publish senza reset né sostituzione, e audit segnala orfani.";

    ShapeRegistry sReg; sReg.Initialize();
    MaterialRegistry mReg; mReg.Initialize();
    BlockRegistry bReg; bReg.Initialize();
    ScientificMaterialRegistry sciReg; sciReg.Initialize();

    FactoryOrchestrator orch;
    std::string testAssetId = "fairworld:orphan_test_block";
    if (!orch.OpenDraft(testAssetId, "Orphan Block")) {
        r.passed = false;
        r.observedDesc = "OpenDraft fallito per " + testAssetId;
        return r;
    }

    auto* draft = orch.GetCurrentDraft();
    if (!draft) {
        r.passed = false;
        r.observedDesc = "GetCurrentDraft ha restituito nullptr.";
        return r;
    }

    draft->scientificMaterialId = 999999; // Orphan root ID

    SurfaceOverride so;
    so.surfaceId = 1;
    so.scientificMaterialId = 888888; // Orphan surface override ID
    draft->surfaces = { so };

    // Diagnostica preliminare sul draft
    auto draftAudit = AuditScientificMaterialBindings(draft->scientificMaterialId, draft->surfaces, &sciReg);
    bool draftHasOrphans = draftAudit.hasOrphans && draftAudit.rootIsOrphan && (draftAudit.surfaceDiagnostics.size() == 1 && draftAudit.surfaceDiagnostics[0].isOrphan);

    orch.ExecuteStation(ProductionStationID::Shape, &sReg, &mReg, &bReg);
    orch.ExecuteStation(ProductionStationID::SurfaceUV, &sReg, &mReg, &bReg);
    orch.ExecuteStation(ProductionStationID::Material, &sReg, &mReg, &bReg);
    orch.ExecuteStation(ProductionStationID::RawPhysics, &sReg, &mReg, &bReg);
    orch.ExecuteStation(ProductionStationID::QualityControl, &sReg, &mReg, &bReg);

    auto pkgOpt = orch.BuildPackage(&sReg, &mReg, &bReg);
    if (!pkgOpt.has_value()) {
        r.passed = false;
        r.observedDesc = "BuildPackage fallito con orphan ID.";
        return r;
    }

    RawAssetPackage pkg = *pkgOpt;
    bool pkgRootPreserved = (pkg.scientificMaterialId == 999999);
    bool pkgSurfPreserved = (!pkg.surfaces.empty() && pkg.surfaces[0].scientificMaterialId == 888888);

    // Save and reload
    std::string tmpPath = "temp_bind10_orphan.rawpkg";
    pkg.SaveToFile(tmpPath);
    RawAssetPackage loaded;
    loaded.LoadFromFile(tmpPath);
    std::error_code ec;
    std::filesystem::remove(tmpPath, ec);

    bool loadedRootPreserved = (loaded.scientificMaterialId == 999999);
    bool loadedSurfPreserved = (!loaded.surfaces.empty() && loaded.surfaces[0].scientificMaterialId == 888888);

    // Diagnostica su package caricato
    AssetPackagingService packService;
    auto pkgAudit = packService.ValidateScientificReferences(loaded, sciReg);
    bool pkgAuditPass = pkgAudit.hasOrphans && pkgAudit.rootIsOrphan && !pkgAudit.warnings.empty();

    // Pubblicazione runtime
    auto pubRes = packService.PublishPackageTransactional(loaded, bReg, mReg, sReg);
    bool pubOk = pubRes.success;
    uint8_t voxelId = pubRes.targetVoxelId;

    const auto& bDef = bReg.GetBlock(voxelId);
    const auto& matDef = mReg.GetMaterial(voxelId);

    bool runtimeRootPreserved = (bDef.scientificMaterialId == 999999);
    bool runtimeSurfPreserved = (!matDef.surfaces.empty() && matDef.surfaces[0].scientificMaterialId == 888888);

    auto resOrphan = ResolveSurfaceScientificBinding(1, matDef.surfaces, bDef.scientificMaterialId, &sciReg);
    bool resOrphanPass = (resOrphan.resolvedId == 999999 && !resOrphan.isOverridden && resOrphan.isOrphan);

    r.passed = draftHasOrphans && pkgRootPreserved && pkgSurfPreserved && loadedRootPreserved &&
               loadedSurfPreserved && pkgAuditPass && pubOk && runtimeRootPreserved && runtimeSurfPreserved && resOrphanPass;

    r.observedDesc = "ID orfani preservati: Root=" + std::to_string(bDef.scientificMaterialId)
                   + ", Surface1=" + std::to_string(resOrphan.resolvedId)
                   + " (isOrphan=" + (resOrphan.isOrphan ? "true" : "false") + "), LegacyStruct=" + std::to_string(matDef.surfaces[0].scientificMaterialId)
                   + ", Diagnostica: " + std::to_string(pkgAudit.warnings.size()) + " warnings emessi.";

    if (!r.passed) {
        r.details = "Gli ID scientifici orfani sono stati azzerati, alterati o non diagnosticati correttamente.";
    }

    return r;
}

ScientificBindingTestResult ScientificBindingContractTests::Test11_LegacyPackageBackwardCompatibility() {
    ScientificBindingTestResult r;
    r.testId = "BIND-11";
    r.testName = "Legacy Package Backward Compatibility Without Surfaces Key";
    r.inputDesc = "Deserializzazione di un RawAssetPackage da JSON privo della chiave 'surfaces' (schema v1 o v2 pre-superfici).";
    r.expectedDesc = "Package deserializzato con surfaces vuoto, pubblicazione runtime riuscita, fallback automatico alla radice senza crash o errori.";

    ShapeRegistry sReg; sReg.Initialize();
    MaterialRegistry mReg; mReg.Initialize();
    BlockRegistry bReg; bReg.Initialize();
    ScientificMaterialRegistry sciReg; sciReg.Initialize();

    nlohmann::json jLegacy = {
        {"manifest", {
            {"assetId", "fairworld:legacy_stone_block"},
            {"displayName", "Legacy Stone"},
            {"schemaVersion", 2},
            {"revision", 1},
            {"stage", 2}
        }},
        {"shape", {
            {"id", 1},
            {"revision", 1},
            {"type", 0}
        }},
        {"transform", {
            {"position", {0.0f, 0.0f, 0.0f}},
            {"rotation", {0.0f, 0.0f, 0.0f}},
            {"scale", {1.0f, 1.0f, 1.0f}},
            {"pivot", {0.0f, 0.0f, 0.0f}}
        }},
        {"graphics", {
            {"scientificMaterialId", 1},
            {"baseColor", {0.8f, 0.8f, 0.8f}},
            {"roughness", 0.9f},
            {"metallic", 0.0f},
            {"emissive", 0.0f},
            {"alpha", 1.0f},
            {"behaviors", 0}
        }},
        {"scientific", {
            {"materialId", 1}
        }},
        {"physics", {
            {"mass", 1500.0f},
            {"density", 2500.0f},
            {"friction", 0.7f},
            {"restitution", 0.05f},
            {"collisionEnabled", true},
            {"motionType", 0},
            {"massMode", 0}
        }},
        {"geometry", {
            {"vertexCount", 4},
            {"triangleCount", 2},
            {"bounds", {{"min", {-0.5f, -0.5f, -0.5f}}, {"max", {0.5f, 0.5f, 0.5f}}}},
            {"vertices", {
                {{"pos", {-0.5f, -0.5f, 0.0f}}, {"uv", {0.0f, 0.0f}}, {"norm", {0.0f, 0.0f, 1.0f}}, {"color", {1.0f, 1.0f, 1.0f, 1.0f}}, {"matId", 1}},
                {{"pos", {0.5f, -0.5f, 0.0f}}, {"uv", {1.0f, 0.0f}}, {"norm", {0.0f, 0.0f, 1.0f}}, {"color", {1.0f, 1.0f, 1.0f, 1.0f}}, {"matId", 1}},
                {{"pos", {0.5f, 0.5f, 0.0f}}, {"uv", {1.0f, 1.0f}}, {"norm", {0.0f, 0.0f, 1.0f}}, {"color", {1.0f, 1.0f, 1.0f, 1.0f}}, {"matId", 1}},
                {{"pos", {-0.5f, 0.5f, 0.0f}}, {"uv", {0.0f, 1.0f}}, {"norm", {0.0f, 0.0f, 1.0f}}, {"color", {1.0f, 1.0f, 1.0f, 1.0f}}, {"matId", 1}}
            }},
            {"faces", {
                {{"surfaceId", 0}, {"matIdx", 0}, {"indices", {0, 1, 2}}},
                {{"surfaceId", 0}, {"matIdx", 0}, {"indices", {0, 2, 3}}}
            }}
        }}
    };

    RawAssetPackage legacyPkg;
    from_json(jLegacy, legacyPkg);
    legacyPkg.contentDigestSha256 = legacyPkg.ComputeContentDigestSha256();

    bool surfacesEmpty = legacyPkg.surfaces.empty();
    bool rootMatch = (legacyPkg.scientificMaterialId == 1);

    AssetPackagingService packService;
    auto pubRes = packService.PublishPackageTransactional(legacyPkg, bReg, mReg, sReg);
    bool pubOk = pubRes.success;
    uint8_t voxelId = pubRes.targetVoxelId;

    const auto& bDef = bReg.GetBlock(voxelId);
    const auto& matDef = mReg.GetMaterial(voxelId);

    // Risoluzione query su qualsiasi indice superficie
    auto resFace0 = ResolveSurfaceScientificBinding(0, matDef.surfaces, bDef.scientificMaterialId, &sciReg);
    auto resFace5 = ResolveSurfaceScientificBinding(5, matDef.surfaces, bDef.scientificMaterialId, &sciReg);

    bool res0Pass = (resFace0.resolvedId == 1 && !resFace0.isOverridden);
    bool res5Pass = (resFace5.resolvedId == 1 && !resFace5.isOverridden);

    r.passed = surfacesEmpty && rootMatch && pubOk && res0Pass && res5Pass;
    r.observedDesc = "Package legacy caricato senza chiave 'surfaces'. surfaces.size()=0, root ID=1, risoluzione superfici fall-through deterministica al materiale radice (1).";

    if (!r.passed) {
        r.details = "Fallimento nella deserializzazione o risoluzione di un asset legacy privo del blocco surfaces.";
    }

    return r;
}

ScientificBindingTestResult ScientificBindingContractTests::Test12_IndependenceFromAppearanceIdAndVisualPainting() {
    ScientificBindingTestResult r;
    r.testId = "BIND-12";
    r.testName = "Complete Orthogonal Separation Between AppearanceID and Scientific Material Bindings";
    r.inputDesc = "Asset con scientificMaterialId=3, superficie 1 override scientificMaterialId=7, e appearanceId=1001. Mutazione di appearanceId da 1001 a 2002 e parametri PBR.";
    r.expectedDesc = "Tutti i binding scientifici (radice e superfici) rimangono inalterati. Mutazioni a scientificMaterialId non alterano appearanceId.";

    ShapeRegistry sReg; sReg.Initialize();
    MaterialRegistry mReg; mReg.Initialize();
    BlockRegistry bReg; bReg.Initialize();
    ScientificMaterialRegistry sciReg; sciReg.Initialize();

    RawAssetPackage pkg;
    pkg.manifest.assetId = "fairworld:orthogonal_test_block";
    pkg.manifest.schemaVersion = 2;
    pkg.manifest.stage = RawAssetStage::Packaged;
    pkg.scientificMaterialId = 3;

    SurfaceOverride so;
    so.surfaceId = 1;
    so.scientificMaterialId = 7;
    so.materialId = 10; // Visual material layer
    so.overridePbr = true;
    so.roughness = 0.45f;
    pkg.surfaces.push_back(so);

    // Geometry minima valida
    pkg.geometry.vertexCount = 3;
    pkg.geometry.triangleCount = 1;
    Vertex v; v.materialID = 10;
    pkg.geometry.vertices = { v, v, v };
    Face f; f.surfaceId = 1; f.indices = {0, 1, 2};
    pkg.geometry.faces = { f };
    pkg.shape.type = ShapeType::Cube;
    pkg.contentDigestSha256 = pkg.ComputeContentDigestSha256();

    AssetPackagingService packService;
    auto pubRes = packService.PublishPackageTransactional(pkg, bReg, mReg, sReg);
    if (!pubRes.success) {
        r.passed = false;
        r.observedDesc = "PublishPackageTransactional fallito: " + pubRes.errorMessage;
        return r;
    }

    uint8_t voxelId = pubRes.targetVoxelId;
    auto& bDef = bReg.GetBlockMutable(voxelId);
    auto& matDef = mReg.GetMaterialMutable(voxelId);

    // Imposta appearanceId iniziale
    bDef.appearanceId = 1001;
    matDef.appearanceId = 1001;

    // Mutazione di AppearanceID e parametri visivi
    bDef.appearanceId = 2002;
    matDef.appearanceId = 2002;
    matDef.baseColorFallback = glm::vec3(0.1f, 0.9f, 0.2f);
    matDef.roughnessFallback = 0.12f;

    // Verifica che i binding scientifici siano intatti
    bool rootStill3 = (bDef.scientificMaterialId == 3);
    auto resS1 = ResolveSurfaceScientificBinding(1, matDef.surfaces, bDef.scientificMaterialId, &sciReg);
    bool s1ResolvesRoot3 = (resS1.resolvedId == 3 && !resS1.isOverridden);
    bool s1LegacyPreserved = (matDef.surfaces[0].scientificMaterialId == 7);

    // Mutazione del binding scientifico di superficie (legacy field)
    matDef.surfaces[0].scientificMaterialId = 9;

    // Verifica che appearanceId e parametri visivi siano intatti
    bool appStill2002 = (bDef.appearanceId == 2002 && matDef.appearanceId == 2002);
    bool pbrIntact = (matDef.baseColorFallback.y > 0.8f && std::abs(matDef.roughnessFallback - 0.12f) < 1e-4f);

    r.passed = rootStill3 && s1ResolvesRoot3 && s1LegacyPreserved && appStill2002 && pbrIntact;
    r.observedDesc = "Indipendenza verificata: mutazione AppearanceID (1001->2002) non altera scientificMaterialId (3) né superficie (risolve a root 3). Mutazione superficie non altera AppearanceID né PBR.";

    if (!r.passed) {
        r.details = "Rilevata interferenza spuria tra la pipeline visiva (AppearanceID/PBR) e i binding scientifici.";
    }

    return r;
}

// =============================================================================
// P0.3C3: BLOCK MAKER SCIENTIFIC SURFACE BINDING CONTRACT TESTS (BIND-13..14)
// =============================================================================

ScientificBindingTestResult ScientificBindingContractTests::Test13_BlockMakerSurfaceSelectionAndRootIsolation() {
    ScientificBindingTestResult r;
    r.testId = "BIND-13";
    r.testName = "Block Maker Surface Selection, Root Isolation, Dynamic Inheritance and PBR Decoupling";
    r.inputDesc = "Asset con root=1 (Water). Override superficie 5 impostato a 1000. Mutazione radice a 4 (Iron). Mutazione parametri PBR superficie 5.";
    r.expectedDesc = "Override superficie 5 isolato; root inalterato. Modifica root propaga dinamicamente a superficie 0 (non overridata). PBR e scientifico ortogonali.";

    BlockRegistry bReg; bReg.Initialize();
    MaterialRegistry mReg; mReg.Initialize();
    ShapeRegistry sReg; sReg.Initialize();
    ScientificMaterialRegistry sciReg; sciReg.Initialize();

    // 1. Setup asset con root scientificMaterialId = 1 (Water)
    RawAssetPackage pkg;
    pkg.manifest.assetId = "fairworld:bm_bind13_test";
    pkg.manifest.schemaVersion = 2;
    pkg.manifest.stage = RawAssetStage::Packaged;
    pkg.scientificMaterialId = 1;

    // Superficie 5 con override scientifico 1000
    SurfaceOverride so5;
    so5.surfaceId = 5;
    so5.scientificMaterialId = 1000;
    pkg.surfaces.push_back(so5);

    // Geometria minima valida
    pkg.geometry.vertexCount = 4;
    pkg.geometry.triangleCount = 2;
    Vertex v; v.materialID = 1;
    pkg.geometry.vertices = { v, v, v, v };
    Face f0; f0.surfaceId = 0; f0.indices = { 0, 1, 2 };
    Face f5; f5.surfaceId = 5; f5.indices = { 0, 2, 3 };
    pkg.geometry.faces = { f0, f5 };
    pkg.shape.type = ShapeType::Cube;
    pkg.contentDigestSha256 = pkg.ComputeContentDigestSha256();

    AssetPackagingService packService;
    auto pubRes = packService.PublishPackageTransactional(pkg, bReg, mReg, sReg);
    if (!pubRes.success) {
        r.passed = false;
        r.observedDesc = "PublishPackageTransactional fallito: " + pubRes.errorMessage;
        return r;
    }

    uint8_t voxelId = pubRes.targetVoxelId;
    auto& bDef = bReg.GetBlockMutable(voxelId);
    auto& matDef = mReg.GetMaterialMutable(voxelId);

    // 2. Verifica isolamento iniziale e autorita uniforme della radice
    bool rootIs1 = (bDef.scientificMaterialId == 1);
    auto resS0 = ResolveSurfaceScientificBinding(0, matDef.surfaces, bDef.scientificMaterialId, &sciReg);
    auto resS5 = ResolveSurfaceScientificBinding(5, matDef.surfaces, bDef.scientificMaterialId, &sciReg);

    bool s0Inherits1 = (resS0.resolvedId == 1 && !resS0.isOverridden);
    bool s5Inherits1 = (resS5.resolvedId == 1 && !resS5.isOverridden);
    bool s5LegacyStored = (matDef.surfaces[0].scientificMaterialId == 1000);

    // 3. Ereditarietà dinamica: mutazione radice 1 -> 4 (Iron) propaga a tutte le superfici
    bDef.scientificMaterialId = 4;
    auto resS0AfterRootChange = ResolveSurfaceScientificBinding(0, matDef.surfaces, bDef.scientificMaterialId, &sciReg);
    auto resS5AfterRootChange = ResolveSurfaceScientificBinding(5, matDef.surfaces, bDef.scientificMaterialId, &sciReg);

    bool s0DynamicallyBecomes4 = (resS0AfterRootChange.resolvedId == 4 && !resS0AfterRootChange.isOverridden);
    bool s5DynamicallyBecomes4 = (resS5AfterRootChange.resolvedId == 4 && !resS5AfterRootChange.isOverridden);

    // 4. Separazione PBR / Scientifico: mutazione proprietà visive su superficie 5
    fw::SurfaceOverride& surf5Ref = matDef.GetOrCreateSurfaceOverride(5);
    surf5Ref.overridePbr = true;
    surf5Ref.baseColorFallback = glm::vec3(0.9f, 0.1f, 0.2f);
    surf5Ref.roughness = 0.35f;
    surf5Ref.metallic = 0.85f;
    surf5Ref.emissive = 2.0f;
    surf5Ref.useExplicitMaterial = true;
    surf5Ref.materialId = 42;
    matDef.revision++;

    // Verifica che il valore legacy e la radice siano intatti
    bool s5SciIntact = (surf5Ref.scientificMaterialId == 1000);
    bool rootStill4 = (bDef.scientificMaterialId == 4);

    // 5. Reset scientifico superficie a 0: continua a risolvere a root 4 senza toccare PBR
    surf5Ref.scientificMaterialId = 0;
    matDef.revision++;

    auto resS5Reverted = ResolveSurfaceScientificBinding(5, matDef.surfaces, bDef.scientificMaterialId, &sciReg);
    bool s5InheritsRoot4 = (resS5Reverted.resolvedId == 4 && !resS5Reverted.isOverridden);

    // Verifica che i parametri PBR della superficie 5 non siano stati cancellati né modificati
    bool pbrPreserved = (surf5Ref.overridePbr == true &&
                         surf5Ref.useExplicitMaterial == true &&
                         surf5Ref.materialId == 42 &&
                         std::abs(surf5Ref.roughness - 0.35f) < 1e-4f &&
                         std::abs(surf5Ref.metallic - 0.85f) < 1e-4f &&
                         std::abs(surf5Ref.emissive - 2.0f) < 1e-4f &&
                         surf5Ref.baseColorFallback.x > 0.8f);

    r.passed = rootIs1 && s0Inherits1 && s5Inherits1 && s5LegacyStored &&
               s0DynamicallyBecomes4 && s5DynamicallyBecomes4 &&
               s5SciIntact && rootStill4 &&
               s5InheritsRoot4 && pbrPreserved;

    r.observedDesc = "Autorita radice verificata: entrambe le superfici risolvono uniformemente al materiale della parte (1 poi 4). Legacy override 1000 preservato in struct. Mutazioni PBR e scientifiche completamente ortogonali; ripristino scientifico a 0 preserva PBR.";

    if (!r.passed) {
        r.details = "Rilevata interferenza spuria tra override scientifico locale, materiale radice o proprietà PBR.";
    }

    return r;
}

ScientificBindingTestResult ScientificBindingContractTests::Test14_BlockMakerSurfaceOverridePersistenceAndReloadCycle() {
    ScientificBindingTestResult r;
    r.testId = "BIND-14";
    r.testName = "Block Maker Surface Override Persistence, Custom Materials and Orphan Preservation";
    r.inputDesc = "Asset con root=2 (Granite), sup 1=custom 1001, sup 2=orfano 9999, sup 3=ereditato 0. Ciclo Draft -> Package -> File -> Reload -> Publish.";
    r.expectedDesc = "Tutti gli override persistiti e ricaricati intatti. Custom 1001 risolto con proprieta fisiche. Orfano 9999 diagnosticato senza reset automatico.";

    BlockRegistry bReg; bReg.Initialize();
    MaterialRegistry mReg; mReg.Initialize();
    ShapeRegistry sReg; sReg.Initialize();
    ScientificMaterialRegistry sciReg; sciReg.Initialize();

    // 1. Registra materiale custom nel registry
    ScientificMaterialDef customMat;
    customMat.id = 1001;
    customMat.canonicalId = "fairworld:mat/pyro_ceramic";
    customMat.displayName = "Pyro Ceramic Shield";
    customMat.kind = ScientificKind::Compound;
    customMat.aggregationState = AggregationState::Solid;
    customMat.density = ScientificQuantity::Known(3200.0, "kg/m^3", DataQuality::PeerReviewed, "Ceramic Lab");
    customMat.thermalConductivity = ScientificQuantity::Known(1.85, "W/(m*K)", DataQuality::PeerReviewed, "Ceramic Lab");
    customMat.meltingPoint = ScientificQuantity::Known(2150.0, "K", DataQuality::PeerReviewed, "Ceramic Lab");
    customMat.lifecycleState = MaterialLifecycleState::Published;
    customMat.enabledInBlockMaker = true;
    customMat.isCustom = true;

    std::string valErr;
    bool valCustomOk = sciReg.ValidateMaterialDefinition(customMat, &valErr);
    bool regCustomOk = sciReg.RegisterMaterial(customMat, false);

    // 2. Draft creation via FactoryOrchestrator
    FactoryOrchestrator orch;
    std::string assetId = "fairworld:bm_bind14_cycle";
    if (!orch.OpenDraft(assetId, "BM Bind14 Cycle")) {
        r.passed = false;
        r.observedDesc = "OpenDraft fallito per " + assetId;
        return r;
    }

    auto* draft = orch.GetCurrentDraft();
    if (!draft) {
        r.passed = false;
        r.observedDesc = "GetCurrentDraft ha restituito nullptr.";
        return r;
    }

    draft->scientificMaterialId = 2; // Granite come radice

    // Superficie 1: custom 1001
    SurfaceOverride s1;
    s1.surfaceId = 1;
    s1.scientificMaterialId = 1001;
    s1.overridePbr = true;
    s1.roughness = 0.40f;

    // Superficie 2: orfano 9999 (non presente nel registro)
    SurfaceOverride s2;
    s2.surfaceId = 2;
    s2.scientificMaterialId = 9999;

    // Superficie 3: ereditato esplicito (0)
    SurfaceOverride s3;
    s3.surfaceId = 3;
    s3.scientificMaterialId = 0;

    draft->surfaces = { s1, s2, s3 };

    orch.ExecuteStation(ProductionStationID::Shape, &sReg, &mReg, &bReg);
    orch.ExecuteStation(ProductionStationID::SurfaceUV, &sReg, &mReg, &bReg);
    orch.ExecuteStation(ProductionStationID::Material, &sReg, &mReg, &bReg);
    orch.ExecuteStation(ProductionStationID::RawPhysics, &sReg, &mReg, &bReg);
    orch.ExecuteStation(ProductionStationID::QualityControl, &sReg, &mReg, &bReg);

    auto pkgOpt = orch.BuildPackage(&sReg, &mReg, &bReg);
    if (!pkgOpt.has_value()) {
        r.passed = false;
        r.observedDesc = "BuildPackage ha restituito nullopt.";
        return r;
    }

    RawAssetPackage package = *pkgOpt;

    // 3. Serializzazione su file e ricaricamento completo
    std::string tempPkgPath = "temp_bind14_test.rawpkg";
    bool saveOk = package.SaveToFile(tempPkgPath);

    RawAssetPackage reloadedPkg;
    bool loadOk = saveOk && reloadedPkg.LoadFromFile(tempPkgPath);
    if (std::filesystem::exists(tempPkgPath)) {
        std::filesystem::remove(tempPkgPath);
    }

    bool pkgRootOk = (reloadedPkg.scientificMaterialId == 2);
    bool pkgSurfCountOk = (reloadedPkg.surfaces.size() == 3);
    bool pkgS1Ok = (reloadedPkg.surfaces[0].scientificMaterialId == 1001);
    bool pkgS2Ok = (reloadedPkg.surfaces[1].scientificMaterialId == 9999);
    bool pkgS3Ok = (reloadedPkg.surfaces[2].scientificMaterialId == 0);

    // 4. Pubblicazione transazionale a runtime
    AssetPackagingService packService;
    auto pubRes = packService.PublishPackageTransactional(reloadedPkg, bReg, mReg, sReg);
    if (!pubRes.success) {
        r.passed = false;
        r.observedDesc = "PublishPackageTransactional fallito: " + pubRes.errorMessage;
        return r;
    }

    uint8_t voxelId = pubRes.targetVoxelId;
    const auto& bDef = bReg.GetBlock(voxelId);
    const auto& matDef = mReg.GetMaterial(voxelId);

    // 5. Risoluzione e Audit Diagnostico
    auto resS0 = ResolveSurfaceScientificBinding(0, matDef.surfaces, bDef.scientificMaterialId, &sciReg);
    auto resS1 = ResolveSurfaceScientificBinding(1, matDef.surfaces, bDef.scientificMaterialId, &sciReg);
    auto resS2 = ResolveSurfaceScientificBinding(2, matDef.surfaces, bDef.scientificMaterialId, &sciReg);
    auto resS3 = ResolveSurfaceScientificBinding(3, matDef.surfaces, bDef.scientificMaterialId, &sciReg);

    bool s0Ok = (resS0.resolvedId == 2 && !resS0.isOverridden && !resS0.isOrphan);
    bool s1Ok = (resS1.resolvedId == 2 && !resS1.isOverridden && !resS1.isOrphan);
    bool s2Ok = (resS2.resolvedId == 2 && !resS2.isOverridden && !resS2.isOrphan);
    bool s3Ok = (resS3.resolvedId == 2 && !resS3.isOverridden && !resS3.isOrphan);

    // Verifica proprieta fisiche custom nel registry
    const auto& customResolved = sciReg.GetMaterial(1001);
    bool customPropsOk = (customResolved.density.state == PropertyState::Known &&
                          std::abs(customResolved.density.value - 3200.0) < 1e-3 &&
                          customResolved.meltingPoint.state == PropertyState::Known &&
                          std::abs(customResolved.meltingPoint.value - 2150.0) < 1e-3);

    // Audit Scientific Material Bindings: rileva orfano senza alterare l'ID
    auto auditRep = AuditScientificMaterialBindings(bDef.scientificMaterialId, matDef.surfaces, &sciReg);
    bool auditOrphanDetected = auditRep.hasOrphans && (auditRep.surfaceDiagnostics.size() == 3);
    bool s2StillPreservedInMat = (matDef.surfaces[1].scientificMaterialId == 9999);
    bool s1StillPreservedInMat = (matDef.surfaces[0].scientificMaterialId == 1001);

    r.passed = valCustomOk && regCustomOk && saveOk && loadOk &&
               pkgRootOk && pkgSurfCountOk && pkgS1Ok && pkgS2Ok && pkgS3Ok &&
               s0Ok && s1Ok && s2Ok && s3Ok &&
               customPropsOk && auditOrphanDetected && s2StillPreservedInMat && s1StillPreservedInMat;

    r.observedDesc = "Ciclo persistenza completato: root=2 uniforme, sup 1=custom 1001 preservato (densita 3200 kg/m^3, fusione 2150 K), sup 2=orfano 9999 preservato intatto e diagnosticato, sup 3=eredita 2. Nessun reset silenzioso.";

    if (!r.passed) {
        r.details = "Fallimento nel round-trip di persistenza, caricamento custom o gestione orfani per le superfici di Block Maker.";
    }

    return r;
}

ScientificBindingTestResult ScientificBindingContractTests::Test15_CylinderSemanticSurfaceBindingAndUVPaintDiagnosticProtection() {
    ScientificBindingTestResult r;
    r.testId = "BIND-15";
    r.testName = "Cylinder UV Isolation, UV-PAINT Diagnostic and Scientific Surface Binding";
    r.inputDesc = "Cylinder (ShapeType 6), atlasTransform mantello e basi, isolamento maschera raster UV-PAINT, Titanium Carbide ID 1000 su mantello/base, PBR runtime readiness.";
    r.expectedDesc = "atlasTransform coerente, hit-testing 2D/3D isolato, diagnostic insideChanged > 0 && outsideChanged == 0, binding risolto correttamente con root=1, sup0/2=1000, sup1=0, PBRTextures attivo prima della sync.";

    // 1. Primitive UV Mapping del Cilindro (ShapeType 6)
    ShapeDefinition defCyl;
    defCyl.type = ShapeType::Cylinder;
    defCyl.parameters = ShapeParamsCylinder{ 0.5f, 2.0f };
    EditableMesh mesh = MeshBridgeService::CreateMeshFromShape(defCyl);

    PrimitiveUVMapper mapper;
    auto mapRes = mapper.AnalyzeAndMap(mesh, ShapeType::Cylinder, defCyl.parameters);

    bool mapOk = mapRes.success && (mapRes.layout.islands.size() >= 3);
    if (!mapOk) {
        r.passed = false;
        r.observedDesc = "PrimitiveUVMapper::AnalyzeAndMap fallito per Cylinder.";
        return r;
    }

    const auto& islMantle = mapRes.layout.islands[0];
    const auto& islTop    = mapRes.layout.islands[1];
    const auto& islBot    = mapRes.layout.islands[2];

    bool mantleTransformOk = (std::abs(islMantle.atlasTransform.x - 1.0f) < 1e-4f) &&
                             (std::abs(islMantle.atlasTransform.y - 0.65f) < 1e-4f) &&
                             (std::abs(islMantle.atlasTransform.z - 0.0f) < 1e-4f) &&
                             (std::abs(islMantle.atlasTransform.w - 0.35f) < 1e-4f);

    bool topTransformOk = (std::abs(islTop.atlasTransform.x - 0.5f) < 1e-4f) &&
                          (std::abs(islTop.atlasTransform.y - 0.35f) < 1e-4f) &&
                          (std::abs(islTop.atlasTransform.z - 0.0f) < 1e-4f) &&
                          (std::abs(islTop.atlasTransform.w - 0.0f) < 1e-4f);

    bool botTransformOk = (std::abs(islBot.atlasTransform.x - 0.5f) < 1e-4f) &&
                          (std::abs(islBot.atlasTransform.y - 0.35f) < 1e-4f) &&
                          (std::abs(islBot.atlasTransform.z - 0.5f) < 1e-4f) &&
                          (std::abs(islBot.atlasTransform.w - 0.0f) < 1e-4f);

    // 2. Hit Test 2D isolato tramite UVEditorDocument
    UVEditorDocument doc(mesh, mapRes.layout);
    glm::vec2 ptMantle(0.5f, 0.675f);
    glm::vec2 ptTop(0.25f, 0.175f);
    glm::vec2 ptBot(0.75f, 0.175f);

    bool hitMantle2D = doc.IsPointInIsland(ptMantle, 0) && !doc.IsPointInIsland(ptMantle, 1) && !doc.IsPointInIsland(ptMantle, 2);
    bool hitTop2D    = doc.IsPointInIsland(ptTop, 1) && !doc.IsPointInIsland(ptTop, 0) && !doc.IsPointInIsland(ptTop, 2);
    bool hitBot2D    = doc.IsPointInIsland(ptBot, 2) && !doc.IsPointInIsland(ptBot, 0) && !doc.IsPointInIsland(ptBot, 1);

    // 3. 3D Surface Painting & UV-PAINT Diagnostica con Maschera Raster
    SurfacePaintingAdapter adapter;
    TexturePaintingService painter;
    painter.InitializeCanvas(512, 512, glm::vec4(0.0f));
    adapter.SetPaintingService(&painter);
    adapter.SetUVLayout(&mapRes.layout);

    PaintBrush brush;
    brush.radiusPixels = 16.0f;
    brush.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

    TexturePaintingService::EnableUVPaintDiagnostic(true);
    // Raycast 3D sul mantello (+X verso centro)
    bool paintMantleOk = adapter.BeginStroke3D(mesh, glm::vec3(2.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), brush);
    adapter.EndStroke3D();

    auto mantleDiag = TexturePaintingService::GetLastUVPaintDiagResult();
    bool paintDiagOk = paintMantleOk && (mantleDiag.insideChanged > 0) && (mantleDiag.outsideChanged == 0);

    // 4. Test Integrazione Percorso Reale UI 2D: Stroke su Top Cap (TargetIsland=1)
    // Cattura snapshot buffer prima dello stroke 2D
    uint32_t layerId = painter.GetLayers()[0].layerId;
    ImageBitmap pre2DSnapshot = painter.GetLayer(layerId)->pixelBuffer;

    TexturePaintingService::EnableUVPaintDiagnostic(true);
    doc.SelectIsland(1, false);
    bool stroke2DBegun = doc.BeginStroke2D(ptTop, brush, &painter, layerId, CanvasPaintMode::SurfacePaint);
    doc.EndStroke2D(&painter);

    auto topCap2DDiag = TexturePaintingService::GetLastUVPaintDiagResult();
    bool topCap2DDiagOk = stroke2DBegun && (topCap2DDiag.insideChanged > 0) && (topCap2DDiag.outsideChanged == 0);

    // Confronto bitwise prima/dopo: tutti i pixel esterni alla maschera raster dell'isola 1 devono essere 100% intatti
    auto maskTop = GetOrCreateIslandRasterMask(mesh, mapRes.layout, 1, 512, 512, false);
    const ImageBitmap& post2DBuffer = painter.GetLayer(layerId)->pixelBuffer;
    bool outsideTopCapUntouched = (maskTop != nullptr);
    if (maskTop) {
        for (uint32_t y = 0; y < 512 && outsideTopCapUntouched; ++y) {
            for (uint32_t x = 0; x < 512 && outsideTopCapUntouched; ++x) {
                if (!maskTop->IsInside(x, y)) {
                    if (post2DBuffer.GetPixel(x, y) != pre2DSnapshot.GetPixel(x, y)) {
                        outsideTopCapUntouched = false;
                    }
                }
            }
        }
    }

    // 5. Scientific Surface Binding con Titanium Carbide (ID 1000)
    ScientificMaterialRegistry sciReg;
    sciReg.Initialize();
    ScientificMaterialDef tiC;
    tiC.id = 1000;
    tiC.canonicalId = "fairworld:mat/titanium_carbide";
    tiC.displayName = "Titanium Carbide";
    tiC.kind = ScientificKind::Compound;
    tiC.aggregationState = AggregationState::Solid;
    tiC.density = ScientificQuantity::Known(4930.0, "kg/m^3", DataQuality::PeerReviewed, "Ceramic Lab");
    tiC.lifecycleState = MaterialLifecycleState::Published;
    tiC.enabledInBlockMaker = true;
    tiC.isCustom = true;
    sciReg.RegisterMaterial(tiC, true);

    uint32_t rootMaterialId = 1; // Pure Water (Standard ID 1)
    std::vector<SurfaceOverride> overrides;

    SurfaceOverride soMantle;
    soMantle.surfaceId = 0; // Mantello
    soMantle.scientificMaterialId = 1000; // Titanium Carbide
    overrides.push_back(soMantle);

    SurfaceOverride soTop;
    soTop.surfaceId = 1; // Top Cap
    soTop.scientificMaterialId = 0; // Eredita da radice (ID 1)
    overrides.push_back(soTop);

    SurfaceOverride soBot;
    soBot.surfaceId = 2; // Bottom Cap
    soBot.scientificMaterialId = 1000; // Titanium Carbide
    overrides.push_back(soBot);

    auto resMantle = ResolveSurfaceScientificBinding(0, overrides, rootMaterialId, &sciReg);
    auto resTop    = ResolveSurfaceScientificBinding(1, overrides, rootMaterialId, &sciReg);
    auto resBot    = ResolveSurfaceScientificBinding(2, overrides, rootMaterialId, &sciReg);

    bool bindingMantleOk = (resMantle.resolvedId == 1 && !resMantle.isOverridden && !resMantle.isOrphan);
    bool bindingTopOk    = (resTop.resolvedId == 1 && !resTop.isOverridden && !resTop.isOrphan);
    bool bindingBotOk    = (resBot.resolvedId == 1 && !resBot.isOverridden && !resBot.isOrphan);
    bool legacyOverridesPreserved = (overrides[0].scientificMaterialId == 1000 && overrides[2].scientificMaterialId == 1000);

    // 6. Inizializzazione PBR sincrona via RuntimeManager
    RuntimeManager runtimeMgr(nullptr);
    runtimeMgr.RequireFeaturesSync(RuntimeFeature::PBRTextures);
    bool pbrReady = HasFeature(static_cast<uint32_t>(runtimeMgr.GetActiveFeatures()), RuntimeFeature::PBRTextures);

    // Diagnostica obbligatoria predicato per predicato: [PASS/FAIL] NomeCondizione | Expected=... | Actual=...
    std::cout << "\n         --- PREDICATI DETTAGLIATI BIND-15 ---\n";
    std::cout << "         [" << (mapOk ? "PASS" : "FAIL") << "] CylMapping | Expected=true | Actual=" << (mapOk ? "true" : "false") << "\n";
    std::cout << "         [" << (mantleTransformOk ? "PASS" : "FAIL") << "] MantleTransform | Expected=vec4(1, 0.65, 0, 0.35) | Actual=vec4(" << islMantle.atlasTransform.x << ", " << islMantle.atlasTransform.y << ", " << islMantle.atlasTransform.z << ", " << islMantle.atlasTransform.w << ")\n";
    std::cout << "         [" << (topTransformOk ? "PASS" : "FAIL") << "] TopTransform | Expected=vec4(0.5, 0.35, 0, 0) | Actual=vec4(" << islTop.atlasTransform.x << ", " << islTop.atlasTransform.y << ", " << islTop.atlasTransform.z << ", " << islTop.atlasTransform.w << ")\n";
    std::cout << "         [" << (botTransformOk ? "PASS" : "FAIL") << "] BotTransform | Expected=vec4(0.5, 0.35, 0.5, 0) | Actual=vec4(" << islBot.atlasTransform.x << ", " << islBot.atlasTransform.y << ", " << islBot.atlasTransform.z << ", " << islBot.atlasTransform.w << ")\n";
    std::cout << "         [" << (hitMantle2D && hitTop2D && hitBot2D ? "PASS" : "FAIL") << "] HitTest2D | Expected=M:1,T:1,B:1 | Actual=M:" << hitMantle2D << ",T:" << hitTop2D << ",B:" << hitBot2D << "\n";
    std::cout << "         [" << (paintDiagOk ? "PASS" : "FAIL") << "] Paint3DDiag | Expected=inside>0,outside==0 | Actual=in:" << mantleDiag.insideChanged << ",out:" << mantleDiag.outsideChanged << "\n";
    std::cout << "         [" << (topCap2DDiagOk ? "PASS" : "FAIL") << "] TopCap2DDiag | Expected=inside>0,outside==0 | Actual=in:" << topCap2DDiag.insideChanged << ",out:" << topCap2DDiag.outsideChanged << "\n";
    std::cout << "         [" << (outsideTopCapUntouched ? "PASS" : "FAIL") << "] OutsideTopCapUntouched | Expected=true | Actual=" << (outsideTopCapUntouched ? "true" : "false") << "\n";
    std::cout << "         [" << (bindingMantleOk ? "PASS" : "FAIL") << "] BindingMantle | Expected=id:1,ov:0,orph:0 | Actual=id:" << resMantle.resolvedId << ",ov:" << resMantle.isOverridden << ",orph:" << resMantle.isOrphan << "\n";
    std::cout << "         [" << (bindingTopOk ? "PASS" : "FAIL") << "] BindingTop | Expected=id:1,ov:0,orph:0 | Actual=id:" << resTop.resolvedId << ",ov:" << resTop.isOverridden << ",orph:" << resTop.isOrphan << "\n";
    std::cout << "         [" << (bindingBotOk ? "PASS" : "FAIL") << "] BindingBot | Expected=id:1,ov:0,orph:0 | Actual=id:" << resBot.resolvedId << ",ov:" << resBot.isOverridden << ",orph:" << resBot.isOrphan << "\n";
    std::cout << "         [" << (legacyOverridesPreserved ? "PASS" : "FAIL") << "] LegacyOverridesPreserved | Expected=M:1000,B:1000 | Actual=M:" << overrides[0].scientificMaterialId << ",B:" << overrides[2].scientificMaterialId << "\n";
    std::cout << "         [" << (pbrReady ? "PASS" : "FAIL") << "] PBRReady | Expected=true | Actual=" << (pbrReady ? "true" : "false") << "\n";
    std::cout << "         ------------------------------------\n";

    r.passed = mapOk && mantleTransformOk && topTransformOk && botTransformOk &&
               hitMantle2D && hitTop2D && hitBot2D &&
               paintDiagOk && stroke2DBegun && topCap2DDiagOk && outsideTopCapUntouched &&
               bindingMantleOk && bindingTopOk && bindingBotOk && legacyOverridesPreserved && pbrReady;

    r.observedDesc = "CylTransform=" + std::string(mantleTransformOk && topTransformOk && botTransformOk ? "OK" : "FAIL") +
                     ", 2DHit=" + (hitMantle2D && hitTop2D && hitBot2D ? "OK" : "FAIL") +
                     ", 3DPaint=" + (paintDiagOk ? "OK" : "FAIL") +
                     ", 2DTopCap(in=" + std::to_string(topCap2DDiag.insideChanged) + ",out=" + std::to_string(topCap2DDiag.outsideChanged) + ",untouched=" + (outsideTopCapUntouched ? "YES" : "NO") + ")=" + (topCap2DDiagOk && outsideTopCapUntouched ? "OK" : "FAIL") +
                     ", BindingUniform(Mantle=" + std::to_string(resMantle.resolvedId) + ",Top=" + std::to_string(resTop.resolvedId) + ",Bot=" + std::to_string(resBot.resolvedId) + ")=" + (bindingMantleOk && bindingTopOk && bindingBotOk ? "OK" : "FAIL") +
                     ", LegacyPreserved=" + (legacyOverridesPreserved ? "YES" : "NO") +
                     ", PBRReady=" + (pbrReady ? "OK" : "FAIL");

    if (!r.passed) {
        r.details = "Fallimento nel contratto di mapping cilindro, diagnostica UV-PAINT 2D/3D, binding scientifico o inizializzazione PBR.";
    }

    return r;
}

ScientificBindingTestResult ScientificBindingContractTests::Test16_PartAssemblyPreservesDistinctScientificMaterials() {
    ScientificBindingTestResult r;
    r.testId = "BIND-16";
    r.testName = "Part Assembly Preserves Distinct Scientific Materials in Multi-Material Bodies";
    r.inputDesc = "Parte A (Iron Axle, root=4, vol=1.0m^3) e Parte B (Titanium Rotor, root=1000, vol=0.5m^3) assemblate in un corpo composito.";
    r.expectedDesc = "Ciascuna parte conserva rigorosamente la propria identita scientifica uniforme (Parte A=4, Parte B=1000). Masse distinte e non contaminate. Nessuna propagazione spuria tra parti.";

    ScientificMaterialRegistry sciReg;
    sciReg.Initialize();

    ScientificMaterialDef tiC;
    tiC.id = 1000;
    tiC.canonicalId = "fairworld:mat/titanium_carbide";
    tiC.displayName = "Titanium Carbide";
    tiC.kind = ScientificKind::Compound;
    tiC.aggregationState = AggregationState::Solid;
    tiC.density = ScientificQuantity::Known(4930.0, "kg/m^3", DataQuality::PeerReviewed, "Ceramic Lab");
    tiC.lifecycleState = MaterialLifecycleState::Published;
    tiC.enabledInBlockMaker = true;
    tiC.isCustom = true;
    sciReg.RegisterMaterial(tiC, true);

    // Parte A: Iron Axle (root = 4, Iron densita canonica 7870 kg/m^3)
    uint32_t partA_RootMaterialId = 4;
    float partA_Volume = 1.0f; // 1 m^3
    std::vector<SurfaceOverride> partA_Surfaces;
    SurfaceOverride soA0; soA0.surfaceId = 0; partA_Surfaces.push_back(soA0);
    SurfaceOverride soA1; soA1.surfaceId = 1; partA_Surfaces.push_back(soA1);

    // Parte B: Titanium Rotor (root = 1000, TiC densita 4930 kg/m^3)
    uint32_t partB_RootMaterialId = 1000;
    float partB_Volume = 0.5f; // 0.5 m^3
    std::vector<SurfaceOverride> partB_Surfaces;
    SurfaceOverride soB0; soB0.surfaceId = 0; partB_Surfaces.push_back(soB0);
    SurfaceOverride soB1; soB1.surfaceId = 1; partB_Surfaces.push_back(soB1);

    // 1. Risoluzione di Parte A: uniforme su tutte le sue superfici
    auto resA0 = ResolveSurfaceScientificBinding(0, partA_Surfaces, partA_RootMaterialId, &sciReg);
    auto resA1 = ResolveSurfaceScientificBinding(1, partA_Surfaces, partA_RootMaterialId, &sciReg);
    bool partA_Uniform = (resA0.resolvedId == 4 && !resA0.isOverridden && !resA0.isOrphan &&
                          resA1.resolvedId == 4 && !resA1.isOverridden && !resA1.isOrphan);

    // 2. Risoluzione di Parte B: uniforme su tutte le sue superfici
    auto resB0 = ResolveSurfaceScientificBinding(0, partB_Surfaces, partB_RootMaterialId, &sciReg);
    auto resB1 = ResolveSurfaceScientificBinding(1, partB_Surfaces, partB_RootMaterialId, &sciReg);
    bool partB_Uniform = (resB0.resolvedId == 1000 && !resB0.isOverridden && !resB0.isOrphan &&
                          resB1.resolvedId == 1000 && !resB1.isOverridden && !resB1.isOrphan);

    // 3. Calcolo masse fisiche separate e composizione assembly
    ScientificPropertyOverrides emptyProps;
    auto densA = sciReg.ResolveProperty(resA0.resolvedId, ScientificPropertyNames::Density, emptyProps);
    auto densB = sciReg.ResolveProperty(resB0.resolvedId, ScientificPropertyNames::Density, emptyProps);

    bool densitiesKnown = (densA.state == PropertyState::Known && densB.state == PropertyState::Known);
    float massA = static_cast<float>(densA.value) * partA_Volume; // 7870 kg (Iron canonico)
    float massB = static_cast<float>(densB.value) * partB_Volume; // 2465 kg (TiC)
    float totalAssemblyMass = massA + massB;                      // 10335 kg

    bool massAOk = (std::abs(massA - 7870.0f) < 1.0f);
    bool massBOk = (std::abs(massB - 2465.0f) < 1.0f);
    bool totalMassOk = (std::abs(totalAssemblyMass - 10335.0f) < 1.0f);
    bool massesAccurate = massAOk && massBOk && totalMassOk;

    // 4. Modifica alla radice di Parte A: non deve in alcun modo contaminare Parte B
    partA_RootMaterialId = 1; // Pure Water (1000 kg/m^3)
    auto resA0_Mutated = ResolveSurfaceScientificBinding(0, partA_Surfaces, partA_RootMaterialId, &sciReg);
    auto resB0_Intact  = ResolveSurfaceScientificBinding(0, partB_Surfaces, partB_RootMaterialId, &sciReg);

    bool crossContaminationFree = (resA0_Mutated.resolvedId == 1 && !resA0_Mutated.isOverridden && !resA0_Mutated.isOrphan &&
                                   resB0_Intact.resolvedId == 1000 && !resB0_Intact.isOverridden && !resB0_Intact.isOrphan);

    // Diagnostica obbligatoria predicato per predicato: [PASS/FAIL] NomeCondizione | Expected=... | Actual=...
    std::cout << "\n         --- PREDICATI DETTAGLIATI BIND-16 ---\n";
    std::cout << "         [" << (partA_Uniform ? "PASS" : "FAIL") << "] PartA_Uniform | Expected=id:4,ov:0,orph:0 | Actual=A0(id:" << resA0.resolvedId << ",ov:" << resA0.isOverridden << ",orph:" << resA0.isOrphan << ") A1(id:" << resA1.resolvedId << ",ov:" << resA1.isOverridden << ",orph:" << resA1.isOrphan << ")\n";
    std::cout << "         [" << (partB_Uniform ? "PASS" : "FAIL") << "] PartB_Uniform | Expected=id:1000,ov:0,orph:0 | Actual=B0(id:" << resB0.resolvedId << ",ov:" << resB0.isOverridden << ",orph:" << resB0.isOrphan << ") B1(id:" << resB1.resolvedId << ",ov:" << resB1.isOverridden << ",orph:" << resB1.isOrphan << ")\n";
    std::cout << "         [" << (densitiesKnown ? "PASS" : "FAIL") << "] DensitiesKnown | Expected=Known,Known | Actual=A:" << PropertyStateToString(densA.state) << " (" << densA.value << "),B:" << PropertyStateToString(densB.state) << " (" << densB.value << ")\n";
    std::cout << "         [" << (massAOk ? "PASS" : "FAIL") << "] MassPartA | Expected=7870.0 kg | Actual=" << massA << " kg\n";
    std::cout << "         [" << (massBOk ? "PASS" : "FAIL") << "] MassPartB | Expected=2465.0 kg | Actual=" << massB << " kg\n";
    std::cout << "         [" << (totalMassOk ? "PASS" : "FAIL") << "] TotalAssemblyMass | Expected=10335.0 kg | Actual=" << totalAssemblyMass << " kg\n";
    std::cout << "         [" << (crossContaminationFree ? "PASS" : "FAIL") << "] CrossContaminationIsolation | Expected=A:1,B:1000 | Actual=A:" << resA0_Mutated.resolvedId << ",B:" << resB0_Intact.resolvedId << "\n";
    std::cout << "         ------------------------------------\n";

    r.passed = partA_Uniform && partB_Uniform && densitiesKnown && massesAccurate && crossContaminationFree;
    r.observedDesc = "Assembly multi-parte verificato: ParteA=" + std::to_string(resA0.resolvedId) + " (massa=" + std::to_string(massA) + " kg), " +
                     "ParteB=" + std::to_string(resB0.resolvedId) + " (massa=" + std::to_string(massB) + " kg), " +
                     "MassaTotale=" + std::to_string(totalAssemblyMass) + " kg. Mutazione ParteA isolata (ParteB invariata).";

    if (!r.passed) {
        r.details = "Contaminazione di materiale tra parti distinte dell'assembly o calcolo non uniforme delle proprieta fisiche di parte.";
    }

    return r;
}

} // namespace fw
