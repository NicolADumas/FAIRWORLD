#include "pch.h"
#include "ZoneRegistry.h"
#include "json.hpp"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace fw {

ZoneRegistry::ZoneRegistry() {
    m_zones.resize(256);
}

void ZoneRegistry::Initialize() {
    RegisterDefaultZones();
    // LoadFromJson("saves/zones.json"); // Could be called by SimulationManager later
}

void ZoneRegistry::RegisterDefaultZones() {
    // 0: Nessuna
    m_zones[0].id = 0;
    m_zones[0].name = "Nessuna (Cancella)";
    m_zones[0].ui_color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    m_zones[0].base_desirability = 0.0f;
    m_zones[0].pollution_output = 0.0f;
    m_zones[0].power_consumption = 0.0f;

    // 1: Residenziale
    m_zones[1].id = 1;
    m_zones[1].name = "Residenziale";
    m_zones[1].ui_color = glm::vec4(0.2f, 0.8f, 0.2f, 0.5f); // Verde
    m_zones[1].base_desirability = 1.0f;
    m_zones[1].pollution_output = 1.0f;
    m_zones[1].power_consumption = 5.0f;

    // 2: Commerciale
    m_zones[2].id = 2;
    m_zones[2].name = "Commerciale";
    m_zones[2].ui_color = glm::vec4(0.2f, 0.2f, 0.8f, 0.5f); // Blu
    m_zones[2].base_desirability = 1.5f;
    m_zones[2].pollution_output = 2.0f;
    m_zones[2].power_consumption = 10.0f;

    // 3: Industriale
    m_zones[3].id = 3;
    m_zones[3].name = "Industriale";
    m_zones[3].ui_color = glm::vec4(0.8f, 0.8f, 0.2f, 0.5f); // Giallo
    m_zones[3].base_desirability = -2.0f;
    m_zones[3].pollution_output = 20.0f;
    m_zones[3].power_consumption = 25.0f;

    // 4: Pubblica
    m_zones[4].id = 4;
    m_zones[4].name = "Pubblica / Servizi";
    m_zones[4].ui_color = glm::vec4(0.8f, 0.8f, 0.8f, 0.5f); // Grigio
    m_zones[4].base_desirability = 2.0f;
    m_zones[4].pollution_output = 0.5f;
    m_zones[4].power_consumption = 15.0f;
}

bool ZoneRegistry::LoadFromJson(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[ZoneRegistry] File " << filepath << " non trovato. Uso i default.\n";
        return false;
    }

    json j;
    try {
        file >> j;
    } catch (const json::parse_error& e) {
        std::cerr << "[ZoneRegistry] Errore parsing JSON: " << e.what() << "\n";
        return false;
    }

    if (j.contains("zones") && j["zones"].is_array()) {
        for (const auto& jZone : j["zones"]) {
            uint8_t id = jZone.value("id", 0);
            if (id == 0 && jZone.value("name", "") != "Nessuna (Cancella)") continue; 

            ZoneDefinition& def = m_zones[id];
            def.id = id;
            def.name = jZone.value("name", "Unknown Zone");
            
            if (jZone.contains("ui_color")) {
                auto color = jZone["ui_color"];
                def.ui_color = glm::vec4(color[0], color[1], color[2], color[3]);
            }
            
            def.base_desirability = jZone.value("base_desirability", 1.0f);
            def.pollution_output = jZone.value("pollution_output", 0.0f);
            def.power_consumption = jZone.value("power_consumption", 0.0f);
        }
        std::cout << "[ZoneRegistry] Caricate " << j["zones"].size() << " zone da " << filepath << "\n";
        return true;
    }
    
    return false;
}

bool ZoneRegistry::SaveToJson(const std::string& filepath) {
    json j;
    json jZones = json::array();

    for (int i = 0; i < 256; ++i) {
        const auto& def = m_zones[i];
        if (i > 0 && def.name == "Nessuna") continue; // Skip uninitialized
        
        json jZone;
        jZone["id"] = def.id;
        jZone["name"] = def.name;
        jZone["ui_color"] = {def.ui_color.x, def.ui_color.y, def.ui_color.z, def.ui_color.w};
        jZone["base_desirability"] = def.base_desirability;
        jZone["pollution_output"] = def.pollution_output;
        jZone["power_consumption"] = def.power_consumption;
        
        jZones.push_back(jZone);
    }
    
    j["zones"] = jZones;
    
    std::ofstream file(filepath);
    if (file.is_open()) {
        file << j.dump(4);
        std::cout << "[ZoneRegistry] Zone salvate in " << filepath << "\n";
        return true;
    }
    return false;
}

const ZoneDefinition& ZoneRegistry::GetZone(uint8_t id) const {
    if (id < 256) return m_zones[id];
    return m_zones[0];
}

} // namespace fw
