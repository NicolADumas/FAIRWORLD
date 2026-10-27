#include "ScientificMaterialRegistry.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <unordered_set>

namespace fw {

using json = nlohmann::json;

// =============================================================================
// JSON SERIALIZATION HELPERS
// =============================================================================

static void to_json(json& j, const ScientificQuantity& q) {
    j = json{
        {"value", q.value},
        {"unit", q.unit},
        {"temperatureKelvin", q.temperatureKelvin},
        {"pressurePascal", q.pressurePascal},
        {"state", PropertyStateToString(q.state)},
        {"quality", DataQualityToString(q.quality)},
        {"source", q.source}
    };
}

static void from_json(const json& j, ScientificQuantity& q) {
    q.value = j.value("value", 0.0);
    q.unit = j.value("unit", "");
    q.temperatureKelvin = j.value("temperatureKelvin", 293.15);
    q.pressurePascal = j.value("pressurePascal", 101325.0);
    q.state = StringToPropertyState(j.value("state", "Unknown"));
    q.quality = StringToDataQuality(j.value("quality", "Estimated"));
    q.source = j.value("source", "");
}

static void to_json(json& j, const ScientificCurve& c) {
    json pts = json::array();
    for (const auto& p : c.points) {
        pts.push_back({{"x", p.first}, {"y", p.second}});
    }
    j = json{
        {"independentVar", c.independentVar},
        {"xUnit", c.xUnit},
        {"yUnit", c.yUnit},
        {"state", PropertyStateToString(c.state)},
        {"source", c.source},
        {"points", pts}
    };
}

static void from_json(const json& j, ScientificCurve& c) {
    c.independentVar = j.value("independentVar", "T");
    c.xUnit = j.value("xUnit", "K");
    c.yUnit = j.value("yUnit", "");
    c.state = StringToPropertyState(j.value("state", "Unknown"));
    c.source = j.value("source", "");
    c.points.clear();
    if (j.contains("points") && j["points"].is_array()) {
        for (const auto& pt : j["points"]) {
            c.points.emplace_back(pt.value("x", 0.0), pt.value("y", 0.0));
        }
    }
}

static void to_json(json& j, const ScientificTensor3x3& t) {
    json arr = json::array();
    for (double val : t.components) arr.push_back(val);
    j = json{
        {"isAnisotropic", t.isAnisotropic},
        {"unit", t.unit},
        {"state", PropertyStateToString(t.state)},
        {"components", arr}
    };
}

static void from_json(const json& j, ScientificTensor3x3& t) {
    t.isAnisotropic = j.value("isAnisotropic", false);
    t.unit = j.value("unit", "");
    t.state = StringToPropertyState(j.value("state", "Unknown"));
    if (j.contains("components") && j["components"].is_array() && j["components"].size() == 9) {
        for (size_t i = 0; i < 9; ++i) {
            t.components[i] = j["components"][i].get<double>();
        }
    }
}

// =============================================================================
// MA-02.3: VALIDATION HELPERS & OVERRIDES SERIALIZATION
// =============================================================================

bool IsValidScientificPropertyName(const std::string& name) {
    static const std::unordered_set<std::string> s_validProps = {
        ScientificPropertyNames::Density,
        ScientificPropertyNames::Porosity,
        ScientificPropertyNames::CompressiveStrength,
        ScientificPropertyNames::TensileStrength,
        ScientificPropertyNames::ElasticModulus,
        ScientificPropertyNames::StaticFriction,
        ScientificPropertyNames::DynamicFriction,
        ScientificPropertyNames::Restitution,
        ScientificPropertyNames::SpecificHeatCapacity,
        ScientificPropertyNames::ThermalConductivity,
        ScientificPropertyNames::ThermalExpansion,
        ScientificPropertyNames::MeltingPoint
    };
    return s_validProps.find(name) != s_validProps.end();
}

std::string GetCanonicalUnitForProperty(const std::string& name) {
    if (name == ScientificPropertyNames::Density) return "kg/m^3";
    if (name == ScientificPropertyNames::Porosity) return "";
    if (name == ScientificPropertyNames::CompressiveStrength) return "Pa";
    if (name == ScientificPropertyNames::TensileStrength) return "Pa";
    if (name == ScientificPropertyNames::ElasticModulus) return "Pa";
    if (name == ScientificPropertyNames::StaticFriction) return "";
    if (name == ScientificPropertyNames::DynamicFriction) return "";
    if (name == ScientificPropertyNames::Restitution) return "";
    if (name == ScientificPropertyNames::SpecificHeatCapacity) return "J/(kg*K)";
    if (name == ScientificPropertyNames::ThermalConductivity) return "W/(m*K)";
    if (name == ScientificPropertyNames::ThermalExpansion) return "1/K";
    if (name == ScientificPropertyNames::MeltingPoint) return "K";
    return "";
}

bool ValidateScientificQuantity(const std::string& propName, const ScientificQuantity& q, std::string* outError) {
    if (!IsValidScientificPropertyName(propName)) {
        if (outError) *outError = "Nome proprieta' non valido o non riconosciuto: " + propName;
        return false;
    }

    // Se lo stato è Unknown o NotApplicable, il valore non è vincolato (senza interpretare Unknown come zero)
    if (q.state == PropertyState::Unknown || q.state == PropertyState::NotApplicable) {
        return true;
    }

    // Validazione unita' (se fornita, deve coincidere con l'unita' SI canonica)
    std::string canonicalUnit = GetCanonicalUnitForProperty(propName);
    if (!q.unit.empty() && q.unit != canonicalUnit) {
        if (outError) *outError = "Unita' non compatibile per '" + propName + "': attesa '" + canonicalUnit + "', trovata '" + q.unit + "'";
        return false;
    }

    // Per PropertyState::Known, validazione finitezza e limiti fisici
    if (!std::isfinite(q.value)) {
        if (outError) *outError = "Valore non finito (NaN/Inf) per proprieta': " + propName;
        return false;
    }

    if (propName == ScientificPropertyNames::Density && q.value <= 0.0) {
        if (outError) *outError = "La densita' deve essere strettamente positiva (> 0)";
        return false;
    }
    if (propName == ScientificPropertyNames::Porosity && (q.value < 0.0 || q.value > 1.0)) {
        if (outError) *outError = "La porosita' deve essere compresa nell'intervallo [0.0, 1.0]";
        return false;
    }
    if (propName == ScientificPropertyNames::CompressiveStrength && q.value < 0.0) {
        if (outError) *outError = "La resistenza a compressione non puo' essere negativa";
        return false;
    }
    if (propName == ScientificPropertyNames::TensileStrength && q.value < 0.0) {
        if (outError) *outError = "La resistenza a trazione non puo' essere negativa";
        return false;
    }
    if (propName == ScientificPropertyNames::ElasticModulus && q.value < 0.0) {
        if (outError) *outError = "Il modulo elastico non puo' essere negativo";
        return false;
    }
    if (propName == ScientificPropertyNames::StaticFriction && q.value < 0.0) {
        if (outError) *outError = "L'attrito statico non puo' essere negativo";
        return false;
    }
    if (propName == ScientificPropertyNames::DynamicFriction && q.value < 0.0) {
        if (outError) *outError = "L'attrito dinamico non puo' essere negativo";
        return false;
    }
    if (propName == ScientificPropertyNames::Restitution && (q.value < 0.0 || q.value > 1.0)) {
        if (outError) *outError = "Il coefficiente di restituzione deve essere compreso nell'intervallo [0.0, 1.0]";
        return false;
    }
    if (propName == ScientificPropertyNames::SpecificHeatCapacity && q.value <= 0.0) {
        if (outError) *outError = "La capacita' termica specifica deve essere strettamente positiva (> 0)";
        return false;
    }
    if (propName == ScientificPropertyNames::ThermalConductivity && q.value < 0.0) {
        if (outError) *outError = "La conducibilita' termica non puo' essere negativa";
        return false;
    }
    if (propName == ScientificPropertyNames::MeltingPoint && q.value < 0.0) {
        if (outError) *outError = "Il punto di fusione (in Kelvin) non puo' essere negativo (< 0 K)";
        return false;
    }

    return true;
}

void to_json(json& j, const ScientificPropertyOverrides& o) {
    j = json::object();
    for (const auto& pair : o.overrides) {
        json jq;
        to_json(jq, pair.second);
        j[pair.first] = jq;
    }
}

void from_json(const json& j, ScientificPropertyOverrides& o) {
    o.Clear();
    if (!j.is_object()) return;
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string& propName = it.key();
        if (IsValidScientificPropertyName(propName)) {
            ScientificQuantity q;
            from_json(it.value(), q);
            std::string err;
            if (ValidateScientificQuantity(propName, q, &err)) {
                o.overrides[propName] = q;
            } else {
                std::cerr << "[ScientificPropertyOverrides] Override scartato per '" << propName << "': " << err << "\n";
            }
        }
    }
}

