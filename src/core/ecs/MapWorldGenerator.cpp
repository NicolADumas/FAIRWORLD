#include "pch.h"
#include "MapWorldGenerator.h"
#include "ForgeWorld.h"
#include "../app/AssetManager.h"
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
            // Cerca se il chunk appartiene a una regione disegnata
            const MapRegion* activeRegion = nullptr;
            for (auto it = planet.regions.rbegin(); it != planet.regions.rend(); ++it) {
                if (cx >= it->rectMin.x && cx <= it->rectMax.x && cz >= it->rectMin.y && cz <= it->rectMax.y) {
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
                biomeData.surfaceBlockId = 1; // Grass
                biomeData.subsurfaceBlockId = 2; // Dirt
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
    
    // Usa parametri esistenti in MapRegion o default se mancanti
    float noiseScale = frequency * 100.0f; 
    int octaves = 4;
    float heightMultiplier = 5.0f;
    
    float noiseVal = noiseGen.octaveNoise(
        normal.x * noiseScale, 
        normal.y * noiseScale, 
        normal.z * noiseScale, 
        octaves,
        0.5
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
