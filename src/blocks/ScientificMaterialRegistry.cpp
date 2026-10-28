#include "ScientificMaterialRegistry.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <cmath>

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

// =============================================================================
// MA-03.1: TAXONOMY & COMPOSITION JSON SERIALIZATION
// =============================================================================

void to_json(json& j, const ComponentQuantity& q) {
    j = json{
        {"kind", QuantityKindToString(q.kind)},
        {"basis", FractionBasisToString(q.basis)},
        {"quality", DataQualityToString(q.quality)},
        {"source", q.source}
    };
    if (q.kind == QuantityKind::Exact) {
        j["exact"] = q.exact;
        j["fraction"] = q.exact; // retrocompatibilita' per consumer che leggono "fraction"
    } else if (q.kind == QuantityKind::Range) {
        j["min"] = q.min;
        j["max"] = q.max;
    }
}

void from_json(const json& j, ComponentQuantity& q) {
    q.basis = StringToFractionBasis(j.value("basis", "Mass"));
    q.quality = StringToDataQuality(j.value("quality", "Estimated"));
    q.source = j.value("source", "");

    if (j.contains("kind")) {
        q.kind = StringToQuantityKind(j["kind"].get<std::string>());
    } else if (j.contains("min") && j.contains("max")) {
        q.kind = QuantityKind::Range;
    } else if (j.contains("exact") || j.contains("fraction")) {
        q.kind = QuantityKind::Exact;
    } else if (j.contains("state") && j["state"].get<std::string>() == "Unknown") {
        q.kind = QuantityKind::Unknown;
    } else {
        q.kind = QuantityKind::Unknown;
    }

    if (q.kind == QuantityKind::Exact) {
        q.exact = j.value("exact", j.value("fraction", 0.0));
        q.min = q.exact;
        q.max = q.exact;
    } else if (q.kind == QuantityKind::Range) {
        q.min = j.value("min", 0.0);
        q.max = j.value("max", 0.0);
        q.exact = 0.5 * (q.min + q.max);
    } else {
        q.exact = 0.0;
        q.min = 0.0;
        q.max = 0.0;
    }
}

void to_json(json& j, const ScientificComponent& c) {
    j = json{
        {"canonicalId", c.canonicalId},
        {"displayName", c.displayName},
        {"formula", c.formula},
        {"quantity", c.quantity},
        {"role", c.role}
    };
    if (c.materialId != INVALID_SCIENTIFIC_MATERIAL_ID) {
        j["materialId"] = c.materialId;
    }
}

void from_json(const json& j, ScientificComponent& c) {
    c.canonicalId = j.value("canonicalId", "");
    c.materialId = j.value("materialId", (ScientificMaterialID)INVALID_SCIENTIFIC_MATERIAL_ID);
    c.displayName = j.value("displayName", "");
    c.formula = j.value("formula", "");
    if (j.contains("quantity")) {
        j.at("quantity").get_to(c.quantity);
    } else if (j.contains("min") && j.contains("max")) {
        c.quantity.kind = QuantityKind::Range;
        c.quantity.min = j.value("min", 0.0);
        c.quantity.max = j.value("max", 0.0);
        c.quantity.exact = 0.5 * (c.quantity.min + c.quantity.max);
        if (j.contains("basis")) c.quantity.basis = StringToFractionBasis(j["basis"].get<std::string>());
    } else if (j.contains("exact") || j.contains("fraction")) {
        c.quantity.kind = QuantityKind::Exact;
        double val = j.value("exact", j.value("fraction", 0.0));
        c.quantity.exact = val;
        c.quantity.min = val;
        c.quantity.max = val;
        if (j.contains("basis")) c.quantity.basis = StringToFractionBasis(j["basis"].get<std::string>());
    } else {
        c.quantity = ComponentQuantity::Unknown();
    }
    c.role = j.value("role", "");
}

void to_json(json& j, const ScientificComposition& comp) {
    j = json{
        {"completeness", CompositionCompletenessToString(comp.completeness)},
        {"defaultBasis", FractionBasisToString(comp.defaultBasis)},
        {"notes", comp.notes}
    };
    json comps = json::array();
    for (const auto& c : comp.components) {
        json jc;
        to_json(jc, c);
        comps.push_back(jc);
    }
    j["components"] = comps;
}

void from_json(const json& j, ScientificComposition& comp) {
    comp.completeness = StringToCompositionCompleteness(j.value("completeness", "Unknown"));
    comp.defaultBasis = StringToFractionBasis(j.value("defaultBasis", "Mass"));
    comp.notes = j.value("notes", "");
    comp.components.clear();
    if (j.contains("components") && j["components"].is_array()) {
        for (const auto& jc : j["components"]) {
            ScientificComponent c;
            from_json(jc, c);
            comp.components.push_back(c);
        }
    }
}

void to_json(json& j, const ScientificRelation& r) {
    j = json{
        {"type", ScientificRelationTypeToString(r.type)},
        {"targetCanonicalId", r.targetCanonicalId},
        {"notes", r.notes}
    };
    if (r.targetId != INVALID_SCIENTIFIC_MATERIAL_ID) {
        j["targetId"] = r.targetId;
    }
}

void from_json(const json& j, ScientificRelation& r) {
    r.type = StringToScientificRelationType(j.value("type", "Unknown"));
    r.targetCanonicalId = j.value("targetCanonicalId", "");
    r.targetId = j.value("targetId", (ScientificMaterialID)INVALID_SCIENTIFIC_MATERIAL_ID);
    r.notes = j.value("notes", "");
}