static void to_json(json& j, const ScientificMaterialDef& m) {
    j = json{
        {"id", m.id},
        {"canonicalId", m.canonicalId},
        {"displayName", m.displayName},
        {"chemicalFormula", m.chemicalFormula},
        {"classification", m.classification},
        {"description", m.description},
        {"density", m.density},
        {"porosity", m.porosity},
        {"compressiveStrength", m.compressiveStrength},
        {"tensileStrength", m.tensileStrength},
        {"elasticModulus", m.elasticModulus},
        {"staticFriction", m.staticFriction},
        {"dynamicFriction", m.dynamicFriction},
        {"restitution", m.restitution},
        {"specificHeatCapacity", m.specificHeatCapacity},
        {"thermalConductivity", m.thermalConductivity},
        {"thermalExpansion", m.thermalExpansion},
        {"meltingPoint", m.meltingPoint}
    };

    if (!m.responseCurves.empty()) {
        json jCurves = json::object();
        for (const auto& pair : m.responseCurves) {
            jCurves[pair.first] = pair.second;
        }
        j["responseCurves"] = jCurves;
    }

    if (!m.tensors.empty()) {
        json jTensors = json::object();
        for (const auto& pair : m.tensors) {
            jTensors[pair.first] = pair.second;
        }
        j["tensors"] = jTensors;
    }
}

