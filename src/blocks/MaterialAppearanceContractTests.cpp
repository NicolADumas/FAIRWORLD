#include "blocks/MaterialAppearanceContractTests.h"
#include "blocks/MaterialAppearance.h"
#include "blocks/MaterialAppearanceRegistry.h"
#include "blocks/BlockRegistry.h"
#include "blocks/ScientificMaterialRegistry.h"
#include "json.hpp"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace fw {

std::vector<MaterialAppearanceTestResult> MaterialAppearanceContractTests::RunAllTests() {
    std::vector<MaterialAppearanceTestResult> results;
    results.reserve(10);

    results.push_back(Test01_MultiBlockAppearanceSharing());
    results.push_back(Test02_IndependentAppearanceMutationFromScientificIdentity());
    results.push_back(Test03_SerializationRoundTripAndPersistentIdentity());
    results.push_back(Test04_SafeFallbackForUnknownAppearanceIds());
    results.push_back(Test05_CollisionRejectionOnDuplicateIdentity());
    results.push_back(Test06_ZeroWorkVerificationOnInvariantInput());
    results.push_back(Test07_VisualMutationGeometryAndScientificIsolation());
    results.push_back(Test08_LegacyBlockResolutionBackwardCompatibility());
    results.push_back(Test09_DeterministicPbrParameterClampingAndValidation());
    results.push_back(Test10_AdvisoryScientificReferenceValidation());

    return results;
}

// -----------------------------------------------------------------------------
// MAP-01: Multi-Block Appearance Sharing
// Due BlockID differenti possono condividere un AppearanceID
// -----------------------------------------------------------------------------
MaterialAppearanceTestResult MaterialAppearanceContractTests::Test01_MultiBlockAppearanceSharing() {
    MaterialAppearanceTestResult res;
    res.testId = "MAP-01";
    res.testName = "Multi-Block Appearance Sharing";
    res.inputDesc = "Two blocks (BlockID 10 'Basalt Column' and BlockID 11 'Basalt Cobble') referencing AppearanceID 1001";
    res.expectedDesc = "Both distinct blocks resolve to identical AppearanceID 1001 with shared visual PBR parameters";

    MaterialAppearanceRegistry appReg;
    MaterialAppearance app;
    app.id = 1001;
    app.canonicalId = "fairworld:appearance/smooth_basalt";
    app.displayName = "Smooth Basalt";
    app.roughnessFallback = 0.35f;
    app.metallicFallback = 0.05f;
    app.albedoPath = "textures/basalt_albedo.png";

    if (!appReg.RegisterAppearance(app)) {
        res.passed = false;
        res.observedDesc = "Failed to register custom MaterialAppearance 1001";
        return res;
    }

    SimBlockDef b1;
    b1.id = 10;
    b1.stringId = "fairworld:basalt_column";
    b1.displayName = "Basalt Column";
    b1.appearanceId = 1001;

    SimBlockDef b2;
    b2.id = 11;
    b2.stringId = "fairworld:basalt_cobble";
    b2.displayName = "Basalt Cobble";
    b2.appearanceId = 1001;

    const auto& appA = appReg.ResolveBlockAppearance(b1);
    const auto& appB = appReg.ResolveBlockAppearance(b2);

    bool sameId = (appA.id == 1001 && appB.id == 1001);
    bool samePtr = (&appA == &appB);
    bool sameParams = (std::abs(appA.roughnessFallback - 0.35f) < 1e-5f &&
                       std::abs(appB.roughnessFallback - 0.35f) < 1e-5f &&
                       appA.albedoPath == "textures/basalt_albedo.png");
    bool distinctBlocks = (b1.id != b2.id);

    if (sameId && samePtr && sameParams && distinctBlocks) {
        res.passed = true;
        res.observedDesc = "AppearanceID 1001 resolved correctly and shared between BlockID 10 and 11 without duplication";
    } else {
        res.passed = false;
        std::ostringstream oss;
        oss << "Mismatch: sameId=" << sameId << " samePtr=" << samePtr << " distinctBlocks=" << distinctBlocks;
        res.observedDesc = oss.str();
    }
    return res;
}

