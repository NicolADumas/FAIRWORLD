#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <cstdint>
#include <array>
#include <memory>
#include <utility>
#include <algorithm>
#include <glm/glm.hpp>
#include "json.hpp"

namespace fw {

// =============================================================================
// MA-02.1: SCIENTIFIC MATERIAL IDENTIFIERS & STATES
// =============================================================================

using ScientificMaterialID = uint32_t;
inline constexpr ScientificMaterialID INVALID_SCIENTIFIC_MATERIAL_ID = 0;

enum class PropertyState : uint8_t {
    Known = 0,
    Unknown = 1,
    NotApplicable = 2
};

inline const char* PropertyStateToString(PropertyState s) {
    switch (s) {
        case PropertyState::Known:         return "Known";
        case PropertyState::Unknown:       return "Unknown";
        case PropertyState::NotApplicable: return "NotApplicable";
        default:                           return "Unknown";
    }
}

inline PropertyState StringToPropertyState(const std::string& str) {
    if (str == "Known") return PropertyState::Known;
    if (str == "NotApplicable") return PropertyState::NotApplicable;
    return PropertyState::Unknown;
}

enum class DataQuality : uint8_t {
    Estimated = 0,
    Experimental = 1,
    PeerReviewed = 2,
    SyntheticStandard = 3
};

inline const char* DataQualityToString(DataQuality q) {
    switch (q) {
        case DataQuality::Estimated:         return "Estimated";
        case DataQuality::Experimental:      return "Experimental";
        case DataQuality::PeerReviewed:      return "PeerReviewed";
        case DataQuality::SyntheticStandard: return "SyntheticStandard";
        default:                             return "Estimated";
    }
}

inline DataQuality StringToDataQuality(const std::string& str) {
    if (str == "Experimental") return DataQuality::Experimental;
    if (str == "PeerReviewed") return DataQuality::PeerReviewed;
    if (str == "SyntheticStandard") return DataQuality::SyntheticStandard;
    return DataQuality::Estimated;
}

// Quantità scalare scientifica con condizioni ambientali e metadati di provenienza
struct ScientificQuantity {
    double value = 0.0;
    std::string unit = "";             // Unità SI standard (es. "kg/m^3", "J/(kg*K)", "W/(m*K)", "Pa")
    double temperatureKelvin = 293.15; // Condizione di misura T (standard 20 °C)
    double pressurePascal = 101325.0;  // Condizione di misura P (standard 1 atm)
    PropertyState state = PropertyState::Unknown;
    DataQuality quality = DataQuality::Estimated;
    std::string source = "";           // Citazione bibliografica, handbook o laboratorio

    static ScientificQuantity Known(double val, const std::string& u, DataQuality q = DataQuality::PeerReviewed, const std::string& src = "") {
        ScientificQuantity qObj;
        qObj.value = val;
        qObj.unit = u;
        qObj.state = PropertyState::Known;
        qObj.quality = q;
        qObj.source = src;
        return qObj;
    }

    static ScientificQuantity Unknown(const std::string& u = "") {
        ScientificQuantity qObj;
        qObj.unit = u;
        qObj.state = PropertyState::Unknown;
        return qObj;
    }

