#pragma once

#include "MaterialAppearance.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>
#include <cstdint>

namespace fw {

class MaterialRegistry;
class BlockRegistry;
struct SimBlockDef;
class ScientificMaterialRegistry;

// =============================================================================
// MA-02A: DIAGNOSTICA RIFERIMENTI SCIENTIFICI IN APPEARANCE
// =============================================================================
struct AppearanceScientificDiagnostic {
    AppearanceID appearanceId = INVALID_APPEARANCE_ID;
    std::string canonicalId;
    uint32_t unresolvedScientificId = 0;
    std::string message;
};

struct AppearanceValidationReport {
    size_t totalAppearancesChecked = 0;
    size_t validAssociations = 0;
    size_t unassociatedAppearances = 0;
    std::vector<AppearanceScientificDiagnostic> warnings;

    bool HasWarnings() const { return !warnings.empty(); }
};

// =============================================================================
// MA-02A: MATERIAL APPEARANCE REGISTRY & COMPATIBILITY ADAPTER
// Catalogo CPU autonomo per il riutilizzo delle definizioni visive.
// Separa l'identità visiva del materiale dall'identità del blocco.
// =============================================================================

class MaterialAppearanceRegistry {
public:
    MaterialAppearanceRegistry();
    ~MaterialAppearanceRegistry() = default;

    void Initialize();
    void Clear();

    // Registrazione e gestione
    bool RegisterAppearance(const MaterialAppearance& def, bool allowUpdate = false, std::string* outError = nullptr);
    bool HasAppearance(AppearanceID id) const;
    bool HasAppearance(const std::string& canonicalId) const;

    const MaterialAppearance& GetAppearance(AppearanceID id) const;
    const MaterialAppearance& GetAppearance(const std::string& canonicalId) const;
    const MaterialAppearance& GetAppearanceByCanonicalId(const std::string& canonicalId) const;
    const MaterialAppearance& GetAppearanceByCanonical(const std::string& canonicalId) const;

    MaterialAppearance* GetAppearanceMutable(AppearanceID id);
    MaterialAppearance* GetAppearanceMutable(const std::string& canonicalId);
    MaterialAppearance* GetAppearanceMutableByCanonicalId(const std::string& canonicalId);

    AppearanceID GetAppearanceIdByCanonical(const std::string& canonicalId) const;
    AppearanceID GetAppearanceIdByCanonicalId(const std::string& canonicalId) const;
    AppearanceID AllocateNextAppearanceId();

    size_t GetActiveAppearanceCount() const;
    std::vector<AppearanceID> GetAllAppearanceIDs() const;
    const std::unordered_map<AppearanceID, MaterialAppearance>& GetAllAppearances() const { return m_appearances; }

    const MaterialAppearance& GetFallbackAppearance() const { return m_fallbackAppearance; }

    // Persistenza JSON
    bool LoadFromJson(const std::string& filepath);
    bool SaveToJson(const std::string& filepath) const;
    bool LoadFromJson(const nlohmann::json& j, std::string* outError = nullptr);
    nlohmann::json SaveToJson() const;
    bool LoadFromJsonString(const std::string& jsonContent, std::string* outError = nullptr);
    std::string SaveToJsonString() const;

    // Validazione consultiva dei riferimenti al registro scientifico (nessun crash né mutazione)
    AppearanceValidationReport ValidateScientificReferences(const ScientificMaterialRegistry& sciRegistry) const;

    // =========================================================================
    // ADATTATORE DI COMPATIBILITÀ RETROATTIVA (LEGACY BLOCKS)
    // Risolve gli aspetti visivi dei blocchi legacy senza cambiare gli ID storici.
    // =========================================================================

    // Genera l'URI canonico per un blocco legacy (es. "fairworld:appearance/legacy_block_1")
    static std::string GetLegacyCanonicalIdForBlock(uint8_t blockId);

    // Risolve l'AppearanceID effettivo per un blocco dato il suo blockId e l'eventuale appearanceId esplicito
    AppearanceID ResolveAppearanceId(uint8_t blockId, AppearanceID explicitAppearanceId = 0) const;

    // Risolve direttamente il MaterialAppearance per un blocco
    const MaterialAppearance& ResolveBlockAppearance(uint8_t blockId, AppearanceID explicitAppearanceId = 0) const;
    const MaterialAppearance& ResolveBlockAppearance(const SimBlockDef& blockDef) const;

    // Inizializza le definizioni visive di default per i blocchi storici (0..255)
    void BootstrapLegacyBlockAppearances(const MaterialRegistry* legacyMatRegistry = nullptr);

private:
    std::unordered_map<AppearanceID, MaterialAppearance> m_appearances;
    std::unordered_map<std::string, AppearanceID> m_canonicalIndex;
    AppearanceID m_nextAppearanceId = 1000; // Gli ID < 1000 sono riservati al mapping 1:1 legacy (1..255)
    MaterialAppearance m_fallbackAppearance;
};

} // namespace fw