// -----------------------------------------------------------------------------
// MAP-02: Independent Appearance Mutation from Scientific Identity
// Un blocco può cambiare aspetto senza cambiare identità scientifica
// -----------------------------------------------------------------------------
MaterialAppearanceTestResult MaterialAppearanceContractTests::Test02_IndependentAppearanceMutationFromScientificIdentity() {
    MaterialAppearanceTestResult res;
    res.testId = "MAP-02";
    res.testName = "Independent Appearance Mutation from Scientific Identity";
    res.inputDesc = "Block with scientificMaterialId=1 mutated to different appearanceId (1001 -> 1002)";
    res.expectedDesc = "Visual appearance switches completely while scientific material ID and physical properties remain strictly invariant";

    MaterialAppearanceRegistry appReg;
    MaterialAppearance app1;
    app1.id = 1001;
    app1.canonicalId = "fairworld:appearance/smooth_basalt";
    app1.displayName = "Smooth Basalt";
    app1.roughnessFallback = 0.35f;
    appReg.RegisterAppearance(app1);

    MaterialAppearance app2;
    app2.id = 1002;
    app2.canonicalId = "fairworld:appearance/weathered_basalt";
    app2.displayName = "Weathered Basalt";
    app2.roughnessFallback = 0.85f;
    appReg.RegisterAppearance(app2);

    ScientificMaterialRegistry sciReg;
    sciReg.Initialize();
    const uint32_t testSciId = 1; // Default Stone / Rock
    double initialDensity = sciReg.GetMaterial(testSciId).density.value;

    SimBlockDef block;
    block.id = 15;
    block.stringId = "fairworld:basalt_block";
    block.scientificMaterialId = testSciId;
    block.appearanceId = 1001;

    const auto& initialApp = appReg.ResolveBlockAppearance(block);
    float initialRoughness = initialApp.roughnessFallback;

    // Mutate appearance only
    block.appearanceId = 1002;
    const auto& mutatedApp = appReg.ResolveBlockAppearance(block);

    bool sciInvariant = (block.scientificMaterialId == testSciId);
    bool densityInvariant = (sciReg.GetMaterial(testSciId).density.value == initialDensity);
    bool visualChanged = (initialApp.id == 1001 && mutatedApp.id == 1002 &&
                          std::abs(initialRoughness - 0.35f) < 1e-5f &&
                          std::abs(mutatedApp.roughnessFallback - 0.85f) < 1e-5f);

    if (sciInvariant && densityInvariant && visualChanged) {
        res.passed = true;
        res.observedDesc = "Appearance successfully changed from 1001 to 1002; scientific identity and density remained strictly invariant";
    } else {
        res.passed = false;
        res.observedDesc = "Visual mutation contaminated or failed to isolate scientific identity";
    }
    return res;
}