    static ScientificQuantity NotApplicable() {
        ScientificQuantity qObj;
        qObj.state = PropertyState::NotApplicable;
        return qObj;
    }
};

// Curva scientifica 1D (per proprietà dipendenti da T, P, etc.)
struct ScientificCurve {
    std::string independentVar = "T"; // "T" (Kelvin), "P" (Pascal)
    std::string xUnit = "K";
    std::string yUnit = "";
    std::vector<std::pair<double, double>> points; // Coppie (x, y)
    PropertyState state = PropertyState::Unknown;
    std::string source = "";
};

// Tensore 3x3 per anisotropia termica o meccanica
struct ScientificTensor3x3 {
    std::array<double, 9> components = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    bool isAnisotropic = false;
    std::string unit = "";
    PropertyState state = PropertyState::Unknown;
};

// =============================================================================
// MA-03.1: SCIENTIFIC TAXONOMY, RELATIONS & COMPOSITION (v2)
// =============================================================================

// Tipologia scientifica o categoria ontologica fondamentale del materiale
enum class ScientificKind : uint8_t {
    Unknown = 0,
    Element = 1,
    Compound = 2,
    Mineral = 3,
    Rock = 4,
    Mixture = 5,
    Alloy = 6,
    Organic = 7,
    Composite = 8
};

inline const char* ScientificKindToString(ScientificKind k) {
    switch (k) {
        case ScientificKind::Element:   return "Element";
        case ScientificKind::Compound:  return "Compound";
        case ScientificKind::Mineral:   return "Mineral";
        case ScientificKind::Rock:      return "Rock";
        case ScientificKind::Mixture:   return "Mixture";
        case ScientificKind::Alloy:     return "Alloy";
        case ScientificKind::Organic:   return "Organic";
        case ScientificKind::Composite: return "Composite";
        case ScientificKind::Unknown:
        default:                        return "Unknown";
    }
}

inline ScientificKind StringToScientificKind(const std::string& str) {
    if (str == "Element")   return ScientificKind::Element;
    if (str == "Compound")  return ScientificKind::Compound;
    if (str == "Mineral")   return ScientificKind::Mineral;
    if (str == "Rock")      return ScientificKind::Rock;
    if (str == "Mixture")   return ScientificKind::Mixture;
    if (str == "Alloy")     return ScientificKind::Alloy;
    if (str == "Organic")   return ScientificKind::Organic;
    if (str == "Composite") return ScientificKind::Composite;
    return ScientificKind::Unknown;
}

// Stato macroscopico standard di aggregazione della materia
enum class AggregationState : uint8_t {
    Unknown = 0,
    Solid = 1,
    Liquid = 2,
    Gas = 3,
    Plasma = 4,
    Supercritical = 5,
    Multiphase = 6
};

inline const char* AggregationStateToString(AggregationState s) {
    switch (s) {
        case AggregationState::Solid:         return "Solid";
        case AggregationState::Liquid:        return "Liquid";
        case AggregationState::Gas:           return "Gas";
        case AggregationState::Plasma:        return "Plasma";
        case AggregationState::Supercritical: return "Supercritical";
        case AggregationState::Multiphase:    return "Multiphase";
        case AggregationState::Unknown:
        default:                              return "Unknown";
    }
}

inline AggregationState StringToAggregationState(const std::string& str) {
    if (str == "Solid")         return AggregationState::Solid;
    if (str == "Liquid")        return AggregationState::Liquid;
    if (str == "Gas")           return AggregationState::Gas;
    if (str == "Plasma")        return AggregationState::Plasma;
    if (str == "Supercritical") return AggregationState::Supercritical;
    if (str == "Multiphase")    return AggregationState::Multiphase;
    return AggregationState::Unknown;
}

// Tipologia di relazione tassonomica o filogenetica tra materiali scientifici
enum class ScientificRelationType : uint8_t {
    Unknown = 0,
    Parent = 1,             // Gerarchia tassonomica diretta (Is-A / SubtypeOf)
    Child = 2,              // Discendente tassonomico inverso
    VariantOf = 3,          // Variante, fase o polimorfo (es. diamante/grafite)
    PhaseOf = 4,            // Fase o stato di aggregazione alternativo (es. ghiaccio/acqua)
    PrecursorOf = 5,        // Precursore di reazione, metamorfismo o trasformazione geologica
    DerivedFrom = 6,        // Derivato / prodotto di trasformazione o lavorazione
    ComponentOf = 7         // Relazione meronimica diretta (fa parte di)
};

inline const char* ScientificRelationTypeToString(ScientificRelationType t) {
    switch (t) {
        case ScientificRelationType::Parent:      return "Parent";
        case ScientificRelationType::Child:       return "Child";
        case ScientificRelationType::VariantOf:   return "VariantOf";
        case ScientificRelationType::PhaseOf:     return "PhaseOf";
        case ScientificRelationType::PrecursorOf: return "PrecursorOf";
        case ScientificRelationType::DerivedFrom: return "DerivedFrom";
        case ScientificRelationType::ComponentOf: return "ComponentOf";
        case ScientificRelationType::Unknown:
        default:                                  return "Unknown";
    }
}

inline ScientificRelationType StringToScientificRelationType(const std::string& str) {
    if (str == "Parent")      return ScientificRelationType::Parent;
    if (str == "Child")       return ScientificRelationType::Child;
    if (str == "VariantOf")   return ScientificRelationType::VariantOf;
    if (str == "PhaseOf")     return ScientificRelationType::PhaseOf;
    if (str == "PrecursorOf") return ScientificRelationType::PrecursorOf;
    if (str == "DerivedFrom") return ScientificRelationType::DerivedFrom;
    if (str == "ComponentOf") return ScientificRelationType::ComponentOf;
    return ScientificRelationType::Unknown;
}

// Relazione scientifica esplicita con target tipizzato
struct ScientificRelation {
    ScientificRelationType type = ScientificRelationType::Unknown;
    std::string targetCanonicalId = "";
    ScientificMaterialID targetId = INVALID_SCIENTIFIC_MATERIAL_ID;
    std::string notes = "";

    bool operator==(const ScientificRelation& other) const {
        return type == other.type &&
               targetCanonicalId == other.targetCanonicalId &&
               targetId == other.targetId;
    }

    bool operator!=(const ScientificRelation& other) const {
        return !(*this == other);
    }
};

// Grado di completezza della determinazione analitica o formulare della composizione
enum class CompositionCompleteness : uint8_t {
    Unknown = 0,
    Partial = 1,
    NominallyComplete = 2
};

inline const char* CompositionCompletenessToString(CompositionCompleteness c) {
    switch (c) {
        case CompositionCompleteness::Partial:           return "Partial";
        case CompositionCompleteness::NominallyComplete: return "NominallyComplete";
        case CompositionCompleteness::Unknown:
        default:                                         return "Unknown";
    }
}

inline CompositionCompleteness StringToCompositionCompleteness(const std::string& str) {
    if (str == "Partial")           return CompositionCompleteness::Partial;
    if (str == "NominallyComplete") return CompositionCompleteness::NominallyComplete;
    return CompositionCompleteness::Unknown;
}

// Modalità di quantificazione frazionaria del componente
enum class QuantityKind : uint8_t {
    Exact = 0,
    Range = 1,
    Unknown = 2
};

inline const char* QuantityKindToString(QuantityKind k) {
    switch (k) {
        case QuantityKind::Exact:   return "Exact";
        case QuantityKind::Range:   return "Range";
        case QuantityKind::Unknown:
        default:                    return "Unknown";
    }
}

inline QuantityKind StringToQuantityKind(const std::string& str) {
    if (str == "Exact") return QuantityKind::Exact;
    if (str == "Range") return QuantityKind::Range;
    return QuantityKind::Unknown;
}

// Base di calcolo della frazione compositiva
enum class FractionBasis : uint8_t {
    Mass = 0,               // Frazione in massa (kg/kg, wt%)
    Volume = 1,             // Frazione in volume (m^3/m^3, vol%)
    Molar = 2,              // Frazione molare (mol/mol)
    Atomic = 3              // Frazione atomica (at%)
};

inline const char* FractionBasisToString(FractionBasis b) {
    switch (b) {
        case FractionBasis::Mass:   return "Mass";
        case FractionBasis::Volume: return "Volume";
        case FractionBasis::Molar:  return "Molar";
        case FractionBasis::Atomic: return "Atomic";
        default:                    return "Mass";
    }
}

inline FractionBasis StringToFractionBasis(const std::string& str) {
    if (str == "Volume" || str == "VolumeFraction") return FractionBasis::Volume;
    if (str == "Molar" || str == "Mole" || str == "MolarFraction" || str == "MoleFraction") return FractionBasis::Molar;
    if (str == "Atomic" || str == "AtomicFraction") return FractionBasis::Atomic;
    return FractionBasis::Mass;
}

// Quantità e metadati di un componente nella composizione (Exact, Range o Unknown)
struct ComponentQuantity {
    QuantityKind kind = QuantityKind::Unknown;
    double exact = 0.0;                // Utilizzato se kind == QuantityKind::Exact
    double min = 0.0;                  // Minimo dell'intervallo (se kind == QuantityKind::Range)
    double max = 0.0;                  // Massimo dell'intervallo (se kind == QuantityKind::Range)
    FractionBasis basis = FractionBasis::Mass;
    DataQuality quality = DataQuality::PeerReviewed;
    std::string source = "";

