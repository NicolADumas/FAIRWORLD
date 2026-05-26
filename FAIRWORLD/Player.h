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