static void from_json(const json& j, ScientificMaterialDef& m) {
    m.id = j.value("id", (ScientificMaterialID)INVALID_SCIENTIFIC_MATERIAL_ID);
    m.canonicalId = j.value("canonicalId", "fairworld:mat/unknown");
    m.displayName = j.value("displayName", "Unknown Material");
    m.chemicalFormula = j.value("chemicalFormula", "");
    m.classification = j.value("classification", "");
    m.description = j.value("description", "");

    if (j.contains("density")) j.at("density").get_to(m.density);
    if (j.contains("porosity")) j.at("porosity").get_to(m.porosity);
    if (j.contains("compressiveStrength")) j.at("compressiveStrength").get_to(m.compressiveStrength);
    if (j.contains("tensileStrength")) j.at("tensileStrength").get_to(m.tensileStrength);
    if (j.contains("elasticModulus")) j.at("elasticModulus").get_to(m.elasticModulus);
    if (j.contains("staticFriction")) j.at("staticFriction").get_to(m.staticFriction);
    if (j.contains("dynamicFriction")) j.at("dynamicFriction").get_to(m.dynamicFriction);
    if (j.contains("restitution")) j.at("restitution").get_to(m.restitution);

    if (j.contains("specificHeatCapacity")) j.at("specificHeatCapacity").get_to(m.specificHeatCapacity);
    if (j.contains("thermalConductivity")) j.at("thermalConductivity").get_to(m.thermalConductivity);
    if (j.contains("thermalExpansion")) j.at("thermalExpansion").get_to(m.thermalExpansion);
    if (j.contains("meltingPoint")) j.at("meltingPoint").get_to(m.meltingPoint);

    m.responseCurves.clear();
    if (j.contains("responseCurves") && j["responseCurves"].is_object()) {
        for (auto it = j["responseCurves"].begin(); it != j["responseCurves"].end(); ++it) {
            m.responseCurves[it.key()] = it.value().get<ScientificCurve>();
        }
    }

    m.tensors.clear();
    if (j.contains("tensors") && j["tensors"].is_object()) {
        for (auto it = j["tensors"].begin(); it != j["tensors"].end(); ++it) {
            m.tensors[it.key()] = it.value().get<ScientificTensor3x3>();
        }
    }
}