    // Helper per verificare se la quantità è nota (Exact o Range)
    bool IsKnown() const {
        return kind == QuantityKind::Exact || kind == QuantityKind::Range;
    }

    // Minimo effettivo della quantità
    double GetMinimum() const {
        if (kind == QuantityKind::Exact) return exact;
        if (kind == QuantityKind::Range) return min;
        return 0.0;
    }

    // Massimo effettivo della quantità
    double GetMaximum() const {
        if (kind == QuantityKind::Exact) return exact;
        if (kind == QuantityKind::Range) return max;
        return 0.0;
    }

    // Valore nominale rappresentativo
    double GetNominalValue() const {
        if (kind == QuantityKind::Exact) return exact;
        if (kind == QuantityKind::Range) return 0.5 * (min + max);
        return 0.0;
    }

    static ComponentQuantity Exact(double val, FractionBasis b = FractionBasis::Mass, DataQuality q = DataQuality::PeerReviewed, const std::string& src = "") {
        ComponentQuantity cq;
        cq.kind = QuantityKind::Exact;
        cq.exact = val;
        cq.min = val;
        cq.max = val;
        cq.basis = b;
        cq.quality = q;
        cq.source = src;
        return cq;
    }

    static ComponentQuantity Range(double minVal, double maxVal, FractionBasis b = FractionBasis::Mass, DataQuality q = DataQuality::PeerReviewed, const std::string& src = "") {
        ComponentQuantity cq;
        cq.kind = QuantityKind::Range;
        cq.min = minVal;
        cq.max = maxVal;
        cq.exact = 0.5 * (minVal + maxVal);
        cq.basis = b;
        cq.quality = q;
        cq.source = src;
        return cq;
    }

    static ComponentQuantity Unknown(FractionBasis b = FractionBasis::Mass) {
        ComponentQuantity cq;
        cq.kind = QuantityKind::Unknown;
        cq.exact = 0.0;
        cq.min = 0.0;
        cq.max = 0.0;
        cq.basis = b;
        cq.quality = DataQuality::Estimated;
        cq.source = "";
        return cq;
    }

    bool operator==(const ComponentQuantity& other) const {
        if (kind != other.kind) return false;
        if (kind == QuantityKind::Exact && exact != other.exact) return false;
        if (kind == QuantityKind::Range && (min != other.min || max != other.max)) return false;
        return basis == other.basis && quality == other.quality && source == other.source;
    }

    bool operator!=(const ComponentQuantity& other) const {
        return !(*this == other);
    }
};

// Componente singolo di una sostanza o miscela scientifica
struct ScientificComponent {
    std::string canonicalId = "";
    ScientificMaterialID materialId = INVALID_SCIENTIFIC_MATERIAL_ID;
    std::string displayName = "";
    std::string formula = "";
    ComponentQuantity quantity;
    std::string role = "";

    bool operator==(const ScientificComponent& other) const {
        return canonicalId == other.canonicalId &&
               materialId == other.materialId &&
               displayName == other.displayName &&
               formula == other.formula &&
               quantity == other.quantity &&
               role == other.role;
    }

    bool operator!=(const ScientificComponent& other) const {
        return !(*this == other);
    }
};

// Composizione scientifica strutturata
struct ScientificComposition {
    CompositionCompleteness completeness = CompositionCompleteness::Unknown;
    FractionBasis defaultBasis = FractionBasis::Mass;
    std::vector<ScientificComponent> components;
    std::string notes = "";

    bool IsEmpty() const {
        return components.empty();
    }

    bool HasComponent(const std::string& canonId) const {
        for (const auto& c : components) {
            if (c.canonicalId == canonId) return true;
        }
        return false;
    }

    bool HasComponent(ScientificMaterialID matId) const {
        if (matId == INVALID_SCIENTIFIC_MATERIAL_ID) return false;
        for (const auto& c : components) {
            if (c.materialId == matId) return true;
        }
        return false;
    }

    bool operator==(const ScientificComposition& other) const {
        if (completeness != other.completeness || defaultBasis != other.defaultBasis || notes != other.notes) return false;
        if (components.size() != other.components.size()) return false;
        for (size_t i = 0; i < components.size(); ++i) {
            if (components[i] != other.components[i]) return false;
        }
        return true;
    }