void to_json(json& j, const ScientificMaterialDef& m) {
    j = json{
        {"id", m.id},
        {"canonicalId", m.canonicalId},
        {"displayName", m.displayName},
        {"chemicalFormula", m.chemicalFormula},
        {"classification", m.classification},
        {"description", m.description},
        {"kind", ScientificKindToString(m.kind)},
        {"aggregationState", AggregationStateToString(m.aggregationState)},
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

    if (!m.tags.empty()) {
        j["tags"] = m.tags;
    }

    if (!m.relations.empty()) {
        json jRels = json::array();
        for (const auto& r : m.relations) {
            json jr;
            to_json(jr, r);
            jRels.push_back(jr);
        }
        j["relations"] = jRels;
    }

    if (!m.composition.IsEmpty() || m.composition.completeness != CompositionCompleteness::Unknown) {
        json jComp;
        to_json(jComp, m.composition);
        j["composition"] = jComp;
    }

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

    if (!m.extendedProperties.Empty()) {
        json jExt;
        to_json(jExt, m.extendedProperties);
        j["extendedProperties"] = jExt;
    }
}

void from_json(const json& j, ScientificMaterialDef& m) {
    m.id = j.value("id", (ScientificMaterialID)INVALID_SCIENTIFIC_MATERIAL_ID);
    m.canonicalId = j.value("canonicalId", "fairworld:mat/unknown");
    m.displayName = j.value("displayName", "Unknown Material");
    m.chemicalFormula = j.value("chemicalFormula", "");
    m.classification = j.value("classification", "");
    m.description = j.value("description", "");

    if (j.contains("kind")) {
        m.kind = StringToScientificKind(j["kind"].get<std::string>());
    } else {
        m.kind = ScientificKind::Unknown;
    }

    if (j.contains("aggregationState")) {
        m.aggregationState = StringToAggregationState(j["aggregationState"].get<std::string>());
    } else {
        m.aggregationState = AggregationState::Unknown;
    }

    m.tags.clear();
    if (j.contains("tags") && j["tags"].is_array()) {
        for (const auto& item : j["tags"]) {
            if (item.is_string()) m.tags.push_back(item.get<std::string>());
        }
    }

    m.relations.clear();
    if (j.contains("relations") && j["relations"].is_array()) {
        for (const auto& item : j["relations"]) {
            ScientificRelation r;
            from_json(item, r);
            m.relations.push_back(r);
        }
    }

    if (j.contains("composition")) {
        from_json(j["composition"], m.composition);
    } else {
        m.composition = ScientificComposition();
    }

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

    m.extendedProperties.Clear();
    if (j.contains("extendedProperties") && j["extendedProperties"].is_array()) {
        from_json(j["extendedProperties"], m.extendedProperties);
    }
}

// =============================================================================
// MA-03.2D: SERIALIZZAZIONE E DEFINIZIONI PROPRIETA' ESTESE
// =============================================================================

void to_json(json& j, const PressureSpec& p) {
    j = json{
        {"refType", PressureSpec::ReferenceTypeToString(p.refType)},
        {"valuePascal", p.valuePascal}
    };
}

void from_json(const json& j, PressureSpec& p) {
    p.refType = PressureSpec::StringToReferenceType(j.value("refType", "Absolute"));
    p.valuePascal = j.value("valuePascal", 101325.0);
}

void to_json(json& j, const MetrologicalUncertainty& u) {
    j = json{
        {"hasUncertainty", u.hasUncertainty},
        {"value", u.value},
        {"type", u.type},
        {"unit", u.unit}
    };
}

void from_json(const json& j, MetrologicalUncertainty& u) {
    u.hasUncertainty = j.value("hasUncertainty", false);
    u.value = j.value("value", 0.0);
    u.type = j.value("type", "");
    u.unit = j.value("unit", "");
}

void to_json(json& j, const SamplePopulationDispersion& d) {
    j = json{
        {"hasDispersion", d.hasDispersion},
        {"stdDev", d.stdDev},
        {"minObserved", d.minObserved},
        {"maxObserved", d.maxObserved},
        {"sampleCount", d.sampleCount},
        {"unit", d.unit}
    };
}

void from_json(const json& j, SamplePopulationDispersion& d) {
    d.hasDispersion = j.value("hasDispersion", false);
    d.stdDev = j.value("stdDev", 0.0);
    d.minObserved = j.value("minObserved", 0.0);
    d.maxObserved = j.value("maxObserved", 0.0);
    d.sampleCount = j.value("sampleCount", size_t(0));
    d.unit = j.value("unit", "");
}

void to_json(json& j, const ExtendedMeasurement& m) {
    j = json{
        {"valueNature", ValueNatureToString(m.valueNature)},
        {"unit", m.unit},
        {"evidenceLevel", EvidenceLevelToString(m.evidenceLevel)},
        {"bibliographicCitation", m.bibliographicCitation},
        {"doiOrIsbn", m.doiOrIsbn}
    };
    if (m.scalarValue.has_value()) {
        j["scalarValue"] = *m.scalarValue;
    }
    if (m.rangeValue.has_value()) {
        j["rangeValue"] = {{"min", m.rangeValue->first}, {"max", m.rangeValue->second}};
    }
    if (!m.qualitativeDescriptor.empty()) {
        j["qualitativeDescriptor"] = m.qualitativeDescriptor;
    }
    if (m.uncertainty.hasUncertainty) {
        json ju;
        to_json(ju, m.uncertainty);
        j["uncertainty"] = ju;
    }
    if (m.dispersion.hasDispersion) {
        json jd;
        to_json(jd, m.dispersion);
        j["dispersion"] = jd;
    }
    if (m.temperatureKelvin.has_value()) {
        j["temperatureKelvin"] = *m.temperatureKelvin;
    }
    if (m.thermodynamicPressure.has_value()) {
        json jp;
        to_json(jp, *m.thermodynamicPressure);
        j["thermodynamicPressure"] = jp;
    }
    if (m.confiningPressure.has_value()) {
        json jp;
        to_json(jp, *m.confiningPressure);
        j["confiningPressure"] = jp;
    }
    if (m.porePressure.has_value()) {
        json jp;
        to_json(jp, *m.porePressure);
        j["porePressure"] = jp;
    }
    if (!m.thermodynamicPhase.empty()) j["thermodynamicPhase"] = m.thermodynamicPhase;
    if (!m.sampleState.empty()) j["sampleState"] = m.sampleState;
    if (!m.experimentalMethod.empty()) j["experimentalMethod"] = m.experimentalMethod;
    if (!m.constitutiveModel.empty()) j["constitutiveModel"] = m.constitutiveModel;
    if (!m.validityRegime.empty()) j["validityRegime"] = m.validityRegime;
    if (!m.interfacePhase1.empty()) j["interfacePhase1"] = m.interfacePhase1;
    if (!m.interfacePhase2.empty()) j["interfacePhase2"] = m.interfacePhase2;
}

void from_json(const json& j, ExtendedMeasurement& m) {
    m.valueNature = StringToValueNature(j.value("valueNature", "ExactScalar"));
    m.unit = j.value("unit", "");
    m.evidenceLevel = StringToEvidenceLevel(j.value("evidenceLevel", "PeerReviewedLiterature"));
    m.bibliographicCitation = j.value("bibliographicCitation", "");
    m.doiOrIsbn = j.value("doiOrIsbn", "");

    if (j.contains("scalarValue") && !j["scalarValue"].is_null()) {
        m.scalarValue = j["scalarValue"].get<double>();
    } else {
        m.scalarValue.reset();
    }

    if (j.contains("rangeValue") && j["rangeValue"].is_object()) {
        double rMin = j["rangeValue"].value("min", 0.0);
        double rMax = j["rangeValue"].value("max", 0.0);
        m.rangeValue = std::make_pair(rMin, rMax);
    } else {
        m.rangeValue.reset();
    }

    m.qualitativeDescriptor = j.value("qualitativeDescriptor", "");

    if (j.contains("uncertainty") && j["uncertainty"].is_object()) {
        from_json(j["uncertainty"], m.uncertainty);
    } else {
        m.uncertainty = MetrologicalUncertainty();
    }

    if (j.contains("dispersion") && j["dispersion"].is_object()) {
        from_json(j["dispersion"], m.dispersion);
    } else {
        m.dispersion = SamplePopulationDispersion();
    }

    if (j.contains("temperatureKelvin") && !j["temperatureKelvin"].is_null()) {
        m.temperatureKelvin = j["temperatureKelvin"].get<double>();
    } else {
        m.temperatureKelvin.reset();
    }

    if (j.contains("thermodynamicPressure") && j["thermodynamicPressure"].is_object()) {
        PressureSpec ps;
        from_json(j["thermodynamicPressure"], ps);
        m.thermodynamicPressure = ps;
    } else {
        m.thermodynamicPressure.reset();
    }

    if (j.contains("confiningPressure") && j["confiningPressure"].is_object()) {
        PressureSpec ps;
        from_json(j["confiningPressure"], ps);
        m.confiningPressure = ps;
    } else {
        m.confiningPressure.reset();
    }

    if (j.contains("porePressure") && j["porePressure"].is_object()) {
        PressureSpec ps;
        from_json(j["porePressure"], ps);
        m.porePressure = ps;
    } else {
        m.porePressure.reset();
    }

    m.thermodynamicPhase = j.value("thermodynamicPhase", "");
    m.sampleState = j.value("sampleState", "");
    m.experimentalMethod = j.value("experimentalMethod", "");
    m.constitutiveModel = j.value("constitutiveModel", "");
    m.validityRegime = j.value("validityRegime", "");
    m.interfacePhase1 = j.value("interfacePhase1", "");
    m.interfacePhase2 = j.value("interfacePhase2", "");
}

void to_json(json& j, const ScientificObservationCollection& c) {
    j = json{
        {"applicability", ApplicabilityStateToString(c.applicability)},
        {"epistemicState", EpistemicStateToString(c.epistemicState)}
    };
    json jMeas = json::array();
    for (const auto& m : c.measurements) {
        json jm;
        to_json(jm, m);
        jMeas.push_back(jm);
    }
    j["measurements"] = jMeas;
}

void from_json(const json& j, ScientificObservationCollection& c) {
    c.applicability = StringToApplicabilityState(j.value("applicability", "Applicable"));
    c.epistemicState = StringToEpistemicState(j.value("epistemicState", "Known"));
    c.measurements.clear();
    if (j.contains("measurements") && j["measurements"].is_array()) {
        for (const auto& jm : j["measurements"]) {
            ExtendedMeasurement m;
            from_json(jm, m);
            c.measurements.push_back(m);
        }
    }
}

void to_json(json& j, const ExtendedPropertyEntry& e) {
    to_json(j, e.observationCollection);
    j["propertyUri"] = e.propertyUri;
}

void from_json(const json& j, ExtendedPropertyEntry& e) {
    e.propertyUri = j.value("propertyUri", "");
    e.propertyIndex = INVALID_PROPERTY_KEY_INDEX;
    from_json(j, e.observationCollection);
}

void to_json(json& j, const ExtendedPropertyStorage& s) {
    j = json::array();
    for (const auto& entry : s.GetEntries()) {
        json je;
        to_json(je, entry);
        j.push_back(je);
    }
}

void from_json(const json& j, ExtendedPropertyStorage& s) {
    std::vector<ExtendedPropertyEntry> entries;
    if (j.is_array()) {
        for (const auto& je : j) {
            ExtendedPropertyEntry e;
            from_json(je, e);
            entries.push_back(e);
        }
    }
    s.SetEntries(std::move(entries));
}

void to_json(json& j, const ScientificPropertyDefinition& def) {
    j = json{
        {"uri", def.uri},
        {"canonicalName", def.canonicalName},
        {"displayName", def.displayName},
        {"dimension", PhysicalDimensionToString(def.dimension)},
        {"canonicalUnit", def.canonicalUnit},
        {"allowedUnits", def.allowedUnits},
        {"level", PhysicalLevelToString(def.level)},
        {"description", def.description}
    };
}

void from_json(const json& j, ScientificPropertyDefinition& def) {
    def.uri = j.value("uri", "");
    def.canonicalName = j.value("canonicalName", "");
    def.displayName = j.value("displayName", "");
    def.dimension = StringToPhysicalDimension(j.value("dimension", "Dimensionless"));
    def.canonicalUnit = j.value("canonicalUnit", "");
    def.allowedUnits.clear();
    if (j.contains("allowedUnits") && j["allowedUnits"].is_array()) {
        def.allowedUnits = j["allowedUnits"].get<std::vector<std::string>>();
    }
    def.level = StringToPhysicalLevel(j.value("level", "Intrinsic"));
    def.description = j.value("description", "");
    def.internalIndex = INVALID_PROPERTY_KEY_INDEX;
}

static std::vector<ScientificPropertyDefinition> CreateCanonicalPropertyDefinitions() {
    std::vector<ScientificPropertyDefinition> defs;
    defs.reserve(16);

    auto addDef = [&](const ScientificPropertyID& uri, const std::string& name, const std::string& dispName,
                      PhysicalDimension dim, const std::string& unit, const std::vector<std::string>& allowed,
                      PhysicalLevel lvl, const std::string& desc) {
        ScientificPropertyDefinition d;
        d.uri = uri;
        d.internalIndex = static_cast<PropertyKeyIndex>(defs.size());
        d.canonicalName = name;
        d.displayName = dispName;
        d.dimension = dim;
        d.canonicalUnit = unit;
        d.allowedUnits = allowed;
        d.level = lvl;
        d.description = desc;
        defs.push_back(d);
    };

    addDef("scientific:prop/isothermal_bulk_modulus", "isothermal_bulk_modulus", "Isothermal Bulk Modulus",
           PhysicalDimension::PressureOrModulus, "Pa", {"Pa", "GPa", "MPa", "bar"}, PhysicalLevel::Intrinsic,
           "Bulk modulus measured at constant temperature");
    addDef("scientific:prop/isentropic_bulk_modulus", "isentropic_bulk_modulus", "Isentropic Bulk Modulus",
           PhysicalDimension::PressureOrModulus, "Pa", {"Pa", "GPa", "MPa", "bar"}, PhysicalLevel::Intrinsic,
           "Bulk modulus measured at constant entropy (adiabatic / ultrasonic)");
    addDef("scientific:prop/shear_modulus", "shear_modulus", "Shear Modulus",
           PhysicalDimension::PressureOrModulus, "Pa", {"Pa", "GPa", "MPa", "bar"}, PhysicalLevel::Intrinsic,
           "Elastic shear modulus (Coulomb modulus)");
    addDef("scientific:prop/poissons_ratio", "poissons_ratio", "Poisson's Ratio",
           PhysicalDimension::Dimensionless, "", {""}, PhysicalLevel::Intrinsic,
           "Ratio of transverse to axial strain under uniaxial stress");
    addDef("scientific:prop/p_wave_velocity", "p_wave_velocity", "P-Wave Velocity",
           PhysicalDimension::Velocity, "m/s", {"m/s", "km/s"}, PhysicalLevel::Intrinsic,
           "Primary compressional acoustic wave velocity");
    addDef("scientific:prop/s_wave_velocity", "s_wave_velocity", "S-Wave Velocity",
           PhysicalDimension::Velocity, "m/s", {"m/s", "km/s"}, PhysicalLevel::Intrinsic,
           "Secondary shear acoustic wave velocity");
    addDef("scientific:prop/dynamic_viscosity", "dynamic_viscosity", "Dynamic Viscosity",
           PhysicalDimension::Viscosity, "Pa*s", {"Pa*s", "mPa*s", "P", "cP"}, PhysicalLevel::Intrinsic,
           "Resistance to gradual deformation by shear or tensile stress");
    addDef("scientific:prop/surface_tension", "surface_tension", "Surface / Interfacial Tension",
           PhysicalDimension::SurfaceTension, "N/m", {"N/m", "mN/m"}, PhysicalLevel::Interface,
           "Contractive tendency of liquid/solid interface");
    addDef("scientific:prop/uniaxial_compressive_strength", "uniaxial_compressive_strength", "Uniaxial Compressive Strength",
           PhysicalDimension::PressureOrModulus, "Pa", {"Pa", "MPa", "GPa"}, PhysicalLevel::Sample,
           "Maximum uniaxial compressive stress before sample failure");
    addDef("scientific:prop/triaxial_compressive_strength", "triaxial_compressive_strength", "Triaxial Compressive Strength",
           PhysicalDimension::PressureOrModulus, "Pa", {"Pa", "MPa", "GPa"}, PhysicalLevel::Sample,
           "Compressive failure strength under confining pressure");
    addDef("scientific:prop/brazilian_tensile_strength", "brazilian_tensile_strength", "Brazilian Tensile Strength",
           PhysicalDimension::PressureOrModulus, "Pa", {"Pa", "MPa", "GPa"}, PhysicalLevel::Sample,
           "Indirect tensile strength determined by diametral compression");
    addDef("scientific:prop/direct_tensile_strength", "direct_tensile_strength", "Direct Tensile Strength",
           PhysicalDimension::PressureOrModulus, "Pa", {"Pa", "MPa", "GPa"}, PhysicalLevel::Sample,
           "Tensile failure strength measured in direct uniaxial tension");
    addDef("scientific:prop/apparent_cohesion", "apparent_cohesion", "Apparent Cohesion",
           PhysicalDimension::PressureOrModulus, "Pa", {"Pa", "kPa", "MPa"}, PhysicalLevel::Sample,
           "Shear strength component independent of normal stress in Mohr-Coulomb model");
    addDef("scientific:prop/internal_friction_angle", "internal_friction_angle", "Internal Friction Angle",
           PhysicalDimension::Dimensionless, "deg", {"deg", "rad"}, PhysicalLevel::Sample,
           "Angle of the Mohr-Coulomb shear failure envelope");
    addDef("scientific:prop/solidus_temperature", "solidus_temperature", "Solidus Temperature",
           PhysicalDimension::Temperature, "K", {"K", "degC"}, PhysicalLevel::Intrinsic,
           "Temperature below which a substance or mixture is completely solid");
    addDef("scientific:prop/liquidus_temperature", "liquidus_temperature", "Liquidus Temperature",
           PhysicalDimension::Temperature, "K", {"K", "degC"}, PhysicalLevel::Intrinsic,
           "Temperature above which a substance or mixture is completely liquid");

    return defs;
}

static bool ValidateExtendedPropertyStorageInternal(
    const ExtendedPropertyStorage& storage,
    const std::vector<ScientificPropertyDefinition>& propDefs,
    const std::unordered_map<ScientificPropertyID, PropertyKeyIndex>& propMap,
    std::string* outError
) {
    std::unordered_set<ScientificPropertyID> seenUris;

    for (const auto& entry : storage.GetEntries()) {
        if (entry.propertyUri.empty()) {
            if (outError) *outError = "Voce proprieta' estesa con URI vuoto.";
            return false;
        }

        if (!seenUris.insert(entry.propertyUri).second) {
            if (outError) *outError = "Proprieta' estesa duplicata '" + entry.propertyUri + "' nello stesso materiale.";
            return false;
        }

        auto it = propMap.find(entry.propertyUri);
        if (it == propMap.end() || it->second >= propDefs.size()) {
            if (outError) *outError = "Proprieta' estesa '" + entry.propertyUri + "' non registrata nel dizionario proprieta'.";
            return false;
        }

        const auto& def = propDefs[it->second];

        const auto& coll = entry.observationCollection;
        if (coll.applicability == ApplicabilityState::NotApplicable) {
            if (!coll.measurements.empty()) {
                if (outError) *outError = "Proprieta' '" + entry.propertyUri + "' contrassegnata NotApplicable ma contiene osservazioni di misura.";
                return false;
            }
        }

        for (const auto& m : coll.measurements) {
            if (m.scalarValue.has_value()) {
                if (!std::isfinite(*m.scalarValue)) {
                    if (outError) *outError = "Valore numerico non finito (NaN/Inf) in '" + entry.propertyUri + "'.";
                    return false;
                }
            }
            if (m.rangeValue.has_value()) {
                if (!std::isfinite(m.rangeValue->first) || !std::isfinite(m.rangeValue->second) || m.rangeValue->first > m.rangeValue->second) {
                    if (outError) *outError = "Intervallo invalido o non finito [min, max] in '" + entry.propertyUri + "'.";
                    return false;
                }
            }
            if (m.temperatureKelvin.has_value()) {
                if (!std::isfinite(*m.temperatureKelvin) || *m.temperatureKelvin < 0.0) {
                    if (outError) *outError = "Temperatura termodinamica non finita o negativa in '" + entry.propertyUri + "'.";
                    return false;
                }
            }
            if (m.thermodynamicPressure.has_value()) {
                if (!std::isfinite(m.thermodynamicPressure->valuePascal)) {
                    if (outError) *outError = "Pressione termodinamica non finita in '" + entry.propertyUri + "'.";
                    return false;
                }
            }
            if (m.confiningPressure.has_value()) {
                if (!std::isfinite(m.confiningPressure->valuePascal)) {
                    if (outError) *outError = "Pressione di confinamento non finita in '" + entry.propertyUri + "'.";
                    return false;
                }
            }
            if (m.porePressure.has_value()) {
                if (!std::isfinite(m.porePressure->valuePascal)) {
                    if (outError) *outError = "Pressione dei pori non finita in '" + entry.propertyUri + "'.";
                    return false;
                }
            }

            if (!m.unit.empty() && !def.IsUnitAllowed(m.unit)) {
                if (outError) *outError = "Unita' '" + m.unit + "' non ammessa per la proprieta' '" + entry.propertyUri + "'.";
                return false;
            }

            if (m.uncertainty.hasUncertainty) {
                if (!std::isfinite(m.uncertainty.value) || m.uncertainty.value < 0.0) {
                    if (outError) *outError = "Incertezza metrologica non finita o negativa in '" + entry.propertyUri + "'.";
                    return false;
                }
            }

            if (m.dispersion.hasDispersion) {
                if (!std::isfinite(m.dispersion.stdDev) || m.dispersion.stdDev < 0.0 ||
                    !std::isfinite(m.dispersion.minObserved) || !std::isfinite(m.dispersion.maxObserved) ||
                    m.dispersion.minObserved > m.dispersion.maxObserved) {
                    if (outError) *outError = "Dispersione campionaria con parametri invalidi in '" + entry.propertyUri + "'.";
                    return false;
                }
            }
        }
    }
    return true;
}

// =============================================================================
// REGISTRY IMPLEMENTATION
// =============================================================================

ScientificMaterialRegistry::ScientificMaterialRegistry() {
    m_fallbackMaterial.id = INVALID_SCIENTIFIC_MATERIAL_ID;
    m_fallbackMaterial.canonicalId = "fairworld:mat/unknown";
    m_fallbackMaterial.displayName = "Unknown Material";
    m_fallbackMaterial.description = "Fallback material definition for unresolved lookups.";

    auto snapshot = std::make_shared<RegistrySnapshot>();
    snapshot->snapshotId = 1;
    snapshot->propertyDefinitions = CreateCanonicalPropertyDefinitions();
    for (size_t i = 0; i < snapshot->propertyDefinitions.size(); ++i) {
        snapshot->propertyUriToIndex[snapshot->propertyDefinitions[i].uri] = static_cast<PropertyKeyIndex>(i);
    }
    m_activeSnapshot = snapshot;
}

void ScientificMaterialRegistry::Initialize() {
    Clear();
    auto snapshot = std::make_shared<RegistrySnapshot>();
    snapshot->snapshotId = 1;
    snapshot->propertyDefinitions = CreateCanonicalPropertyDefinitions();
    for (size_t i = 0; i < snapshot->propertyDefinitions.size(); ++i) {
        snapshot->propertyUriToIndex[snapshot->propertyDefinitions[i].uri] = static_cast<PropertyKeyIndex>(i);
    }
    m_activeSnapshot = snapshot;
    RegisterStandardMaterials();
    m_activeSnapshot->materials = m_materials;
    m_activeSnapshot->canonicalToIdMap = m_canonicalToIdMap;
}

void ScientificMaterialRegistry::Clear() {
    m_materials.clear();
    m_canonicalToIdMap.clear();
    if (m_activeSnapshot) {
        auto snapshot = std::make_shared<RegistrySnapshot>();
        snapshot->snapshotId = m_activeSnapshot->snapshotId + 1;
        snapshot->propertyDefinitions = m_activeSnapshot->propertyDefinitions;
        snapshot->propertyUriToIndex = m_activeSnapshot->propertyUriToIndex;
        m_activeSnapshot = snapshot;
    }
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

    ScientificMaterialDef materialToRegister = def;
    if (m_activeSnapshot && !materialToRegister.extendedProperties.Empty()) {
        std::vector<ExtendedPropertyEntry> rebuilt;
        bool modified = false;
        for (auto entry : materialToRegister.extendedProperties.GetEntries()) {
            if (entry.propertyIndex == INVALID_PROPERTY_KEY_INDEX && !entry.propertyUri.empty()) {
                auto it = m_activeSnapshot->propertyUriToIndex.find(entry.propertyUri);
                if (it != m_activeSnapshot->propertyUriToIndex.end()) {
                    entry.propertyIndex = it->second;
                    modified = true;
                }
            }
            rebuilt.push_back(entry);
        }
        if (modified) {
            materialToRegister.extendedProperties.SetEntries(std::move(rebuilt));
        }
    }

    auto itId = m_materials.find(materialToRegister.id);
    auto itCanon = m_canonicalToIdMap.find(materialToRegister.canonicalId);

    // Se l'ID o il canonicalId sono gia' presenti nel registry
    if (itId != m_materials.end() || itCanon != m_canonicalToIdMap.end()) {
        // Verifica se si tratta dello stesso identico materiale (stesso ID e stesso canonicalId)
        bool isSameMaterial = (itId != m_materials.end() && itId->second.canonicalId == materialToRegister.canonicalId) &&
                              (itCanon != m_canonicalToIdMap.end() && itCanon->second == materialToRegister.id);

        if (isSameMaterial) {
            if (allowUpdate) {
                // Aggiornamento autoritativo consentito (es. da JSON rispetto a default in-memory)
                m_materials[materialToRegister.id] = materialToRegister;
                m_canonicalToIdMap[materialToRegister.canonicalId] = materialToRegister.id;
                if (m_activeSnapshot) {
                    m_activeSnapshot->materials[materialToRegister.id] = materialToRegister;
                    m_activeSnapshot->canonicalToIdMap[materialToRegister.canonicalId] = materialToRegister.id;
                }
                return true;
            } else {
                std::cerr << "[ScientificMaterialRegistry] Registrazione duplicata rifiutata: ScientificMaterialID " << materialToRegister.id 
                          << " ('" << materialToRegister.canonicalId << "') gia' registrato.\n";
                return false;
            }
        }

        // Conflitto reale: ID numerico gia' assegnato a un altro canonicalId
        if (itId != m_materials.end() && itId->second.canonicalId != materialToRegister.canonicalId) {
            std::cerr << "[ScientificMaterialRegistry] Conflitto ID: ScientificMaterialID " << materialToRegister.id 
                      << " gia' assegnato a '" << itId->second.canonicalId 
                      << "'. Rifiutata registrazione per '" << materialToRegister.canonicalId << "'.\n";
            return false;
        }

        // Conflitto reale: canonicalId gia' mappato a un altro ID numerico
        if (itCanon != m_canonicalToIdMap.end() && itCanon->second != materialToRegister.id) {
            std::cerr << "[ScientificMaterialRegistry] Conflitto canonicalId: '" << materialToRegister.canonicalId 
                      << "' gia' mappato all'ID " << itCanon->second 
                      << ". Rifiutata registrazione con ID " << materialToRegister.id << ".\n";
            return false;
        }
    }

    m_materials[materialToRegister.id] = materialToRegister;
    m_canonicalToIdMap[materialToRegister.canonicalId] = materialToRegister.id;
    if (m_activeSnapshot) {
        m_activeSnapshot->materials[materialToRegister.id] = materialToRegister;
        m_activeSnapshot->canonicalToIdMap[materialToRegister.canonicalId] = materialToRegister.id;
    }
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

static bool ValidateCatalogData(
    const std::unordered_map<ScientificMaterialID, ScientificMaterialDef>& materials,
    const std::unordered_map<std::string, ScientificMaterialID>& canonicalToIdMap,
    std::string* outError
) {
    auto hasMatId = [&](ScientificMaterialID id) {
        return id != INVALID_SCIENTIFIC_MATERIAL_ID && materials.find(id) != materials.end();
    };
    auto hasMatCanon = [&](const std::string& canon) {
        return !canon.empty() && canonicalToIdMap.find(canon) != canonicalToIdMap.end();
    };
    auto getMat = [&](ScientificMaterialID id) -> const ScientificMaterialDef& {
        auto it = materials.find(id);
        return it->second;
    };
    auto getIdByCanon = [&](const std::string& canon) -> ScientificMaterialID {
        auto it = canonicalToIdMap.find(canon);
        return (it != canonicalToIdMap.end()) ? it->second : INVALID_SCIENTIFIC_MATERIAL_ID;
    };

    // 1. Validazione Riferimenti, Duplicati e Composizioni per ciascun materiale
    for (const auto& [matId, mat] : materials) {
        // A. Validazione Relazioni
        std::unordered_set<std::string> seenRelKeys;
        for (const auto& rel : mat.relations) {
            if (rel.type == ScientificRelationType::Unknown) {
                if (outError) *outError = "Relazione con tipo 'Unknown' non valida nel materiale '" + mat.canonicalId + "'.";
                return false;
            }

            if (rel.targetCanonicalId.empty() && rel.targetId == INVALID_SCIENTIFIC_MATERIAL_ID) {
                if (outError) *outError = "Relazione priva di target nel materiale '" + mat.canonicalId + "'.";
                return false;
            }

            ScientificMaterialID resolvedTargetId = rel.targetId;
            std::string resolvedTargetCanon = rel.targetCanonicalId;

            if (resolvedTargetId != INVALID_SCIENTIFIC_MATERIAL_ID) {
                if (!hasMatId(resolvedTargetId)) {
                    if (outError) *outError = "Relazione nel materiale '" + mat.canonicalId + 
                                              "' fa riferimento a targetId numerico inesistente: " + std::to_string(resolvedTargetId);
                    return false;
                }
                if (resolvedTargetCanon.empty()) {
                    resolvedTargetCanon = getMat(resolvedTargetId).canonicalId;
                }
            }

            if (!resolvedTargetCanon.empty()) {
                if (!hasMatCanon(resolvedTargetCanon)) {
                    if (outError) *outError = "Relazione nel materiale '" + mat.canonicalId + 
                                              "' fa riferimento a target canonico inesistente: '" + resolvedTargetCanon + "'";
                    return false;
                }
                if (resolvedTargetId == INVALID_SCIENTIFIC_MATERIAL_ID) {
                    resolvedTargetId = getIdByCanon(resolvedTargetCanon);
                }
            }

            // Coerenza tra targetId e targetCanonicalId se entrambi specificati
            if (rel.targetId != INVALID_SCIENTIFIC_MATERIAL_ID && !rel.targetCanonicalId.empty()) {
                if (getIdByCanon(rel.targetCanonicalId) != rel.targetId) {
                    if (outError) *outError = "Discrepanza targetId e targetCanonicalId nella relazione del materiale '" + mat.canonicalId + "'.";
                    return false;
                }
            }

            // Verifica autoriferimento ciclico
            if (resolvedTargetId == mat.id || resolvedTargetCanon == mat.canonicalId) {
                if (outError) *outError = "Autoriferimento ciclico illegale: il materiale '" + mat.canonicalId + "' ha una relazione verso se stesso.";
                return false;
            }

            // Verifica duplicati relazioni
            std::string relKey = std::to_string(static_cast<uint8_t>(rel.type)) + ":" + resolvedTargetCanon;
            if (!seenRelKeys.insert(relKey).second) {
                if (outError) *outError = "Relazione duplicata nel materiale '" + mat.canonicalId + "': relazione '" + 
                                          ScientificRelationTypeToString(rel.type) + "' verso '" + resolvedTargetCanon + "' ripetuta.";
                return false;
            }
        }

        // B. Validazione Composizione
        if (!mat.composition.IsEmpty() || mat.composition.completeness != CompositionCompleteness::Unknown) {
            std::unordered_set<std::string> seenCompKeys;
            double sumMin = 0.0;
            double sumMax = 0.0;
            bool hasRange = false;
            size_t knownCount = 0;
            size_t totalCount = mat.composition.components.size();

            for (const auto& comp : mat.composition.components) {
                if (comp.canonicalId.empty() && comp.displayName.empty() && comp.materialId == INVALID_SCIENTIFIC_MATERIAL_ID) {
                    if (outError) *outError = "Componente privo di identificatore nella composizione di '" + mat.canonicalId + "'.";
                    return false;
                }

                if (comp.materialId != INVALID_SCIENTIFIC_MATERIAL_ID && !hasMatId(comp.materialId)) {
                    if (outError) *outError = "Componente nella composizione di '" + mat.canonicalId + 
                                              "' fa riferimento a materialId inesistente: " + std::to_string(comp.materialId);
                    return false;
                }

                if (!comp.canonicalId.empty() && comp.canonicalId.rfind("fairworld:mat/", 0) == 0 && !hasMatCanon(comp.canonicalId)) {
                    if (outError) *outError = "Componente nella composizione di '" + mat.canonicalId + 
                                              "' fa riferimento a materiale canonico inesistente: '" + comp.canonicalId + "'";
                    return false;
                }

                // Autoriferimento compositivo
                if (comp.materialId == mat.id || (!comp.canonicalId.empty() && comp.canonicalId == mat.canonicalId)) {
                    if (outError) *outError = "Composizione ciclica: il materiale '" + mat.canonicalId + "' include se stesso come componente.";
                    return false;
                }

                // Controllo duplicati componenti
                std::string compKey = comp.canonicalId.empty() ? comp.displayName : comp.canonicalId;
                if (!seenCompKeys.insert(compKey).second) {
                    if (outError) *outError = "Componente duplicato '" + compKey + "' nella composizione di '" + mat.canonicalId + "'.";
                    return false;
                }

                // Regola: non sommare frazioni di basi differenti. Verifica coerenza base
                if (comp.quantity.IsKnown()) {
                    if (comp.quantity.basis != mat.composition.defaultBasis) {
                        if (outError) *outError = "Base frazionaria del componente '" + compKey + "' ('" + 
                                                  FractionBasisToString(comp.quantity.basis) + "') discordante dalla defaultBasis ('" + 
                                                  FractionBasisToString(mat.composition.defaultBasis) + "') in '" + mat.canonicalId + "'.";
                        return false;
                    }
                }

                // Validazione quantità componente secondo QuantityKind
                if (comp.quantity.kind == QuantityKind::Exact) {
                    if (!std::isfinite(comp.quantity.exact)) {
                        if (outError) *outError = "Frazione Exact non finita (NaN/Inf) per componente '" + compKey + "' in '" + mat.canonicalId + "'.";
                        return false;
                    }
                    if (comp.quantity.exact < 0.0 || comp.quantity.exact > 1.0 + 1e-6) {
                        if (outError) *outError = "Frazione Exact fuori intervallo [0, 1] (" + std::to_string(comp.quantity.exact) + ") per componente '" + compKey + "' in '" + mat.canonicalId + "'.";
                        return false;
                    }
                    sumMin += comp.quantity.exact;
                    sumMax += comp.quantity.exact;
                    knownCount++;
                } else if (comp.quantity.kind == QuantityKind::Range) {
                    if (!std::isfinite(comp.quantity.min) || !std::isfinite(comp.quantity.max)) {
                        if (outError) *outError = "Intervallo frazionario Range non finito per componente '" + compKey + "' in '" + mat.canonicalId + "'.";
                        return false;
                    }
                    if (comp.quantity.min < 0.0 || comp.quantity.max > 1.0 + 1e-6 || comp.quantity.min > comp.quantity.max) {
                        if (outError) *outError = "Intervallo frazionario Range non valido [" + std::to_string(comp.quantity.min) + ", " + std::to_string(comp.quantity.max) + "] per componente '" + compKey + "' in '" + mat.canonicalId + "'.";
                        return false;
                    }
                    sumMin += comp.quantity.min;
                    sumMax += comp.quantity.max;
                    hasRange = true;
                    knownCount++;
                }
                // Se QuantityKind::Unknown: Unknown non equivale mai a zero e non altera sumMin/sumMax
            }

            // Regola per composizioni parziali
            if (mat.composition.completeness == CompositionCompleteness::Partial) {
                if (sumMin > 1.0 + 1e-4) {
                    if (outError) *outError = "Composizione Partial: somma dei minimi delle frazioni note supera 1.0 (" + std::to_string(sumMin) + ") in '" + mat.canonicalId + "'.";
                    return false;
                }
            }

            // Regola per composizioni nominalmente complete
            if (mat.composition.completeness == CompositionCompleteness::NominallyComplete) {
                if (totalCount == 0) {
                    if (outError) *outError = "Composizione dichiarata NominallyComplete ma priva di componenti in '" + mat.canonicalId + "'.";
                    return false;
                }
                if (knownCount != totalCount) {
                    if (outError) *outError = "Composizione NominallyComplete contiene componenti con quantita' Unknown in '" + mat.canonicalId + "'. Tutti i componenti devono essere noti.";
                    return false;
                }
                constexpr double TOLERANCE = 1e-3;
                if (!hasRange) {
                    if (std::abs(sumMin - 1.0) > TOLERANCE) {
                        if (outError) *outError = "Composizione NominallyComplete non somma a 1 entro tolleranza (somma: " + std::to_string(sumMin) + ") in '" + mat.canonicalId + "'.";
                        return false;
                    }
                } else {
                    if (sumMin > 1.0 + TOLERANCE || sumMax < 1.0 - TOLERANCE) {
                        if (outError) *outError = "Composizione NominallyComplete con Range non compatibile con 1.0 (intervallo somme: [" + std::to_string(sumMin) + ", " + std::to_string(sumMax) + "]) in '" + mat.canonicalId + "'.";
                        return false;
                    }
                }
            }
            // Se CompositionCompleteness::Unknown, non si deduce vuota né completa
        }
    }

    // 2. Rilevamento Cicli nei Grafi Gerarchici
    std::unordered_map<ScientificMaterialID, std::vector<ScientificMaterialID>> relAdj;
    for (const auto& [id, mat] : materials) {
        for (const auto& rel : mat.relations) {
            ScientificMaterialID targetId = rel.targetId;
            if (targetId == INVALID_SCIENTIFIC_MATERIAL_ID && !rel.targetCanonicalId.empty()) {
                targetId = getIdByCanon(rel.targetCanonicalId);
            }
            if (targetId != INVALID_SCIENTIFIC_MATERIAL_ID) {
                if (rel.type == ScientificRelationType::Parent || rel.type == ScientificRelationType::DerivedFrom) {
                    relAdj[mat.id].push_back(targetId);
                } else if (rel.type == ScientificRelationType::Child) {
                    relAdj[targetId].push_back(mat.id);
                }
            }
        }
    }

    std::unordered_map<ScientificMaterialID, int> visitState;
    std::vector<ScientificMaterialID> currentPath;

    auto dfsRelCheck = [&](auto& self, ScientificMaterialID u) -> bool {
        visitState[u] = 1;
        currentPath.push_back(u);

        auto it = relAdj.find(u);
        if (it != relAdj.end()) {
            for (ScientificMaterialID v : it->second) {
                if (visitState[v] == 1) {
                    if (outError) {
                        std::string cycleStr;
                        for (ScientificMaterialID stepId : currentPath) {
                            cycleStr += getMat(stepId).canonicalId + " -> ";
                        }
                        cycleStr += getMat(v).canonicalId;
                        *outError = "Rilevato ciclo nelle relazioni tassonomiche: " + cycleStr;
                    }
                    return false;
                }
                if (visitState[v] == 0) {
                    if (!self(self, v)) return false;
                }
            }
        }

        currentPath.pop_back();
        visitState[u] = 2;
        return true;
    };

    for (const auto& [id, _] : materials) {
        if (visitState[id] == 0) {
            if (!dfsRelCheck(dfsRelCheck, id)) return false;
        }
    }

    // 3. Rilevamento Cicli nelle Dipendenze di Composizione
    std::unordered_map<ScientificMaterialID, std::vector<ScientificMaterialID>> compAdj;
    for (const auto& [id, mat] : materials) {
        for (const auto& comp : mat.composition.components) {
            ScientificMaterialID compMatId = comp.materialId;
            if (compMatId == INVALID_SCIENTIFIC_MATERIAL_ID && !comp.canonicalId.empty()) {
                compMatId = getIdByCanon(comp.canonicalId);
            }
            if (compMatId != INVALID_SCIENTIFIC_MATERIAL_ID) {
                compAdj[mat.id].push_back(compMatId);
            }
        }
    }

    visitState.clear();
    currentPath.clear();

    auto dfsCompCheck = [&](auto& self, ScientificMaterialID u) -> bool {
        visitState[u] = 1;
        currentPath.push_back(u);

        auto it = compAdj.find(u);
        if (it != compAdj.end()) {
            for (ScientificMaterialID v : it->second) {
                if (visitState[v] == 1) {
                    if (outError) {
                        std::string cycleStr;
                        for (ScientificMaterialID stepId : currentPath) {
                            cycleStr += getMat(stepId).canonicalId + " -> ";
                        }
                        cycleStr += getMat(v).canonicalId;
                        *outError = "Rilevato ciclo nelle dipendenze di composizione: " + cycleStr;
                    }
                    return false;
                }
                if (visitState[v] == 0) {
                    if (!self(self, v)) return false;
                }
            }
        }

        currentPath.pop_back();
        visitState[u] = 2;
        return true;
    };

    for (const auto& [id, _] : materials) {
        if (visitState[id] == 0) {
            if (!dfsCompCheck(dfsCompCheck, id)) return false;
        }
    }

    return true;
}

bool ScientificMaterialRegistry::ValidateCatalog(std::string* outError) const {
    return ValidateCatalogData(m_materials, m_canonicalToIdMap, outError);
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
    if (schemaVersion < 1 || schemaVersion > 3) {
        std::cerr << "[ScientificMaterialRegistry] Versione schema non supportata: " << schemaVersion 
                  << " in " << filepath << ". Versioni supportate: [1, 2, 3].\n";
        return false;
    }

    if (!j.contains("materials") || !j["materials"].is_array()) {
        std::cerr << "[ScientificMaterialRegistry] Struttura 'materials' non trovata in " << filepath << "\n";
        return false;
    }

    // Caricamento transazionale: isolamento in stato temporaneo candidate
    auto candidateMaterials = m_materials;
    auto candidateCanonicalMap = m_canonicalToIdMap;

    auto candidateSnapshot = std::make_shared<RegistrySnapshot>();
    candidateSnapshot->snapshotId = m_activeSnapshot ? (m_activeSnapshot->snapshotId + 1) : 1;
    if (m_activeSnapshot) {
        candidateSnapshot->propertyDefinitions = m_activeSnapshot->propertyDefinitions;
        candidateSnapshot->propertyUriToIndex = m_activeSnapshot->propertyUriToIndex;
    } else {
        candidateSnapshot->propertyDefinitions = CreateCanonicalPropertyDefinitions();
        for (size_t i = 0; i < candidateSnapshot->propertyDefinitions.size(); ++i) {
            candidateSnapshot->propertyUriToIndex[candidateSnapshot->propertyDefinitions[i].uri] = static_cast<PropertyKeyIndex>(i);
        }
    }

    if (j.contains("propertyDefinitions") && j["propertyDefinitions"].is_array()) {
        for (const auto& jDef : j["propertyDefinitions"]) {
            ScientificPropertyDefinition def;
            from_json(jDef, def);
            if (def.uri.empty()) {
                std::cerr << "[ScientificMaterialRegistry] propertyDefinition con URI vuoto rifiutata.\n";
                return false;
            }
            if (candidateSnapshot->propertyUriToIndex.find(def.uri) == candidateSnapshot->propertyUriToIndex.end()) {
                def.internalIndex = static_cast<PropertyKeyIndex>(candidateSnapshot->propertyDefinitions.size());
                candidateSnapshot->propertyUriToIndex[def.uri] = def.internalIndex;
                candidateSnapshot->propertyDefinitions.push_back(def);
            }
        }
    }

    size_t loadedCount = 0;
    size_t conflictCount = 0;
    std::unordered_set<ScientificMaterialID> jsonSeenIds;
    std::unordered_set<std::string> jsonSeenCanonicals;

    for (const auto& jMat : j["materials"]) {
        try {
            ScientificMaterialDef def;
            from_json(jMat, def);

            if (def.id == INVALID_SCIENTIFIC_MATERIAL_ID) {
                std::cerr << "[ScientificMaterialRegistry] Errore: tentativo di registrare ID 0 invalido nel file " << filepath << "!\n";
                conflictCount++;
                continue;
            }
            if (def.canonicalId.empty()) {
                std::cerr << "[ScientificMaterialRegistry] Errore: canonicalId vuoto per ID " << def.id << " nel file " << filepath << "!\n";
                conflictCount++;
                continue;
            }

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

            // Ricostruzione indici per le proprietà estese (schema v3)
            if (schemaVersion == 3 && !def.extendedProperties.Empty()) {
                std::vector<ExtendedPropertyEntry> rebuiltEntries;
                for (auto entry : def.extendedProperties.GetEntries()) {
                    auto it = candidateSnapshot->propertyUriToIndex.find(entry.propertyUri);
                    if (it == candidateSnapshot->propertyUriToIndex.end()) {
                        std::cerr << "[ScientificMaterialRegistry] Proprieta' '" << entry.propertyUri 
                                  << "' non registrata nel dizionario. Transazione annullata.\n";
                        return false;
                    }
                    entry.propertyIndex = it->second;
                    rebuiltEntries.push_back(entry);
                }
                def.extendedProperties.SetEntries(std::move(rebuiltEntries));

                std::string extErr;
                if (!ValidateExtendedPropertyStorageInternal(def.extendedProperties, candidateSnapshot->propertyDefinitions, candidateSnapshot->propertyUriToIndex, &extErr)) {
                    std::cerr << "[ScientificMaterialRegistry] Validazione proprieta' estese fallita per '" 
                              << def.canonicalId << "': " << extErr << ". Transazione annullata.\n";
                    return false;
                }
            }

            // Controllo conflitti nello stato candidato (con allowUpdate = true per sincronizzare default)
            auto itId = candidateMaterials.find(def.id);
            auto itCanon = candidateCanonicalMap.find(def.canonicalId);

            if (itId != candidateMaterials.end() || itCanon != candidateCanonicalMap.end()) {
                bool isSameMaterial = (itId != candidateMaterials.end() && itId->second.canonicalId == def.canonicalId) &&
                                      (itCanon != candidateCanonicalMap.end() && itCanon->second == def.id);

                if (isSameMaterial) {
                    candidateMaterials[def.id] = def;
                    candidateCanonicalMap[def.canonicalId] = def.id;
                    jsonSeenIds.insert(def.id);
                    jsonSeenCanonicals.insert(def.canonicalId);
                    loadedCount++;
                    continue;
                }

                if (itId != candidateMaterials.end() && itId->second.canonicalId != def.canonicalId) {
                    std::cerr << "[ScientificMaterialRegistry] Conflitto ID: ScientificMaterialID " << def.id 
                              << " gia' assegnato a '" << itId->second.canonicalId 
                              << "'. Rifiutata registrazione per '" << def.canonicalId << "'.\n";
                    conflictCount++;
                    continue;
                }

                if (itCanon != candidateCanonicalMap.end() && itCanon->second != def.id) {
                    std::cerr << "[ScientificMaterialRegistry] Conflitto canonicalId: '" << def.canonicalId 
                              << "' gia' mappato all'ID " << itCanon->second 
                              << ". Rifiutata registrazione con ID " << def.id << ".\n";
                    conflictCount++;
                    continue;
                }
            }

            candidateMaterials[def.id] = def;
            candidateCanonicalMap[def.canonicalId] = def.id;
            jsonSeenIds.insert(def.id);
            jsonSeenCanonicals.insert(def.canonicalId);
            loadedCount++;
        } catch (const std::exception& e) {
            std::cerr << "[ScientificMaterialRegistry] Errore deserializzazione materiale: " << e.what() << "\n";
            conflictCount++;
        }
    }

    if (loadedCount == 0) {
        std::cerr << "[ScientificMaterialRegistry] Nessun materiale valido caricato da " << filepath << ". Transazione annullata.\n";
        return false;
    }

    // Validazione rigorosa dello stato candidato se schemaVersion >= 2
    if (schemaVersion >= 2) {
        std::string valError;
        if (!ValidateCatalogData(candidateMaterials, candidateCanonicalMap, &valError)) {
            std::cerr << "[ScientificMaterialRegistry] Validazione catalogo fallita per " 
                      << filepath << ": " << valError << ". Transazione annullata: stato e indici invariati.\n";
            return false;
        }
    }

    // COMMIT TRANSAZIONALE ATOMICO: solo a validazione completamente superata
    m_materials = std::move(candidateMaterials);
    m_canonicalToIdMap = std::move(candidateCanonicalMap);
    candidateSnapshot->materials = m_materials;
    candidateSnapshot->canonicalToIdMap = m_canonicalToIdMap;
    m_activeSnapshot = std::move(candidateSnapshot);

    std::cout << "[ScientificMaterialRegistry] Caricati " << loadedCount 
              << " materiali scientifici da " << filepath 
              << " (schema v" << schemaVersion << ", conflitti/scartati: " << conflictCount << ")\n";
    return true;
}

bool ScientificMaterialRegistry::SaveToJson(const std::string& filepath) const {
    json j;
    bool hasAnyExtended = false;
    for (const auto& pair : m_materials) {
        if (!pair.second.extendedProperties.Empty()) {
            hasAnyExtended = true;
            break;
        }
    }
    j["schemaVersion"] = hasAnyExtended ? 3 : 2;

    if (hasAnyExtended && m_activeSnapshot && !m_activeSnapshot->propertyDefinitions.empty()) {
        json jDefs = json::array();
        for (const auto& d : m_activeSnapshot->propertyDefinitions) {
            json jd;
            to_json(jd, d);
            jDefs.push_back(jd);
        }
        j["propertyDefinitions"] = jDefs;
    }

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

RegistrySnapshotHandle ScientificMaterialRegistry::GetSnapshotHandle() const {
    return RegistrySnapshotHandle(m_activeSnapshot);
}

std::shared_ptr<const RegistrySnapshot> ScientificMaterialRegistry::GetSnapshot() const {
    return m_activeSnapshot;
}

const ScientificPropertyDefinition* ScientificMaterialRegistry::GetPropertyDefinition(const ScientificPropertyID& propUri) const {
    if (!m_activeSnapshot) return nullptr;
    return m_activeSnapshot->GetPropertyDef(propUri);
}

std::vector<ScientificPropertyID> ScientificMaterialRegistry::GetRegisteredPropertyUris() const {
    if (!m_activeSnapshot) return {};
    std::vector<ScientificPropertyID> uris;
    uris.reserve(m_activeSnapshot->propertyDefinitions.size());
    for (const auto& def : m_activeSnapshot->propertyDefinitions) {
        uris.push_back(def.uri);
    }
    return uris;
}

bool ScientificMaterialRegistry::ValidateExtendedProperties(const ExtendedPropertyStorage& storage, std::string* outError) const {
    if (!m_activeSnapshot) {
        if (outError) *outError = "Active snapshot non disponibile.";
        return false;
    }
    return ValidateExtendedPropertyStorageInternal(storage, m_activeSnapshot->propertyDefinitions, m_activeSnapshot->propertyUriToIndex, outError);
}

QueryResult ScientificMaterialRegistry::QueryExtendedProperty(
    ScientificMaterialID matId, 
    const ScientificPropertyID& propUri, 
    const QueryPredicate& predicate
) const {
    QueryResult result;
    result.outcome = QueryOutcome::PropertyAbsent;

    if (!m_activeSnapshot) {
        result.diagnosticMessage = "Active snapshot non disponibile.";
        return result;
    }

    const auto* propDef = m_activeSnapshot->GetPropertyDef(propUri);
    if (!propDef) {
        result.outcome = QueryOutcome::PropertyAbsent;
        result.diagnosticMessage = "Proprieta' '" + propUri + "' non registrata nel dizionario.";
        return result;
    }

    const auto* mat = m_activeSnapshot->GetMaterial(matId);
    if (!mat) {
        result.outcome = QueryOutcome::PropertyAbsent;
        result.diagnosticMessage = "Materiale ID " + std::to_string(matId) + " non trovato nello snapshot.";
        return result;
    }

    const auto* entry = mat->extendedProperties.FindEntryByIndex(propDef->internalIndex);
    if (!entry) {
        entry = mat->extendedProperties.FindEntryByUri(propUri);
    }

    if (!entry) {
        result.outcome = QueryOutcome::PropertyAbsent;
        result.diagnosticMessage = "Proprieta' assente nel materiale '" + mat->canonicalId + "'.";
        return result;
    }

    const auto& coll = entry->observationCollection;

    if (coll.applicability == ApplicabilityState::NotApplicable) {
        result.outcome = QueryOutcome::PropertyNotApplicable;
        result.diagnosticMessage = "Proprieta' formalmente non applicabile al materiale.";
        return result;
    }

    if (coll.epistemicState == EpistemicState::Unknown) {
        result.outcome = QueryOutcome::PropertyUnknown;
        result.diagnosticMessage = "Proprieta' applicabile ma con valore Unknown.";
        return result;
    }

    double tempTol = (std::isfinite(predicate.temperatureToleranceKelvin) && predicate.temperatureToleranceKelvin >= 0.0)
                     ? predicate.temperatureToleranceKelvin : 0.5;
    double pressTol = (std::isfinite(predicate.pressureTolerancePascal) && predicate.pressureTolerancePascal >= 0.0)
                      ? predicate.pressureTolerancePascal : 100.0;

    for (const auto& m : coll.measurements) {
        bool match = true;

        if (predicate.requiredTemperatureKelvin.has_value()) {
            if (!m.temperatureKelvin.has_value()) {
                match = false;
            } else if (std::abs(*m.temperatureKelvin - *predicate.requiredTemperatureKelvin) > tempTol) {
                match = false;
            }
        }

        if (match && predicate.requiredThermodynamicPressure.has_value()) {
            if (!m.thermodynamicPressure.has_value()) {
                match = false;
            } else if (m.thermodynamicPressure->refType != predicate.requiredThermodynamicPressure->refType) {
                match = false;
            } else if (std::abs(m.thermodynamicPressure->valuePascal - predicate.requiredThermodynamicPressure->valuePascal) > pressTol) {
                match = false;
            }
        }

        if (match && predicate.requiredConfiningPressure.has_value()) {
            if (!m.confiningPressure.has_value()) {
                match = false;
            } else if (m.confiningPressure->refType != predicate.requiredConfiningPressure->refType) {
                match = false;
            } else if (std::abs(m.confiningPressure->valuePascal - predicate.requiredConfiningPressure->valuePascal) > pressTol) {
                match = false;
            }
        }

        if (match && predicate.requiredPorePressure.has_value()) {
            if (!m.porePressure.has_value()) {
                match = false;
            } else if (m.porePressure->refType != predicate.requiredPorePressure->refType) {
                match = false;
            } else if (std::abs(m.porePressure->valuePascal - predicate.requiredPorePressure->valuePascal) > pressTol) {
                match = false;
            }
        }

        if (match && predicate.requiredThermodynamicPhase.has_value()) {
            if (m.thermodynamicPhase.empty() || m.thermodynamicPhase != *predicate.requiredThermodynamicPhase) {
                match = false;
            }
        }

        if (match && predicate.requiredSampleState.has_value()) {
            if (m.sampleState.empty() || m.sampleState != *predicate.requiredSampleState) {
                match = false;
            }
        }

        if (match && predicate.requiredExperimentalMethod.has_value()) {
            if (m.experimentalMethod.empty() || m.experimentalMethod != *predicate.requiredExperimentalMethod) {
                match = false;
            }
        }

        if (match && predicate.requiredConstitutiveModel.has_value()) {
            if (m.constitutiveModel.empty() || m.constitutiveModel != *predicate.requiredConstitutiveModel) {
                match = false;
            }
        }

        if (match && predicate.requiredValidityRegime.has_value()) {
            if (m.validityRegime.empty() || m.validityRegime != *predicate.requiredValidityRegime) {
                match = false;
            }
        }

        if (match && predicate.requiredInterfacePhases.has_value()) {
            const auto& req1 = predicate.requiredInterfacePhases->first;
            const auto& req2 = predicate.requiredInterfacePhases->second;
            if (m.interfacePhase1.empty() || m.interfacePhase2.empty()) {
                match = false;
            } else {
                bool pairMatch = (m.interfacePhase1 == req1 && m.interfacePhase2 == req2) ||
                                 (m.interfacePhase1 == req2 && m.interfacePhase2 == req1);
                if (!pairMatch) match = false;
            }
        }

        if (match) {
            result.matchingMeasurements.push_back(&m);
        }
    }

    if (result.matchingMeasurements.size() == 1) {
        result.outcome = QueryOutcome::MatchSingle;
        result.diagnosticMessage = "Trovata esattamente 1 osservazione compatibile.";
    } else if (result.matchingMeasurements.size() > 1) {
        result.outcome = QueryOutcome::MatchMultiple;
        result.diagnosticMessage = "Trovate " + std::to_string(result.matchingMeasurements.size()) + 
                                  " osservazioni compatibili (nessuna selezione implicita).";
    } else {
        if (coll.measurements.empty()) {
            result.outcome = QueryOutcome::PropertyUnknown;
            result.diagnosticMessage = "Nessuna osservazione presente per la proprieta' (Unknown).";
        } else {
            result.outcome = QueryOutcome::NoMatchConditions;
            result.diagnosticMessage = "Proprieta' presente ma nessuna osservazione soddisfa le condizioni richieste.";
        }
    }

    return result;
}

QueryResult ScientificMaterialRegistry::QueryExtendedProperty(
    const std::string& canonicalMatId, 
    const ScientificPropertyID& propUri, 
    const QueryPredicate& predicate
) const {
    ScientificMaterialID id = GetMaterialIdByCanonical(canonicalMatId);
    if (id == INVALID_SCIENTIFIC_MATERIAL_ID) {
        QueryResult res;
        res.outcome = QueryOutcome::PropertyAbsent;
        res.diagnosticMessage = "Materiale canonico '" + canonicalMatId + "' non trovato.";
        return res;
    }
    return QueryExtendedProperty(id, propUri, predicate);
}

std::vector<ScientificMaterialID> ScientificMaterialRegistry::GetMaterialsByKind(ScientificKind kind) const {
    std::vector<ScientificMaterialID> result;
    for (const auto& [id, mat] : m_materials) {
        if (mat.kind == kind) {
            result.push_back(id);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<ScientificMaterialID> ScientificMaterialRegistry::GetMaterialsByAggregationState(AggregationState state) const {
    std::vector<ScientificMaterialID> result;
    for (const auto& [id, mat] : m_materials) {
        if (mat.aggregationState == state) {
            result.push_back(id);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<ScientificRelation> ScientificMaterialRegistry::GetRelationsForMaterial(ScientificMaterialID id) const {
    auto it = m_materials.find(id);
    if (it != m_materials.end()) {
        return it->second.relations;
    }
    return {};
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
        m.description = "Pure liquid water at standard temperature and pressure.";
        m.kind = ScientificKind::Compound;
        m.aggregationState = AggregationState::Liquid;
        m.density = ScientificQuantity::Known(1000.0, "kg/m^3", DataQuality::SyntheticStandard, "IAPWS Release 1995");
        m.porosity = ScientificQuantity::NotApplicable();
        m.compressiveStrength = ScientificQuantity::NotApplicable();
        m.compressiveStrength.unit = "Pa";
        m.tensileStrength = ScientificQuantity::NotApplicable();
        m.tensileStrength.unit = "Pa";
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
        m.kind = ScientificKind::Rock;
        m.aggregationState = AggregationState::Solid;
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
        m.kind = ScientificKind::Rock;
        m.aggregationState = AggregationState::Solid;
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
        m.kind = ScientificKind::Unknown;
        m.aggregationState = AggregationState::Solid;
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
