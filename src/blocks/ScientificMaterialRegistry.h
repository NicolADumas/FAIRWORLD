#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <cstdint>
#include <array>
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

// Definizione completa di un materiale scientifico
struct ScientificMaterialDef {
    ScientificMaterialID id = INVALID_SCIENTIFIC_MATERIAL_ID;
    std::string canonicalId = "fairworld:mat/unknown";
    std::string displayName = "Unknown Material";
    std::string chemicalFormula = "";
    std::string classification = ""; // Es. "Igneous / Plutonic", "Sedimentary", "Metallic Alloy"
    std::string description = "";

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

    bool IsValid() const {
        return id != INVALID_SCIENTIFIC_MATERIAL_ID && !canonicalId.empty();
    }
};

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
};

} // namespace fw
