#pragma once
#include <string>
#include <cstdint>
#include <algorithm>
#include "json.hpp" // Include nlohmann::json

namespace fw {

enum class BiomeAffinity : uint8_t {
    Ideal = 0,
    Neutral = 1,
    Hostile = 2
};

struct BuildingComponent {
    std::string prefab_id;
    int efficiency = 100;      // 0-100%
    int energy_consumed = 0; 
    float local_scale = 0.0625f; // Scala microvoxel fissa
    
    // Inizializza da JSON
    void FromJSON(const nlohmann::json& j) {
        prefab_id = j.value("prefab_id", "");
        efficiency = j.value("efficiency", 100);
        energy_consumed = j.value("energy_consumed", 10);
        local_scale = j.value("local_scale", 0.0625f);
    }
};

struct AffinityComponent {
    BiomeAffinity current_biome = BiomeAffinity::Neutral;
    uint32_t preferred_biome_mask = 0xFFFF; // Maschera per check multi-bioma
    int degradation_counter = 0;
    
    void UpdateAffinity(uint32_t tile_biome_id) {
        if ((preferred_biome_mask & (1 << tile_biome_id)) != 0) {
            current_biome = BiomeAffinity::Ideal;
            degradation_counter = std::max(0, degradation_counter - 1); // Recupero se in zona ideale
        } else {
            current_biome = BiomeAffinity::Hostile;
            degradation_counter++; // Degrado continuo
        }
    }
};

} // namespace fw
