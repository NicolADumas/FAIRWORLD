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
        j["terrainLibrary"] = json::array();
        for (const auto& t : terrainLibrary) {
            json tj;
            tj["id"] = t.id;
            tj["name"] = t.name;
            tj["baseType"] = static_cast<int>(t.baseType);
            tj["basePerlinFrequency"] = t.basePerlinFrequency;
            tj["baseGravityModifier"] = t.baseGravityModifier;
            tj["baseAngularRadius"] = t.baseAngularRadius;
            tj["baseSurfaceBlockId"] = t.baseSurfaceBlockId;
            tj["baseSubsurfaceBlockId"] = t.baseSubsurfaceBlockId;
            tj["seed"] = t.seed;
            tj["subRegions"] = json::array();
            for (const auto& r : t.subRegions) {
                json rj;
                rj["eulerAngles"] = { r.eulerAngles.x, r.eulerAngles.y, r.eulerAngles.z };
                rj["angularRadius"] = r.angularRadius;
                rj["rectMin"] = { r.rectMin.x, r.rectMin.y };
                rj["rectMax"] = { r.rectMax.x, r.rectMax.y };
                rj["type"] = static_cast<int>(r.type);
                rj["shape"] = static_cast<int>(r.shape);
                rj["label"] = r.label;
                rj["surfaceBlockId"] = r.surfaceBlockId;
                rj["subsurfaceBlockId"] = r.subsurfaceBlockId;
                tj["subRegions"].push_back(rj);
            }
            j["terrainLibrary"].push_back(tj);
        }

        j["planets"] = json::array();

        for (const auto& planet : planets) {
            json pj;
            pj["type"] = static_cast<int>(planet.type);
            pj["name"] = planet.name;
            pj["planetRadius"] = planet.planetRadius;
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
                rj["eulerAngles"] = { region.eulerAngles.x, region.eulerAngles.y, region.eulerAngles.z };
                rj["angularRadius"] = region.angularRadius;
                rj["rectMin"] = { region.rectMin.x, region.rectMin.y };
                rj["rectMax"] = { region.rectMax.x, region.rectMax.y };
                rj["type"] = static_cast<int>(region.type);
                rj["shape"] = static_cast<int>(region.shape);
                rj["label"] = region.label;
                rj["seed"] = region.seed;
                rj["gravityModifier"] = region.gravityModifier;
                rj["perlinFrequency"] = region.perlinFrequency;
                rj["treeDensity"] = region.treeDensity;
                rj["surfaceBlockId"] = region.surfaceBlockId;
                rj["subsurfaceBlockId"] = region.subsurfaceBlockId;
                pj["regions"].push_back(rj);
            }
            
            pj["chunkInstances"] = json::array();
            for (const auto& inst : planet.chunkInstances) {
                json cj;
                cj["name"] = inst.name;
                cj["templateId"] = inst.templateId;
                cj["eulerAngles"] = { inst.eulerAngles.x, inst.eulerAngles.y, inst.eulerAngles.z };
                cj["angularRadius"] = inst.angularRadius;
                cj["isGridAligned"] = inst.isGridAligned;
                cj["faceIndex"] = inst.faceIndex;
                cj["gridX"] = inst.gridX;
                cj["gridY"] = inst.gridY;
                pj["chunkInstances"].push_back(cj);
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
        
        terrainLibrary.clear();
        if (j.contains("terrainLibrary") && j["terrainLibrary"].is_array()) {
            for (const auto& tj : j["terrainLibrary"]) {
                TerrainTemplate t;
                t.id = tj.value("id", "default");
                t.name = tj.value("name", "Unknown");
                t.baseType = static_cast<MapRegionType>(tj.value("baseType", 0));
                t.basePerlinFrequency = tj.value("basePerlinFrequency", 0.03f);
                t.baseGravityModifier = tj.value("baseGravityModifier", 1.0f);
                t.baseAngularRadius = tj.value("baseAngularRadius", 0.2f);
                t.baseSurfaceBlockId = tj.value("baseSurfaceBlockId", 1);
                t.baseSubsurfaceBlockId = tj.value("baseSubsurfaceBlockId", 3);
                t.seed = tj.value("seed", 0U);
                if (tj.contains("subRegions") && tj["subRegions"].is_array()) {
                    for (const auto& rj : tj["subRegions"]) {
                        MapRegion r;
                        if (rj.contains("eulerAngles") && rj["eulerAngles"].is_array() && rj["eulerAngles"].size() >= 3) {
                            r.eulerAngles.x = rj["eulerAngles"][0].get<float>();
                            r.eulerAngles.y = rj["eulerAngles"][1].get<float>();
                            r.eulerAngles.z = rj["eulerAngles"][2].get<float>();
                        } else if (rj.contains("centerNormal") && rj["centerNormal"].is_array() && rj["centerNormal"].size() >= 3) {
                            glm::vec3 cn(rj["centerNormal"][0].get<float>(), rj["centerNormal"][1].get<float>(), rj["centerNormal"][2].get<float>());
                            r.eulerAngles.x = glm::degrees(asin(cn.y));
                            r.eulerAngles.y = glm::degrees(atan2(cn.z, cn.x));
                            r.eulerAngles.z = 0.0f;
                        }
                        r.angularRadius = rj.value("angularRadius", 0.2f);
                        if (rj.contains("rectMin") && rj["rectMin"].is_array() && rj["rectMin"].size() >= 2) {
                            r.rectMin.x = rj["rectMin"][0].get<int>();
                            r.rectMin.y = rj["rectMin"][1].get<int>();
                        }
                        if (rj.contains("rectMax") && rj["rectMax"].is_array() && rj["rectMax"].size() >= 2) {
                            r.rectMax.x = rj["rectMax"][0].get<int>();
                            r.rectMax.y = rj["rectMax"][1].get<int>();
                        }
                        r.type = static_cast<MapRegionType>(rj.value("type", 0));
                        r.shape = static_cast<RegionShape>(rj.value("shape", 0));
                        r.label = rj.value("label", "");
                        r.surfaceBlockId = rj.value("surfaceBlockId", 1);
                        r.subsurfaceBlockId = rj.value("subsurfaceBlockId", 3);
                        t.subRegions.push_back(r);
                    }
                }
                terrainLibrary.push_back(t);
            }
        }
        
        planets.clear();

        if (j.contains("planets") && j["planets"].is_array()) {
            for (const auto& pj : j["planets"]) {
                PlanetMap planet;
                planet.type = static_cast<PlanetType>(pj.value("type", 0));
                planet.name = pj.value("name", "Unknown");
                planet.planetRadius = pj.value("planetRadius", 50.0f);

                planet.minX = pj.value("minX", -6);
                planet.maxX = pj.value("maxX", 6);
                planet.minY = pj.value("minY", 0);
                planet.maxY = pj.value("maxY", 128);
                planet.minZ = pj.value("minZ", -6);
                planet.maxZ = pj.value("maxZ", 6);
                
                if (pj.contains("chunkOverrides") && pj["chunkOverrides"].is_array()) {
                    for (const auto& cj : pj["chunkOverrides"]) {
                        ChunkDataExport c;
                        c.coord.x = cj.value("x", 0);
                        c.coord.z = cj.value("z", 0);
                        c.meta.type = static_cast<ChunkType>(cj.value("type", 0));
                        c.meta.biomeID = cj.value("biomeID", 1);
                        c.meta.canSpawnMobs = cj.value("canSpawnMobs", true);
                        c.meta.isDestructible = cj.value("isDestructible", true);
                        planet.chunkOverrides.push_back(c);
                    }
                }
                
                if (pj.contains("regions") && pj["regions"].is_array()) {
                    for (const auto& rj : pj["regions"]) {
                        MapRegion region;
                        if (rj.contains("eulerAngles") && rj["eulerAngles"].is_array() && rj["eulerAngles"].size() >= 3) {
                            region.eulerAngles.x = rj["eulerAngles"][0].get<float>();
                            region.eulerAngles.y = rj["eulerAngles"][1].get<float>();
                            region.eulerAngles.z = rj["eulerAngles"][2].get<float>();
                        } else if (rj.contains("centerNormal") && rj["centerNormal"].is_array() && rj["centerNormal"].size() >= 3) {
                            glm::vec3 cn(rj["centerNormal"][0].get<float>(), rj["centerNormal"][1].get<float>(), rj["centerNormal"][2].get<float>());
                            region.eulerAngles.x = glm::degrees(asin(cn.y));
                            region.eulerAngles.y = glm::degrees(atan2(cn.z, cn.x));
                            region.eulerAngles.z = 0.0f;
                        }
                        region.angularRadius = rj.value("angularRadius", 0.2f);
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
                        region.shape = static_cast<RegionShape>(rj.value("shape", 0));
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
                
                if (pj.contains("chunkInstances") && pj["chunkInstances"].is_array()) {
                    for (const auto& cj : pj["chunkInstances"]) {
                        PlanetChunkInstance inst;
                        inst.name = cj.value("name", "Nuova Zona");
                        inst.templateId = cj.value("templateId", "");
                        if (cj.contains("eulerAngles") && cj["eulerAngles"].is_array() && cj["eulerAngles"].size() >= 3) {
                            inst.eulerAngles.x = cj["eulerAngles"][0].get<float>();
                            inst.eulerAngles.y = cj["eulerAngles"][1].get<float>();
                            inst.eulerAngles.z = cj["eulerAngles"][2].get<float>();
                        } else if (cj.contains("centerNormal") && cj["centerNormal"].is_array() && cj["centerNormal"].size() >= 3) {
                            // Fallback per vecchi salvataggi
                            glm::vec3 cn(cj["centerNormal"][0].get<float>(), cj["centerNormal"][1].get<float>(), cj["centerNormal"][2].get<float>());
                            inst.eulerAngles.x = glm::degrees(asin(cn.y));
                            inst.eulerAngles.y = glm::degrees(atan2(cn.z, cn.x));
                            inst.eulerAngles.z = 0.0f;
                        }
                        inst.angularRadius = cj.value("angularRadius", 0.2f);
                        inst.isGridAligned = cj.value("isGridAligned", false);
                        inst.faceIndex = cj.value("faceIndex", 0);
                        inst.gridX = cj.value("gridX", 0);
                        inst.gridY = cj.value("gridY", 0);
                        planet.chunkInstances.push_back(inst);
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
