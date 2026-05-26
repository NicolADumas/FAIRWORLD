#include "pch.h"
#include "Player.h"
#include "json.hpp"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

void Player::Initialize() {
    stats.Initialize();
}

bool Player::GainExp(int amount) {
    bool leveledUp = stats.AddExperience(amount);
    if (leveledUp) {
        freeStatPoints += 5; // Riceve 5 punti da distribuire liberamente
    }
    return leveledUp;
}

bool Player::SpendPoint(const std::string& attr) {
    if (freeStatPoints <= 0) return false;

    if (attr == "vit") { stats.AddVIT(1); }
    else if (attr == "str") { stats.AddSTR(1); }
    else if (attr == "dex") { stats.AddDEX(1); }
    else if (attr == "int") { stats.AddINT(1); }
    else if (attr == "res") { stats.AddRES(1); }
    else if (attr == "luk") { stats.AddLUK(1); }
    else { return false; }

    freeStatPoints--;
    stats.Initialize(); // Ricalcola i derived stats
    return true;
}

bool Player::SaveToJson(const std::string& path) {
    json j;
    j["name"] = name;
    j["level"] = stats.level;
    j["currentExp"] = stats.currentExp;
    j["freeStatPoints"] = freeStatPoints;
    
    j["attributes"] = {
        {"vit", stats.GetVIT()},
        {"str", stats.GetSTR()},
        {"dex", stats.GetDEX()},
        {"intl", stats.GetINT()},
        {"res", stats.GetRES()},
        {"luk", stats.GetLUK()}
    };
    
    j["currentHP"] = stats.currentHP;
    j["currentMP"] = stats.currentMP;
    j["currentStamina"] = stats.currentStamina;

    inventory.SaveToJson(j);

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << j.dump(4);
    return true;
}

bool Player::LoadFromJson(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        // Se non esiste, inizializza con i valori di default e crea il file
        Initialize();
        SaveToJson(path);
        return true;
    }

    try {
        json j;
        file >> j;
        name = j.value("name", "Hero");
        stats.level = j.value("level", 1);
        stats.currentExp = j.value("currentExp", 0);
        freeStatPoints = j.value("freeStatPoints", 0);

        if (j.contains("attributes")) {
            auto attr = j["attributes"];
            stats.SetVIT(attr.value("vit", 10));
            stats.SetSTR(attr.value("str", 10));
            stats.SetDEX(attr.value("dex", 10));
            stats.SetINT(attr.value("intl", 10));
            stats.SetRES(attr.value("res", 10));
            stats.SetLUK(attr.value("luk", 10));
        }
        
        stats.Initialize(); // Ricalcola i massimi

        stats.currentHP = j.value("currentHP", stats.GetMaxHP());
        stats.currentMP = j.value("currentMP", stats.GetMaxMP());
        stats.currentStamina = j.value("currentStamina", stats.GetMaxStamina());
        
        inventory.LoadFromJson(j);
        
        return true;
    }
    catch (...) {
        Initialize();
        return false;
    }
}
