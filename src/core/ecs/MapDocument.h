#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "World.h" // Assicurati che PlanetType sia definito qui

namespace fw {

enum class MapRegionType : int {
    Forest = 0,
    Desert,
    Tundra,
    Ocean,
    Volcano,
    City,
    Dungeon,
    Portal
};

struct MapRegion {
    glm::vec2 center;       // Coordinate normalizzate [0..1]
    float radius;           // Raggio d'influenza
    MapRegionType type;
    std::string label;
    uint32_t seed;
    float gravityModifier = 1.0f;
    float perlinFrequency = 0.03f;
    float treeDensity = 0.5f;
};

struct PlanetMap {
    PlanetType type;
    std::string name;
    std::vector<MapRegion> regions;
};

struct MapDocument {
    std::vector<PlanetMap> planets;
    bool isCompiled = false;

    // Dichiarazione dei metodi di I/O
    bool SaveJSON(const std::string& path);
    bool LoadJSON(const std::string& path);
};

} // namespace fw