    bool operator!=(const ScientificComposition& other) const {
        return !(*this == other);
    }
};

// =============================================================================
// MA-03.2D: EXTENDED SCIENTIFIC PROPERTY MODEL & STORAGE
// =============================================================================

enum class PhysicalDimension : uint8_t {
    Dimensionless = 0,
    Density,                 // kg/m^3
    PressureOrModulus,       // Pa
    SpecificHeat,            // J/(kg*K)
    ThermalConductivity,     // W/(m*K)
    ThermalExpansion,        // 1/K
    Temperature,             // K
    SurfaceTension,          // N/m
    Velocity,                // m/s
    Viscosity,               // Pa*s
    EnthalpyOrEnergyPerMass  // J/kg
};

inline const char* PhysicalDimensionToString(PhysicalDimension d) {
    switch (d) {
        case PhysicalDimension::Dimensionless:           return "Dimensionless";
        case PhysicalDimension::Density:                 return "Density";
        case PhysicalDimension::PressureOrModulus:       return "PressureOrModulus";
        case PhysicalDimension::SpecificHeat:            return "SpecificHeat";
        case PhysicalDimension::ThermalConductivity:     return "ThermalConductivity";
        case PhysicalDimension::ThermalExpansion:        return "ThermalExpansion";
        case PhysicalDimension::Temperature:             return "Temperature";
        case PhysicalDimension::SurfaceTension:          return "SurfaceTension";
        case PhysicalDimension::Velocity:                return "Velocity";
        case PhysicalDimension::Viscosity:               return "Viscosity";
        case PhysicalDimension::EnthalpyOrEnergyPerMass: return "EnthalpyOrEnergyPerMass";
        default:                                         return "Dimensionless";
    }
}

inline PhysicalDimension StringToPhysicalDimension(const std::string& str) {
    if (str == "Density") return PhysicalDimension::Density;
    if (str == "PressureOrModulus") return PhysicalDimension::PressureOrModulus;
    if (str == "SpecificHeat") return PhysicalDimension::SpecificHeat;
    if (str == "ThermalConductivity") return PhysicalDimension::ThermalConductivity;
    if (str == "ThermalExpansion") return PhysicalDimension::ThermalExpansion;
    if (str == "Temperature") return PhysicalDimension::Temperature;
    if (str == "SurfaceTension") return PhysicalDimension::SurfaceTension;
    if (str == "Velocity") return PhysicalDimension::Velocity;
    if (str == "Viscosity") return PhysicalDimension::Viscosity;
    if (str == "EnthalpyOrEnergyPerMass") return PhysicalDimension::EnthalpyOrEnergyPerMass;
    return PhysicalDimension::Dimensionless;
}

enum class PhysicalLevel : uint8_t {
    Intrinsic = 0,
    Sample = 1,
    Interface = 2,
    Operational = 3
};

inline const char* PhysicalLevelToString(PhysicalLevel l) {
    switch (l) {
        case PhysicalLevel::Intrinsic:   return "Intrinsic";
        case PhysicalLevel::Sample:      return "Sample";
        case PhysicalLevel::Interface:   return "Interface";
        case PhysicalLevel::Operational: return "Operational";
        default:                         return "Intrinsic";
    }
}

inline PhysicalLevel StringToPhysicalLevel(const std::string& str) {
    if (str == "Sample") return PhysicalLevel::Sample;
    if (str == "Interface") return PhysicalLevel::Interface;
    if (str == "Operational") return PhysicalLevel::Operational;
    return PhysicalLevel::Intrinsic;
}

enum class ApplicabilityState : uint8_t {
    Applicable = 0,
    NotApplicable = 1,
    Conditional = 2
};

inline const char* ApplicabilityStateToString(ApplicabilityState a) {
    switch (a) {
        case ApplicabilityState::Applicable:    return "Applicable";
        case ApplicabilityState::NotApplicable: return "NotApplicable";
        case ApplicabilityState::Conditional:   return "Conditional";
        default:                                return "Applicable";
    }
}

inline ApplicabilityState StringToApplicabilityState(const std::string& str) {
    if (str == "NotApplicable") return ApplicabilityState::NotApplicable;
    if (str == "Conditional") return ApplicabilityState::Conditional;
    return ApplicabilityState::Applicable;
}

enum class EpistemicState : uint8_t {
    Known = 0,
    Unknown = 1,
    Assumed = 2,
    Disputed = 3
};

inline const char* EpistemicStateToString(EpistemicState e) {
    switch (e) {
        case EpistemicState::Known:    return "Known";
        case EpistemicState::Unknown:  return "Unknown";
        case EpistemicState::Assumed:  return "Assumed";
        case EpistemicState::Disputed: return "Disputed";
        default:                       return "Unknown";
    }
}

inline EpistemicState StringToEpistemicState(const std::string& str) {
    if (str == "Known") return EpistemicState::Known;
    if (str == "Assumed") return EpistemicState::Assumed;
    if (str == "Disputed") return EpistemicState::Disputed;
    return EpistemicState::Unknown;
}

enum class EvidenceLevel : uint8_t {
    PrimaryExperimental = 0,
    PeerReviewedLiterature = 1,
    StandardCompendium = 2,
    DerivedModel = 3,
    EducatedEstimate = 4
};

inline const char* EvidenceLevelToString(EvidenceLevel e) {
    switch (e) {
        case EvidenceLevel::PrimaryExperimental:    return "PrimaryExperimental";
        case EvidenceLevel::PeerReviewedLiterature: return "PeerReviewedLiterature";
        case EvidenceLevel::StandardCompendium:     return "StandardCompendium";
        case EvidenceLevel::DerivedModel:           return "DerivedModel";
        case EvidenceLevel::EducatedEstimate:       return "EducatedEstimate";
        default:                                    return "EducatedEstimate";
    }
}

inline EvidenceLevel StringToEvidenceLevel(const std::string& str) {
    if (str == "PrimaryExperimental") return EvidenceLevel::PrimaryExperimental;
    if (str == "PeerReviewedLiterature") return EvidenceLevel::PeerReviewedLiterature;
    if (str == "StandardCompendium") return EvidenceLevel::StandardCompendium;
    if (str == "DerivedModel") return EvidenceLevel::DerivedModel;
    return EvidenceLevel::EducatedEstimate;
}

enum class ValueNature : uint8_t {
    ExactScalar = 0,
    NominalScalar = 1,
    Range = 2,
    QualitativeDescriptor = 3,
    Calculated = 4
};

inline const char* ValueNatureToString(ValueNature v) {
    switch (v) {
        case ValueNature::ExactScalar:           return "ExactScalar";
        case ValueNature::NominalScalar:         return "NominalScalar";
        case ValueNature::Range:                 return "Range";
        case ValueNature::QualitativeDescriptor: return "QualitativeDescriptor";
        case ValueNature::Calculated:            return "Calculated";
        default:                                 return "ExactScalar";
    }
}

inline ValueNature StringToValueNature(const std::string& str) {
    if (str == "NominalScalar") return ValueNature::NominalScalar;
    if (str == "Range") return ValueNature::Range;
    if (str == "QualitativeDescriptor") return ValueNature::QualitativeDescriptor;
    if (str == "Calculated") return ValueNature::Calculated;
    return ValueNature::ExactScalar;
}

struct PressureSpec {
    enum class ReferenceType : uint8_t {
        Absolute = 0,
        Gauge = 1,
        Differential = 2
    };

