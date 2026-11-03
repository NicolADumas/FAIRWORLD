#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include "blocks/ScientificMaterialRegistry.h"
#include "blocks/MaterialAppearanceRegistry.h"

namespace fw {

// =============================================================================
// MATERIAL AUTHORING WORKFLOW (P0.3C1)
// Gestore transazionale per l'authoring dei materiali scientifici:
// 1. Copia di lavoro modificabile (Working Copy isolata, nessuna mutazione prematura).
// 2. Protezione assoluta dei materiali built-in (< 1000).
// 3. Validazione preventiva SI, ontologica e canonicalId prima del commit.
// 4. Registrazione controllata nel registry (con avanzamento HWM e snapshot CoW).
// 5. Salvataggio atomico su disco con rilevamento Lost-Update / conflitti.
// 6. Preservazione di ID canonici, stabilità dei riferimenti e separazione PBR.
// =============================================================================

enum class AuthoringSessionState : uint8_t {
    Idle = 0,            // Nessuna sessione attiva
    EditingNew = 1,      // Creazione di un nuovo materiale personalizzato (Draft)
    EditingExisting = 2  // Modifica di un materiale custom esistente
};

struct AuthoringValidationResult {
    bool isValid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    void AddError(const std::string& err) {
        isValid = false;
        errors.push_back(err);
    }
    void AddWarning(const std::string& warn) {
        warnings.push_back(warn);
    }
};

class MaterialAuthoringWorkflow {
public:
    explicit MaterialAuthoringWorkflow(ScientificMaterialRegistry& registry);
    ~MaterialAuthoringWorkflow() = default;

    // --- 1. Gestione Sessione di Editing ---
    
    // Avvia la creazione di un nuovo materiale custom
    bool BeginCreateCustom(
        const std::string& canonicalId,
        const std::string& displayName,
        ScientificKind kind = ScientificKind::Mineral,
        std::string* outError = nullptr
    );

    // Avvia la modifica di un materiale custom esistente (ID >= 1000).
    // Blocca categoricamente la modifica diretta di materiali built-in (< 1000).
    bool BeginEditExisting(ScientificMaterialID id, std::string* outError = nullptr);

    // Clona un materiale esistente (built-in o custom) come base per un NUOVO materiale custom
    bool BeginCloneAsCustom(
        ScientificMaterialID sourceId,
        const std::string& newCanonicalId,
        const std::string& newDisplayName,
        std::string* outError = nullptr
    );

    // Annulla la sessione corrente scartando le modifiche non committate
    void DiscardSession();

    // Stato della sessione
    bool HasActiveSession() const { return m_state != AuthoringSessionState::Idle; }
    AuthoringSessionState GetSessionState() const { return m_state; }
    bool IsDirty() const { return m_isDirty; }
    bool IsCommitted() const { return m_isCommitted; }

    // --- 2. Copia di Lavoro Modificabile ---
    
    ScientificMaterialDef& GetWorkingCopy();
    const ScientificMaterialDef& GetWorkingCopy() const;
    void MarkDirty() { m_isDirty = true; }

    // Modifica assistita di una delle 12 proprietà fisiche SI standard
    bool SetPhysicalProperty(
        const std::string& propertyName,
        double value,
        const std::string& unit = "",
        DataQuality quality = DataQuality::PeerReviewed,
        const std::string& source = "",
        std::string* outError = nullptr
    );

    // Imposta una proprietà come Non Applicabile
    bool SetPropertyNotApplicable(const std::string& propertyName, std::string* outError = nullptr);

    // Imposta una proprietà come Sconosciuta (Unknown)
    bool SetPropertyUnknown(const std::string& propertyName, const std::string& unit = "", std::string* outError = nullptr);

    // --- 3. Validazione ---
    
    AuthoringValidationResult ValidateWorkingCopy() const;

    // --- 4. Commit nel Registry ---
    
    // Registra la copia di lavoro in ScientificMaterialRegistry.
    // Se è un nuovo materiale e viene fornito un catalogFilepath, riserva l'ID in modo durevole;
    // altrimenti riserva il prossimo ID custom in memoria.
    bool CommitToRegistry(
        std::string* outError = nullptr,
        const std::string& catalogFilepath = ""
    );

    // --- 5. Ciclo di Vita (Lifecycle) ---
    
    // Pubblica il materiale attivo (Draft -> Published)
    bool PublishActiveMaterial(std::string* outError = nullptr);

    // Depreca il materiale attivo (Published/Draft -> Deprecated)
    // Non permette di deprecare materiali built-in.
    bool DeprecateActiveMaterial(std::string* outError = nullptr);

    // Imposta abilitazione in Block Maker
    bool SetEnabledInBlockMaker(bool enabled, std::string* outError = nullptr);

    // --- 6. Persistenza Atomica & Gestione Conflitti ---
    
    // Salva il catalogo custom su disco verificando la generazione del file (protezione Lost Update)
    bool SaveCatalogToDisk(
        const std::string& catalogFilepath,
        std::string* outError = nullptr,
        bool forceOverwrite = false
    );

    // Ricarica il catalogo custom da disco aggiornando la generazione
    bool ReloadCatalogFromDisk(
        const std::string& catalogFilepath,
        std::string* outError = nullptr
    );

    // Operazioni di lifecycle dirette (senza sessione attiva aperta)
    bool PublishMaterialDirect(ScientificMaterialID id, std::string* outError = nullptr);
    bool DeprecateMaterialDirect(ScientificMaterialID id, std::string* outError = nullptr);
    bool SetEnabledInBlockMakerDirect(ScientificMaterialID id, bool enabled, std::string* outError = nullptr);

    // Stato di persistenza su disco
    bool IsCatalogUnsavedOnDisk() const { return m_catalogUnsavedOnDisk; }
    void SetCatalogUnsavedOnDisk(bool unsaved) { m_catalogUnsavedOnDisk = unsaved; }

    // --- 7. Helper Statici ---
    
    // Conversioni termometriche al confine UI (Celsius-first UI)
    static constexpr double KelvinFromCelsius(double celsius) { return celsius + 273.15; }
    static constexpr double CelsiusFromKelvin(double kelvin) { return kelvin - 273.15; }

    // Verifica se un ID appartiene ai materiali built-in protetti
    static bool IsBuiltInProtected(ScientificMaterialID id);

    // Verifica ortogonalità tra proprietà scientifiche e aspetti visivi (MaterialAppearance)
    static bool VerifyAppearanceOrthogonality(
        const ScientificMaterialRegistry& sciReg,
        ScientificMaterialID sciId,
        const MaterialAppearance& appearanceBefore,
        const MaterialAppearance& appearanceAfter
    );

private:
    ScientificMaterialRegistry& m_registry;
    AuthoringSessionState m_state = AuthoringSessionState::Idle;
    ScientificMaterialDef m_workingCopy;
    ScientificMaterialDef m_baselineCopy;
    bool m_isDirty = false;
    bool m_isCommitted = false;
    bool m_catalogUnsavedOnDisk = false;
};

} // namespace fw
