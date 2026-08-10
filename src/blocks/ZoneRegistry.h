#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

namespace fw {

struct ZoneDefinition {
    uint8_t id = 0;
    std::string name = "Nessuna";
    glm::vec4 ui_color = glm::vec4(0.0f); // Colore per l'Editor (es. Verde per Residenziale)
    
    // -- Attributi Personalizzabili di Gioco --
    float base_desirability = 1.0f; // Quanto attira i cittadini?
    float pollution_output = 0.0f;  // Quanti eventi termici/inquinamento genera?
    float power_consumption = 0.0f; // Quanta rete elettrica drena per metro quadro?
};

class ZoneRegistry {
public:
    ZoneRegistry();
    ~ZoneRegistry() = default;

    void Initialize();
    bool LoadFromJson(const std::string& filepath);
    bool SaveToJson(const std::string& filepath);
    
    const std::vector<ZoneDefinition>& GetAllZones() const { return m_zones; }
    const ZoneDefinition& GetZone(uint8_t id) const;

private:
    void RegisterDefaultZones();
    std::vector<ZoneDefinition> m_zones;
};

} // namespace fw
