#include "pch.h"
#include "MapDocument.h"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>

using json = nlohmann::json;

namespace fw {

bool MapDocument::SaveJSON(const std::string& path) {
    try {
        // Assicurati che la directory esista
        std::filesystem::path fsPath(path);
        if (fsPath.has_parent_path()) {
            std::filesystem::create_directories(fsPath.parent_path());
        }

        json j;
        j["isCompiled"] = isCompiled;
        j["planets"] = json::array();

        for (const auto& planet : planets) {
            json pj;
            pj["type"] = static_cast<int>(planet.type);
            pj["name"] = planet.name;
            pj["regions"] = json::array();

            for (const auto& region : planet.regions) {
                json rj;
                rj["center"] = { region.center.x, region.center.y };
                rj["radius"] = region.radius;
                rj["type"] = static_cast<int>(region.type);
                rj["label"] = region.label;
                rj["seed"] = region.seed;
                rj["gravityModifier"] = region.gravityModifier;
                rj["perlinFrequency"] = region.perlinFrequency;
                rj["treeDensity"] = region.treeDensity;
                pj["regions"].push_back(rj);
            }
            j["planets"].push_back(pj);
        }

        std::ofstream file(path);
        if (!file.is_open()) return false;
        file << j.dump(4);
        file.close();
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[MapDocument] Errore nel salvataggio JSON: " << e.what() << "\n";
        return false;
    }
}

bool MapDocument::LoadJSON(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        json j;
        file >> j;
        file.close();

        isCompiled = j.value("isCompiled", false);
        planets.clear();

        if (j.contains("planets") && j["planets"].is_array()) {
            for (const auto& pj : j["planets"]) {
                PlanetMap planet;
                planet.type = static_cast<PlanetType>(pj.value("type", 0));
                planet.name = pj.value("name", "Unknown");

                if (pj.contains("regions") && pj["regions"].is_array()) {
                    for (const auto& rj : pj["regions"]) {
                        MapRegion region;
                        if (rj.contains("center") && rj["center"].is_array() && rj["center"].size() >= 2) {
                            region.center.x = rj["center"][0].get<float>();
                            region.center.y = rj["center"][1].get<float>();
                        } else {
                            region.center = glm::vec2(0.5f);
                        }
                        region.radius = rj.value("radius", 0.1f);
                        region.type = static_cast<MapRegionType>(rj.value("type", 0));
                        region.label = rj.value("label", "Region");
                        region.seed = rj.value("seed", 12345U);
                        region.gravityModifier = rj.value("gravityModifier", 1.0f);
                        region.perlinFrequency = rj.value("perlinFrequency", 0.03f);
                        region.treeDensity = rj.value("treeDensity", 0.5f);
                        planet.regions.push_back(region);
                    }
                }
                planets.push_back(planet);
            }
        }
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[MapDocument] Errore nel caricamento JSON: " << e.what() << "\n";
        return false;
    }
}

} // namespace fw
