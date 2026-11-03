#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// P0.3B1: SUITE TEST CONTRATTO ASSEGNAZIONE MATERIALI SCIENTIFICI (BIND-01..07)
// Verifica la rappresentazione canonica, ereditarietà dal materiale radice,
// override locali per superficie, diagnostica orfani e indipendenza visiva.
// =============================================================================

struct ScientificBindingTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class ScientificBindingContractTests {
public:
    static std::vector<ScientificBindingTestResult> RunAllTests();

    // 1. Ereditarietà: le superfici senza override (o con scientificMaterialId == 0)
    // ereditano deterministicamente il materiale scientifico radice.
    static ScientificBindingTestResult Test01_RootMaterialInheritance();

    // 2. Override locale: una superficie con scientificMaterialId != 0 adotta il proprio
    // materiale scientifico, mentre le restanti superfici continuano a ereditare la radice.
    static ScientificBindingTestResult Test02_LocalSurfaceOverride();

    // 3. Diagnostica materiale inesistente: un ID scientifico orfano (non nel catalogo)
    // viene preservato intatto su disco senza crash né reset silenzioso e flaggato come orfano.
    static ScientificBindingTestResult Test03_NonExistentScientificMaterialDiagnostic();

    // 4. Compatibilità retroattiva: asset e JSON pregressi privi della chiave
    // "scientificMaterialId" deserializzano con successo a 0 (ereditato) e round-trip serialization.
    static ScientificBindingTestResult Test04_LegacyAssetCompatibilityAndSerialization();

    // 5. Indipendenza visiva: mutazioni su materialId (PBR), texture o AppearanceID
    // non alterano scientificMaterialId, e mutazioni scientifiche non alterano proprietà visive.
    static ScientificBindingTestResult Test05_IndependenceFromAppearanceAndVisualMaterial();

    // 6. Adattatore semantico MeshFace: risoluzione del materiale per facce poligonali
    // tramite face.surfaceId senza memorizzare campi ridondanti in MeshFace.
    static ScientificBindingTestResult Test06_MeshFaceSemanticAdapterResolution();

    // 7. Non duplicazione delle proprietà: SurfaceOverride memorizza esclusivamente l'ID
    // senza duplicare densità, attrito o altri valori SI locali.
    static ScientificBindingTestResult Test07_ZeroPhysicalPropertyDuplication();

    // =========================================================================
    // P0.3B2: PERSISTENCE & WORKFLOW TESTS (BIND-08..12)
    // =========================================================================

    // 8. Round-trip completo attraverso il workflow reale:
    // RawAssetDraft -> FactoryOrchestrator -> RawAssetPackage -> Save/Load -> Runtime Registries.
    static ScientificBindingTestResult Test08_FullWorkflowDraftPackageSaveLoadRuntimeRoundTrip();

    // 9. Ereditarietà dinamica dalla radice e preservazione degli override locali.
    static ScientificBindingTestResult Test09_RootMaterialAndSurfaceOverrideDynamics();

    // 10. Preservazione e diagnostica degli ID scientifici orfani senza reset silenzioso.
    static ScientificBindingTestResult Test10_OrphanScientificIdPreservedAcrossPersistenceAndDiagnosed();

    // 11. Compatibilità retroattiva per package legacy privi della chiave "surfaces".
    static ScientificBindingTestResult Test11_LegacyPackageBackwardCompatibility();

    // 12. Separazione ortogonale e indipendenza tra AppearanceID e scientificMaterialId.
    static ScientificBindingTestResult Test12_IndependenceFromAppearanceIdAndVisualPainting();

    // =========================================================================
    // P0.3C3: BLOCK MAKER SCIENTIFIC SURFACE BINDING TESTS (BIND-13..14)
    // =========================================================================

    // 13. Block Maker: Selezione superficie, isolamento radice, ereditarietà dinamica e separazione PBR.
    static ScientificBindingTestResult Test13_BlockMakerSurfaceSelectionAndRootIsolation();

    // 14. Block Maker: Persistenza ciclo Draft -> Package -> Save -> Load -> Publish, materiali custom e orfani.
    static ScientificBindingTestResult Test14_BlockMakerSurfaceOverridePersistenceAndReloadCycle();

    // 15. Block Maker: Cilindro (ShapeType 6) atlasTransform, hit-testing 2D/3D, isolamento diagnostica UV-PAINT e binding scientifico.
    static ScientificBindingTestResult Test15_CylinderSemanticSurfaceBindingAndUVPaintDiagnosticProtection();

    // 16. Multi-part Assembly: Verifica che due o più parti assemblate conservino materiali scientifici differenti (P0.3C3).
    static ScientificBindingTestResult Test16_PartAssemblyPreservesDistinctScientificMaterials();
};

} // namespace fw