    ReferenceType refType = ReferenceType::Absolute;
    double valuePascal = 101325.0;

    static const char* ReferenceTypeToString(ReferenceType t) {
        switch (t) {
            case ReferenceType::Absolute:     return "Absolute";
            case ReferenceType::Gauge:        return "Gauge";
            case ReferenceType::Differential: return "Differential";
            default:                          return "Absolute";
        }
    }

    static ReferenceType StringToReferenceType(const std::string& str) {
        if (str == "Gauge") return ReferenceType::Gauge;
        if (str == "Differential") return ReferenceType::Differential;
        return ReferenceType::Absolute;
    }

    bool operator==(const PressureSpec& o) const {
        return refType == o.refType && std::abs(valuePascal - o.valuePascal) < 1e-6;
    }
    bool operator!=(const PressureSpec& o) const { return !(*this == o); }
};

struct MetrologicalUncertainty {
    bool hasUncertainty = false;
    double value = 0.0;
    std::string type = "";
    std::string unit = "";
};

struct SamplePopulationDispersion {
    bool hasDispersion = false;
    double stdDev = 0.0;
    double minObserved = 0.0;
    double maxObserved = 0.0;
    size_t sampleCount = 0;
    std::string unit = "";
};

struct ExtendedMeasurement {
    ValueNature valueNature = ValueNature::ExactScalar;
    std::optional<double> scalarValue;
    std::optional<std::pair<double, double>> rangeValue;
    std::string qualitativeDescriptor;
    std::string unit;

    // Metrologia e dispersione
    MetrologicalUncertainty uncertainty;
    SamplePopulationDispersion dispersion;

    // Condizioni sperimentali
    std::optional<double> temperatureKelvin;
    std::optional<PressureSpec> thermodynamicPressure;
    std::optional<PressureSpec> confiningPressure;
    std::optional<PressureSpec> porePressure;

    // Fasi e identificatori del campione/modello
    std::string thermodynamicPhase;
    std::string sampleState;
    std::string experimentalMethod;
    std::string constitutiveModel;
    std::string validityRegime;
    std::string interfacePhase1;
    std::string interfacePhase2;

    // Provenienza
    EvidenceLevel evidenceLevel = EvidenceLevel::PeerReviewedLiterature;
    std::string bibliographicCitation;
    std::string doiOrIsbn;
};

struct ScientificObservationCollection {
    ApplicabilityState applicability = ApplicabilityState::Applicable;
    EpistemicState epistemicState = EpistemicState::Known;
    std::vector<ExtendedMeasurement> measurements;
};

using ScientificPropertyID = std::string;
using PropertyKeyIndex = uint16_t;
inline constexpr PropertyKeyIndex INVALID_PROPERTY_KEY_INDEX = 0xFFFF;

struct ScientificPropertyDefinition {
    ScientificPropertyID uri;
    PropertyKeyIndex internalIndex = INVALID_PROPERTY_KEY_INDEX;
    std::string canonicalName;
    std::string displayName;
    PhysicalDimension dimension = PhysicalDimension::Dimensionless;
    std::string canonicalUnit;
    std::vector<std::string> allowedUnits;
    PhysicalLevel level = PhysicalLevel::Intrinsic;
    std::string description;

    bool IsUnitAllowed(const std::string& u) const {
        if (u == canonicalUnit) return true;
        for (const auto& a : allowedUnits) {
            if (a == u) return true;
        }
        return false;
    }
};

struct ExtendedPropertyEntry {
    PropertyKeyIndex propertyIndex = INVALID_PROPERTY_KEY_INDEX;
    ScientificPropertyID propertyUri;
    ScientificObservationCollection observationCollection;

    bool operator<(const ExtendedPropertyEntry& o) const {
        if (propertyIndex != o.propertyIndex) {
            return propertyIndex < o.propertyIndex;
        }
        return propertyUri < o.propertyUri;
    }
};

class ExtendedPropertyStorage {
public:
    ExtendedPropertyStorage() = default;

