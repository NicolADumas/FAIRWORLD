#include "pch.h"
#include "MapWorldGenerator.h"
#include "ForgeWorld.h"
#include "../app/AssetManager.h"
#include "BlockRegistry.h"
#include <iostream>
#include "BiomeComponents.h"

#include "../utils/PerlinNoise.h"

#include "GameWorld.h"

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
    
    for (int cz = dimManager.GetMinZ(); cz <= dimManager.GetMaxZ(); ++cz) {
        for (int cx = dimManager.GetMinX(); cx <= dimManager.GetMaxX(); ++cx) {
            
            const ChunkMetadata* meta = dimManager.GetChunkMetadata(cx, cz);
            ChunkMetadata defaultMeta;
            const ChunkMetadata& activeMeta = meta ? *meta : defaultMeta;

            // Salta i chunk etichettati come OuterBoundary se non vogliamo che contengano MicroVoxel 
            // (potremmo instanziare mesh low-poly per l'orizzonte, ma qui stiamo generando VoxelComponent)
            if (activeMeta.type == ChunkType::OuterBoundary) {
                continue;
            }

            std::string chunkName = "WorldChunk_" + std::to_string(cx) + "_" + std::to_string(cz);
            entt::entity chunkEnt = targetWorld.CreateChunkEntity(chunkName, {cx * 16.0f, 0.0f, cz * 16.0f});
            auto& chunk = targetWorld.GetRegistry().get<fw::VoxelChunkComponent>(chunkEnt);
            // Cerca se il chunk appartiene a una regione disegnata (in base alla sua FORMA GEOMETRICA)
            const MapRegion* activeRegion = nullptr;
            for (auto it = planet.regions.rbegin(); it != planet.regions.rend(); ++it) {
                bool isInside = false;
                if (it->shape == RegionShape::Circle) {
                    float centerCX = (it->rectMin.x + it->rectMax.x) * 0.5f;
                    float centerCZ = (it->rectMin.y + it->rectMax.y) * 0.5f;
                    float radiusX = (it->rectMax.x - it->rectMin.x + 1) * 0.5f;
                    float radiusZ = (it->rectMax.y - it->rectMin.y + 1) * 0.5f;
                    float dx = (cx - centerCX) / (radiusX > 0.001f ? radiusX : 1.0f);
                    float dz = (cz - centerCZ) / (radiusZ > 0.001f ? radiusZ : 1.0f);
                    if (dx * dx + dz * dz <= 1.0f) isInside = true;
                } else if (it->shape == RegionShape::Rhombus) {
                    float centerCX = (it->rectMin.x + it->rectMax.x) * 0.5f;
                    float centerCZ = (it->rectMin.y + it->rectMax.y) * 0.5f;
                    float radiusX = (it->rectMax.x - it->rectMin.x + 1) * 0.5f;
                    float radiusZ = (it->rectMax.y - it->rectMin.y + 1) * 0.5f;
                    float dx = std::abs(cx - centerCX) / (radiusX > 0.001f ? radiusX : 1.0f);
                    float dz = std::abs(cz - centerCZ) / (radiusZ > 0.001f ? radiusZ : 1.0f);
                    if (dx + dz <= 1.0f) isInside = true;
                } else if (it->shape == RegionShape::Star) {
                    float centerCX = (it->rectMin.x + it->rectMax.x) * 0.5f;
                    float centerCZ = (it->rectMin.y + it->rectMax.y) * 0.5f;
                    float radiusX = (it->rectMax.x - it->rectMin.x + 1) * 0.5f;
                    float radiusZ = (it->rectMax.y - it->rectMin.y + 1) * 0.5f;
                    float dx = (cx - centerCX) / (radiusX > 0.001f ? radiusX : 1.0f);
                    float dz = (cz - centerCZ) / (radiusZ > 0.001f ? radiusZ : 1.0f);
                    float dist = std::sqrt(dx * dx + dz * dz);
                    float angle = std::atan2(dz, dx);
                    float starFactor = 0.65f + 0.35f * std::cos(5.0f * angle);
                    if (dist <= starFactor) isInside = true;
                } else {
                    if (cx >= it->rectMin.x && cx <= it->rectMax.x && cz >= it->rectMin.y && cz <= it->rectMax.y) {
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
                
                uint8_t idGrass = 1;
                uint8_t idDirt = 2;
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
            
            // NON markiamo come dirty ne' generato qui. Ci penseranno i sistemi ECS.
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
