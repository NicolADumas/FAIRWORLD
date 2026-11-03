#pragma once

#include "State.h"
#include "blocks/ScientificMaterialRegistry.h"
#include "blocks/MaterialAppearanceRegistry.h"
#include "blocks/MaterialAuthoringWorkflow.h"
#include <string>
#include <vector>
#include <memory>

struct SharedContext;

namespace fw {

// =============================================================================
// MA-2: MATERIAL ATLAS STATE (SCIENTIFIC CATALOG, INSPECTOR & INTERACTIVE EDITOR)
// =============================================================================

class MaterialAtlasState : public State {
public:
    explicit MaterialAtlasState(SharedContext* context);
    ~MaterialAtlasState() override;

    bool Init() override;
    void Update(float dt) override;
    void Render() override;

    void OnEnter() override;
    void OnExit() override;

    // OPT-02.1 & MA-2: Interfaccia UI reattiva ma con cache: nessun lavoro superfluo durante l'inattività
    bool RequiresContinuousUpdate() const override { return false; }

    // Accesso al workflow per test e diagnostica (P0.3C2)
    MaterialAuthoringWorkflow* GetAuthoringWorkflow() { return m_authoringWorkflow.get(); }
    const MaterialAuthoringWorkflow* GetAuthoringWorkflow() const { return m_authoringWorkflow.get(); }
    bool IsAuthoringMode() const { return m_authoringMode; }

private:
    // Struttura dati per la cache della vista catalogo (ordinata e filtrata)
    struct CachedCatalogItem {
        ScientificMaterialID id = INVALID_SCIENTIFIC_MATERIAL_ID;
        std::string canonicalId;
        std::string displayName;
        std::string chemicalFormula;
        ScientificKind kind = ScientificKind::Unknown;
        AggregationState aggregationState = AggregationState::Unknown;
        MaterialLifecycleState lifecycleState = MaterialLifecycleState::Published;
        bool isCustom = false;
        bool enabledInBlockMaker = true;
    };

    void InvalidateCatalogCache();
    void RebuildCatalogCache();

    // Sotto-componenti di rendering UI principali
    void RenderTopBar();
    void RenderCatalogPane(float width, float height);
    void RenderDetailPane(float width, float height);

    // Sezioni di dettaglio (READ-ONLY INSPECTOR)
    void RenderScientificIdentity(const ScientificMaterialDef& def);
    void RenderScientificClassification(const ScientificMaterialDef& def);
    void RenderScientificComposition(const ScientificMaterialDef& def);
    void RenderPhysicalPropertiesTable(const ScientificMaterialDef& def);
    void RenderExtendedPropertiesSection(const ScientificMaterialDef& def);
    void RenderLifecycleAndProvenance(const ScientificMaterialDef& def);
    void RenderAppearanceSection(const ScientificMaterialDef& def);

    // Nuove sezioni di authoring (INTERACTIVE EDITOR - P0.3C2)
    void RenderAuthoringToolbar(const ScientificMaterialDef* currentDef);
    void RenderAuthoringEditorPane(float width, float height);
    void RenderEditorIdentitySection(ScientificMaterialDef& wc);
    void RenderEditorClassificationSection(ScientificMaterialDef& wc);
    void RenderEditorCompositionSection(ScientificMaterialDef& wc);
    void RenderEditorPhysicalPropertiesTable(ScientificMaterialDef& wc);
    void RenderEditorLifecycleBar(ScientificMaterialDef& wc);
    void RenderModals();

    void SetStatusNotification(const std::string& msg, bool isError = false, float duration = 4.0f);

private:
    SharedContext* m_context = nullptr;

    // Workflow di authoring transazionale
    std::unique_ptr<MaterialAuthoringWorkflow> m_authoringWorkflow;
    std::string m_customCatalogPath = "assets/definitions/custom_materials.json";
    bool m_authoringMode = false;

    // Selezione attiva
    ScientificMaterialID m_selectedMaterialId = INVALID_SCIENTIFIC_MATERIAL_ID;

    // Filtri di ricerca e catalogazione
    char m_searchQuery[128] = { 0 };
    int m_filterCategory = 0;       // 0 = Tutti, 1..8 = ScientificKind
    int m_filterOrigin = 0;         // 0 = Tutti, 1 = Built-in (<1000), 2 = Custom (>=1000)
    int m_filterLifecycle = 0;      // 0 = Tutti, 1 = Published, 2 = Draft, 3 = Deprecated
    int m_filterBlockMaker = 0;     // 0 = Tutti, 1 = Abilitati, 2 = Disabilitati
    int m_sortOrder = 0;            // 0 = ID Crescente, 1 = ID Decrescente, 2 = Nome A-Z, 3 = Nome Z-A

    // Cache del catalogo filtrato (ottimizzazione zero allocazioni per frame in idle)
    std::vector<CachedCatalogItem> m_cachedCatalog;
    bool m_cacheDirty = true;
    uint64_t m_lastRegistryRevision = 0;

    // Notifiche di stato temporanee
    std::string m_statusNotification = "";
    bool m_statusIsError = false;
    float m_statusTimer = 0.0f;

    // Validazione feedback
    AuthoringValidationResult m_validationResult;
    bool m_showValidationFeedback = false;

    // Modali UI (P0.3C2)
    bool m_showNewMaterialModal = false;
    char m_newCanonicalId[128] = "fairworld:mat/custom_material";
    char m_newDisplayName[128] = "Nuovo Materiale Custom";
    int m_newKindIndex = 2; // Compound

    bool m_showCloneModal = false;
    char m_cloneCanonicalId[128] = "";
    char m_cloneDisplayName[128] = "";

    bool m_showLostUpdateModal = false;
    std::string m_lostUpdateError = "";

    bool m_showDeprecateConfirmModal = false;
    ScientificMaterialID m_materialToDeprecate = INVALID_SCIENTIFIC_MATERIAL_ID;

    bool m_showDiscardConfirmModal = false;
};

} // namespace fw