    const std::vector<ExtendedPropertyEntry>& GetEntries() const { return m_entries; }
    size_t Size() const { return m_entries.size(); }
    bool Empty() const { return m_entries.empty(); }
    void Clear() { m_entries.clear(); }

    const ExtendedPropertyEntry* FindEntryByIndex(PropertyKeyIndex index) const {
        if (index == INVALID_PROPERTY_KEY_INDEX || m_entries.empty()) return nullptr;
        auto it = std::lower_bound(m_entries.begin(), m_entries.end(), index,
            [](const ExtendedPropertyEntry& e, PropertyKeyIndex val) {
                return e.propertyIndex < val;
            });
        if (it != m_entries.end() && it->propertyIndex == index) {
            return &(*it);
        }
        return nullptr;
    }

    const ExtendedPropertyEntry* FindEntryByUri(const ScientificPropertyID& uri) const {
        for (const auto& e : m_entries) {
            if (e.propertyUri == uri) return &e;
        }
        return nullptr;
    }

    bool AddOrUpdateEntry(const ExtendedPropertyEntry& entry) {
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            bool match = false;
            if (!entry.propertyUri.empty() && it->propertyUri == entry.propertyUri) {
                match = true;
            } else if (entry.propertyIndex != INVALID_PROPERTY_KEY_INDEX && it->propertyIndex == entry.propertyIndex) {
                match = true;
            }
            if (match) {
                PropertyKeyIndex preservedIndex = it->propertyIndex;
                *it = entry;
                if (it->propertyIndex == INVALID_PROPERTY_KEY_INDEX && preservedIndex != INVALID_PROPERTY_KEY_INDEX) {
                    it->propertyIndex = preservedIndex;
                }
                SortEntries();
                return false;
            }
        }
        m_entries.push_back(entry);
        SortEntries();
        return true;
    }

    void SortEntries() {
        std::sort(m_entries.begin(), m_entries.end());
    }

    void SetEntries(std::vector<ExtendedPropertyEntry> entries) {
        m_entries = std::move(entries);
        SortEntries();
    }

private:
    std::vector<ExtendedPropertyEntry> m_entries;
};

// =============================================================================
// MA-03.2D.3: RESOLVER & SNAPSHOT TYPES
// =============================================================================

enum class QueryOutcome : uint8_t {
    PropertyAbsent = 0,
    PropertyNotApplicable = 1,
    PropertyUnknown = 2,
    MatchSingle = 3,
    MatchMultiple = 4,
    NoMatchConditions = 5
};

inline const char* QueryOutcomeToString(QueryOutcome o) {
    switch (o) {
        case QueryOutcome::PropertyAbsent:        return "PropertyAbsent";
        case QueryOutcome::PropertyNotApplicable: return "PropertyNotApplicable";
        case QueryOutcome::PropertyUnknown:       return "PropertyUnknown";
        case QueryOutcome::MatchSingle:           return "MatchSingle";
        case QueryOutcome::MatchMultiple:         return "MatchMultiple";
        case QueryOutcome::NoMatchConditions:     return "NoMatchConditions";
        default:                                  return "Unknown";
    }
}

struct QueryPredicate {
    std::optional<double> requiredTemperatureKelvin;
    double temperatureToleranceKelvin = 0.5;

    std::optional<PressureSpec> requiredThermodynamicPressure;
    double pressureTolerancePascal = 100.0;

    std::optional<PressureSpec> requiredConfiningPressure;
    std::optional<PressureSpec> requiredPorePressure;

    std::optional<std::string> requiredThermodynamicPhase;
    std::optional<std::string> requiredSampleState;
    std::optional<std::string> requiredExperimentalMethod;
    std::optional<std::string> requiredConstitutiveModel;
    std::optional<std::string> requiredValidityRegime;
    std::optional<std::pair<std::string, std::string>> requiredInterfacePhases;
};

struct QueryResult {
    QueryOutcome outcome = QueryOutcome::PropertyAbsent;
    std::vector<const ExtendedMeasurement*> matchingMeasurements;
    std::string diagnosticMessage;
};

struct ScientificMaterialDef;

struct RegistrySnapshot {
    uint32_t snapshotId = 0;
    std::unordered_map<ScientificMaterialID, ScientificMaterialDef> materials;
    std::unordered_map<std::string, ScientificMaterialID> canonicalToIdMap;
    std::vector<ScientificPropertyDefinition> propertyDefinitions;
    std::unordered_map<ScientificPropertyID, PropertyKeyIndex> propertyUriToIndex;

    const ScientificPropertyDefinition* GetPropertyDef(const ScientificPropertyID& uri) const {
        auto it = propertyUriToIndex.find(uri);
        if (it != propertyUriToIndex.end() && it->second < propertyDefinitions.size()) {
            return &propertyDefinitions[it->second];
        }
        return nullptr;
    }

    const ScientificMaterialDef* GetMaterial(ScientificMaterialID id) const;
    const ScientificMaterialDef* GetMaterial(const std::string& canon) const;
};

class RegistrySnapshotHandle {
public:
    RegistrySnapshotHandle() = default;
    explicit RegistrySnapshotHandle(std::shared_ptr<const RegistrySnapshot> snapshot)
        : m_snapshot(std::move(snapshot)) {}

    bool IsValid() const { return m_snapshot != nullptr; }
    const RegistrySnapshot* operator->() const { return m_snapshot.get(); }
    const RegistrySnapshot& operator*() const { return *m_snapshot; }
    std::shared_ptr<const RegistrySnapshot> GetSnapshot() const { return m_snapshot; }

private:
    std::shared_ptr<const RegistrySnapshot> m_snapshot;
};