// =============================================================================
// REGISTRY IMPLEMENTATION
// =============================================================================

ScientificMaterialRegistry::ScientificMaterialRegistry() {
    m_fallbackMaterial.id = INVALID_SCIENTIFIC_MATERIAL_ID;
    m_fallbackMaterial.canonicalId = "fairworld:mat/unknown";
    m_fallbackMaterial.displayName = "Unknown Material";
    m_fallbackMaterial.description = "Fallback material definition for unresolved lookups.";
}

void ScientificMaterialRegistry::Initialize() {
    Clear();
    RegisterStandardMaterials();
}

void ScientificMaterialRegistry::Clear() {
    m_materials.clear();
    m_canonicalToIdMap.clear();
}

bool ScientificMaterialRegistry::RegisterMaterial(const ScientificMaterialDef& def, bool allowUpdate) {
    if (def.id == INVALID_SCIENTIFIC_MATERIAL_ID) {
        std::cerr << "[ScientificMaterialRegistry] Errore: tentativo di registrare ID 0 invalido!\n";
        return false;
    }
    if (def.canonicalId.empty()) {
        std::cerr << "[ScientificMaterialRegistry] Errore: canonicalId vuoto per ID " << def.id << "!\n";
        return false;
    }

    auto itId = m_materials.find(def.id);
    auto itCanon = m_canonicalToIdMap.find(def.canonicalId);

    // Se l'ID o il canonicalId sono gia' presenti nel registry
    if (itId != m_materials.end() || itCanon != m_canonicalToIdMap.end()) {
        // Verifica se si tratta dello stesso identico materiale (stesso ID e stesso canonicalId)
        bool isSameMaterial = (itId != m_materials.end() && itId->second.canonicalId == def.canonicalId) &&
                              (itCanon != m_canonicalToIdMap.end() && itCanon->second == def.id);

        if (isSameMaterial) {
            if (allowUpdate) {
                // Aggiornamento autoritativo consentito (es. da JSON rispetto a default in-memory)
                m_materials[def.id] = def;
                m_canonicalToIdMap[def.canonicalId] = def.id;
                return true;
            } else {
                std::cerr << "[ScientificMaterialRegistry] Registrazione duplicata rifiutata: ScientificMaterialID " << def.id 
                          << " ('" << def.canonicalId << "') gia' registrato.\n";
                return false;
            }
        }

        // Conflitto reale: ID numerico gia' assegnato a un altro canonicalId
        if (itId != m_materials.end() && itId->second.canonicalId != def.canonicalId) {
            std::cerr << "[ScientificMaterialRegistry] Conflitto ID: ScientificMaterialID " << def.id 
                      << " gia' assegnato a '" << itId->second.canonicalId 
                      << "'. Rifiutata registrazione per '" << def.canonicalId << "'.\n";
            return false;
        }

        // Conflitto reale: canonicalId gia' mappato a un altro ID numerico
        if (itCanon != m_canonicalToIdMap.end() && itCanon->second != def.id) {
            std::cerr << "[ScientificMaterialRegistry] Conflitto canonicalId: '" << def.canonicalId 
                      << "' gia' mappato all'ID " << itCanon->second 
                      << ". Rifiutata registrazione con ID " << def.id << ".\n";
            return false;
        }
    }

    m_materials[def.id] = def;
    m_canonicalToIdMap[def.canonicalId] = def.id;
    return true;
}