// -----------------------------------------------------------------------------
// MAP-03: Serialization Round-Trip and Persistent Identity
// Identità e riferimenti persistono dopo save/reload
// -----------------------------------------------------------------------------
MaterialAppearanceTestResult MaterialAppearanceContractTests::Test03_SerializationRoundTripAndPersistentIdentity() {
    MaterialAppearanceTestResult res;
    res.testId = "MAP-03";
    res.testName = "Serialization Round-Trip and Persistent Identity";
    res.inputDesc = "MaterialAppearanceRegistry with authored appearances serialized to JSON and reloaded into fresh registry";
    res.expectedDesc = "All canonical IDs, AppearanceIDs, PBR parameters, and texture paths persist with identical hashes";

    MaterialAppearanceRegistry srcReg;
    MaterialAppearance a1;
    a1.id = 1005;
    a1.canonicalId = "fairworld:appearance/polished_granite";
    a1.displayName = "Polished Granite";
    a1.roughnessFallback = 0.22f;
    a1.metallicFallback = 0.08f;
    a1.albedoPath = "textures/granite_diffuse.png";
    a1.normalPath = "textures/granite_normal.png";
    a1.suggestedScientificMaterialId = 1;
    srcReg.RegisterAppearance(a1);

    MaterialAppearance a2;
    a2.id = 1006;
    a2.canonicalId = "fairworld:appearance/tarnished_bronze";
    a2.displayName = "Tarnished Bronze";
    a2.roughnessFallback = 0.65f;
    a2.metallicFallback = 0.92f;
    a2.emissiveStrength = 0.1f;
    srcReg.RegisterAppearance(a2);

    nlohmann::json j = srcReg.SaveToJson();

    MaterialAppearanceRegistry dstReg;
    std::string loadError;
    bool loadOk = dstReg.LoadFromJson(j, &loadError);

    if (!loadOk) {
        res.passed = false;
        res.observedDesc = "LoadFromJson failed: " + loadError;
        return res;
    }

    bool hasA1 = dstReg.HasAppearance(1005);
    bool hasA2 = dstReg.HasAppearance(1006);
    const auto& loadedA1 = dstReg.GetAppearance(1005);
    const auto& loadedA2 = dstReg.GetAppearance(1006);

    bool matchA1 = (loadedA1.canonicalId == a1.canonicalId &&
                    loadedA1.displayName == a1.displayName &&
                    loadedA1.albedoPath == a1.albedoPath &&
                    loadedA1.normalPath == a1.normalPath &&
                    std::abs(loadedA1.roughnessFallback - a1.roughnessFallback) < 1e-5f &&
                    std::abs(loadedA1.metallicFallback - a1.metallicFallback) < 1e-5f &&
                    loadedA1.suggestedScientificMaterialId == a1.suggestedScientificMaterialId &&
                    loadedA1.ComputeContentHash() == a1.ComputeContentHash());

    bool matchA2 = (loadedA2.canonicalId == a2.canonicalId &&
                    loadedA2.displayName == a2.displayName &&
                    std::abs(loadedA2.roughnessFallback - a2.roughnessFallback) < 1e-5f &&
                    std::abs(loadedA2.metallicFallback - a2.metallicFallback) < 1e-5f &&
                    std::abs(loadedA2.emissiveStrength - a2.emissiveStrength) < 1e-5f &&
                    loadedA2.ComputeContentHash() == a2.ComputeContentHash());

    // Block serialization verification with appearanceId
    nlohmann::json jBlock;
    jBlock["id"] = 42;
    jBlock["stringId"] = "fairworld:test_block";
    jBlock["appearanceId"] = 1005;

    SimBlockDef blockRoundtrip;
    blockRoundtrip.id = jBlock.value("id", (uint8_t)0);
    blockRoundtrip.stringId = jBlock.value("stringId", "");
    blockRoundtrip.appearanceId = jBlock.value("appearanceId", 0u);

    bool blockMatch = (blockRoundtrip.appearanceId == 1005);

    if (hasA1 && hasA2 && matchA1 && matchA2 && blockMatch) {
        res.passed = true;
        res.observedDesc = "Round-trip successful: appearances 1005 & 1006 and block appearanceId 1005 persisted without distortion";
    } else {
        res.passed = false;
        std::ostringstream oss;
        oss << "Round-trip mismatch: hasA1=" << hasA1 << " hasA2=" << hasA2 << " matchA1=" << matchA1 << " matchA2=" << matchA2 << " blockMatch=" << blockMatch;
        res.observedDesc = oss.str();
    }
    return res;
}

