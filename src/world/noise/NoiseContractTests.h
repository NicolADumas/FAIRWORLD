#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// MEM-02: SUITE TEST REGRESSIONE CONTRATTO NOISE RULE OF FIVE (NOISE-01 .. NOISE-07)
// Verifica formale isolata della ownership, semantica di copia/movimento e determinismo numerico.
// =============================================================================

struct NoiseContractTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class NoiseContractTests {
public:
    static std::vector<NoiseContractTestResult> RunAllTests();

    // 1. Creazione e distruzione ripetute (1000 cicli RAII puliti)
    static NoiseContractTestResult Test01_RepeatedCreationDestruction();

    // 2. Costruttore di copia profonda: output identico deterministico e indipendenza
    static NoiseContractTestResult Test02_DeepCopyConstructorIndependence();

    // 3. Operatore di assegnamento di copia: determinismo e indipendenza
    static NoiseContractTestResult Test03_CopyAssignmentIndependence();

    // 4. Costruttore di movimento: trasferimento corretto, moved-from sicuro senza crash
    static NoiseContractTestResult Test04_MoveConstructorSafety();

    // 5. Operatore di assegnamento di movimento: trasferimento corretto e self-assignment safety
    static NoiseContractTestResult Test05_MoveAssignmentSafety();

    // 6. Nessun double-free in riallocazione di container (std::vector<Noise>)
    static NoiseContractTestResult Test06_ContainerReallocationNoDoubleFree();

    // 7. Determinismo numerico invariato rispetto all'algoritmo di base
    static NoiseContractTestResult Test07_NumericalDeterminismInvariance();
};

} // namespace fw