bool ScientificMaterialRegistry::HasMaterial(ScientificMaterialID id) const {
    if (id == INVALID_SCIENTIFIC_MATERIAL_ID) return false;
    return m_materials.find(id) != m_materials.end();
}

bool ScientificMaterialRegistry::HasMaterial(const std::string& canonicalId) const {
    if (canonicalId.empty()) return false;
    return m_canonicalToIdMap.find(canonicalId) != m_canonicalToIdMap.end();
}

const ScientificMaterialDef& ScientificMaterialRegistry::GetMaterial(ScientificMaterialID id) const {
    if (id == INVALID_SCIENTIFIC_MATERIAL_ID) return m_fallbackMaterial;
    auto it = m_materials.find(id);
    if (it != m_materials.end()) {
        return it->second;
    }
    // Gestione esplicita ID non risolti: ritorna fallback sicuro senza crash
    return m_fallbackMaterial;
}

const ScientificMaterialDef& ScientificMaterialRegistry::GetMaterial(const std::string& canonicalId) const {
    if (canonicalId.empty()) return m_fallbackMaterial;
    auto it = m_canonicalToIdMap.find(canonicalId);
    if (it != m_canonicalToIdMap.end()) {
        return GetMaterial(it->second);
    }
    return m_fallbackMaterial;
}

ScientificMaterialID ScientificMaterialRegistry::GetMaterialIdByCanonical(const std::string& canonicalId) const {
    auto it = m_canonicalToIdMap.find(canonicalId);
    if (it != m_canonicalToIdMap.end()) {
        return it->second;
    }
    return INVALID_SCIENTIFIC_MATERIAL_ID;
}

ScientificQuantity ScientificMaterialRegistry::ResolveProperty(
    ScientificMaterialID baseId, 
    const std::string& propertyName, 
    const ScientificPropertyOverrides& overrides) const 
{
    // 1. Priorita' assoluta all'override esplicito per-asset (preservando Known/Unknown/NotApplicable)
    if (const ScientificQuantity* ov = overrides.GetOverride(propertyName)) {
        return *ov;
    }

    // 2. Risoluzione tramite ereditarieta' dal materiale scientifico di base
    if (baseId != INVALID_SCIENTIFIC_MATERIAL_ID && HasMaterial(baseId)) {
        const auto& mat = GetMaterial(baseId);
        if (propertyName == ScientificPropertyNames::Density) return mat.density;
        if (propertyName == ScientificPropertyNames::Porosity) return mat.porosity;
        if (propertyName == ScientificPropertyNames::CompressiveStrength) return mat.compressiveStrength;
        if (propertyName == ScientificPropertyNames::TensileStrength) return mat.tensileStrength;
        if (propertyName == ScientificPropertyNames::ElasticModulus) return mat.elasticModulus;
        if (propertyName == ScientificPropertyNames::StaticFriction) return mat.staticFriction;
        if (propertyName == ScientificPropertyNames::DynamicFriction) return mat.dynamicFriction;
        if (propertyName == ScientificPropertyNames::Restitution) return mat.restitution;
        if (propertyName == ScientificPropertyNames::SpecificHeatCapacity) return mat.specificHeatCapacity;
        if (propertyName == ScientificPropertyNames::ThermalConductivity) return mat.thermalConductivity;
        if (propertyName == ScientificPropertyNames::ThermalExpansion) return mat.thermalExpansion;
        if (propertyName == ScientificPropertyNames::MeltingPoint) return mat.meltingPoint;
    }

    // 3. Proprieta' non trovata ne' ereditabile: ritorna Unknown con unita' SI canonica
    return ScientificQuantity::Unknown(GetCanonicalUnitForProperty(propertyName));
}

