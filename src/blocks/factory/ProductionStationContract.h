#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "RawAssetContract.h"

namespace fw {

// =============================================================================
// PRODUCTION STATION IDENTIFIERS (BM-FC3)
// =============================================================================
enum class ProductionStationID : uint8_t {
    Shape = 0,          // Geometria analitica / primitiva parametrica
    Sculpt = 1,         // Deformazione e modellazione mesh (BM-S)
    SurfaceUV = 2,      // Mappatura e sezionamento superfici semantiche
    Texture = 3,        // Texture Pack: importazione e associazione mappe
    Painter = 4,        // Vertex painting e pittura texture
    Material = 5,       // Proprietà PBR, shader features e GPU layer
    RawPhysics = 6,     // Massa, collisioni, termodinamica grezza
    QualityControl = 7, // Audit automatico, validazione di conformità (QC)
    Packaging = 8       // Assemblaggio e congelamento del RawAssetPackage
};

inline const char* GetStationName(ProductionStationID id) {
    switch (id) {
        case ProductionStationID::Shape:          return "Shape";
        case ProductionStationID::Sculpt:         return "Sculpt";
        case ProductionStationID::SurfaceUV:      return "Surface & UV";
        case ProductionStationID::Texture:        return "Texture";
        case ProductionStationID::Painter:        return "Painter";
        case ProductionStationID::Material:       return "Material (PBR)";
        case ProductionStationID::RawPhysics:     return "Raw Physics";
        case ProductionStationID::QualityControl: return "Quality Control";
        case ProductionStationID::Packaging:      return "Packaging";
        default:                                  return "Unknown Station";
    }
}

inline const char* GetStationDescription(ProductionStationID id) {
    switch (id) {
        case ProductionStationID::Shape:          return "Definizione della forma parametrica e geometria base dell'asset.";
        case ProductionStationID::Sculpt:         return "Modellazione avanzata, intaglio vertici e scultura volumetrica.";
        case ProductionStationID::SurfaceUV:      return "Sezionamento delle superfici semantiche e proiezioni UV.";
        case ProductionStationID::Texture:        return "Importazione, normalizzazione e binding delle mappe texture PBR.";
        case ProductionStationID::Painter:        return "Pittura diretta sui vertici e mascheratura colori.";
        case ProductionStationID::Material:       return "Configurazione dei parametri fisici PBR e proprietà shader.";
        case ProductionStationID::RawPhysics:     return "Definizione di massa, frizione, bounciness e proprietà termiche.";
        case ProductionStationID::QualityControl: return "Validazione rigorosa, verifica dei vincoli e assenza di degenerazioni.";
        case ProductionStationID::Packaging:      return "Compilazione, fingerprinting e packaging dell'asset grezzo per la pubblicazione.";
        default:                                  return "Stazione non descritta.";
    }
}

// Stato operativo della stazione
enum class StationStatus : uint8_t {
    Uninitialized = 0,
    NotImplemented,  // Per stazioni pianificate ma non ancora implementate
    PendingInput,    // Attende prerequisiti o dati mancanti da stazioni a monte
    Dirty,           // Input modificato, richiede rielaborazione
    Processing,      // Elaborazione in corso
    Completed,       // Output generati e validati con successo
    Failed           // Errore di esecuzione o validazione
};

// Report diagnostico per singola stazione
struct StationDiagnostic {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<std::string> infos;

    bool HasErrors() const { return !errors.empty(); }
    bool HasWarnings() const { return !warnings.empty(); }

    void Clear() {
        errors.clear();
        warnings.clear();
        infos.clear();
    }
};

struct StationRequirement {
    std::string name;
    bool isMandatory = true;
};

struct StationArtifact {
    std::string name;
    std::string typeName;
};

// Contesto di esecuzione passato alle stazioni (disaccoppiato da BlockMakerState)
struct StationContext {
    RawAssetDraft& draft;
    class ShapeRegistry* shapeRegistry = nullptr;
    class MaterialRegistry* materialRegistry = nullptr;
    class BlockRegistry* blockRegistry = nullptr;
    void* auxiliaryUserData = nullptr;
};

// =============================================================================
// PRODUCTION STATION INTERFACE CONTRACT (BM-FC3)
// =============================================================================
class IProductionStation {
public:
    virtual ~IProductionStation() = default;

    virtual ProductionStationID GetID() const = 0;
    virtual const char* GetName() const = 0;
    virtual const char* GetDescription() const = 0;

    // Dichiarazione esplicita delle dipendenze e contratti I/O
    virtual std::vector<ProductionStationID> GetPrerequisiteStations() const = 0;
    virtual std::vector<StationRequirement> GetRequiredInputs() const = 0;
    virtual std::vector<StationArtifact> GetProducedOutputs() const = 0;

    // Stato, telemetria e diagnostica
    virtual StationStatus GetStatus() const = 0;
    virtual uint64_t GetLastProcessedRevision() const = 0;
    virtual const StationDiagnostic& GetDiagnostic() const = 0;

    virtual bool IsImplemented() const { return true; }

    // Ciclo di lavorazione
    virtual bool CanExecute(const StationContext& context) const = 0;
    virtual bool Execute(StationContext& context) = 0;
    virtual void Invalidate() = 0;
    virtual void Reset() = 0;
};

} // namespace fw