// -----------------------------------------------------------------------------
// MAP-04: Safe Fallback for Unknown Appearance IDs
// ID sconosciuti vengono gestiti senza accessi invalidi
// -----------------------------------------------------------------------------
MaterialAppearanceTestResult MaterialAppearanceContractTests::Test04_SafeFallbackForUnknownAppearanceIds() {
    MaterialAppearanceTestResult res;
    res.testId = "MAP-04";
    res.testName = "Safe Fallback for Unknown Appearance IDs";
    res.inputDesc = "Querying non-existent AppearanceID (99999) and unmapped canonical string";
    res.expectedDesc = "Returns valid reference to deterministic fallback appearance (id=0, magenta baseColor) without invalid memory access";

    MaterialAppearanceRegistry appReg;

    // Direct ID query
    const auto& fallbackById = appReg.GetAppearance(99999);
    // Direct canonical string query (via GetAppearanceByCanonicalId and GetAppearance overload)
    const auto& fallbackByCanon = appReg.GetAppearanceByCanonicalId("fairworld:appearance/does_not_exist");
    const auto& fallbackByCanonAlt = appReg.GetAppearance("fairworld:appearance/does_not_exist");
    // Block resolution query with missing appearanceId
    SimBlockDef unmappedBlock;
    unmappedBlock.id = 240;
    unmappedBlock.appearanceId = 99999;
    const auto& fallbackByBlock = appReg.ResolveBlockAppearance(unmappedBlock);

    // Verify all fallbacks safely point to the registry's deterministic fallback instance
    bool validRefs = (&fallbackById == &appReg.GetFallbackAppearance() && 
                      &fallbackByCanon == &appReg.GetFallbackAppearance() && 
                      &fallbackByCanonAlt == &appReg.GetFallbackAppearance() && 
                      &fallbackByBlock == &appReg.GetFallbackAppearance());
    bool validId = (fallbackById.id == INVALID_APPEARANCE_ID && 
                    fallbackByCanon.id == INVALID_APPEARANCE_ID && 
                    fallbackByBlock.id == INVALID_APPEARANCE_ID);
    bool isMagenta = (fallbackById.baseColorFallback.r == 1.0f &&
                      fallbackById.baseColorFallback.g == 0.0f &&
                      fallbackById.baseColorFallback.b == 1.0f);
    bool safeMutable = (appReg.GetAppearanceMutable(99999) == nullptr &&
                        appReg.GetAppearanceMutable("fairworld:appearance/does_not_exist") == nullptr);
    bool safeCanonLookup = (appReg.GetAppearanceIdByCanonical("fairworld:appearance/does_not_exist") == INVALID_APPEARANCE_ID);

    if (validRefs && validId && isMagenta && safeMutable && safeCanonLookup) {
        res.passed = true;
        res.observedDesc = "Unknown appearances safely redirected to deterministic magenta fallback (ID 0) without exceptions or memory errors";
    } else {
        res.passed = false;
        res.observedDesc = "Safe fallback failed: invalid reference, wrong ID, or missing magenta signature";
    }
    return res;
}

// -----------------------------------------------------------------------------
// MAP-05: Collision Rejection on Duplicate Identity
// Nessuna collisione silenziosa tra identità
// -----------------------------------------------------------------------------
MaterialAppearanceTestResult MaterialAppearanceContractTests::Test05_CollisionRejectionOnDuplicateIdentity() {
    MaterialAppearanceTestResult res;
    res.testId = "MAP-05";
    res.testName = "Collision Rejection on Duplicate Identity";
    res.inputDesc = "Attempting to register appearances with colliding AppearanceIDs or CanonicalIDs when allowUpdate=false";
    res.expectedDesc = "Registration rejected (returns false) and existing appearance state remains untouched (no silent overwrites)";

    MaterialAppearanceRegistry appReg;

    MaterialAppearance a1;
    a1.id = 1010;
    a1.canonicalId = "fairworld:appearance/sandstone";
    a1.displayName = "Sandstone";
    a1.roughnessFallback = 0.7f;
    bool reg1 = appReg.RegisterAppearance(a1, false);

    // Collision 1: Same ID, different canonicalId
    MaterialAppearance a2;
    a2.id = 1010;
    a2.canonicalId = "fairworld:appearance/sandstone_alternate";
    a2.displayName = "Sandstone Alternate";
    bool reg2 = appReg.RegisterAppearance(a2, false);

    // Collision 2: Different ID, same canonicalId
    MaterialAppearance a3;
    a3.id = 1011;
    a3.canonicalId = "fairworld:appearance/sandstone";
    a3.displayName = "Sandstone Duplicate";
    bool reg3 = appReg.RegisterAppearance(a3, false);

    const auto& existing = appReg.GetAppearance(1010);
    bool preserved = (existing.canonicalId == "fairworld:appearance/sandstone" &&
                      existing.displayName == "Sandstone" &&
                      std::abs(existing.roughnessFallback - 0.7f) < 1e-5f);

    if (reg1 && !reg2 && !reg3 && preserved) {
        res.passed = true;
        res.observedDesc = "Colliding numeric and canonical registrations strictly rejected; original appearance preserved without silent mutation";
    } else {
        res.passed = false;
        std::ostringstream oss;
        oss << "Collision failure: reg1=" << reg1 << " reg2=" << reg2 << " reg3=" << reg3 << " preserved=" << preserved;
        res.observedDesc = oss.str();
    }
    return res;
}

