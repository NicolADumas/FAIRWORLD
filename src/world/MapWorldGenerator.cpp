#include "pch.h"
#include "MapWorldGenerator.h"
#include "ForgeWorld.h"
#include "../app/AssetManager.h"
#include "BlockRegistry.h"
#include <iostream>
#include "BiomeComponents.h"

#include "../utils/PerlinNoise.h"

#include "GameWorld.h"
#include <glm/gtx/quaternion.hpp>

namespace fw {

void MapWorldGenerator::Generate(const MapDocument& doc, int planetIndex, GameWorld& targetWorld, fw::JobSystem* jobs) {
    if (planetIndex < 0 || planetIndex >= (int)doc.planets.size()) return;
    
    const PlanetMap& planet = doc.planets[planetIndex];

    // TODO: Usare jobs->Execute() quando JobSystem e' esposto
    // Per ora facciamo generazione sincrona per collaudo
    std::cout << "[MapWorldGenerator] Inizio generazione voxel per " << planet.name << "...\n";
    
    // Crea il DimensionsManager al volo in base ai dati della mappa
    fw::DimensionsManager dimManager;
    dimManager.SetBounds(planet.minX, planet.maxX, planet.minZ, planet.maxZ);
    for (const auto& co : planet.chunkOverrides) {
        dimManager.SetChunkMetadata(co.coord.x, co.coord.z, co.meta);
    }
    
    std::vector<fw::MapRegion> combinedRegions = planet.regions;
    for (const auto& inst : planet.chunkInstances) {
        for (const auto& tmpl : doc.terrainLibrary) {
            if (tmpl.id == inst.templateId) {
                int baseX = inst.gridX;
                int baseZ = inst.gridY;
                for (const auto& sub : tmpl.subRegions) {
                    fw::MapRegion projected = sub;
                    projected.faceIndex = inst.faceIndex;
                    projected.isGridAligned = inst.isGridAligned;
                    projected.rectMin += glm::ivec2(baseX, baseZ);
                    projected.rectMax += glm::ivec2(baseX, baseZ);
                    combinedRegions.push_back(projected);
                }
                break;
            }
        }
    }

    auto generateChunk = [&](int global_cx, int global_cz, int local_cx, int local_cz, int face, const glm::vec3& pos, const glm::quat& rot) {
        const ChunkMetadata* meta = dimManager.GetChunkMetadata(global_cx, global_cz);
        ChunkMetadata defaultMeta;
        const ChunkMetadata& activeMeta = meta ? *meta : defaultMeta;

        if (activeMeta.type == ChunkType::OuterBoundary) return;

        std::string chunkName = "WorldChunk_" + std::to_string(global_cx) + "_" + std::to_string(global_cz);
        entt::entity chunkEnt = targetWorld.CreateChunkEntity(chunkName, {pos.x, pos.y, pos.z});
        
        // APPLICA LA ROTAZIONE SFERICA
        if (targetWorld.GetRegistry().all_of<fw::TransformComponent>(chunkEnt)) {
            auto& trans = targetWorld.GetRegistry().get<fw::TransformComponent>(chunkEnt);
            trans.rotation = {rot.x, rot.y, rot.z, rot.w};
        }

        auto& chunk = targetWorld.GetRegistry().get<fw::VoxelChunkComponent>(chunkEnt);
        
        const MapRegion* activeRegion = nullptr;
        for (auto it = combinedRegions.rbegin(); it != combinedRegions.rend(); ++it) {
            if (it->faceIndex != -1 && it->faceIndex != face) continue;

            float test_cx = (it->faceIndex != -1) ? (float)local_cx : (float)global_cx;
            float test_cz = (it->faceIndex != -1) ? (float)local_cz : (float)global_cz;

            bool isInside = false;
            if (it->shape == RegionShape::Circle) {
                float centerCX = (it->rectMin.x + it->rectMax.x) * 0.5f;
                float centerCZ = (it->rectMin.y + it->rectMax.y) * 0.5f;
                float radiusX = (it->rectMax.x - it->rectMin.x + 1) * 0.5f;
                float radiusZ = (it->rectMax.y - it->rectMin.y + 1) * 0.5f;
                float dx = (test_cx - centerCX) / (radiusX > 0.001f ? radiusX : 1.0f);
                float dz = (test_cz - centerCZ) / (radiusZ > 0.001f ? radiusZ : 1.0f);
                if (dx * dx + dz * dz <= 1.0f) isInside = true;
            } else if (it->shape == RegionShape::Rhombus) {
                float centerCX = (it->rectMin.x + it->rectMax.x) * 0.5f;
                float centerCZ = (it->rectMin.y + it->rectMax.y) * 0.5f;
                float radiusX = (it->rectMax.x - it->rectMin.x + 1) * 0.5f;
                float radiusZ = (it->rectMax.y - it->rectMin.y + 1) * 0.5f;
                float dx = std::abs(test_cx - centerCX) / (radiusX > 0.001f ? radiusX : 1.0f);
                float dz = std::abs(test_cz - centerCZ) / (radiusZ > 0.001f ? radiusZ : 1.0f);
                if (dx + dz <= 1.0f) isInside = true;
            } else if (it->shape == RegionShape::Star) {
                float centerCX = (it->rectMin.x + it->rectMax.x) * 0.5f;
                float centerCZ = (it->rectMin.y + it->rectMax.y) * 0.5f;
                float radiusX = (it->rectMax.x - it->rectMin.x + 1) * 0.5f;
                float radiusZ = (it->rectMax.y - it->rectMin.y + 1) * 0.5f;
                float dx = (test_cx - centerCX) / (radiusX > 0.001f ? radiusX : 1.0f);
                float dz = (test_cz - centerCZ) / (radiusZ > 0.001f ? radiusZ : 1.0f);
                float dist = std::sqrt(dx * dx + dz * dz);
                float angle = std::atan2(dz, dx);
                float starFactor = 0.65f + 0.35f * std::cos(5.0f * angle);
                if (dist <= starFactor) isInside = true;
            } else {
                if (test_cx >= it->rectMin.x && test_cx <= it->rectMax.x && test_cz >= it->rectMin.y && test_cz <= it->rectMax.y) {
                    isInside = true;
                }
            }
            
            if (isInside) {
                activeRegion = &(*it);
                break;
            }
        }

        fw::BiomeDataComponent biomeData;
        if (activeRegion) {
            biomeData.type = activeRegion->type;
            biomeData.surfaceBlockId = activeRegion->surfaceBlockId;
            biomeData.subsurfaceBlockId = activeRegion->subsurfaceBlockId;
            biomeData.isCustomMapped = true;
        } else {
            biomeData.type = fw::MapRegionType::Forest; 
            uint8_t idGrass = 255, idDirt = 255;
            if (auto reg = targetWorld.GetBlockRegistry()) {
                idGrass = reg->GetBlock("fairworld:grass").id;
                idDirt = reg->GetBlock("fairworld:dirt").id;
            }
            biomeData.surfaceBlockId = idGrass;
            biomeData.subsurfaceBlockId = idDirt;
            biomeData.isCustomMapped = false;
        }
        
        targetWorld.GetRegistry().emplace<fw::BiomeDataComponent>(chunkEnt, biomeData);
        targetWorld.GetRegistry().emplace<fw::TerrainGenTag>(chunkEnt);
    };

    if (planet.planetRadius > 0.0f) {
        float S = 16.0f; 
        float R = planet.planetRadius;
        int N = (int)std::ceil((glm::pi<float>() * R) / (2.0f * S));
        if (N < 1) N = 1;

        for (int face = 0; face < 6; ++face) {
            for (int cy = -N; cy <= N; ++cy) {
                for (int cx = -N; cx <= N; ++cx) {
                    glm::vec3 localPos(0.0f);
                    if (face == 0) localPos = glm::vec3(cx * S, cy * S, R);         // +Z
                    else if (face == 1) localPos = glm::vec3(-cx * S, cy * S, -R);  // -Z
                    else if (face == 2) localPos = glm::vec3(R, cy * S, -cx * S);   // +X
                    else if (face == 3) localPos = glm::vec3(-R, cy * S, cx * S);   // -X
                    else if (face == 4) localPos = glm::vec3(cx * S, R, -cy * S);   // +Y
                    else if (face == 5) localPos = glm::vec3(cx * S, -R, cy * S);   // -Y
                    
                    glm::vec3 normal = glm::normalize(localPos);
                    glm::vec3 spherePos = normal * R;

                    // Gestione singolarità per rotation 180 gradi
                    glm::quat q;
                    glm::vec3 up(0, 1, 0);
                    if (glm::dot(up, normal) < -0.999f) {
                        q = glm::angleAxis(glm::radians(180.0f), glm::vec3(1, 0, 0));
                    } else {
                        q = glm::rotation(up, normal);
                    }

                    int global_cx = cx + (face % 3) * (N * 2 + 1);
                    int global_cz = cy + (face / 3) * (N * 2 + 1);

                    generateChunk(global_cx, global_cz, cx, cy, face, spherePos, q);
                }
            }
        }
    } else {
        for (int cz = dimManager.GetMinZ(); cz <= dimManager.GetMaxZ(); ++cz) {
            for (int cx = dimManager.GetMinX(); cx <= dimManager.GetMaxX(); ++cx) {
                generateChunk(cx, cz, cx, cz, -1, glm::vec3(cx * 16.0f, 0.0f, cz * 16.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            }
        }
    }
    
    std::cout << "[MapWorldGenerator] Generazione completata con successo!\n";
}

float MapWorldGenerator::SampleSphericalNoise(const glm::vec3& normal, const MapRegion& regionInfo, float frequency) {
    ::PerlinNoise noiseGen(regionInfo.seed);
    
    // Parametri base ereditati dal template
    float noiseScale = frequency * 100.0f; 
    int octaves = 4;
    float persistence = 0.5f;
    float heightMultiplier = 5.0f;
    
    // --- FORME GEOLOGICHE SPECIFICHE PER TIPO DI TERRENO ---
    switch (regionInfo.type) {
        case MapRegionType::Desert:
            // Deserto: Dune sabbiose morbide e larghe (pochi ottavi, scale più grande)
            octaves = 2;
            persistence = 0.35f;
            noiseScale *= 0.7f;
            break;
        case MapRegionType::Ocean:
            // Oceano: Superficie estremamente piatta
            octaves = 1;
            heightMultiplier = 0.8f;
            break;
        case MapRegionType::Volcano:
            // Vulcano: Montagne molto alte, frastagliate e appuntite
            octaves = 6;
            persistence = 0.65f;
            heightMultiplier = 15.0f;
            noiseScale *= 1.2f;
            break;
        case MapRegionType::Tundra:
            // Tundra/Ghiacciaio: Terreno ruvido, solcato e freddo
            octaves = 5;
            persistence = 0.55f;
            noiseScale *= 1.4f;
            heightMultiplier = 8.0f;
            break;
        case MapRegionType::City:
        case MapRegionType::Portal:
            // Zone Artificiali: Terreno appiattito per costruire
            octaves = 2;
            heightMultiplier = 1.0f;
            break;
        case MapRegionType::Forest:
        default:
            // Foresta/Base: Colline morbide standard
            octaves = 4;
            persistence = 0.5f;
            break;
    }
    
    float noiseVal = noiseGen.octaveNoise(
        normal.x * noiseScale, 
        normal.y * noiseScale, 
        normal.z * noiseScale, 
        octaves,
        persistence
    );
    
    return noiseVal * heightMultiplier;
}

const ::BiomeDef* MapWorldGenerator::EvaluateBiome(float temp, float humidity, float height, AssetManager* assets) {
    if (!assets) return nullptr;
    
    const auto& biomes = assets->GetBiomes();
    if (biomes.empty()) return nullptr;
    
    // Ritorna il primo bioma che soddisfa i criteri ambientali
    for (const auto& b : biomes) {
        if (temp >= b.minTemperature && temp <= b.maxTemperature &&
            humidity >= b.minHumidity && humidity <= b.maxHumidity &&
            height >= b.minHeight && height <= b.maxHeight) {
            return &b;
        }
    }
    
    // Fallback al primo bioma se nessuno match
    return &biomes[0];
}

} // namespace fw