// Definizione completa di un materiale scientifico
struct ScientificMaterialDef {
    ScientificMaterialID id = INVALID_SCIENTIFIC_MATERIAL_ID;
    std::string canonicalId = "fairworld:mat/unknown";
    std::string displayName = "Unknown Material";
    std::string chemicalFormula = "";
    std::string classification = ""; // Es. "Igneous / Plutonic", "Sedimentary", "Metallic Alloy"
    std::string description = "";

    // MA-03.1: Faccette Tassonomiche v2
    ScientificKind kind = ScientificKind::Unknown;
    AggregationState aggregationState = AggregationState::Unknown;
    std::vector<std::string> tags;

    // MA-03.1: Relazioni Tassonomiche v2
    std::vector<ScientificRelation> relations;

    // MA-03.1: Composizione Scientifica v2
    ScientificComposition composition;

    // Proprietà Meccaniche & Gravitazionali (SI)
    ScientificQuantity density;              // kg/m^3
    ScientificQuantity porosity;             // frazione adimensionale (0..1)
    ScientificQuantity compressiveStrength;  // Pa
    ScientificQuantity tensileStrength;      // Pa
    ScientificQuantity elasticModulus;       // Pa (Modulo di Young)
    ScientificQuantity staticFriction;       // adimensionale
    ScientificQuantity dynamicFriction;      // adimensionale
    ScientificQuantity restitution;          // coefficiente di restituzione (0..1)

    // Proprietà Termiche & Energetiche (SI)
    ScientificQuantity specificHeatCapacity; // J / (kg * K)
    ScientificQuantity thermalConductivity;  // W / (m * K)
    ScientificQuantity thermalExpansion;     // 1 / K
    ScientificQuantity meltingPoint;         // K

    // Estensibilità: Curve e Tensori per supporto futuro
    std::unordered_map<std::string, ScientificCurve> responseCurves;
    std::unordered_map<std::string, ScientificTensor3x3> tensors;

    // MA-03.2D: Storage delle Proprietà Scientifiche Estese (Sorted Flat Vector)
    ExtendedPropertyStorage extendedProperties;

    bool IsValid() const {
        return id != INVALID_SCIENTIFIC_MATERIAL_ID && !canonicalId.empty();
    }
};

inline const ScientificMaterialDef* RegistrySnapshot::GetMaterial(ScientificMaterialID id) const {
    auto it = materials.find(id);
    if (it != materials.end()) return &it->second;
    return nullptr;
}

inline const ScientificMaterialDef* RegistrySnapshot::GetMaterial(const std::string& canon) const {
    auto it = canonicalToIdMap.find(canon);
    if (it != canonicalToIdMap.end()) return GetMaterial(it->second);
    return nullptr;
}

// =============================================================================
// MA-02.3: SCIENTIFIC PROPERTY OVERRIDES & VALIDATION
// =============================================================================

namespace ScientificPropertyNames {
    inline constexpr const char* Density = "density";
    inline constexpr const char* Porosity = "porosity";
    inline constexpr const char* CompressiveStrength = "compressiveStrength";
    inline constexpr const char* TensileStrength = "tensileStrength";
    inline constexpr const char* ElasticModulus = "elasticModulus";
    inline constexpr const char* StaticFriction = "staticFriction";
    inline constexpr const char* DynamicFriction = "dynamicFriction";
    inline constexpr const char* Restitution = "restitution";
    inline constexpr const char* SpecificHeatCapacity = "specificHeatCapacity";
    inline constexpr const char* ThermalConductivity = "thermalConductivity";
    inline constexpr const char* ThermalExpansion = "thermalExpansion";
    inline constexpr const char* MeltingPoint = "meltingPoint";
}

bool IsValidScientificPropertyName(const std::string& name);
std::string GetCanonicalUnitForProperty(const std::string& name);
bool ValidateScientificQuantity(const std::string& propName, const ScientificQuantity& q, std::string* outError = nullptr);

struct ScientificPropertyOverrides {
    std::unordered_map<std::string, ScientificQuantity> overrides;

    bool SetOverride(const std::string& propName, const ScientificQuantity& quantity, std::string* outError = nullptr) {
        if (!ValidateScientificQuantity(propName, quantity, outError)) {
            return false;
        }
        overrides[propName] = quantity;
        return true;
    }

    bool RemoveOverride(const std::string& propName) {
        return overrides.erase(propName) > 0;
    }

    bool HasOverride(const std::string& propName) const {
        return overrides.find(propName) != overrides.end();
    }

    const ScientificQuantity* GetOverride(const std::string& propName) const {
        auto it = overrides.find(propName);
        if (it != overrides.end()) {
            return &it->second;
        }
        return nullptr;
    }

    void Clear() {
        overrides.clear();
    }

    bool Empty() const {
        return overrides.empty();
    }

    bool empty() const {
        return overrides.empty();
    }

    size_t Size() const {
        return overrides.size();
    }

    size_t size() const {
        return overrides.size();
    }

    bool operator==(const ScientificPropertyOverrides& other) const {
        if (overrides.size() != other.overrides.size()) return false;
        for (const auto& pair : overrides) {
            auto it = other.overrides.find(pair.first);
            if (it == other.overrides.end()) return false;
            const auto& a = pair.second;
            const auto& b = it->second;
            if (a.state != b.state) return false;
            if (a.state == PropertyState::Known && (a.value != b.value || a.unit != b.unit)) return false;
            if (a.quality != b.quality || a.source != b.source) return false;
            if (a.temperatureKelvin != b.temperatureKelvin || a.pressurePascal != b.pressurePascal) return false;
        }
        return true;
    }

    bool operator!=(const ScientificPropertyOverrides& other) const {
        return !(*this == other);
    }
};