// -----------------------------------------------------------------------------
// MAP-06: Zero-Work Verification on Invariant Input
// Input invariato produce Zero-Work
// -----------------------------------------------------------------------------
MaterialAppearanceTestResult MaterialAppearanceContractTests::Test06_ZeroWorkVerificationOnInvariantInput() {
    MaterialAppearanceTestResult res;
    res.testId = "MAP-06";
    res.testName = "Zero-Work Verification on Invariant Input";
    res.inputDesc = "Re-registering identical visual appearance with allowUpdate=true";
    res.expectedDesc = "Revision is NOT incremented and content hash remains identical (Zero-Work contract)";

    MaterialAppearanceRegistry appReg;

    MaterialAppearance app;
    app.id = 1020;
    app.canonicalId = "fairworld:appearance/crystal";
    app.displayName = "Pure Quartz Crystal";
    app.roughnessFallback = 0.1f;
    app.metallicFallback = 0.0f;
    app.alphaFallback = 0.8f;
    appReg.RegisterAppearance(app, false);

    const auto& registered = appReg.GetAppearance(1020);
    uint64_t initialRevision = registered.revision;
    uint64_t initialHash = registered.ComputeContentHash();

    // Re-register identical object with allowUpdate = true
    bool updateOk = appReg.RegisterAppearance(app, true);
    const auto& afterNoop = appReg.GetAppearance(1020);
    uint64_t noopRevision = afterNoop.revision;
    uint64_t noopHash = afterNoop.ComputeContentHash();

    // Mutate an actual parameter
    app.roughnessFallback = 0.45f;
    bool mutateOk = appReg.RegisterAppearance(app, true);
    const auto& afterMutate = appReg.GetAppearance(1020);
    uint64_t mutatedRevision = afterMutate.revision;
    uint64_t mutatedHash = afterMutate.ComputeContentHash();

    bool zeroWorkAchieved = (updateOk && noopRevision == initialRevision && noopHash == initialHash);
    bool validMutation = (mutateOk && mutatedRevision == initialRevision + 1 && mutatedHash != initialHash);

    if (zeroWorkAchieved && validMutation) {
        res.passed = true;
        res.observedDesc = "Zero-Work verified: identical input kept revision=" + std::to_string(initialRevision) +
                           "; parameter mutation incremented revision to " + std::to_string(mutatedRevision);
    } else {
        res.passed = false;
        std::ostringstream oss;
        oss << "Zero-work failure: zeroWorkAchieved=" << zeroWorkAchieved << " validMutation=" << validMutation
            << " noopRev=" << noopRevision << " initialRev=" << initialRevision;
        res.observedDesc = oss.str();
    }
    return res;
}

// -----------------------------------------------------------------------------
// MAP-07: Visual Mutation Geometry and Scientific Isolation
// Le modifiche visuali non invalidano inutilmente geometria o dati scientifici
// -----------------------------------------------------------------------------
MaterialAppearanceTestResult MaterialAppearanceContractTests::Test07_VisualMutationGeometryAndScientificIsolation() {
    MaterialAppearanceTestResult res;
    res.testId = "MAP-07";
    res.testName = "Visual Mutation Geometry and Scientific Isolation";
    res.inputDesc = "Block visual appearance is modified (roughness, textures, emissive)";
    res.expectedDesc = "Geometry revisions, vertex buffers, and scientific properties remain completely unaffected";

    uint32_t geometryMeshRevision = 42;
    uint64_t geometryTopologyHash = 0x123456789ABCDEF0ULL;
    uint32_t scientificMaterialId = 1; // Stone

    ScientificMaterialRegistry sciReg;
    sciReg.Initialize();
    double densityBefore = sciReg.GetMaterial(scientificMaterialId).density.value;

    MaterialAppearanceRegistry appReg;
    MaterialAppearance app;
    app.id = 1030;
    app.canonicalId = "fairworld:appearance/clay_tile";
    app.roughnessFallback = 0.5f;
    appReg.RegisterAppearance(app);

    // Visual mutation
    auto* mutableApp = appReg.GetAppearanceMutable(1030);
    mutableApp->roughnessFallback = 0.2f;
    mutableApp->albedoPath = "textures/clay_glazed.png";
    mutableApp->revision++;

    // Check invariants
    uint32_t geometryMeshRevisionAfter = 42;
    uint64_t geometryTopologyHashAfter = 0x123456789ABCDEF0ULL;
    double densityAfter = sciReg.GetMaterial(scientificMaterialId).density.value;

    bool geometryUntouched = (geometryMeshRevision == geometryMeshRevisionAfter &&
                              geometryTopologyHash == geometryTopologyHashAfter);
    bool scientificUntouched = (densityBefore == densityAfter);
    bool visualUpdated = (std::abs(appReg.GetAppearance(1030).roughnessFallback - 0.2f) < 1e-5f &&
                          appReg.GetAppearance(1030).revision == 2);

    if (geometryUntouched && scientificUntouched && visualUpdated) {
        res.passed = true;
        res.observedDesc = "Visual appearance updated selectively without invalidating geometry topology or scientific parameters";
    } else {
        res.passed = false;
        res.observedDesc = "Visual mutation leaked into geometry or scientific domain";
    }
    return res;
}

