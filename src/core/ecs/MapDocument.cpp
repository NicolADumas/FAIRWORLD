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

            // Nuovi campi per DimensionsManager
            pj["minX"] = planet.minX;
            pj["maxX"] = planet.maxX;
            pj["minY"] = planet.minY;
            pj["maxY"] = planet.maxY;
            pj["minZ"] = planet.minZ;
            pj["maxZ"] = planet.maxZ;
            pj["chunkOverrides"] = json::array();

            for (const auto& overrideChunk : planet.chunkOverrides) {
                json cj;
                cj["x"] = overrideChunk.coord.x;
                cj["z"] = overrideChunk.coord.z;
                cj["type"] = static_cast<int>(overrideChunk.meta.type);
                cj["biomeID"] = overrideChunk.meta.biomeID;
                cj["canSpawnMobs"] = overrideChunk.meta.canSpawnMobs;
                cj["isDestructible"] = overrideChunk.meta.isDestructible;
                pj["chunkOverrides"].push_back(cj);
            }

            for (const auto& region : planet.regions) {
                json rj;
                rj["rectMin"] = { region.rectMin.x, region.rectMin.y };
                rj["rectMax"] = { region.rectMax.x, region.rectMax.y };
                rj["type"] = static_cast<int>(region.type);
                rj["label"] = region.label;
                rj["seed"] = region.seed;
                rj["gravityModifier"] = region.gravityModifier;
                rj["perlinFrequency"] = region.perlinFrequency;
                rj["treeDensity"] = region.treeDensity;
                rj["surfaceBlockId"] = region.surfaceBlockId;
                rj["subsurfaceBlockId"] = region.subsurfaceBlockId;
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

                planet.minX = pj.value("minX", -6);
                planet.maxX = pj.value("maxX", 6);
                planet.minY = pj.value("minY", 0);
                planet.maxY = pj.value("maxY", 128);
                planet.minZ = pj.value("minZ", -6);
                planet.maxZ = pj.value("maxZ", 6);
                
                if (pj.contains("regions") && pj["regions"].is_array()) {
                    for (const auto& rj : pj["regions"]) {
                        MapRegion region;
                        if (rj.contains("rectMin") && rj["rectMin"].is_array() && rj["rectMin"].size() >= 2) {
                            region.rectMin.x = rj["rectMin"][0].get<int>();
                            region.rectMin.y = rj["rectMin"][1].get<int>();
                        } else {
                            region.rectMin = glm::ivec2(-2, -2);
                        }
                        if (rj.contains("rectMax") && rj["rectMax"].is_array() && rj["rectMax"].size() >= 2) {
                            region.rectMax.x = rj["rectMax"][0].get<int>();
                            region.rectMax.y = rj["rectMax"][1].get<int>();
                        } else {
                            region.rectMax = glm::ivec2(2, 2);
                        }
                        
                        region.type = static_cast<MapRegionType>(rj.value("type", 0));
                        region.label = rj.value("label", "Region");
                        region.seed = rj.value("seed", 12345U);
                        region.gravityModifier = rj.value("gravityModifier", 1.0f);
                        region.perlinFrequency = rj.value("perlinFrequency", 0.03f);
                        region.treeDensity = rj.value("treeDensity", 0.5f);
                        region.surfaceBlockId = rj.value("surfaceBlockId", 1);
                        region.subsurfaceBlockId = rj.value("subsurfaceBlockId", 3);
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
