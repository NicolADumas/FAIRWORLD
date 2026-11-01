#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// MA-02A: SUITE TEST REGRESSIONE CONTRATTO MATERIAL APPEARANCE (MAP-01 .. MAP-10)
// Verifica formale isolata della fondazione CPU di Material Atlas.
// =============================================================================

struct MaterialAppearanceTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class MaterialAppearanceContractTests {
public:
    static std::vector<MaterialAppearanceTestResult> RunAllTests();

    // 1. Due BlockID differenti possono condividere un AppearanceID
    static MaterialAppearanceTestResult Test01_MultiBlockAppearanceSharing();

    // 2. Un blocco può cambiare aspetto senza cambiare identità scientifica
    static MaterialAppearanceTestResult Test02_IndependentAppearanceMutationFromScientificIdentity();

    // 3. Identità e riferimenti persistono dopo save/reload (JSON round-trip)
    static MaterialAppearanceTestResult Test03_SerializationRoundTripAndPersistentIdentity();

    // 4. ID sconosciuti vengono gestiti senza accessi invalidi (Fallback deterministico)
    static MaterialAppearanceTestResult Test04_SafeFallbackForUnknownAppearanceIds();

    // 5. Nessuna collisione silenziosa tra identità (rifiuto duplicati con allowUpdate=false)
    static MaterialAppearanceTestResult Test05_CollisionRejectionOnDuplicateIdentity();

    // 6. Input invariato produce Zero-Work (nessun incremento di revisione o rielaborazione)
    static MaterialAppearanceTestResult Test06_ZeroWorkVerificationOnInvariantInput();

    // 7. Le modifiche visuali non invalidano geometria o dati scientifici
    static MaterialAppearanceTestResult Test07_VisualMutationGeometryAndScientificIsolation();

    // 8. Gli asset legacy continuano a essere risolti correttamente (appearanceId == 0)
    static MaterialAppearanceTestResult Test08_LegacyBlockResolutionBackwardCompatibility();

    // 9. Validazione deterministica e clamping dei parametri PBR
    static MaterialAppearanceTestResult Test09_DeterministicPbrParameterClampingAndValidation();

    // 10. Validazione consultiva (advisory) dei riferimenti al registro scientifico
    static MaterialAppearanceTestResult Test10_AdvisoryScientificReferenceValidation();
};

} // namespace fw