bool ScientificMaterialRegistry::LoadFromJson(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[ScientificMaterialRegistry] File non trovato: " << filepath 
                  << ". Attivato fallback esplicito: utilizzati materiali scientifici standard.\n";
        if (m_materials.empty()) {
            RegisterStandardMaterials();
        }
        return false;
    }

    json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        std::cerr << "[ScientificMaterialRegistry] Errore parsing JSON in " << filepath << ": " << e.what() 
                  << ". Mantenute definizioni esistenti.\n";
        if (m_materials.empty()) {
            RegisterStandardMaterials();
        }
        return false;
    }

    uint32_t schemaVersion = j.value("schemaVersion", 1u);
    if (schemaVersion > 1) {
        std::cerr << "[ScientificMaterialRegistry] Versione schema non supportata: " << schemaVersion << " in " << filepath << "\n";
        return false;
    }

    if (!j.contains("materials") || !j["materials"].is_array()) {
        std::cerr << "[ScientificMaterialRegistry] Struttura 'materials' non trovata in " << filepath << "\n";
        return false;
    }

    size_t loadedCount = 0;
    size_t conflictCount = 0;
    std::unordered_set<ScientificMaterialID> jsonSeenIds;
    std::unordered_set<std::string> jsonSeenCanonicals;

    for (const auto& jMat : j["materials"]) {
        try {
            ScientificMaterialDef def;
            from_json(jMat, def);

            // Controllo duplicati interni al file JSON stesso
            if (jsonSeenIds.count(def.id) > 0) {
                std::cerr << "[ScientificMaterialRegistry] Duplicato nel JSON: ScientificMaterialID " << def.id 
                          << " ripetuto nel file " << filepath << ". Rifiutato.\n";
                conflictCount++;
                continue;
            }
            if (jsonSeenCanonicals.count(def.canonicalId) > 0) {
                std::cerr << "[ScientificMaterialRegistry] Duplicato nel JSON: canonicalId '" << def.canonicalId 
                          << "' ripetuto nel file " << filepath << ". Rifiutato.\n";
                conflictCount++;
                continue;
            }

            // Caricamento autoritativo con allowUpdate = true per sincronizzare default pre-registrati
            if (RegisterMaterial(def, /*allowUpdate=*/true)) {
                jsonSeenIds.insert(def.id);
                jsonSeenCanonicals.insert(def.canonicalId);
                loadedCount++;
            } else {
                conflictCount++;
            }
        } catch (const std::exception& e) {
            std::cerr << "[ScientificMaterialRegistry] Errore deserializzazione materiale: " << e.what() << "\n";
            conflictCount++;
        }
    }

    std::cout << "[ScientificMaterialRegistry] Caricati " << loadedCount 
              << " materiali scientifici da " << filepath 
              << " (conflitti/scartati: " << conflictCount << ")\n";
    return loadedCount > 0;
}

bool ScientificMaterialRegistry::SaveToJson(const std::string& filepath) const {
    json j;
    j["schemaVersion"] = 1;
    json jMaterials = json::array();

    // Ordina per ScientificMaterialID crescente per garantire persistenza deterministica
    std::vector<ScientificMaterialID> sortedIds;
    sortedIds.reserve(m_materials.size());
    for (const auto& pair : m_materials) sortedIds.push_back(pair.first);
    std::sort(sortedIds.begin(), sortedIds.end());

    for (ScientificMaterialID id : sortedIds) {
        const auto& def = m_materials.at(id);
        json jMat;
        to_json(jMat, def);
        jMaterials.push_back(jMat);
    }

    j["materials"] = jMaterials;

    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[ScientificMaterialRegistry] Impossibile aprire file per scrittura: " << filepath << "\n";
        return false;
    }

    file << j.dump(4);
    return true;
}

