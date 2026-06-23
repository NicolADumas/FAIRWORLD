#pragma once
#include "CharacterStats.h"
#include <string>
#include "Inventory.h"

class Player {
public:
    std::string name = "Hero";
    CharacterStats stats;
    int freeStatPoints = 0;
    
    Inventory inventory;

    // Massa corporea base del personaggio [kg] — separata dal carico
    // Modifica questo per personaggi diversi (es. nano=60kg, gigante=120kg)
    static constexpr float BASE_BODY_MASS = 70.0f;

    // Massa totale del Player = corpo + tutto l'inventario [kg]
    // Usata dal RigidBody per: gravità, fall damage, galleggiamento, inerzia
    float GetTotalMassKg() const {
        return BASE_BODY_MASS + inventory.GetInventoryWeightKg();
    }

    // Capacità di carico massima in base alla Forza [kg]
    float GetCarryCapacityKg() const {
        return Inventory::GetCarryCapacityKg(stats.GetSTR());
    }

    // Frazione di encumbrance [0..1+] — >1.0 = sovraccarico
    float GetEncumbranceRatio() const {
        return inventory.GetEncumbranceRatio(stats.GetSTR());
    }

    // --- EQUIPMENT & COMBAT ---
    std::string equippedWeaponPath = "assets/models/sword.vox";
    int equippedWeaponDamage = 15;
    float attackAnimTimer = 0.0f;

    // --- TRANSFORMS (VR & Desktop ViewModel) ---
    glm::mat4 rightHandTransform = glm::mat4(1.0f);
    glm::mat4 leftHandTransform = glm::mat4(1.0f);

    void Initialize();
    bool GainExp(int amount);            // Ritorna true se sale di livello
    bool SpendPoint(const std::string& attr); // Incrementa un attributo primario ("vit", "str", "dex", etc.)

    bool SaveToJson(const std::string& path);
    bool LoadFromJson(const std::string& path);
};