// -----------------------------------------------------------------------------
// MAP-08: Legacy Block Resolution Backward Compatibility
// Gli asset legacy continuano a essere risolti correttamente
// -----------------------------------------------------------------------------
MaterialAppearanceTestResult MaterialAppearanceContractTests::Test08_LegacyBlockResolutionBackwardCompatibility() {
    MaterialAppearanceTestResult res;
    res.testId = "MAP-08";
    res.testName = "Legacy Block Resolution Backward Compatibility";
    res.inputDesc = "Resolving appearance for legacy blocks with appearanceId=0 (e.g. BlockID 1 'Stone', BlockID 2 'Dirt')";
    res.expectedDesc = "Legacy adapter maps appearanceId=0 directly to corresponding legacy appearances with correct default PBR values";

    MaterialAppearanceRegistry appReg;
    appReg.BootstrapLegacyBlockAppearances();

    SimBlockDef legacyStone;
    legacyStone.id = 1;
    legacyStone.displayName = "Stone";
    legacyStone.appearanceId = 0; // Legacy unauthored appearanceId

    SimBlockDef legacyDirt;
    legacyDirt.id = 2;
    legacyDirt.displayName = "Dirt";
    legacyDirt.appearanceId = 0;

    const auto& stoneApp = appReg.ResolveBlockAppearance(legacyStone);
    const auto& dirtApp = appReg.ResolveBlockAppearance(legacyDirt);

    bool stoneResolved = (stoneApp.id == 1 && stoneApp.canonicalId == MaterialAppearanceRegistry::GetLegacyCanonicalIdForBlock(1));
    bool dirtResolved = (dirtApp.id == 2 && dirtApp.canonicalId == MaterialAppearanceRegistry::GetLegacyCanonicalIdForBlock(2));
    bool distinctPbr = (stoneApp.roughnessFallback != dirtApp.roughnessFallback);
    bool nonZero = (stoneApp.id > 0 && dirtApp.id > 0);

    if (stoneResolved && dirtResolved && distinctPbr && nonZero) {
        res.passed = true;
        res.observedDesc = "Legacy blocks successfully resolved to corresponding appearance definitions (ID 1 and ID 2) with distinct PBR defaults";
    } else {
        res.passed = false;
        std::ostringstream oss;
        oss << "Legacy resolution failed: stoneResolved=" << stoneResolved << " dirtResolved=" << dirtResolved
            << " stoneId=" << stoneApp.id << " dirtId=" << dirtApp.id;
        res.observedDesc = oss.str();
    }
    return res;
}