void ScientificMaterialRegistry::RegisterStandardMaterials() {
    // 1. Acqua Pura (Liquid Standard)
    {
        ScientificMaterialDef m;
        m.id = 1;
        m.canonicalId = "fairworld:mat/water";
        m.displayName = "Pure Water";
        m.chemicalFormula = "H2O";
        m.classification = "Liquid Oxide";
        m.description = "Pure liquid water at STP.";
        m.density = ScientificQuantity::Known(1000.0, "kg/m^3", DataQuality::SyntheticStandard, "IAPWS Release 1995");
        m.porosity = ScientificQuantity::NotApplicable();
        m.compressiveStrength = ScientificQuantity::NotApplicable();
        m.tensileStrength = ScientificQuantity::NotApplicable();
        m.elasticModulus = ScientificQuantity::Known(2.2e9, "Pa", DataQuality::PeerReviewed, "Bulk Modulus");
        m.staticFriction = ScientificQuantity::Known(0.1, "", DataQuality::Estimated, "Hydrodynamic boundary");
        m.dynamicFriction = ScientificQuantity::Known(0.05, "", DataQuality::Estimated, "Viscous shear");
        m.restitution = ScientificQuantity::Known(0.0, "", DataQuality::Experimental, "Inelastic liquid boundary");
        m.specificHeatCapacity = ScientificQuantity::Known(4184.0, "J/(kg*K)", DataQuality::SyntheticStandard, "CRC Handbook 97th");
        m.thermalConductivity = ScientificQuantity::Known(0.598, "W/(m*K)", DataQuality::SyntheticStandard, "CRC Handbook 97th");
        m.thermalExpansion = ScientificQuantity::Known(2.07e-4, "1/K", DataQuality::Experimental, "Volumetric at 20 C");
        m.meltingPoint = ScientificQuantity::Known(273.15, "K", DataQuality::SyntheticStandard, "ITS-90");
        RegisterMaterial(m);
    }

    // 2. Granito (Plutonic Igneous Rock)
    {
        ScientificMaterialDef m;
        m.id = 2;
        m.canonicalId = "fairworld:mat/granite";
        m.displayName = "Granite";
        m.chemicalFormula = "SiO2 (72%), Al2O3 (14%), K2O/Na2O (9%)";
        m.classification = "Igneous / Felsic Plutonic";
        m.description = "Coarse-grained intrusive igneous rock rich in quartz and feldspar.";
        m.density = ScientificQuantity::Known(2650.0, "kg/m^3", DataQuality::PeerReviewed, "USGS Rock Properties");
        m.porosity = ScientificQuantity::Known(0.015, "", DataQuality::Experimental, "Average crystalline porosity");
        m.compressiveStrength = ScientificQuantity::Known(1.75e8, "Pa", DataQuality::PeerReviewed, "ASTM C170 Uniaxial");
        m.tensileStrength = ScientificQuantity::Known(7.5e6, "Pa", DataQuality::Experimental, "Brazilian tensile test");
        m.elasticModulus = ScientificQuantity::Known(5.0e10, "Pa", DataQuality::PeerReviewed, "Young's Modulus");
        m.staticFriction = ScientificQuantity::Known(0.65, "", DataQuality::Experimental, "Byerlee's law");
        m.dynamicFriction = ScientificQuantity::Known(0.55, "", DataQuality::Experimental, "Byerlee's law");
        m.restitution = ScientificQuantity::Known(0.15, "", DataQuality::Estimated, "Elastic collision recovery");
        m.specificHeatCapacity = ScientificQuantity::Known(790.0, "J/(kg*K)", DataQuality::PeerReviewed, "Waples & Waples 2004");
        m.thermalConductivity = ScientificQuantity::Known(2.8, "W/(m*K)", DataQuality::PeerReviewed, "Robertson 1988");
        m.thermalExpansion = ScientificQuantity::Known(8.0e-6, "1/K", DataQuality::Experimental, "Linear thermal coefficient");
        m.meltingPoint = ScientificQuantity::Known(1488.15, "K", DataQuality::PeerReviewed, "Liquidus temperature (~1215 C)");
        RegisterMaterial(m);
    }

    // 3. Basalto (Volcanic Mafic Rock)
    {
        ScientificMaterialDef m;
        m.id = 3;
        m.canonicalId = "fairworld:mat/basalt";
        m.displayName = "Basalt";
        m.chemicalFormula = "SiO2 (49%), Al2O3 (16%), FeO/Fe2O3 (11%), CaO (10%), MgO (7%)";
        m.classification = "Igneous / Mafic Volcanic";
        m.description = "Aphanitic extrusive igneous rock forming oceanic crust and planetary plains.";
        m.density = ScientificQuantity::Known(2900.0, "kg/m^3", DataQuality::PeerReviewed, "USGS Rock Properties");
        m.porosity = ScientificQuantity::Known(0.03, "", DataQuality::Experimental, "Vesicular-compact mean");
        m.compressiveStrength = ScientificQuantity::Known(2.0e8, "Pa", DataQuality::PeerReviewed, "Uniaxial compressive strength");
        m.tensileStrength = ScientificQuantity::Known(1.2e7, "Pa", DataQuality::Experimental, "Tensile strength");
        m.elasticModulus = ScientificQuantity::Known(6.5e10, "Pa", DataQuality::PeerReviewed, "Young's Modulus");
        m.staticFriction = ScientificQuantity::Known(0.70, "", DataQuality::Experimental, "Dry surface contact");
        m.dynamicFriction = ScientificQuantity::Known(0.60, "", DataQuality::Experimental, "Sliding friction");
        m.restitution = ScientificQuantity::Known(0.20, "", DataQuality::Estimated, "Rock impact rebound");
        m.specificHeatCapacity = ScientificQuantity::Known(840.0, "J/(kg*K)", DataQuality::PeerReviewed, "Thermal properties of rocks");
        m.thermalConductivity = ScientificQuantity::Known(2.1, "W/(m*K)", DataQuality::PeerReviewed, "Clauser & Huenges 1995");
        m.thermalExpansion = ScientificQuantity::Known(6.5e-6, "1/K", DataQuality::Experimental, "Linear expansion coefficient");
        m.meltingPoint = ScientificQuantity::Known(1473.15, "K", DataQuality::PeerReviewed, "Liquidus temperature (~1200 C)");
        RegisterMaterial(m);
    }

    // 4. Ferro Puro / Acciaio Standard (Metallic Element/Alloy)
    {
        ScientificMaterialDef m;
        m.id = 4;
        m.canonicalId = "fairworld:mat/iron";
        m.displayName = "Iron / Structural Steel";
        m.chemicalFormula = "Fe (>98%), C (<0.2%)";
        m.classification = "Transition Metal / Alloy";
        m.description = "Structural iron with high tensile strength and thermal conductivity.";
        m.density = ScientificQuantity::Known(7870.0, "kg/m^3", DataQuality::SyntheticStandard, "NIST Chemistry WebBook");
        m.porosity = ScientificQuantity::Known(0.0, "", DataQuality::SyntheticStandard, "Full density wrought metal");
        m.compressiveStrength = ScientificQuantity::Known(2.5e8, "Pa", DataQuality::PeerReviewed, "Yield strength in compression");
        m.tensileStrength = ScientificQuantity::Known(4.0e8, "Pa", DataQuality::PeerReviewed, "Ultimate tensile strength");
        m.elasticModulus = ScientificQuantity::Known(2.1e11, "Pa", DataQuality::SyntheticStandard, "Young's Modulus 210 GPa");
        m.staticFriction = ScientificQuantity::Known(0.50, "", DataQuality::Experimental, "Steel on steel dry");
        m.dynamicFriction = ScientificQuantity::Known(0.40, "", DataQuality::Experimental, "Steel on steel dry");
        m.restitution = ScientificQuantity::Known(0.45, "", DataQuality::Experimental, "Elastic ball rebound");
        m.specificHeatCapacity = ScientificQuantity::Known(450.0, "J/(kg*K)", DataQuality::SyntheticStandard, "NIST 298K");
        m.thermalConductivity = ScientificQuantity::Known(80.0, "W/(m*K)", DataQuality::SyntheticStandard, "NIST 298K");
        m.thermalExpansion = ScientificQuantity::Known(1.18e-5, "1/K", DataQuality::SyntheticStandard, "Linear coefficient");
        m.meltingPoint = ScientificQuantity::Known(1811.0, "K", DataQuality::SyntheticStandard, "Pure Fe melting point (1538 C)");
        RegisterMaterial(m);
    }
}

} // namespace fw
