#pragma once
#include <glm/glm.hpp>
#include "Inventory.h"
#include "BlockMaterial.h"

// =====================================================================
// DroppedItem — Entità fisica nel mondo che rappresenta un oggetto
// caduto a terra (blocco rotto con inventario pieno, o drop da mob).
// =====================================================================
// Obbedisce alle leggi di Newton:
//   F_g = m * g                    (gravità)
//   F_A = ρ_fluido * g * V_spost   (Archimede, se in acqua)
//   F_d = -k * v                   (attrito viscoso)
//
// Se ρ_materiale < ρ_acqua (1000 kg/m³), il drop galleggia.
// =====================================================================

struct DroppedItem {
    InventoryItem item;             // L'oggetto contenuto
    glm::vec3     position;         // r(t) — posizione nel mondo [m]
    glm::vec3     velocity;         // v(t) — velocità [m/s]
    float         lifetime;         // Secondi rimasti prima di despawnare
    float         bobTimer;         // Timer per l'animazione di fluttuazione (rendering)
    bool          isAlive;          // false = da rimuovere dal vector

    DroppedItem()
        : position(0.0f), velocity(0.0f), lifetime(300.0f),
          bobTimer(0.0f), isAlive(true) {}

    // Controlla se il materiale galleggia in acqua (Principio di Archimede)
    // F_A = ρ_water * g * V > F_g = ρ_block * g * V
    // => galleggia se ρ_block < ρ_water
    bool ShouldFloat() const {
        if (item.type != ItemType::Block) return false;
        const auto& mat = GetBlockMaterial((BlockType)item.blockType);
        return mat.density < PhysicsConstants::RHO_WATER;
    }

    // Massa dell'item (per la simulazione fisica)
    float GetMass() const {
        if (item.type != ItemType::Block) return 1.0f; // Default per non-blocchi
        const auto& mat = GetBlockMaterial((BlockType)item.blockType);
        return mat.mass > 0.0f ? mat.mass * 0.001f : 1.0f; // Scala per gameplay (1/1000)
    }
};