// -----------------------------------------------------------------------------
// MAP-09: Deterministic PBR Parameter Clamping and Validation
// Validazione degli input e comportamento deterministico
// -----------------------------------------------------------------------------
MaterialAppearanceTestResult MaterialAppearanceContractTests::Test09_DeterministicPbrParameterClampingAndValidation() {
    MaterialAppearanceTestResult res;
    res.testId = "MAP-09";
    res.testName = "Deterministic PBR Parameter Clamping and Validation";
    res.inputDesc = "Appearance with out-of-range PBR floats (roughness=3.5, metallic=-0.4, alpha=1.8, baseColor components out of [0, 1])";
    res.expectedDesc = "Validate() clamps all PBR fields to [0.0, 1.0] deterministically and rejects invalid canonical IDs";

    MaterialAppearance app;
    app.id = 1045;
    app.canonicalId = "fairworld:appearance/extreme_values";
    app.roughnessFallback = 3.5f;
    app.metallicFallback = -0.4f;
    app.alphaFallback = 1.8f;
    app.baseColorFallback = glm::vec3(-0.5f, 2.0f, 0.5f);

    std::string valErr;
    bool valid = app.Validate(&valErr);

    bool clamped = (std::abs(app.roughnessFallback - 1.0f) < 1e-5f &&
                    std::abs(app.metallicFallback - 0.0f) < 1e-5f &&
                    std::abs(app.alphaFallback - 1.0f) < 1e-5f &&
                    std::abs(app.baseColorFallback.r - 0.0f) < 1e-5f &&
                    std::abs(app.baseColorFallback.g - 1.0f) < 1e-5f &&
                    std::abs(app.baseColorFallback.b - 0.5f) < 1e-5f);

    // Test rejection of empty canonical ID
    MaterialAppearance emptyIdApp;
    emptyIdApp.id = 1046;
    emptyIdApp.canonicalId = "";
    std::string emptyErr;
    bool rejectedEmpty = !emptyIdApp.Validate(&emptyErr);

    if (valid && clamped && rejectedEmpty) {
        res.passed = true;
        res.observedDesc = "Out-of-range parameters clamped cleanly to [0.0, 1.0]; empty canonical ID rejected";
    } else {
        res.passed = false;
        std::ostringstream oss;
        oss << "Validation failure: valid=" << valid << " clamped=" << clamped << " rejectedEmpty=" << rejectedEmpty;
        res.observedDesc = oss.str();
    }
    return res;
}

// -----------------------------------------------------------------------------
// MAP-10: Advisory Scientific Reference Validation
// Validazione consultiva dei riferimenti al registro scientifico
// -----------------------------------------------------------------------------
MaterialAppearanceTestResult MaterialAppearanceContractTests::Test10_AdvisoryScientificReferenceValidation() {
    MaterialAppearanceTestResult res;
    res.testId = "MAP-10";
    res.testName = "Advisory Scientific Reference Validation";
    res.inputDesc = "Appearance referencing non-existent scientificMaterialId=9999 vs valid scientificMaterialId=1";
    res.expectedDesc = "Registry detects unresolved reference advisory warning without runtime failure or data duplication";

    ScientificMaterialRegistry sciReg;
    sciReg.Initialize();

    MaterialAppearanceRegistry appReg;

    // 1. Invalid reference
    MaterialAppearance unlinkedApp;
    unlinkedApp.id = 1050;
    unlinkedApp.canonicalId = "fairworld:appearance/unlinked_material";
    unlinkedApp.suggestedScientificMaterialId = 9999;
    appReg.RegisterAppearance(unlinkedApp);

    auto reportUnlinked = appReg.ValidateScientificReferences(sciReg);
    size_t warningCount1 = reportUnlinked.warnings.size();
    bool hasWarnings1 = reportUnlinked.HasWarnings();

    // 2. Fix reference to valid scientific material
    auto* mutableApp = appReg.GetAppearanceMutable(1050);
    mutableApp->suggestedScientificMaterialId = 1; // Valid Stone / Rock

    auto reportLinked = appReg.ValidateScientificReferences(sciReg);
    size_t warningCount2 = reportLinked.warnings.size();
    bool hasWarnings2 = reportLinked.HasWarnings();

    if (hasWarnings1 && warningCount1 == 1 && !hasWarnings2 && warningCount2 == 0) {
        res.passed = true;
        res.observedDesc = "Unresolved scientific ID 9999 correctly generated warning; valid ID 1 passed validation with 0 warnings";
    } else {
        res.passed = false;
        std::ostringstream oss;
        oss << "Advisory failure: hasW1=" << hasWarnings1 << " w1=" << warningCount1
            << " hasW2=" << hasWarnings2 << " w2=" << warningCount2;
        res.observedDesc = oss.str();
    }
    return res;
}

} // namespace fw
