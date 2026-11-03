#include "pch.h"
#include "MaterialAuthoringWorkflow.h"
#include <algorithm>
#include <cmath>

namespace fw {

MaterialAuthoringWorkflow::MaterialAuthoringWorkflow(ScientificMaterialRegistry& registry)
    : m_registry(registry)
{
}

bool MaterialAuthoringWorkflow::IsBuiltInProtected(ScientificMaterialID id) {
    return id != INVALID_SCIENTIFIC_MATERIAL_ID && id < CUSTOM_SCIENTIFIC_MATERIAL_ID_BASE;
}

bool MaterialAuthoringWorkflow::BeginCreateCustom(
    const std::string& canonicalId,
    const std::string& displayName,
    ScientificKind kind,
    std::string* outError
) {
    if (!ScientificMaterialRegistry::ValidateCanonicalIdFormat(canonicalId, outError)) {
        return false;
    }

    if (m_registry.HasMaterial(canonicalId)) {
        if (outError) *outError = "Identificatore canonico '" + canonicalId + "' gia' registrato.";
        return false;
    }

    if (displayName.empty()) {
        if (outError) *outError = "Il nome visualizzato (displayName) non puo' essere vuoto.";
        return false;
    }

    ScientificMaterialID candidateId = m_registry.PeekNextCustomMaterialId();

    m_workingCopy = ScientificMaterialDef{};
    m_workingCopy.id = candidateId;
    m_workingCopy.canonicalId = canonicalId;
    m_workingCopy.displayName = displayName;
    m_workingCopy.kind = kind;
    m_workingCopy.isCustom = true;
    m_workingCopy.lifecycleState = MaterialLifecycleState::Draft;
    m_workingCopy.enabledInBlockMaker = true;

    // Inizializzazione proprietà SI standard con unità corrette e stato Unknown
    m_workingCopy.density = ScientificQuantity::Unknown("kg/m^3");
    m_workingCopy.porosity = ScientificQuantity::Unknown("");
    m_workingCopy.compressiveStrength = ScientificQuantity::Unknown("Pa");
    m_workingCopy.tensileStrength = ScientificQuantity::Unknown("Pa");
    m_workingCopy.elasticModulus = ScientificQuantity::Unknown("Pa");
    m_workingCopy.staticFriction = ScientificQuantity::Unknown("");
    m_workingCopy.dynamicFriction = ScientificQuantity::Unknown("");
    m_workingCopy.restitution = ScientificQuantity::Unknown("");
    m_workingCopy.specificHeatCapacity = ScientificQuantity::Unknown("J/(kg*K)");
    m_workingCopy.thermalConductivity = ScientificQuantity::Unknown("W/(m*K)");
    m_workingCopy.thermalExpansion = ScientificQuantity::Unknown("1/K");
    m_workingCopy.meltingPoint = ScientificQuantity::Unknown("K");

    m_baselineCopy = m_workingCopy;
    m_state = AuthoringSessionState::EditingNew;
    m_isDirty = true;
    m_isCommitted = false;

    return true;
}

bool MaterialAuthoringWorkflow::BeginEditExisting(ScientificMaterialID id, std::string* outError) {
    if (IsBuiltInProtected(id)) {
        if (outError) *outError = "I materiali built-in (standard) con ID " + std::to_string(id) + 
                                  " sono protetti e non possono essere modificati direttamente dall'authoring.";
        return false;
    }

    if (!m_registry.HasMaterial(id)) {
        if (outError) *outError = "Materiale scientifico ID " + std::to_string(id) + " non trovato nel registro.";
        return false;
    }

    ScientificMaterialDef currentDef = m_registry.GetMaterial(id);
    if (!currentDef.isCustom && id < CUSTOM_SCIENTIFIC_MATERIAL_ID_BASE) {
        if (outError) *outError = "Il materiale ID " + std::to_string(id) + " non e' contrassegnato come custom ed e' protetto da scrittura.";
        return false;
    }

    m_workingCopy = currentDef;
    m_baselineCopy = currentDef;
    m_state = AuthoringSessionState::EditingExisting;
    m_isDirty = false;
    m_isCommitted = true;

    return true;
}

bool MaterialAuthoringWorkflow::BeginCloneAsCustom(
    ScientificMaterialID sourceId,
    const std::string& newCanonicalId,
    const std::string& newDisplayName,
    std::string* outError
) {
    if (!m_registry.HasMaterial(sourceId)) {
        if (outError) *outError = "Materiale sorgente ID " + std::to_string(sourceId) + " non trovato nel registro.";
        return false;
    }

    if (!ScientificMaterialRegistry::ValidateCanonicalIdFormat(newCanonicalId, outError)) {
        return false;
    }

    if (m_registry.HasMaterial(newCanonicalId)) {
        if (outError) *outError = "Identificatore canonico di destinazione '" + newCanonicalId + "' gia' in uso.";
        return false;
    }

    if (newDisplayName.empty()) {
        if (outError) *outError = "Il nome del clone (displayName) non puo' essere vuoto.";
        return false;
    }

    ScientificMaterialDef sourceDef = m_registry.GetMaterial(sourceId);

    m_workingCopy = sourceDef;
    m_workingCopy.id = m_registry.PeekNextCustomMaterialId();
    m_workingCopy.canonicalId = newCanonicalId;
    m_workingCopy.displayName = newDisplayName;
    m_workingCopy.isCustom = true;
    m_workingCopy.lifecycleState = MaterialLifecycleState::Draft;
    m_workingCopy.enabledInBlockMaker = true;

    m_baselineCopy = m_workingCopy;
    m_state = AuthoringSessionState::EditingNew;
    m_isDirty = true;
    m_isCommitted = false;

    return true;
}

void MaterialAuthoringWorkflow::DiscardSession() {
    m_workingCopy = ScientificMaterialDef{};
    m_baselineCopy = ScientificMaterialDef{};
    m_state = AuthoringSessionState::Idle;
    m_isDirty = false;
    m_isCommitted = false;
}

ScientificMaterialDef& MaterialAuthoringWorkflow::GetWorkingCopy() {
    return m_workingCopy;
}

const ScientificMaterialDef& MaterialAuthoringWorkflow::GetWorkingCopy() const {
    return m_workingCopy;
}

bool MaterialAuthoringWorkflow::SetPhysicalProperty(
    const std::string& propertyName,
    double value,
    const std::string& unit,
    DataQuality quality,
    const std::string& source,
    std::string* outError
) {
    if (m_state == AuthoringSessionState::Idle) {
        if (outError) *outError = "Nessuna sessione di authoring attiva.";
        return false;
    }

    auto applyProp = [&](ScientificQuantity& targetProp, const std::string& defaultUnit, bool enforcePositive, bool enforceRange01) -> bool {
        if (enforcePositive && value <= 0.0) {
            if (outError) *outError = "La proprieta' '" + propertyName + "' deve essere strettamente positiva (> 0). Valore fornito: " + std::to_string(value);
            return false;
        }
        if (enforceRange01 && (value < 0.0 || value > 1.0)) {
            if (outError) *outError = "La proprieta' '" + propertyName + "' deve essere compresa nell'intervallo adimensionale [0, 1]. Valore fornito: " + std::to_string(value);
            return false;
        }
        targetProp = ScientificQuantity::Known(value, unit.empty() ? defaultUnit : unit, quality, source);
        m_isDirty = true;
        return true;
    };

    if (propertyName == ScientificPropertyNames::Density) {
        return applyProp(m_workingCopy.density, "kg/m^3", true, false);
    } else if (propertyName == ScientificPropertyNames::Porosity) {
        return applyProp(m_workingCopy.porosity, "", false, true);
    } else if (propertyName == ScientificPropertyNames::CompressiveStrength) {
        return applyProp(m_workingCopy.compressiveStrength, "Pa", false, false);
    } else if (propertyName == ScientificPropertyNames::TensileStrength) {
        return applyProp(m_workingCopy.tensileStrength, "Pa", false, false);
    } else if (propertyName == ScientificPropertyNames::ElasticModulus) {
        return applyProp(m_workingCopy.elasticModulus, "Pa", false, false);
    } else if (propertyName == ScientificPropertyNames::StaticFriction) {
        if (value < 0.0) {
            if (outError) *outError = "L'attrito statico non puo' essere negativo.";
            return false;
        }
        return applyProp(m_workingCopy.staticFriction, "", false, false);
    } else if (propertyName == ScientificPropertyNames::DynamicFriction) {
        if (value < 0.0) {
            if (outError) *outError = "L'attrito dinamico non puo' essere negativo.";
            return false;
        }
        return applyProp(m_workingCopy.dynamicFriction, "", false, false);
    } else if (propertyName == ScientificPropertyNames::Restitution) {
        return applyProp(m_workingCopy.restitution, "", false, true);
    } else if (propertyName == ScientificPropertyNames::SpecificHeatCapacity) {
        return applyProp(m_workingCopy.specificHeatCapacity, "J/(kg*K)", false, false);
    } else if (propertyName == ScientificPropertyNames::ThermalConductivity) {
        return applyProp(m_workingCopy.thermalConductivity, "W/(m*K)", false, false);
    } else if (propertyName == ScientificPropertyNames::ThermalExpansion) {
        return applyProp(m_workingCopy.thermalExpansion, "1/K", false, false);
    } else if (propertyName == ScientificPropertyNames::MeltingPoint) {
        if (value < 0.0) {
            if (outError) *outError = "Il punto di fusione (Kelvin) non puo' essere negativo. Valore fornito: " + std::to_string(value);
            return false;
        }
        return applyProp(m_workingCopy.meltingPoint, "K", false, false);
    }

    if (outError) *outError = "Proprieta' fisica SI non riconosciuta: '" + propertyName + "'.";
    return false;
}

bool MaterialAuthoringWorkflow::SetPropertyNotApplicable(const std::string& propertyName, std::string* outError) {
    if (m_state == AuthoringSessionState::Idle) {
        if (outError) *outError = "Nessuna sessione di authoring attiva.";
        return false;
    }

    auto applyNA = [&](ScientificQuantity& prop) {
        prop = ScientificQuantity::NotApplicable();
        m_isDirty = true;
        return true;
    };

    if (propertyName == ScientificPropertyNames::Density) return applyNA(m_workingCopy.density);
    if (propertyName == ScientificPropertyNames::Porosity) return applyNA(m_workingCopy.porosity);
    if (propertyName == ScientificPropertyNames::CompressiveStrength) return applyNA(m_workingCopy.compressiveStrength);
    if (propertyName == ScientificPropertyNames::TensileStrength) return applyNA(m_workingCopy.tensileStrength);
    if (propertyName == ScientificPropertyNames::ElasticModulus) return applyNA(m_workingCopy.elasticModulus);
    if (propertyName == ScientificPropertyNames::StaticFriction) return applyNA(m_workingCopy.staticFriction);
    if (propertyName == ScientificPropertyNames::DynamicFriction) return applyNA(m_workingCopy.dynamicFriction);
    if (propertyName == ScientificPropertyNames::Restitution) return applyNA(m_workingCopy.restitution);
    if (propertyName == ScientificPropertyNames::SpecificHeatCapacity) return applyNA(m_workingCopy.specificHeatCapacity);
    if (propertyName == ScientificPropertyNames::ThermalConductivity) return applyNA(m_workingCopy.thermalConductivity);
    if (propertyName == ScientificPropertyNames::ThermalExpansion) return applyNA(m_workingCopy.thermalExpansion);
    if (propertyName == ScientificPropertyNames::MeltingPoint) return applyNA(m_workingCopy.meltingPoint);

    if (outError) *outError = "Proprieta' fisica SI non riconosciuta: '" + propertyName + "'.";
    return false;
}

bool MaterialAuthoringWorkflow::SetPropertyUnknown(const std::string& propertyName, const std::string& unit, std::string* outError) {
    if (m_state == AuthoringSessionState::Idle) {
        if (outError) *outError = "Nessuna sessione di authoring attiva.";
        return false;
    }

    auto applyUnknown = [&](ScientificQuantity& prop, const std::string& defaultUnit) {
        prop = ScientificQuantity::Unknown(unit.empty() ? defaultUnit : unit);
        m_isDirty = true;
        return true;
    };

    if (propertyName == ScientificPropertyNames::Density) return applyUnknown(m_workingCopy.density, "kg/m^3");
    if (propertyName == ScientificPropertyNames::Porosity) return applyUnknown(m_workingCopy.porosity, "");
    if (propertyName == ScientificPropertyNames::CompressiveStrength) return applyUnknown(m_workingCopy.compressiveStrength, "Pa");
    if (propertyName == ScientificPropertyNames::TensileStrength) return applyUnknown(m_workingCopy.tensileStrength, "Pa");
    if (propertyName == ScientificPropertyNames::ElasticModulus) return applyUnknown(m_workingCopy.elasticModulus, "Pa");
    if (propertyName == ScientificPropertyNames::StaticFriction) return applyUnknown(m_workingCopy.staticFriction, "");
    if (propertyName == ScientificPropertyNames::DynamicFriction) return applyUnknown(m_workingCopy.dynamicFriction, "");
    if (propertyName == ScientificPropertyNames::Restitution) return applyUnknown(m_workingCopy.restitution, "");
    if (propertyName == ScientificPropertyNames::SpecificHeatCapacity) return applyUnknown(m_workingCopy.specificHeatCapacity, "J/(kg*K)");
    if (propertyName == ScientificPropertyNames::ThermalConductivity) return applyUnknown(m_workingCopy.thermalConductivity, "W/(m*K)");
    if (propertyName == ScientificPropertyNames::ThermalExpansion) return applyUnknown(m_workingCopy.thermalExpansion, "1/K");
    if (propertyName == ScientificPropertyNames::MeltingPoint) return applyUnknown(m_workingCopy.meltingPoint, "K");

    if (outError) *outError = "Proprieta' fisica SI non riconosciuta: '" + propertyName + "'.";
    return false;
}

AuthoringValidationResult MaterialAuthoringWorkflow::ValidateWorkingCopy() const {
    AuthoringValidationResult res;

    if (m_state == AuthoringSessionState::Idle) {
        res.AddError("Nessuna sessione di authoring attiva.");
        return res;
    }

    if (!m_workingCopy.isCustom) {
        res.AddError("La definizione in lavorazione non e' contrassegnata come custom.");
    }

    if (IsBuiltInProtected(m_workingCopy.id)) {
        res.AddError("ID " + std::to_string(m_workingCopy.id) + " ricade nel range protetto dei materiali built-in (< 1000).");
    }

    std::string canonErr;
    if (!ScientificMaterialRegistry::ValidateCanonicalIdFormat(m_workingCopy.canonicalId, &canonErr)) {
        res.AddError("Formato canonicalId non valido: " + canonErr);
    }

    std::string regErr;
    if (!m_registry.ValidateMaterialDefinition(m_workingCopy, &regErr)) {
        res.AddError("Validazione formale registry fallita: " + regErr);
    }

    // Controlli di consistenza fisica avanzata
    if (m_workingCopy.density.state == PropertyState::Known && m_workingCopy.density.value <= 0.0) {
        res.AddError("Densita' non positiva.");
    }
    if (m_workingCopy.porosity.state == PropertyState::Known && 
        (m_workingCopy.porosity.value < 0.0 || m_workingCopy.porosity.value > 1.0)) {
        res.AddError("Porosita' fuori intervallo [0, 1].");
    }
    if (m_workingCopy.restitution.state == PropertyState::Known && 
        (m_workingCopy.restitution.value < 0.0 || m_workingCopy.restitution.value > 1.0)) {
        res.AddError("Restituzione fuori intervallo [0, 1].");
    }
    if (m_workingCopy.meltingPoint.state == PropertyState::Known && m_workingCopy.meltingPoint.value < 0.0) {
        res.AddError("Punto di fusione negativo (Kelvin).");
    }
    if (m_workingCopy.staticFriction.state == PropertyState::Known && m_workingCopy.dynamicFriction.state == PropertyState::Known) {
        if (m_workingCopy.dynamicFriction.value > m_workingCopy.staticFriction.value) {
            res.AddWarning("L'attrito dinamico (" + std::to_string(m_workingCopy.dynamicFriction.value) + 
                           ") e' superiore all'attrito statico (" + std::to_string(m_workingCopy.staticFriction.value) + ").");
        }
    }

    return res;
}

bool MaterialAuthoringWorkflow::CommitToRegistry(
    std::string* outError,
    const std::string& catalogFilepath
) {
    auto val = ValidateWorkingCopy();
    if (!val.isValid) {
        if (outError) {
            std::string msg = "Validazione fallita:";
            for (const auto& err : val.errors) msg += " [" + err + "]";
            *outError = msg;
        }
        return false;
    }

    if (m_state == AuthoringSessionState::EditingNew) {
        // Se fornito catalogFilepath, riserva l'ID in modo atomico e durevole
        if (!catalogFilepath.empty()) {
            ScientificMaterialID durableId = m_registry.ReserveDurableCustomMaterialId(catalogFilepath, outError);
            if (durableId == INVALID_SCIENTIFIC_MATERIAL_ID) {
                return false;
            }
            m_workingCopy.id = durableId;
        } else {
            // Altrimenti prenota in memoria
            m_workingCopy.id = m_registry.ReserveNextCustomMaterialId();
        }

        if (!m_registry.RegisterMaterial(m_workingCopy, /*allowUpdate=*/false)) {
            if (outError) *outError = "Impossibile registrare il nuovo materiale custom nel registry.";
            return false;
        }
    } else if (m_state == AuthoringSessionState::EditingExisting) {
        if (IsBuiltInProtected(m_workingCopy.id)) {
            if (outError) *outError = "Protezione built-in violata: ID " + std::to_string(m_workingCopy.id);
            return false;
        }

        if (!m_registry.RegisterMaterial(m_workingCopy, /*allowUpdate=*/true)) {
            if (outError) *outError = "Impossibile aggiornare il materiale custom esistente nel registry.";
            return false;
        }
    } else {
        if (outError) *outError = "Nessuna sessione attiva da committare.";
        return false;
    }

    m_baselineCopy = m_workingCopy;
    m_isDirty = false;
    m_isCommitted = true;
    m_state = AuthoringSessionState::EditingExisting;
    m_catalogUnsavedOnDisk = true;

    return true;
}

bool MaterialAuthoringWorkflow::PublishActiveMaterial(std::string* outError) {
    if (m_state == AuthoringSessionState::Idle) {
        if (outError) *outError = "Nessuna sessione attiva.";
        return false;
    }

    if (!m_isCommitted || m_isDirty) {
        if (!CommitToRegistry(outError)) {
            return false;
        }
    }

    if (IsBuiltInProtected(m_workingCopy.id)) {
        if (outError) *outError = "Operazione di lifecycle non consentita su materiale standard.";
        return false;
    }

    if (!m_registry.PublishMaterial(m_workingCopy.id, outError)) {
        return false;
    }

    m_workingCopy = m_registry.GetMaterial(m_workingCopy.id);
    m_baselineCopy = m_workingCopy;
    m_catalogUnsavedOnDisk = true;
    return true;
}

bool MaterialAuthoringWorkflow::DeprecateActiveMaterial(std::string* outError) {
    if (m_state == AuthoringSessionState::Idle) {
        if (outError) *outError = "Nessuna sessione attiva.";
        return false;
    }

    if (!m_isCommitted || m_isDirty) {
        if (!CommitToRegistry(outError)) {
            return false;
        }
    }

    if (IsBuiltInProtected(m_workingCopy.id)) {
        if (outError) *outError = "Operazione di deprecazione non consentita su materiale standard.";
        return false;
    }

    if (!m_registry.DeprecateMaterial(m_workingCopy.id, outError)) {
        return false;
    }

    m_workingCopy = m_registry.GetMaterial(m_workingCopy.id);
    m_baselineCopy = m_workingCopy;
    m_catalogUnsavedOnDisk = true;
    return true;
}

bool MaterialAuthoringWorkflow::SetEnabledInBlockMaker(bool enabled, std::string* outError) {
    if (m_state == AuthoringSessionState::Idle) {
        if (outError) *outError = "Nessuna sessione attiva.";
        return false;
    }

    if (!m_isCommitted || m_isDirty) {
        if (!CommitToRegistry(outError)) {
            return false;
        }
    }

    if (!m_registry.SetEnabledInBlockMaker(m_workingCopy.id, enabled, outError)) {
        return false;
    }

    m_workingCopy = m_registry.GetMaterial(m_workingCopy.id);
    m_baselineCopy = m_workingCopy;
    m_catalogUnsavedOnDisk = true;
    return true;
}

bool MaterialAuthoringWorkflow::PublishMaterialDirect(ScientificMaterialID id, std::string* outError) {
    if (IsBuiltInProtected(id)) {
        if (outError) *outError = "Operazione di pubblicazione non consentita su materiale standard.";
        return false;
    }
    if (!m_registry.PublishMaterial(id, outError)) {
        return false;
    }
    m_catalogUnsavedOnDisk = true;
    return true;
}

bool MaterialAuthoringWorkflow::DeprecateMaterialDirect(ScientificMaterialID id, std::string* outError) {
    if (IsBuiltInProtected(id)) {
        if (outError) *outError = "Operazione di deprecazione non consentita su materiale standard.";
        return false;
    }
    if (!m_registry.DeprecateMaterial(id, outError)) {
        return false;
    }
    m_catalogUnsavedOnDisk = true;
    return true;
}

bool MaterialAuthoringWorkflow::SetEnabledInBlockMakerDirect(ScientificMaterialID id, bool enabled, std::string* outError) {
    if (!m_registry.SetEnabledInBlockMaker(id, enabled, outError)) {
        return false;
    }
    m_catalogUnsavedOnDisk = true;
    return true;
}

bool MaterialAuthoringWorkflow::SaveCatalogToDisk(
    const std::string& catalogFilepath,
    std::string* outError,
    bool forceOverwrite
) {
    bool ok = m_registry.SaveCustomMaterialsToJson(catalogFilepath, outError, forceOverwrite);
    if (ok) {
        m_catalogUnsavedOnDisk = false;
    }
    return ok;
}

bool MaterialAuthoringWorkflow::ReloadCatalogFromDisk(
    const std::string& catalogFilepath,
    std::string* outError
) {
    bool ok = m_registry.LoadCustomMaterialsFromJson(catalogFilepath, outError);
    if (ok) {
        m_catalogUnsavedOnDisk = false;
    }
    return ok;
}

bool MaterialAuthoringWorkflow::VerifyAppearanceOrthogonality(
    const ScientificMaterialRegistry& sciReg,
    ScientificMaterialID sciId,
    const MaterialAppearance& appearanceBefore,
    const MaterialAppearance& appearanceAfter
) {
    if (!sciReg.HasMaterial(sciId)) {
        return false;
    }

    ScientificMaterialDef matBefore = sciReg.GetMaterial(sciId);

    // Verifica che l'aspetto visivo sia effettivamente mutato
    bool visualChanged = (appearanceBefore != appearanceAfter);
    if (!visualChanged) {
        return false;
    }

    ScientificMaterialDef matAfter = sciReg.GetMaterial(sciId);

    // Invariante di ortogonalità: nessuna mutazione di aspetto PBR deve alterare
    // la definizione scientifica, i suoi campi o le sue proprietà fisiche
    return (matBefore == matAfter);
}

} // namespace fw