void to_json(nlohmann::json& j, const ScientificPropertyOverrides& o);
void from_json(const nlohmann::json& j, ScientificPropertyOverrides& o);

// MA-03.1: Serializzazione JSON v2 per Tassonomia e Composizione
void to_json(nlohmann::json& j, const ComponentQuantity& q);
void from_json(const nlohmann::json& j, ComponentQuantity& q);

void to_json(nlohmann::json& j, const ScientificComponent& c);
void from_json(const nlohmann::json& j, ScientificComponent& c);

void to_json(nlohmann::json& j, const ScientificComposition& comp);
void from_json(const nlohmann::json& j, ScientificComposition& comp);

void to_json(nlohmann::json& j, const ScientificRelation& r);
void from_json(const nlohmann::json& j, ScientificRelation& r);

void to_json(nlohmann::json& j, const ScientificMaterialDef& m);
void from_json(const nlohmann::json& j, ScientificMaterialDef& m);

// MA-03.2D: Serializzazione JSON v3 per Proprietà Estese
void to_json(nlohmann::json& j, const PressureSpec& p);
void from_json(const nlohmann::json& j, PressureSpec& p);

void to_json(nlohmann::json& j, const MetrologicalUncertainty& u);
void from_json(const nlohmann::json& j, MetrologicalUncertainty& u);

void to_json(nlohmann::json& j, const SamplePopulationDispersion& d);
void from_json(const nlohmann::json& j, SamplePopulationDispersion& d);

void to_json(nlohmann::json& j, const ExtendedMeasurement& m);
void from_json(const nlohmann::json& j, ExtendedMeasurement& m);

void to_json(nlohmann::json& j, const ScientificObservationCollection& c);
void from_json(const nlohmann::json& j, ScientificObservationCollection& c);

void to_json(nlohmann::json& j, const ExtendedPropertyEntry& e);
void from_json(const nlohmann::json& j, ExtendedPropertyEntry& e);

void to_json(nlohmann::json& j, const ExtendedPropertyStorage& s);
void from_json(const nlohmann::json& j, ExtendedPropertyStorage& s);

void to_json(nlohmann::json& j, const ScientificPropertyDefinition& def);
void from_json(const nlohmann::json& j, ScientificPropertyDefinition& def);

// =============================================================================
// SCIENTIFIC MATERIAL REGISTRY (Headless, Zero Vulkan dependencies)
// =============================================================================

class ScientificMaterialRegistry {
public:
    ScientificMaterialRegistry();
    ~ScientificMaterialRegistry() = default;

    void Initialize();
    bool LoadFromJson(const std::string& filepath);
    bool SaveToJson(const std::string& filepath) const;

    bool RegisterMaterial(const ScientificMaterialDef& def, bool allowUpdate = false);
    bool HasMaterial(ScientificMaterialID id) const;
    bool HasMaterial(const std::string& canonicalId) const;

    const ScientificMaterialDef& GetMaterial(ScientificMaterialID id) const;
    const ScientificMaterialDef& GetMaterial(const std::string& canonicalId) const;
    ScientificMaterialID GetMaterialIdByCanonical(const std::string& canonicalId) const;

    // MA-02.3: Risoluzione gerarchica delle proprietà (override esplicito -> ereditarietà dal registry -> Unknown)
    ScientificQuantity ResolveProperty(
        ScientificMaterialID baseId, 
        const std::string& propertyName, 
        const ScientificPropertyOverrides& overrides = ScientificPropertyOverrides()
    ) const;

    // MA-03.2D: Snapshot e Risoluzione Proprietà Scientifiche Estese
    RegistrySnapshotHandle GetSnapshotHandle() const;
    std::shared_ptr<const RegistrySnapshot> GetSnapshot() const;

    QueryResult QueryExtendedProperty(
        ScientificMaterialID matId, 
        const ScientificPropertyID& propUri, 
        const QueryPredicate& predicate = QueryPredicate()
    ) const;

    QueryResult QueryExtendedProperty(
        const std::string& canonicalMatId, 
        const ScientificPropertyID& propUri, 
        const QueryPredicate& predicate = QueryPredicate()
    ) const;

    const ScientificPropertyDefinition* GetPropertyDefinition(const ScientificPropertyID& propUri) const;
    std::vector<ScientificPropertyID> GetRegisteredPropertyUris() const;

    // MA-03.2D: Validazione storage proprietà estese contro il dizionario attivo dello snapshot
    bool ValidateExtendedProperties(const ExtendedPropertyStorage& storage, std::string* outError = nullptr) const;

    // MA-03.1: Validazione tassonomia v2, riferimenti, duplicati, cicli e composizioni
    bool ValidateCatalog(std::string* outError = nullptr) const;

    // MA-03.1: Query tassonomiche v2
    std::vector<ScientificMaterialID> GetMaterialsByKind(ScientificKind kind) const;
    std::vector<ScientificMaterialID> GetMaterialsByAggregationState(AggregationState state) const;
    std::vector<ScientificRelation> GetRelationsForMaterial(ScientificMaterialID id) const;

    const std::unordered_map<ScientificMaterialID, ScientificMaterialDef>& GetAllMaterials() const {
        return m_materials;
    }
    size_t GetMaterialCount() const { return m_materials.size(); }

    void Clear();

private:
    void RegisterStandardMaterials();

    std::unordered_map<ScientificMaterialID, ScientificMaterialDef> m_materials;
    std::unordered_map<std::string, ScientificMaterialID> m_canonicalToIdMap;
    ScientificMaterialDef m_fallbackMaterial;
    std::shared_ptr<RegistrySnapshot> m_activeSnapshot;
};

} // namespace fw
